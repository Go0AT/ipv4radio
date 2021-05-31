#include "mytbf.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <algorithm>
#include <syslog.h>
struct mytbf_t{
    int cps;
    int busrt;
    int token;
    int pos;
    pthread_mutex_t mut;
    pthread_cond_t cond;
};

static struct mytbf_t *job[MYTBF_MAX];
static pthread_mutex_t mut_job = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static pthread_t tid;

static void *thr_alrm(void *p){
    while(true){
        pthread_mutex_lock(&mut_job);
        for(int i = 0 ; i < MYTBF_MAX ; i++ ){
            if(job[i] != NULL){
                mytbf_returntoken(job[i],job[i]->cps);
            }
        }
        pthread_mutex_unlock(&mut_job);
        sleep(1);
    }
}

static void module_unload(){
    pthread_cancel(tid);
    pthread_join(tid,NULL);
    for(int i = 0 ; i < MYTBF_MAX ; i++ ){
        if(job[i] != NULL){
            mytbf_destroy(job[i]);
        }
    }
    return ;
}

static void module_load(){
    if(pthread_create(&tid,NULL,thr_alrm,NULL)){
        fprintf(stderr,"pthread_create():%s\n",strerror(errno));
        exit(1);
    }
    atexit(module_unload);
}

static int get_free_pos_unlocked(){
    for(int i = 0 ; i < MYTBF_MAX ; i++ ){
        if(job[i] == NULL){
            return i;
        }
    }
    return -1;
}

struct mytbf_t *mytbf_init(int cps, int burst){
    pthread_once(&init_once,module_load);
    struct mytbf_t *me;
    me = (struct mytbf_t *)malloc(sizeof(*me));
    if(me == NULL){
        syslog(LOG_WARNING,"tbf OOM!");
        return NULL;
    }
    me->cps = cps;
    me->busrt = burst;
    me->token = 0;
    pthread_mutex_init(&me->mut,NULL);
    pthread_cond_init(&me->cond,NULL);
    pthread_mutex_lock(&mut_job);
    int pos = get_free_pos_unlocked();
    if(pos < 0){
        syslog(LOG_WARNING,"no pos!");
        pthread_mutex_unlock(&mut_job);
        free(me);
        return NULL;
    }
    me->pos = pos;
    job[pos] = me;
    pthread_mutex_unlock(&mut_job);
    return me;
}

int mytbf_fetchtoken(struct mytbf_t *ptr, int size){
    pthread_mutex_lock(&ptr->mut);
    while(ptr->token <= 0){
        pthread_cond_wait(&ptr->cond,&ptr->mut);
    }
    int num = std::min(ptr->token,size);
    ptr->token -= num;
    pthread_mutex_unlock(&ptr->mut);
    return num;
}
int mytbf_returntoken(struct mytbf_t *ptr, int size){
    pthread_mutex_lock(&ptr->mut);
    ptr->token += size;
    ptr->token = std::min(ptr->token,ptr->busrt);
    pthread_cond_broadcast(&ptr->cond);
    pthread_mutex_unlock(&ptr->mut);
    return 0;
}
int mytbf_destroy(struct mytbf_t *ptr){
    pthread_mutex_lock(&mut_job);
    job[ptr->pos] = NULL;
    pthread_mutex_unlock(&mut_job);
    pthread_mutex_destroy(&ptr->mut);
    pthread_cond_destroy(&ptr->cond);
    free(ptr);
    return 0;
}