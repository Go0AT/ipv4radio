#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <getopt.h>
#include "client.h"
#include "../include/proto.h"
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <errno.h>
/* *
 * -M --mgroup 指定多播组
 * -P --port 指定接收端口
 * -p --player 指定播放器
 * -H --help 显示帮助
 * 
 * */
struct client_conf_t client_conf = {
    .rcvport = const_cast<char*>(DEFAULT_RCVPORT),
    .mgroup = const_cast<char*>(DEFAULT_MGROUP),
    .player_cmd = const_cast<char*>(DEFAULT_PLAYERCMD)
};

static void printhelp(){
    printf("-P --port   指定接收端口\n");
    printf("-M --mgroup 指定多播组\n");
    printf("-p --player 指定播放器命令行\n");
    printf("-H --help   显示帮助\n");
}

static size_t writen(int fd,const char *buf,size_t len){
    int pos = 0;
    while(len > 0){
        int ret = write(fd,buf + pos,len);
        if(ret < 0){
            if(errno == EINTR)
                continue;
            perror("write()");
            return -1;
        }
        len -= ret;
        pos += ret;
    }
    return len;
}

int main(int argc,char **argv){
    /**
     * 分析命令行传参
     * */
    int index = 0;
    struct option argarr[] = {
        {"port",1,NULL,'P'},{"mgroup",1,NULL,'M'},
        {"player",1,NULL,'p'},{"help",0,NULL,'H'},
        {NULL,0,NULL,0}
    };
    while(true){
        int param = getopt_long(argc,argv,"P:M:p:H",argarr,&index);
        if(param < 0) break;
        switch (param)
        {
            case 'P':
                client_conf.rcvport = optarg;
                break;
            case 'M':
                client_conf.mgroup = optarg;
                break;
            case 'p':
                client_conf.player_cmd = optarg;
                break;
            case 'H':
                printhelp();
                return 0;
            default:
                abort();
                break;
        }
    }
    int sd = socket(AF_INET,SOCK_DGRAM,0);
    if(sd < 0){
        perror("socket()");
        return 0;
    }
    /**
     * 加入多播组
     * */
    struct ip_mreqn mreq;
    inet_pton(AF_INET,client_conf.mgroup,&mreq.imr_multiaddr);
    inet_pton(AF_INET,"0.0.0.0",&mreq.imr_address);
    /** 
     * 指定imr_ifindex时需要针对特定的环境来决定
     * */
    mreq.imr_ifindex = if_nametoindex("eth0");
    if(setsockopt(sd,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq)) < 0){
        perror("setsockopt()");
        return 0;
    }
    /**
     * 绑定特定的端口
     * */
    struct sockaddr_in laddr;
    laddr.sin_family = AF_INET;
    laddr.sin_port = htons(atoi(client_conf.rcvport));
    inet_pton(AF_INET,"0.0.0.0",&laddr.sin_addr);
    if(bind(sd,(sockaddr *)&laddr,sizeof(laddr)) < 0){
        perror("bind()");
        return 0;
    }
    /**
     * 创建子进程用以播放音乐
     * */
    int pd[2];
    if(pipe(pd) < 0){
        perror("pipe()");
        return 0;
    }
    pid_t pid = fork();
    if(pid < 0){
        perror("fork()");
        return 0;
    }
    if(pid == 0){
        /* *
         * 调用解码器
         */
        close(sd);
        close(pd[1]);
        dup2(pd[0],0);
        if(pd[0] > 0)
            close(pd[0]);
        /**
         * mpg123自身存在的问题:如果一首歌开头有很多垃圾数据，会直接放弃解析，终止程序，
         * 在这个程序中，父进程会在writen()函数中调用write()时终止(返回前)，经测试，
         * 进程向读端已经关闭的管道中write()会直接终止程序，没有任何错误提示
         * */
        execl("/bin/sh","sh","-c",client_conf.player_cmd,NULL);
        perror("execl()");
        return 0;
    }
    close(pd[0]);
    /**
     * 接收收节目单
     * 选择频道
     * 收频道包,发给子进程
     */
    struct msg_list_t *msg_list;
    msg_list = (msg_list_t *)malloc(MSG_LIST_MAX);
    if(msg_list == NULL){
        perror("malloc()");
        return 0;
    }
    /**
     * 根据手册,socklen_t 在用于接收数据时需要先初始化其长度为sizeof(sockaddr_in),
     * 防止发生获得的发送地址异常
     * */
    struct sockaddr_in raddr;
    socklen_t raddr_len;
    int len;
    while(true){
        raddr_len = sizeof(raddr);
        len = recvfrom(sd,msg_list,MSG_LIST_MAX,0,(sockaddr *)&raddr,&raddr_len);
        if(len < sizeof(msg_list_t)){
            fprintf(stderr,"message is to small.\n");
            continue;
        }
        if(msg_list->chnid != LISTCHNID){
            fprintf(stderr,"chnid is not match");
            continue;
        }
        break;
    }
    /**
     * 打印节目单并选择频道
     */
    msg_listentry_t *pos = msg_list->entry;
    while(pos < (msg_listentry_t *)((char*)msg_list + len)){
        printf("%d:%s",pos->chnid,pos->desc);
        pos = (msg_listentry_t *)((char*)pos + ntohs(pos->len));
    }
    free(msg_list);
    int chos;
    puts("which channel do you want to listen:");
    int ret = scanf("%d",&chos);
    if(ret != 1) return 0;
    msg_channel_t *msg_channel;
    msg_channel = (msg_channel_t *)malloc(MSG_CHNNEL_MAX);
    if(msg_channel == NULL){
        perror("malloc");
        return 0;
    }
    sockaddr_in nraddr;
    socklen_t nraddr_len;
    while(1){
        /**
         * 需要判断recvfrom的返回值是否为-1，-1表示函数出错
         * 存在的BUG:小概率发生recvfrom()报参数非法的错误，即在同一环境下用相同的步骤运行客户端时，会发生报错
         * */
        nraddr_len = sizeof(nraddr);
        len = recvfrom(sd,msg_channel,MSG_CHNNEL_MAX,0,(sockaddr *)&nraddr,&nraddr_len);
        if(len < 0){
            perror("recvfrom()");
            continue;
        }
        /**
         * 判断包是否和之前收到的频道列表的包来自相同的地方,
         * 端口或许可以不同，因为服务器并没有在发送数据时指定特定的端口,
         */
        if(nraddr.sin_addr.s_addr != raddr.sin_addr.s_addr/*  || nraddr.sin_port != raddr.sin_port */){
            fprintf(stderr,"Ignore:address not match.\n");
            continue;
        }
        if(len < sizeof(msg_channel_t)){
            fprintf(stderr,"Ignore:message too small.\n");
            continue;
        }
        if(msg_channel->chnid == chos){
            fprintf(stdout,"accepted msg:%d recieved.\n",msg_channel->chnid);
            if(writen(pd[1],(char *)msg_channel->data,len - sizeof(chnid_t)) < 0){
                exit(1);
            }
        }
    }
    free(msg_channel);
    close(sd);
    return 0;
}