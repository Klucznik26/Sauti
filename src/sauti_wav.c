#include "sauti_wav.h"
#include <math.h>
#include <stddef.h>
static int u16(FILE *f,uint16_t v){
    unsigned char b[2]={(unsigned char)v,(unsigned char)(v>>8)};
    return fwrite(b,1,2,f)==2;
}
static int u32(FILE *f,uint32_t v){
    unsigned char b[4]={(unsigned char)v,(unsigned char)(v>>8),
                        (unsigned char)(v>>16),(unsigned char)(v>>24)};
    return fwrite(b,1,4,f)==4;
}
static int header(FILE *f,uint32_t sample_rate,uint32_t bytes){
    return fwrite("RIFF",1,4,f)==4 && u32(f,36u+bytes) &&
        fwrite("WAVEfmt ",1,8,f)==8 && u32(f,16) &&
        u16(f,1) && u16(f,2) && u32(f,sample_rate) &&
        u32(f,sample_rate*4u) && u16(f,4) && u16(f,16) &&
        fwrite("data",1,4,f)==4 && u32(f,bytes);
}
int sauti_wav_begin(SautiWav *w,FILE *file,uint32_t rate){
    if(!w||!file||rate==0||rate>1073741823u)return 0;
    w->file=file;w->frames=0;w->sample_rate=rate;w->failed=0;
    if(!header(file,rate,0)){w->failed=1;return 0;}
    return 1;
}
static int16_t pcm(float x){
    if(!isfinite(x))return 0;
    if(x>=1.f)return 32767;
    if(x<=-1.f)return -32768;
    return (int16_t)lrintf(x*32767.f);
}
int sauti_wav_append(SautiWav *w,const float *l,const float *r,uint32_t n){
    if(!w||!w->file||w->failed||!l||!r)return 0;
    /* RIFF has 32-bit length fields; never produce a wrapped WAV header. */
    if(w->frames+(uint64_t)n>UINT32_MAX/4u-9u){w->failed=1;return 0;}
    unsigned char buf[2048];
    uint32_t offset=0;
    while(offset<n){
        uint32_t count=n-offset;
        if(count>512u)count=512u;
        for(uint32_t i=0;i<count;++i){
            uint16_t a=(uint16_t)pcm(l[offset+i]);
            uint16_t b=(uint16_t)pcm(r[offset+i]);
            buf[i*4]=(unsigned char)a;
            buf[i*4+1]=(unsigned char)(a>>8);
            buf[i*4+2]=(unsigned char)b;
            buf[i*4+3]=(unsigned char)(b>>8);
        }
        if(fwrite(buf,4,count,w->file)!=count){w->failed=1;return 0;}
        w->frames+=count;offset+=count;
    }
    return 1;
}
int sauti_wav_finish(SautiWav *w){
    if(!w||!w->file)return 0;
    FILE *f=w->file;
    int ok=!w->failed;
    /* Repair the header even after a partial write: a truncated recording
     * must never pretend to contain frames that weren't written. */
    if(fseek(f,0,SEEK_SET)!=0 ||
       !header(f,w->sample_rate,(uint32_t)(w->frames*4u)))ok=0;
    if(fclose(f)!=0)ok=0;
    w->file=NULL;
    w->failed=!ok;
    return ok;
}
