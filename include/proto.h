#ifndef PROTO_H__
#define PROTO_H__
#include "site_type.h"

/**
 * 默认多播组
 * 默认接收端口(服务器和客户端都用)
 * 音乐频道数目
 * 节目单频道号
 * 最小的音乐频道号
 * 最大的音乐频道号
 * 音乐数据报的最大长度
 * 音乐数据发送的最大长度
 * */
#define DEFAULT_MGROUP "224.2.2.2"
#define DEFAULT_RCVPORT "1989"
#define CHNNR 100
#define LISTCHNID 0
#define MINCHNID 1
#define MAXCHNID (MINCHNID + CHNNR - 1)
#define MSG_CHNNEL_MAX (65536 - 20 - 8)
#define MSG_DATA (MSG_CHNNEL_MAX-sizeof(chnid_t))
/**
 * 节目单数据报的最大长度
 * 节目单列表的最大长度
 * */
#define MSG_LIST_MAX (65536 - 20 - 8)
#define MAX_ENTRY (MSG_LIST_MAX-sizeof(chnid_t))
#pragma pack(1)
struct msg_listentry_t{
    chnid_t chnid;
    uint16_t len;
    uint8_t desc[1];
};

#pragma pack(1)
struct msg_channel_t{
    chnid_t chnid;
    uint8_t data[1];
};

#pragma pack(1)
struct msg_list_t{
    chnid_t chnid;
    struct msg_listentry_t entry[1];
};

#endif