#include "sauti_decoder.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct { size_t at; } Dummy;
static int probe_no(const uint8_t *d,size_t n){(void)d;(void)n;return 0;}
static int probe_yes(const uint8_t *d,size_t n){return n>=3 && !memcmp(d,"PCM",3);}
static void *open_pcm(const uint8_t *d,size_t n,SautiPcmInfo *info){
    (void)d;(void)n;
    Dummy *p=malloc(sizeof(*p));if(!p)return NULL;
    p->at=0;info->channels=2;info->sample_rate=48000;info->total_frames=4;
    return p;
}
static size_t read_pcm(void *v,float *pcm,size_t frames){
    Dummy *d=v;size_t count=4-d->at;
    if(count>frames)count=frames;
    for(size_t i=0;i<count*2;++i)pcm[i]=0.25f;
    d->at+=count;return count;
}
static int seek_pcm(void *v,uint64_t frame){
    if(frame>4)return 0;
    ((Dummy*)v)->at=(size_t)frame;
    return 1;
}
static void close_pcm(void *v){free(v);}
int main(void){
    static const SautiDecoderOps no={"no",probe_no,open_pcm,read_pcm,seek_pcm,close_pcm};
    static const SautiDecoderOps yes={"test-pcm",probe_yes,open_pcm,read_pcm,seek_pcm,close_pcm};
    const SautiDecoderOps *list[]={&no,&yes};
    const uint8_t bytes[]={'P','C','M'};
    SautiDecoder dec={0};float pcm[8]={0};
    assert(!sauti_decoder_open(&dec,list,2,(const uint8_t*)"XYZ",3));
    assert(sauti_decoder_open(&dec,list,2,bytes,sizeof(bytes)));
    assert(!strcmp(dec.ops->name,"test-pcm"));
    assert(dec.info.channels==2 && dec.info.sample_rate==48000);
    assert(sauti_decoder_read(&dec,pcm,3)==3 && pcm[0]==0.25f);
    assert(sauti_decoder_read(&dec,pcm,3)==1);
    assert(sauti_decoder_read(&dec,pcm,3)==0);
    assert(sauti_decoder_seek(&dec,1));
    assert(sauti_decoder_read(&dec,pcm,3)==3);
    assert(!sauti_decoder_seek(&dec,5));
    sauti_decoder_close(&dec);
    assert(sauti_decoder_read(&dec,pcm,2)==0);
    assert(!sauti_decoder_seek(&dec,0));
    return 0;
}
