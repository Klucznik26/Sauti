#include "sauti_decoder.h"
#include <string.h>

int sauti_decoder_ops_valid(const SautiDecoderOps *ops){
    return ops && ops->name && ops->probe && ops->open &&
           ops->read && ops->seek && ops->close;
}

void sauti_decoder_close(SautiDecoder *decoder){
    if(!decoder)return;
    if(decoder->state && decoder->ops && decoder->ops->close)
        decoder->ops->close(decoder->state);
    memset(decoder,0,sizeof(*decoder));
}

int sauti_decoder_open(SautiDecoder *out,const SautiDecoderOps *const *registry,
                       size_t count,const uint8_t *data,size_t length){
    if(!out)return 0;
    memset(out,0,sizeof(*out));
    if(!registry || !data || !length)return 0;
    for(size_t i=0;i<count;++i){
        const SautiDecoderOps *ops=registry[i];
        if(!sauti_decoder_ops_valid(ops) || !ops->probe(data,length))continue;
        SautiPcmInfo info={0};
        void *state=ops->open(data,length,&info);
        if(!state)continue;
        if(info.sample_rate==0 || info.channels==0 || info.channels>32){
            ops->close(state);
            continue;
        }
        out->ops=ops;out->state=state;out->info=info;
        return 1;
    }
    return 0;
}

size_t sauti_decoder_read(SautiDecoder *decoder,float *samples,size_t frames){
    if(!decoder || !decoder->state || !decoder->ops || !samples || !frames)return 0;
    size_t got=decoder->ops->read(decoder->state,samples,frames);
    return got<=frames?got:0;
}

int sauti_decoder_seek(SautiDecoder *decoder,uint64_t frame){
    return decoder && decoder->state && decoder->ops &&
           decoder->ops->seek(decoder->state,frame);
}
