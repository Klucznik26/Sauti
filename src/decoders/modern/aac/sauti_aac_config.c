/* MPEG-4 AudioSpecificConfig: lightweight C grammar over ASM bitreader.
 * Only explicit hierarchical AOT 5/29 signaling is classified as HE-AAC.
 * An AAC-LC AOT 2 may still contain implicit SBR/PS; do not classify it
 * conclusively until the raw_data_block extension parser is implemented. */
#include "sauti_aac_headers.h"
#include <string.h>

static const uint32_t rates[]={
    96000,88200,64000,48000,44100,32000,24000,
    22050,16000,12000,11025,8000,7350
};
static int read_bits(const uint8_t *data,size_t bytes,size_t *pos,
                     unsigned count,uint32_t *value){
    return sauti_aac_bits_read_asm(data,bytes,pos,count,value);
}
static int read_aot(const uint8_t *data,size_t bytes,size_t *pos,
                    uint32_t *aot){
    if (!read_bits(data,bytes,pos,5,aot)) return 0;
    if (*aot==31u){
        uint32_t tail;
        if (!read_bits(data,bytes,pos,6,&tail))return 0;
        *aot=32u+tail;
    }
    return 1;
}
static int read_rate(const uint8_t *data,size_t bytes,size_t *pos,
                     uint32_t *rate){
    uint32_t index;
    if(!read_bits(data,bytes,pos,4,&index))return 0;
    if(index==15u){
        if(!read_bits(data,bytes,pos,24,rate))return 0;
        return *rate>=7350u && *rate<=192000u;
    }
    if(index>=13u)return 0;
    *rate=rates[index];
    return 1;
}
int sauti_aac_asc_parse(const uint8_t *data,size_t bytes,SautiAacConfig *out){
    if(!data||!out)return 0;
    size_t bit=0;
    uint32_t aot=0, rate=0, channels=0, core_aot=0, ext_rate=0;
    if(!read_aot(data,bytes,&bit,&aot) ||
       !read_rate(data,bytes,&bit,&rate) ||
       !read_bits(data,bytes,&bit,4,&channels) ||
       channels<1u || channels>2u) return 0;
    SautiAacConfig next;
    memset(&next,0,sizeof(next));
    next.core_sample_rate=rate;
    next.output_sample_rate=rate;
    next.core_channels=channels;
    next.output_channels=channels;
    if(aot==5u || aot==29u){
        if(!read_rate(data,bytes,&bit,&ext_rate) ||
           !read_aot(data,bytes,&bit,&core_aot) || core_aot!=2u)
            return 0;
        if(aot==29u && channels!=1u)return 0;
        next.output_sample_rate=ext_rate;
        next.output_channels=aot==29u?2u:channels;
        next.sbr_present=1;
        next.ps_present=aot==29u;
        next.explicit_extension=1;
        next.core_aot=core_aot;
    }else if(aot==2u){
        next.core_aot=2u;
        /* Optional syncExtensionType 0x2b7 / implicit FIL data will be
         * interpreted by a future bitstream stage. Not falsely advertised. */
    }else return 0;
    *out=next;
    return 1;
}
