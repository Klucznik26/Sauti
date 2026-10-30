/* Atari Falcon WinRec DVSM: unpacked PCM and Pack-2 mono/stereo Delta.
 * NASM performs sample conversion and saturated Delta accumulation.
 * ADPCM and PACK override are not yet supported. */
#include "sauti_decoder_dvsm.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
typedef struct {
    const uint8_t *pcm;
    uint64_t frames, pos;
    uint32_t channels,bits;
    float *decoded;
} DvsmState;
extern void sauti_dvsm_pcm_f32_asm(const uint8_t*,float*,size_t,uint32_t);
extern void sauti_dvsm_delta_mono_asm(const uint8_t*,float*,size_t,const int16_t*);
extern void sauti_dvsm_delta_stereo_block_asm(const uint8_t*,float*,size_t,const int16_t*);
extern void sauti_dvsm_voice_mono_asm(const uint8_t*,float*,size_t,const int16_t*);
extern void sauti_dvsm_voice_stereo_block_asm(const uint8_t*,float*,size_t,const int16_t*);
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint16_t be16(const uint8_t *p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static int probe(const uint8_t *p,size_t n){
    return p&&n>=6&&!memcmp(p,"DVSM\0\0",6);
}
static void *open_dvsm(const uint8_t *p,size_t n,SautiPcmInfo *info){
    static const uint32_t rates[]={8195,9834,12293,16390,19668,24585,32780,49170};
    if(!info||!probe(p,n)||n<16)return NULL;
    size_t offset=be16(p+6);
    uint32_t hz=be16(p+8);
    uint32_t pack=p[10],mode=p[11],channels=(mode&2u)?1u:2u;
    uint32_t bits=(mode&1u)?16u:8u;
    if(mode>3||offset<16||offset>n)return NULL;
    if(pack!=0 && pack!=2 && pack!=4)return NULL;
    if((pack==2 || pack==4) && bits!=16)return NULL;
    if(hz<8)hz=rates[hz];
    if(hz<256||hz>384000)return NULL;
    /* WinRec extension cookies: four-byte ID, big-endian 32-bit total length.
     * Avoid interpreting unknown extensions until verified; skip using header length.
     */
    const size_t frame_bytes=(size_t)channels*(bits/8u);
    size_t bytes=n-offset;
    if(bytes==0)return NULL;
    if(pack==0 && bytes%frame_bytes)return NULL;
    if((pack==2 || pack==4) && bytes<2)return NULL;
    uint64_t stereo_frames=0;
    uint32_t block=be32(p+12);
    if((pack==2 || pack==4) && channels==2){
        /* Each stereo block resets both predictors with a 4-byte L/R seed.
         * The trailing block may be shorter but must hold whole LR pairs. */
        if(block<4 || (pack==2 && (block&1u)))return NULL;
        size_t at=0;
        while(at<bytes){
            size_t part=bytes-at;
            if(part>block)part=block;
            if(part<4 || (pack==2 && (part&1u)))return NULL;
            stereo_frames+=(uint64_t)(1+(part-4)/(pack==2?2u:1u));
            if(stereo_frames>SIZE_MAX/(2u*sizeof(float)))return NULL;
            at+=part;
        }
    }
    DvsmState *s=calloc(1,sizeof(*s));
    if(!s)return NULL;
    s->pcm=p+offset;s->frames=pack==0?bytes/frame_bytes:(channels==1?(pack==2?(uint64_t)(bytes-1):(uint64_t)(1+2*(bytes-2))):stereo_frames);
    if(pack==2 || pack==4){
        if(s->frames>SIZE_MAX/(channels*sizeof(float))){free(s);return NULL;}
        s->decoded=malloc((size_t)s->frames*channels*sizeof(float));
        if(!s->decoded){free(s);return NULL;}
        int16_t steps[256]={0};
        if(pack==4){
            static const int16_t voice[16]={-8192,-4096,-2048,-1024,-512,-256,-64,0,64,256,512,1024,2048,4096,8192,16384};
            memcpy(steps,voice,sizeof(voice));
        }
        if(pack==2)for(unsigned i=1;i<128;i++){
            double v=floor(pow(1.084618362,(double)i)+0.5);
            if(v>32767.0)v=32767.0;
            steps[i]=(int16_t)v;
        }
        if(pack==2)steps[128]=-32768;
        if(pack==2)for(unsigned i=129;i<256;i++){
            double v=floor(pow(1.084618362,(double)(256-i))+0.5);
            if(v>32767.0)v=32767.0;
            steps[i]=(int16_t)-v;
        }
        if(channels==1){
            if(pack==2)sauti_dvsm_delta_mono_asm(s->pcm,s->decoded,(size_t)s->frames,steps);
            else sauti_dvsm_voice_mono_asm(s->pcm,s->decoded,(size_t)s->frames,steps);
        }else{
            size_t at=0,frame=0;
            while(at<bytes){
                size_t part=bytes-at;
                if(part>block)part=block;
                size_t count=1+(part-4)/(pack==2?2u:1u);
                if(pack==2)sauti_dvsm_delta_stereo_block_asm(s->pcm+at,s->decoded+frame*2,count,steps);
                else sauti_dvsm_voice_stereo_block_asm(s->pcm+at,s->decoded+frame*2,count,steps);
                frame+=count;
                at+=part;
            }
        }
    }
    s->channels=channels;s->bits=bits;
    info->sample_rate=hz;info->channels=channels;info->total_frames=s->frames;
    return s;
}
static size_t read_dvsm(void *ctx,float *dst,size_t frames){
    DvsmState *s=ctx;
    if(!s||!dst)return 0;
    uint64_t available=s->frames-s->pos;
    if((uint64_t)frames>available)frames=(size_t)available;
    if(!frames)return 0;
    if(s->decoded){
        memcpy(dst,s->decoded+(size_t)s->pos*s->channels,frames*s->channels*sizeof(float));
    }else{
        size_t bps=s->bits/8u;
        const uint8_t *p=s->pcm+(size_t)s->pos*s->channels*bps;
        sauti_dvsm_pcm_f32_asm(p,dst,frames*s->channels,s->bits);
    }
    s->pos+=frames;
    return frames;
}
static int seek_dvsm(void *ctx,uint64_t frame){
    DvsmState *s=ctx;
    if(!s||frame>s->frames)return 0;
    s->pos=frame;return 1;
}
static void close_dvsm(void *ctx){
    DvsmState *s=ctx;
    if(s){free(s->decoded);free(s);}
}
const SautiDecoderOps sauti_decoder_dvsm_ops={
    "atari-falcon-dvsm-pcm",probe,open_dvsm,read_dvsm,seek_dvsm,close_dvsm
};
