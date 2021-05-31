#include <cstdio>
#include <cstdlib>
#include "medialib.h"
#include "server_conf.h"
#include "mytbf.h"
#include <cstdio>
#include <cstdlib>
#include <glob.h>
#include <syslog.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/**
 * MP3_BITRATE选择这个值的原因是与mpg123的音频解析速率相同，注意看下面流量速率/8了，
 * thr_channel代码中的读取桶中数据的速率也相同
 * 存在的BUG:或许因为程序本身的运行延迟，导致解析完一秒中的音频后下一秒的音频还没到来，
 * 会有小小的卡顿(在程序的整个运行周期持续存在)，但是这种情况的出现概率比完全流程的出现概率要小，
 * 完全流畅时，也会在程序的整个运行周期中存在，暂时还没想到很好的解决办法，如果稍微让传输速率
 * 大于解析速率，那么迟早接收缓冲去会满，造成丢包。
 * 一个可能可行的解决方案：保证在一首歌的播放时间内不会发生泄漏，在切换歌曲的时候发几个空包，
 * 用时间去清空接收缓冲区，可能需要修改套接字参数
 * */

#define PATHSIZE 1024
#define LINEBUFSIZE 1024
#define MP3_BITRATE (128*1024)
extern server_conf_t server_conf; 
struct channel_context_t{
    chnid_t chnid;
    char *desc;
    glob_t mp3glob;
    int pos;
    int fd;
    off_t offset;
    struct mytbf_t *tbf;
};

/**
 * 最后没有清除该数组里面的desc和glot_t里面的堆内存
 * */
static channel_context_t channel[MAXCHNID + 1];

static channel_context_t *path2entry(const char *path){
    char pathstr[PATHSIZE] = {'\0'};
    char linebuf[LINEBUFSIZE];
    strcat(pathstr,path);
    strcat(pathstr,"/desc.txt");
    syslog(LOG_WARNING,"/desc:%s",pathstr);
    FILE *fp = fopen(pathstr,"r");
    /**
     * 如果目录下面没有desc.txt文件，则可以认定为无效目录
     * */
    if(fp == NULL){
        syslog(LOG_WARNING,"cannot open desc:%s",pathstr);
        return NULL;
    }
    /**
     * 如果desc.txt中没有内容或者读错误,则依然认定为无效目
     * */
    if(fgets(linebuf,LINEBUFSIZE,fp) == NULL){
        syslog(LOG_WARNING,"empty desc:%s",pathstr);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    channel_context_t *me;
    me = (channel_context_t *)malloc(sizeof(*me));
    if(me == NULL){
        return NULL;
    }
    me->tbf = mytbf_init(MP3_BITRATE / 8,MP3_BITRATE / 8 * 5);
    if(me->tbf == NULL){
        syslog(LOG_WARNING,"tbf_create error!");
        free(me);
        return NULL;
    }
    me->desc = strdup(linebuf);
    strncpy(pathstr,path,PATHSIZE);
    strcat(pathstr,"/*.mp3");
    syslog(LOG_WARNING,"path:%s",pathstr);
    static chnid_t curr_id = MINCHNID;
    if(glob(pathstr,0,NULL,&me->mp3glob) != 0){
        ++curr_id;
        free(me);
        return NULL;
    }
    me->pos = 0;
    me->offset = 0;
    me->fd = open(me->mp3glob.gl_pathv[me->pos],O_RDONLY);
    if(me->fd < 0){
        ++curr_id;
        free(me);
        return NULL;
    }
    me->chnid = curr_id;
    curr_id++;
    return me;
}

int mlib_getchnlist(mlib_listentry_t **result, int *num){
    glob_t globres;
    for(int i = 0 ; i < MAXCHNID + 1 ; i++ ){
        channel[i].chnid = -1;
    }
    char path[PATHSIZE];
    snprintf(path,PATHSIZE,"%s/*",server_conf.media_dir);
    if(glob(path,0,NULL,&globres) != 0){
        return -1;
    }
    mlib_listentry_t *ptr;
    channel_context_t *res;
    ptr = (mlib_listentry_t *)malloc(sizeof(mlib_listentry_t) * globres.gl_pathc);
    if(ptr == NULL){
        syslog(LOG_ERR,"malloc() error.");
        exit(1);
    }
    syslog(LOG_WARNING,"dir's num:%d",globres.gl_pathc);
    int cnt = 0;
    for(int i = 0 ; i < globres.gl_pathc ; i++ ){
        /**
         * gl_pathv[i] is something like "/var/media/ch1"
         * */
        res = path2entry(globres.gl_pathv[i]);
        if(res != NULL){
            memcpy(channel + res->chnid,res,sizeof(*res));
            // channel[res->chnid] = res;
            ptr[cnt].chnid = res->chnid;
            /**
             * strdup()动态内存复制字符串,该函数在拷贝auto类型字符串是不会分配多余空间
             * */
            ptr[cnt].desc = strdup(res->desc);
            free(res);
            ++cnt;
        }
    }
    globfree(&globres);
    *result = (mlib_listentry_t *)realloc(ptr,sizeof(mlib_listentry_t) * cnt);
    *num = cnt;
    return 0;
}

int mlib_freechnlist(mlib_listentry_t *ptr){
    free(ptr);
    return 0;
}

/**
 * 如果尝试打开某首歌失败，则尝试打开下一首歌,
 * 如果循环完整个文件夹，都没找到一首可以打开的歌会出错，未处理
 * */
static int open_next(chnid_t chnid){
    for(int i = 0 ; i < channel[chnid].mp3glob.gl_pathc ; i++ ){
        ++channel[chnid].pos;
        if(channel[chnid].pos == channel[chnid].mp3glob.gl_pathc){
            channel[chnid].pos = 0;
        }
        close(channel[chnid].fd);
        channel[chnid].fd = open(channel[chnid].mp3glob.gl_pathv[channel[chnid].pos],O_RDONLY);
        if(channel[chnid].fd < 0){
            syslog(LOG_WARNING,"open %s fail.",channel[chnid].mp3glob.gl_pathv[channel[chnid].pos]);
            continue;   
        }
        channel[chnid].offset = 0;
        return 0;
    }
    return 0;
}

/**
 * 该函数模仿标准UNIX的read函数来编写，返回实际读到的字节数
 * */
size_t mlib_readchn(chnid_t chnid, void *buf, size_t size){
    /**
     * 虽然流量控制，但是tbfsize值不会为0，至少可以发出去一个字节的数据
     * */
    int tbfsize = mytbf_fetchtoken(channel[chnid].tbf,size);
    printf("[%d]tbfsize = %d\n",chnid,tbfsize);
    int len;
    while(true){
        len = pread(channel[chnid].fd,buf,tbfsize,channel[chnid].offset);
        if(len < 0){
            /**
             * 这首歌可能出问题了，尝试打开下一首歌
             * */
            open_next(chnid);
        }else if(len == 0){
            /**
             * 当这首歌结束，先发送一个空包给客户端，再进入下一首歌的数据读取
             * */
            open_next(chnid);
            break;
        }else{
            channel[chnid].offset += len;
            break;
        }
    }
    /**
     * 如果没有读满从桶中拿出来的量，则将剩余量放回桶中
     * */
    if(tbfsize - len > 0)
        mytbf_returntoken(channel[chnid].tbf,tbfsize - len);
    return len;
}