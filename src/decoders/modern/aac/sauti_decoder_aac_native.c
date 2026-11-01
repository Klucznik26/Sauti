/* Native AAC-LC stereo ADTS bridge into Sauti's portable decoder API.
 * No libavcodec / SDL / file I/O. One cached PCM frame; seek replays
 * earlier ADTS frames to recover exact IMDCT overlap and PNS state.
 * Open rejects unsupported frames rather than masking discontinuities. */
#include "sauti_decoder_aac_native.h"
#include "sauti_aac_cpe.h"
#include "sauti_aac_headers.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *bytes;
    size_t bytes_length;
    size_t *offsets; /* frame_count + 1, sentinel at end */
    size_t frame_count;
    size_t next_frame;
    uint64_t position;
    SautiAacLcSynthesis history[2];
    float cache[2048];
    size_t cache_frame;
} NativeAac;

static int probe(const uint8_t *data,size_t size){
    SautiAacAdts h={0};
    return data && size>=7 &&
           sauti_aac_adts_parse_asm(data,size,&h) &&
           h.core_aot==2u && h.core_channels==2u &&
           h.raw_blocks==1u && !h.crc_present;
}

static int reset(NativeAac *s){
    if(!sauti_aac_lc_synthesis_init(&s->history[0],0) ||
       !sauti_aac_lc_synthesis_init(&s->history[1],0))return 0;
    s->next_frame=0;
    s->cache_frame=1024;
    return 1;
}

static int decode_next(NativeAac *s){
    if(s->next_frame>=s->frame_count)return 0;
    const size_t offset=s->offsets[s->next_frame];
    const size_t length=s->offsets[s->next_frame+1]-offset;
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    if(sauti_aac_lc_adts_stereo_decode_ex(s->bytes+offset,length,
                                          s->history,s->cache,&reason)!=1)
        return 0;
    for(size_t i=0;i<2048;i++)
        if(!isfinite(s->cache[i]))return 0;
    s->next_frame++;
    s->cache_frame=0;
    return 1;
}
static void release(void *ctx){
    NativeAac *s=ctx;
    if(s){free(s->offsets);free(s);}
}
static void *open_native(const uint8_t *data,size_t length,SautiPcmInfo *info){
    if(!info || !probe(data,length))return NULL;
    /* 512 MiB input ceiling; decoded PCM is intentionally not buffered. */
    if(length>512u*1024u*1024u)return NULL;
    size_t offset=0,count=0,capacity=0;
    size_t *offsets=NULL;
    unsigned rate=0;
    while(offset<length){
        SautiAacAdts h={0};
        if(!sauti_aac_adts_parse_asm(data+offset,length-offset,&h) ||
           h.frame_bytes<7 || h.frame_bytes>length-offset ||
           h.core_aot!=2u || h.core_channels!=2u ||
           h.raw_blocks!=1u || h.crc_present)goto fail;
        if(!count)rate=h.core_sample_rate;
        if(rate!=h.core_sample_rate)goto fail;
        if(count>=SIZE_MAX/1024u)goto fail;
        if(count+2u>capacity){
            size_t next=capacity?capacity*2u:128u;
            if(next<capacity || next>SIZE_MAX/sizeof(*offsets))goto fail;
            size_t *resized=realloc(offsets,next*sizeof(*offsets));
            if(!resized)goto fail;
            offsets=resized;capacity=next;
        }
        offsets[count++]=offset;
        offset+=h.frame_bytes;
    }
    if(!count)goto fail;
    offsets[count]=length;
    NativeAac *s=calloc(1,sizeof(*s));
    if(!s)goto fail;
    s->bytes=data;s->bytes_length=length;
    s->offsets=offsets;s->frame_count=count;
    if(!reset(s)){release(s);return NULL;}
    /* Verify complete, uninterrupted decoding at open. The caller will
     * never see a half-playable stream using unsupported AAC-LC tools. */
    for(size_t i=0;i<count;i++){
        if(!decode_next(s)){release(s);return NULL;}
    }
    if(!reset(s)){release(s);return NULL;}
    s->position=0;
    info->sample_rate=rate;
    info->channels=2;
    info->total_frames=(uint64_t)count*1024u;
    return s;
fail:
    free(offsets);
    return NULL;
}
static size_t read_native(void *ctx,float *output,size_t frames){
    NativeAac *s=ctx;
    if(!s || !output || !frames)return 0;
    const uint64_t total=(uint64_t)s->frame_count*1024u;
    if(s->position>=total)return 0;
    uint64_t available=total-s->position;
    if((uint64_t)frames>available)frames=(size_t)available;
    size_t written=0;
    while(written<frames){
        if(s->cache_frame==1024u && !decode_next(s))break;
        size_t take=frames-written;
        if(take>1024u-s->cache_frame)take=1024u-s->cache_frame;
        memcpy(output+written*2u,s->cache+s->cache_frame*2u,
               take*2u*sizeof(float));
        s->cache_frame+=take;
        written+=take;
        s->position+=take;
    }
    return written;
}
static int seek_native(void *ctx,uint64_t position){
    NativeAac *s=ctx;
    if(!s || position>(uint64_t)s->frame_count*1024u)return 0;
    if(position==s->position)return 1;
    /* Replaying from the start gives exact overlap, window and RNG
     * history, including when seeking into the middle of a frame. */
    if(!reset(s))return 0;
    const uint64_t frame_index=position/1024u;
    for(uint64_t i=0;i<frame_index;i++)
        if(!decode_next(s))return 0;
    s->cache_frame=1024u; /* consumed history frames, no pending PCM */
    if(position%1024u){
        if(!decode_next(s))return 0;
        s->cache_frame=(size_t)(position%1024u);
    }
    s->position=position;
    return 1;
}
const SautiDecoderOps sauti_decoder_aac_native_ops={
    "aac-lc-native-adts",probe,open_native,read_native,seek_native,release
};
