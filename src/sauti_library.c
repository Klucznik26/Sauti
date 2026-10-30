#include "sauti_library.h"
#include <string.h>
int sauti_library_name(const char *filename,char *out,size_t cap){
    if(!filename||!out||cap<2)return 0;
    size_t n=strlen(filename);
    if(n<=4||strcmp(filename+n-4,".m3u")!=0||n-4>=cap)return 0;
    for(size_t i=0;i<n-4;++i){
        unsigned char ch=(unsigned char)filename[i];
        if(!((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||
            (ch>='0'&&ch<='9')||ch==' '||ch=='_'||ch=='-'))return 0;
    }
    memcpy(out,filename,n-4);
    out[n-4]=0;
    return 1;
}
int sauti_library_find(const char *names,size_t stride,int count,const char *wanted){
    if(!names||!wanted||!stride||count<0)return -1;
    for(int i=0;i<count;++i)
        if(strcmp(names+(size_t)i*stride,wanted)==0)return i;
    return -1;
}
size_t sauti_library_append_name(char *buffer,size_t cap,const char *input){
    if(!buffer||!input||!cap)return 0;
    size_t n=0;
    while(n<cap&&buffer[n])++n;
    if(n>=cap)return 0;
    for(;*input&&n+1<cap;++input){
        unsigned char ch=(unsigned char)*input;
        if((ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||
           (ch>='0'&&ch<='9')||ch==' '||ch=='_'||ch=='-')
            buffer[n++]=(char)ch;
    }
    buffer[n]=0;
    return n;
}
void sauti_library_backspace(char *buffer){
    if(!buffer)return;
    size_t n=strlen(buffer);
    if(n)buffer[n-1]=0;
}
