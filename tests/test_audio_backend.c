#include "sauti_audio_backend.h"
#include <assert.h>
#include <stdio.h>
static int closed=0,armed=0;
static void *fake_open(SautiStereoRing *s,SautiAudioRing *m,SautiRecordRing *r,
                       int monitor,const char *target){
    (void)m;(void)r;(void)monitor;(void)target;
    return s?(void *)s:NULL;
}
static int fake_status(const void *h){return h?3:-1;}
static uint32_t fake_rate(const void *h){return h?44100u:0u;}
static void fake_arm(void *h,int enabled){if(h)armed=enabled;}
static void fake_close(void *h){if(h)++closed;}
int main(void){
    const SautiAudioBackendOps ops={fake_open,fake_status,fake_rate,fake_arm,fake_close};
    SautiAudioBackend backend={0};
    SautiStereoRing *ring=(SautiStereoRing *)&closed;
    assert(sauti_audio_status(&backend)==-1);
    assert(sauti_audio_sample_rate(&backend)==0);
    assert(!sauti_audio_open(&backend,&ops,NULL,NULL,NULL,0,NULL));
    assert(sauti_audio_open(&backend,&ops,ring,NULL,NULL,0,NULL));
    assert(!sauti_audio_open(&backend,&ops,ring,NULL,NULL,0,NULL));
    assert(sauti_audio_status(&backend)==3);
    assert(sauti_audio_sample_rate(&backend)==44100);
    sauti_audio_record_enable(&backend,1);
    assert(armed==1);
    sauti_audio_close(&backend);
    assert(closed==1&&backend.handle==NULL&&backend.ops==NULL);
    sauti_audio_close(&backend);
    assert(closed==1);
    puts("Sauti audio backend contract tests passed");
    return 0;
}
