#ifndef SAUTI_LIBRARY_H
#define SAUTI_LIBRARY_H
#include <stddef.h>
/* Filename and selection rules, independent of Linux/SDL/OS APIs. */
int sauti_library_name(const char *filename,char *out,size_t cap);
int sauti_library_find(const char *names,size_t stride,int count,const char *wanted);
size_t sauti_library_append_name(char *buffer,size_t cap,const char *input);
void sauti_library_backspace(char *buffer);
#endif
