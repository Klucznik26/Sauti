#include "sauti_pcm_output.h"
#include <string.h>
int sauti_pcm_output_open(SautiPcmOutput *out,void *context,
                          const SautiPcmOutputOps *ops,SautiPcmFormat format){
    if(!out || !context || !ops || !ops->open || !ops->write ||
       !ops->pause || !ops->flush || !ops->queued_frames || !ops->close ||
       !format.sample_rate || !format.channels || format.channels>32 ||
       out->opened)return 0;
    if(!ops->open(context,format))return 0;
    out->context=context;
    out->ops=ops;
    out->format=format;
    out->opened=1;
    return 1;
}
size_t sauti_pcm_output_write(SautiPcmOutput *out,const float *pcm,size_t frames){
    if(!out || !out->opened || !pcm || !frames)return 0;
    size_t written=out->ops->write(out->context,pcm,frames);
    return written<=frames?written:0;
}
void sauti_pcm_output_pause(SautiPcmOutput *out,int pause){
    if(out && out->opened)out->ops->pause(out->context,pause!=0);
}
void sauti_pcm_output_flush(SautiPcmOutput *out){
    if(out && out->opened)out->ops->flush(out->context);
}
size_t sauti_pcm_output_queued(const SautiPcmOutput *out){
    return out && out->opened?out->ops->queued_frames(out->context):0;
}
void sauti_pcm_output_close(SautiPcmOutput *out){
    if(!out)return;
    if(out->opened)out->ops->close(out->context);
    memset(out,0,sizeof(*out));
}
