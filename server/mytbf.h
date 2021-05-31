#ifndef MYTBF_H__
#define MYTBF_H__

#define MYTBF_MAX 1024
struct mytbf_t;

struct mytbf_t *mytbf_init(int cps,int burst);
int mytbf_fetchtoken(struct mytbf_t *,int );
int mytbf_returntoken(struct mytbf_t *,int );
int mytbf_destroy(struct mytbf_t * );

#endif