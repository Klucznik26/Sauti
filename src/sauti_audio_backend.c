#include "sauti_audio_backend.h"
int sauti_audio_open(SautiAudioBackend *b,const SautiAudioBackendOps *ops,
                     SautiStereoRing *stereo,SautiAudioRing *mono,
                     SautiRecordRing *record,int monitor,const char *target){
    if(!b||!ops||!ops->open||!ops->status||!ops->sample_rate||
       !ops->record_enable||!ops->close||b->handle||
       ((!stereo&&!mono)||(stereo&&mono)))return 0;
    void *handle=ops->open(stereo,mono,record,monitor,target);
    if(!handle)return 0;
    b->ops=ops;b->handle=handle;
    return 1;
}
int sauti_audio_status(const SautiAudioBackend *b){
    return b&&b->handle&&b->ops?b->ops->status(b->handle):-1;
}
uint32_t sauti_audio_sample_rate(const SautiAudioBackend *b){
    return b&&b->handle&&b->ops?b->ops->sample_rate(b->handle):0;
}
void sauti_audio_record_enable(SautiAudioBackend *b,int enabled){
    if(b&&b->handle&&b->ops)b->ops->record_enable(b->handle,enabled);
}
void sauti_audio_close(SautiAudioBackend *b){
    if(!b)return;
    if(b->handle&&b->ops)b->ops->close(b->handle);
    b->handle=0;b->ops=0;
}
