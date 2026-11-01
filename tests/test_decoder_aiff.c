#include "sauti_decoder.h"
#include "sauti_decoder_aiff.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint8_t b[256];size_t n;} File;
static void put32(uint8_t *p,uint32_t n){
    p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);
    p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;
}
static void begin(File *f,int aifc){
    memset(f,0,sizeof(*f));
    memcpy(f->b,"FORM",4);
    memcpy(f->b+8,aifc?"AIFC":"AIFF",4);f->n=12;
}
static void chunk(File *f,const char *tag,const uint8_t *data,size_t n){
    assert(f->n+n+9u<=sizeof(f->b));
    memcpy(f->b+f->n,tag,4);put32(f->b+f->n+4,(uint32_t)n);
    memcpy(f->b+f->n+8,data,n);f->n+=8+n;
    if(n&1u)f->b[f->n++]=0;
}
static void finish(File *f){put32(f->b+4,(uint32_t)(f->n-8u));}
static int open_decoder(SautiDecoder *d,const File *f){
    const SautiDecoderOps *list[]={&sauti_decoder_aiff_ops};
    return sauti_decoder_open(d,list,1,f->b,f->n);
}
static void mono(void){
    File f;begin(&f,0);
    uint8_t comm[]={0,1,0,0,0,2,0,16,0x40,0x0e,0xac,0x44,0,0,0,0,0,0};
    uint8_t ssnd[]={0,0,0,0,0,0,0,0,0x80,0,0x7f,0xff};
    uint8_t meta[]={1,2,3};
    chunk(&f,"COMM",comm,sizeof(comm));
    chunk(&f,"ANNO",meta,sizeof(meta));
    chunk(&f,"SSND",ssnd,sizeof(ssnd));finish(&f);
    SautiDecoder d={0};float pcm[2]={0};
    assert(open_decoder(&d,&f));
    assert(d.info.channels==1&&d.info.sample_rate==44100&&d.info.total_frames==2);
    assert(sauti_decoder_read(&d,pcm,2)==2);
    assert(pcm[0]==-1.f&&pcm[1]>0.999f);
    assert(sauti_decoder_seek(&d,1));
    assert(sauti_decoder_read(&d,pcm,1)==1&&pcm[0]>0.999f);
    assert(!sauti_decoder_seek(&d,3));sauti_decoder_close(&d);
    f.b[4]=0xff;assert(!open_decoder(&d,&f));
}
static void stereo_aifc(void){
    File f;begin(&f,1);
    uint8_t comm[]={0,2,0,0,0,2,0,16,0x40,0x0e,0xac,0x44,0,0,0,0,0,0,
                    's','o','w','t'};
    uint8_t ssnd[]={0,0,0,0,0,0,0,0,0,0x80,0xff,0x7f,0,0,0,0x40};
    chunk(&f,"COMM",comm,sizeof(comm));chunk(&f,"SSND",ssnd,sizeof(ssnd));finish(&f);
    SautiDecoder d={0};float pcm[4];
    assert(open_decoder(&d,&f));
    assert(d.info.channels==2 && d.info.total_frames==2);
    assert(sauti_decoder_read(&d,pcm,2)==2);
    assert(pcm[0]==-1.f && pcm[1]>0.999f && pcm[2]==0.f && pcm[3]==0.5f);
    sauti_decoder_close(&d);
    /* Unsupported AIFC compression may never be mistaken for PCM. */
    memcpy(f.b+12+8+18,"ima4",4);assert(!open_decoder(&d,&f));
}
static void invalid(void){
    File f;begin(&f,0);
    uint8_t comm[]={0,1,0,0,0,3,0,24,0x40,0x0e,0xac,0x44,0,0,0,0,0,0};
    uint8_t ssnd[]={0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    chunk(&f,"COMM",comm,sizeof(comm));chunk(&f,"SSND",ssnd,sizeof(ssnd));finish(&f);
    SautiDecoder d={0};
    assert(!open_decoder(&d,&f)); /* PCM shorter than declared frames. */
    f.b[f.n-1]=0xff;assert(!open_decoder(&d,&f));
}
int main(void){mono();stereo_aifc();invalid();puts("PASS AIFF PCM: mono, AIFC sowt stereo, seek, bounds, rejection");return 0;}
