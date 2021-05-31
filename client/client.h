#ifndef CLIENT_H__
#define CLIENT_H__

/**
 * -b 150 的作用是防止解析文件速度过快
 * 本人虚拟机上的mpg123有着毛病，不知道其他的机器是否有相同的问题
 * */
#define DEFAULT_PLAYERCMD "/usr/bin/mpg123 -b 150 - > /dev/null"

struct client_conf_t{
    char *rcvport;
    char *mgroup;
    char *player_cmd;
};

extern struct client_conf_t client_conf;

#endif