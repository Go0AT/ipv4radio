#ifndef SERVER_CONF_H__
#define SERVER_CONF_H__
/**
 * 默认路径也需要随不同的机器环境变动
 * */
#define DEFAULT_MEDIADIR "/var/media"
#define DEFAULT_IF "eth0"

enum{
    RUN_DAEMON = 1,
    RUN_FOREGROUND
};
struct server_conf_t{
    char *rcvport;
    char *mgroup;
    char *media_dir;
    char runmode;
    char *ifname;
};


#endif