#ifndef MEDIALIB_H__
#define MEDIALIB_H__
#include "../include/proto.h"
#include <cstdlib>
struct mlib_listentry_t{
    chnid_t chnid;
    char *desc;
};

int mlib_getchnlist(mlib_listentry_t **,int *);
int mlib_freechnlist(mlib_listentry_t *);
size_t mlib_readchn(chnid_t ,void *,size_t );

#endif