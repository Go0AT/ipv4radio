#include <cstdio>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <cstring>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if.h>
#include "medialib.h"
#include "server_conf.h"
#include "../include/proto.h"
#include "thr_channel.h"
#include "thr_list.h"
/**
 * 解析命令行参数
 * 设置为守护进程
 * socket初始化
 * 获取频道信息
 * 创建节目单线程
 * 创建频道线程
 * */

/**
 * -M   选择多播组
 * -P   选择接收端口(指发送到多播组中计算机的某个端口,服务器发送数据时并没有指定自己的发送端口)
 * -F   是否为前台进程
 * -D   设置音频路径
 * -I   选择网络接口
 * -H   显示帮助
 * (或许加入端口选项比较好)
 * */

server_conf_t server_conf{
    .rcvport = DEFAULT_RCVPORT,
    .mgroup = DEFAULT_MGROUP,
    .media_dir = DEFAULT_MEDIADIR,
    .runmode = RUN_DAEMON,
    .ifname = DEFAULT_IF
};

static void printhelp(){
    printf("-M   选择多播组\n");
    printf("-P   选择接收端口(服务端客户端都一样)\n");
    printf("-F   是否为前台进程\n");
    printf("-D   设置音频路径\n");
    printf("-I   选择网络接口\n");
    printf("-H   显示帮助\n");
}
/**
 * 设为全局变量只为能够在daemon_exit()中调用
 * */
mlib_listentry_t *list;

static void daemon_exit(int s){
    thr_list_destroy();
    thr_channel_destroyall();
    mlib_freechnlist(list);
    closelog();
    exit(0);
}

static int daemonize(){
    pid_t pid = fork();
    if(pid < 0){
        // perror("fork()");
        syslog(LOG_ERR,"fork():%s",strerror(errno));
        return -1;
    }
    if(pid > 0) exit(0);
    int fd = open("/dev/null",O_RDWR);
    if(fd < 0){
        // perror("open()");
        syslog(LOG_WARNING,"open():%s",strerror(errno));
        return -2;
    }
    dup2(fd,0);
    dup2(fd,1);
    dup2(fd,2);
    if(fd > 2) close(fd);
    /**
     * 将运行路径设为根是为了防止卸载某一路径时失败
     * umask(0)是将创建文件权限的指定权完全掌握在手中
     * */
    chdir("/");
    umask(0);
    setsid();
    return 0;
}
int sd;
static int socket_init(){
    sd = socket(AF_INET,SOCK_DGRAM,0);
    if(sd < 0){
        syslog(LOG_ERR,"socket():%s",strerror(errno));
        exit(1);
    }
    struct ip_mreqn mreq;
    /**
     * inet_pton转换后得到的就是网络字节序的数据
     * 在指定接口时,imr_address和imr_ifindex有一个特指，就可特指端口，另一个选项可置为0(INADDR_ANY)
     * */
    inet_pton(AF_INET,server_conf.mgroup,&mreq.imr_multiaddr);
    inet_pton(AF_INET,"0.0.0.0",&mreq.imr_address);
    mreq.imr_ifindex = if_nametoindex(server_conf.ifname);
    /**
     * 其实用下面注释掉的结构体也可以用来指定端口，但是需要端口的IP地址，相对来说麻烦点
     * */
    // struct in_addr laddr;
    // inet_pton(AF_INET,"172.17.52.91",&laddr);
    if(setsockopt(sd,IPPROTO_IP,IP_MULTICAST_IF,&mreq,sizeof(mreq)) < 0){
        syslog(LOG_ERR,"setsockopt(IP_MULTICAST_IF):%s",strerror(errno));
        exit(1);
    }
    /**
     * 设置TTL时因注意不能使用int类型，而应该使用unsigned char类型，
     * 详见<<unp>> P441
     * 组播貌似并不能用在广域网中，或者说延迟过高?
     * */
    unsigned char live = 128;
    if(setsockopt(sd,IPPROTO_IP,IP_MULTICAST_TTL,&live,sizeof(live)) < 0){
        syslog(LOG_ERR,"setsockopt(IP_MULTICAST_TTL):%s",strerror(errno));
        exit(1);
    }
    /**
     * IP_MULTICASR_LOOP防止默认行为本地回环地址接收本地发送的数据的发生,
     * 只会作用于该socket，其他进程中独立的socket并不会受影响，所以在客户端
     * 指定这个选项毫无意义，甚至会有额外的开销?(但是客户端不发送广播数据的话应该没影响)
     * */
    // int y = 0;
    // if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &y, sizeof(y)) < 0)
    // {
    //     perror("setsockopt()");
    //     return 0;
    // }
    /**
     * 作为发送端，省略bind()，每次随机指派一个端口发送数据
     * */
    return sd;
}

int main(int argc,char **argv){
    openlog("natradio",LOG_PID | LOG_PERROR,LOG_DAEMON);
    /**
     * 考虑如何退出守护进程，
     * 防止抢占式的调用信号处理函数
     * */
    struct sigaction sa;
    sa.sa_handler = daemon_exit;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask,SIGINT);
    sigaddset(&sa.sa_mask,SIGQUIT);
    sigaddset(&sa.sa_mask,SIGTERM);
    sigaction(SIGTERM,&sa,NULL);
    sigaction(SIGINT,&sa,NULL);
    sigaction(SIGQUIT,&sa,NULL);
    while(true){
        int param = getopt(argc,argv,"M:P:FD:I:H");
        if(param < 0) break;
        switch(param){
            case 'M':
                server_conf.mgroup = optarg;
                break;
            case 'P':
                server_conf.rcvport = optarg;
                break;
            case 'F':
                server_conf.runmode = RUN_FOREGROUND;
                break;
            case 'D':
                server_conf.media_dir = optarg;
                break;
            case 'I':
                server_conf.ifname = optarg;
            case 'H':
                printhelp();
                return 0;
            default:
                abort();
        }
    }
    /**
     * 创建守护进程
     * */
    if(server_conf.runmode == RUN_DAEMON){
        if(daemonize() != 0) return 0;
    }else if(server_conf.runmode != RUN_FOREGROUND){
        fprintf(stderr,"EINVAL server_conf.runmode\n");
        return 0;
    }
    syslog(LOG_WARNING,"become daemon!");
    /**
     * socket初始化
     * */
    socket_init();
    mlib_listentry_t *list;
    int list_size;
    mlib_getchnlist(&list,&list_size);
    syslog(LOG_WARNING,"list_size = %d",list_size);
    thr_list_create(list,list_size);
    for(int i = 0 ; i < list_size ; i++ ){
        thr_channel_create(list + i);
    }
    syslog(LOG_WARNING,"everthing is over!");
    while(1) pause();
    return 0;
}