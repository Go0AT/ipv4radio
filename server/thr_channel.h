#ifndef THR_CHANNEL_H__
#define THR_CHANNEL_H__
#include "medialib.h"
int thr_channel_create(mlib_listentry_t *);
int thr_channel_destroy(mlib_listentry_t *);
int thr_channel_destroyall();
#endif