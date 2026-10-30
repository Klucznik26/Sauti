#ifndef SAUTI_PLAYLIST_H
#define SAUTI_PLAYLIST_H
#include <stddef.h>
#include <stdio.h>
/* Platform-independent queue operations on caller-owned fixed-width slots.
 * No SDL, audio-server, filesystem-path, or OS APIs are used here. */
int sauti_queue_add(char *slots,size_t stride,int *count,int capacity,const char *path);
int sauti_queue_remove(char *slots,size_t stride,int *count,int index);
int sauti_queue_take(char *slots,size_t stride,int *count,int index,char *out,size_t cap);
int sauti_playlist_write(FILE *fp,const char *slots,size_t stride,int count);
int sauti_playlist_read(FILE *fp,char *slots,size_t stride,int *count,int capacity,
                        int (*accept)(const char *path,void *ctx),void *ctx);
#endif
