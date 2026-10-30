#include "sauti_pcm_output.h"
#include <assert.h>
typedef struct {size_t queued;int paused;int opened;} Sink;
static int open_sink(void *c,SautiPcmFormat f){
    Sink *s=c;
    if(f.sample_rate!=48000 || f.channels!=2)return 0;
    s->opened=1;
    return 1;
}
static size_t write_sink(void *c,const float *samples,size_t n){
    Sink *s=c;
    if(samples[0]==0.25f)s->queued+=n;
    return n;
}
static void pause_sink(void *c,int paused){((Sink*)c)->paused=paused;}
static void flush_sink(void *c){((Sink*)c)->queued=0;}
static size_t queued_sink(const void *c){return ((const Sink*)c)->queued;}
static void close_sink(void *c){((Sink*)c)->opened=0;}
int main(void){
    static const SautiPcmOutputOps ops={
        open_sink,write_sink,pause_sink,flush_sink,queued_sink,close_sink
    };
    SautiPcmOutput out={0};
    Sink s={0};
    float pcm[4]={0.25f,0.0f,0.0f,0.0f};
    assert(!sauti_pcm_output_write(&out,pcm,2));
    assert(sauti_pcm_output_open(&out,&s,&ops,(SautiPcmFormat){48000,2}));
    assert(!sauti_pcm_output_open(&out,&s,&ops,(SautiPcmFormat){48000,2}));
    assert(sauti_pcm_output_write(&out,pcm,2)==2);
    assert(sauti_pcm_output_queued(&out)==2);
    sauti_pcm_output_pause(&out,1);
    assert(s.paused);
    sauti_pcm_output_flush(&out);
    assert(!sauti_pcm_output_queued(&out));
    sauti_pcm_output_close(&out);
    assert(!s.opened && !out.opened);
    return 0;
}
