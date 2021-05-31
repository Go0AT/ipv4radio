#include "thr_channel.h"
#include "../include/proto.h"
#include "medialib.h"
#include "server_conf.h"
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
extern int sd;
extern server_conf_t server_conf;
static int tid_nextpos = 0;

struct thr_channel_ent_t{
    chnid_t chnid;
    pthread_t tid;
};

/**
 * 注意该数组的下标号和频道号并不匹配，
 * 同时也没有初始化来判断该数组下标是否有被用到，
 * 一般情况下，[0,tid_nextpos]下标的数组值是有被用到的
 * */
thr_channel_ent_t thr_channel[CHNNR];

static void *thr_channl_snder(void *ptr){
    msg_channel_t *sbufp;
    mlib_listentry_t *ent = (mlib_listentry_t *)ptr;
    sbufp = (msg_channel_t *)malloc(MSG_CHNNEL_MAX);
    if(sbufp == NULL){
        syslog(LOG_ERR,"malloc():%s",strerror(errno));
        exit(1);
    }
    sbufp->chnid = ent->chnid;
    sockaddr_in raddr;
    raddr.sin_family = AF_INET;
    raddr.sin_port = htons(atoi(server_conf.rcvport));
    inet_pton(AF_INET,server_conf.mgroup,&raddr.sin_addr);
    while(true){
        int len = mlib_readchn(ent->chnid,sbufp->data,128 * 1024 / 8);
        if(sendto(sd,sbufp,len + sizeof(chnid_t),0,(sockaddr *)&raddr,sizeof(raddr)) < 0){
            /**
             * 发送数据包失败
             * */
            syslog(LOG_ERR,"thr_channel(%d):sendto():%s",ent->chnid,strerror(errno));
            break;
        }
        syslog(LOG_WARNING,"chnid:[%d]:bytes[%d]",ent->chnid,len);
        sched_yield();
    }
    pthread_exit(NULL);
}

/**
 * 创建线程去发送音乐数据给客户端，获取音乐数据的方式为拿到该频道的频道号，
 * 通过频道号去调用medialib接口中的函数取得音乐数据
 * */
int thr_channel_create(mlib_listentry_t *ptr){
    int err = pthread_create(&thr_channel[tid_nextpos].tid,NULL,thr_channl_snder,ptr);
    if(err){
        /**
         * 某个频道创建失败并不影响其他频道，不至于结束进程
         * */
        syslog(LOG_WARNING,"pthread_create():%s",strerror(err));
        return -err;
    }
    thr_channel[tid_nextpos].chnid = ptr->chnid;
    ++tid_nextpos;
    return 0;
}
/**
 * 明显的看出，一个数组下标只能使用一次
 * */
int thr_channel_destroy(mlib_listentry_t *ptr){
    for(int i = 0 ; i < CHNNR ; i++ ){
        if(thr_channel[i].chnid == ptr->chnid){
            /**
             * 假设给线程发信号取消线程不会产生错误
             * */
            pthread_cancel(thr_channel[i].tid);
            pthread_join(thr_channel[i].tid,NULL);
            thr_channel[i].chnid = -1;
            break;
        }
    }
    return 0;
}
int thr_channel_destroyall(){
    for(int i = 0 ; i < CHNNR ; i++ ){
        if(thr_channel[i].chnid > 0){
            pthread_cancel(thr_channel[i].tid);
            pthread_join(thr_channel[i].tid,NULL);
            thr_channel[i].chnid = -1;
        }
    }
    return 0;
}