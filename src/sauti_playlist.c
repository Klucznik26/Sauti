#include "sauti_playlist.h"
#include <string.h>
static char *slot(char *items,size_t stride,int index){return items+stride*(size_t)index;}
int sauti_queue_add(char *items,size_t stride,int *count,int cap,const char *path){
    if(!items||!count||!path||!path[0]||stride<2||*count<0||*count>=cap)return 0;
    size_t n=strlen(path);
    if(n>=stride)return 0;
    for(int i=0;i<*count;++i)
        if(strcmp(slot(items,stride,i),path)==0)return 0;
    memcpy(slot(items,stride,*count),path,n+1);
    ++*count;return 1;
}
int sauti_queue_remove(char *items,size_t stride,int *count,int index){
    if(!items||!count||stride==0||index<0||index>=*count)return 0;
    --*count;
    if(index<*count)memmove(slot(items,stride,index),slot(items,stride,index+1),
                             (size_t)(*count-index)*stride);
    return 1;
}
int sauti_queue_take(char *items,size_t stride,int *count,int index,char *out,size_t cap){
    if(!items||!count||!out||index<0||index>=*count||!cap)return 0;
    const char *src=slot(items,stride,index);
    size_t n=strlen(src);
    if(n>=cap)return 0;
    memcpy(out,src,n+1);
    return sauti_queue_remove(items,stride,count,index);
}
int sauti_playlist_write(FILE *fp,const char *items,size_t stride,int count){
    if(!fp||!items||!stride||count<0)return 0;
    if(fputs("#EXTM3U\n",fp)<0)return 0;
    for(int i=0;i<count;++i)
        if(fprintf(fp,"%s\n",items+stride*(size_t)i)<0)return 0;
    return 1;
}
int sauti_playlist_read(FILE *fp,char *items,size_t stride,int *count,int cap,
                        int (*accept)(const char *,void *),void *ctx){
    if(!fp||!items||!count||stride<2||cap<0)return 0;
    *count=0;
    char line[8192];
    while(*count<cap && fgets(line,sizeof(line),fp)){
        size_t n=strcspn(line,"\r\n");
        /* A full buffer without line ending may be only a fragment.
         * An EOF-terminated final entry, however, is a valid M3U line. */
        int terminated=line[n]=='\r'||line[n]=='\n';
        if(!terminated && !feof(fp)){
            int c=fgetc(fp);
            if(c!=EOF){
                while(c!='\n'&&c!=EOF)c=fgetc(fp);
                continue;
            }
        }
        if(n==0||n>=stride||line[0]=='#')continue;
        line[n]=0;
        if(accept&&!accept(line,ctx))continue;
        (void)sauti_queue_add(items,stride,count,cap,line);
    }
    return !ferror(fp);
}
