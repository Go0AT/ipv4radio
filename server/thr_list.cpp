#include "thr_list.h"
#include "server_conf.h"
#include <pthread.h>
#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
static pthread_t tid_list;
static int nr_list_ent;
static mlib_listentry_t *list_ent;
extern int sd;
extern server_conf_t server_conf;
/**
 * 存在内存泄漏，如需回收需要使用信号处理函数
 * 取消线程时并没有释放entlistp内存
 * */
static void *thr_list(void *p){
    int totalsize = sizeof(chnid_t);
    msg_list_t *entlistp;
    msg_listentry_t *entryp;
    for(int i = 0 ; i < nr_list_ent ; i++ ){
        syslog(LOG_WARNING,"list name:%s",list_ent[i].desc);
        totalsize += sizeof(msg_listentry_t) + strlen(list_ent[i].desc);
    }
    entlistp = (msg_list_t *)malloc(totalsize);
    if(entlistp == NULL){
        syslog(LOG_ERR,"malloc():%s",strerror(errno));
    }
    entlistp->chnid = LISTCHNID;
    entryp = entlistp->entry;
    for(int i = 0 ; i < nr_list_ent ; i++ ){
        int size = sizeof(msg_listentry_t) + strlen(list_ent[i].desc);
        entryp->chnid = list_ent[i].chnid;
        entryp->len = htons(size);
        strcpy((char *)entryp->desc,list_ent[i].desc);
        entryp = (msg_listentry_t *)((char *)entryp + size);
    }
    sockaddr_in raddr;
    raddr.sin_family = AF_INET;
    raddr.sin_port = htons(atoi(server_conf.rcvport));
    inet_pton(AF_INET,server_conf.mgroup,&raddr.sin_addr);
    while(true){
        int ret = sendto(sd,entlistp,totalsize,0,(sockaddr *)&raddr,sizeof(raddr));
        syslog(LOG_WARNING,"send music list:%d",ret);
        if(ret < 0){
            syslog(LOG_WARNING,"sendto(sd,entlistp,...):%s",strerror(errno));
        }
        sleep(1);
    }
}

int thr_list_create(mlib_listentry_t *listp, int nr_entry){
    list_ent = listp;
    nr_list_ent = nr_entry;
    int ret = pthread_create(&tid_list,NULL,thr_list,NULL);
    if(ret){
        syslog(LOG_ERR,"pthread_create():%s",strerror(ret));
        return -1;
    }
    return 0;
}

int thr_list_destroy(){
    pthread_cancel(tid_list);
    pthread_join(tid_list,NULL);
    return 0;
}