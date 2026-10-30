#include "sauti_decoder_wav.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    const uint8_t *samples;
    uint64_t frames;
    uint64_t position;
    uint16_t channels;
    uint16_t bits;
    uint16_t format;
} WavState;

static uint16_t le16(const uint8_t *p){
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1]<<8));
}
static uint32_t le32(const uint8_t *p){
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static int wav_probe(const uint8_t *p,size_t n){
    return p && n>=12 && !memcmp(p,"RIFF",4) && !memcmp(p+8,"WAVE",4);
}
static void *wav_open(const uint8_t *p,size_t n,SautiPcmInfo *info){
    if(!wav_probe(p,n) || !info)return NULL;
    size_t riff_end=(size_t)le32(p+4)+8u;
    if(riff_end<12 || riff_end>n)return NULL;
    uint16_t format=0,channels=0,bits=0,align=0;
    uint32_t sample_rate=0;
    const uint8_t *audio=NULL;size_t audio_bytes=0;
    int fmt_seen=0;
    for(size_t off=12;off<=riff_end && riff_end-off>=8;){
        uint32_t sz=le32(p+off+4);
        size_t start=off+8;
        if(sz>riff_end-start)return NULL;
        if(!memcmp(p+off,"fmt ",4)){
            if(sz<16 || fmt_seen)return NULL;
            format=le16(p+start);
            channels=le16(p+start+2);
            sample_rate=le32(p+start+4);
            align=le16(p+start+12);
            bits=le16(p+start+14);
            if(format==0xfffe){
                static const uint8_t guid_tail[12]={0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if(sz<40 || le16(p+start+16)<22 ||
                   memcmp(p+start+28,guid_tail,12)!=0 ||
                   le16(p+start+18)==0 || le16(p+start+18)>bits ||
                   p[start+26]!=0 || p[start+27]!=0)return NULL;
                format=le16(p+start+24);
            }
            fmt_seen=1;
        }else if(!memcmp(p+off,"data",4) && !audio){
            audio=p+start;audio_bytes=sz;
        }
        size_t step=(size_t)sz+(sz&1u);
        if(step>riff_end-start)break;
        off=start+step;
    }
    if(!fmt_seen || !audio || format!=1 || !sample_rate ||
       channels==0 || channels>32 ||
       !((format==1 && (bits==8 || bits==16 || bits==24 || bits==32)) ||
         (format==3 && (bits==32 || bits==64))) ||
       align!=(uint16_t)(channels*(bits/8)) || audio_bytes%align)return NULL;
    WavState *w=malloc(sizeof(*w));
    if(!w)return NULL;
    w->samples=audio;w->frames=audio_bytes/align;w->position=0;
    w->channels=channels;w->bits=bits;w->format=format;
    info->sample_rate=sample_rate;
    info->channels=channels;
    info->total_frames=w->frames;
    return w;
}
static size_t wav_read(void *ctx,float *out,size_t frames){
    WavState *w=ctx;
    uint64_t remaining=w->frames-w->position;
    if((uint64_t)frames>remaining)frames=(size_t)remaining;
    size_t channels=w->channels;
    size_t offset=(size_t)w->position*channels*(w->bits/8u);
    const uint8_t *src=w->samples+offset;
    for(size_t i=0;i<frames*channels;++i){
        const uint8_t *v=src+i*(w->bits/8u);
        if(w->format==3){
            if(w->bits==32){
                uint32_t raw=le32(v);
                float x;
                memcpy(&x,&raw,4);
                out[i]=isfinite(x)?x:0.0f;
            }else{
                uint64_t raw=(uint64_t)le32(v)|((uint64_t)le32(v+4)<<32);
                double x;
                memcpy(&x,&raw,8);
                out[i]=isfinite(x)?(float)x:0.0f;
            }
        }else if(w->bits==8){
            out[i]=((float)v[0]-128.0f)/128.0f;
        }else{
            uint32_t raw=w->bits==16?le16(v):
                w->bits==24?((uint32_t)v[0]|((uint32_t)v[1]<<8)|((uint32_t)v[2]<<16)):
                le32(v);
            int32_t value=(int32_t)(raw<<(32u-w->bits));
            out[i]=(float)value/2147483648.0f;
        }
    }
    w->position+=frames;
    return frames;
}
static int wav_seek(void *ctx,uint64_t frame){
    WavState *w=ctx;
    if(frame>w->frames)return 0;
    w->position=frame;
    return 1;
}
static void wav_close(void *ctx){free(ctx);}
const SautiDecoderOps sauti_decoder_wav_ops={
    "wav-pcm",wav_probe,wav_open,wav_read,wav_seek,wav_close
};
