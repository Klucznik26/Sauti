/* Native AAC-LC stereo ADTS bridge into Sauti's portable decoder API.
 * No libavcodec / SDL / file I/O. One cached PCM frame; seek replays
 * earlier ADTS frames to recover exact IMDCT overlap and PNS state.
 * Open validates all ADTS headers and the first AAC payload; later
 * payloads are decoded incrementally during read/seek. */
#include "sauti_decoder_aac_native.h"
#include "sauti_aac_cpe.h"
#include "sauti_aac_lc_multi.h"
#include "sauti_aac_headers.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { AAC_CHECKPOINT_FRAMES = 64 };
typedef struct {
    SautiAacLcSynthesis history[2];
    unsigned char valid;
} NativeCheckpoint;
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
    NativeCheckpoint *checkpoints;
    size_t checkpoint_count;
} NativeAac;



static int probe(const uint8_t *data,size_t size){
    SautiAacAdts h={0};
    return data && size>=7 &&
           sauti_aac_adts_parse_asm(data,size,&h) &&
           h.core_aot==2u && h.core_channels==2u &&
           h.raw_blocks==1u;
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
    if(s->checkpoints && s->next_frame%AAC_CHECKPOINT_FRAMES==0){
        const size_t k=s->next_frame/AAC_CHECKPOINT_FRAMES;
        if(k<s->checkpoint_count && !s->checkpoints[k].valid){
            memcpy(s->checkpoints[k].history,s->history,
                   sizeof(s->history));
            s->checkpoints[k].valid=1;
        }
    }
    s->cache_frame=0;
    return 1;
}
static void release(void *ctx){
    NativeAac *s=ctx;
    if(s){free(s->checkpoints);free(s->offsets);free(s);}
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
           h.raw_blocks!=1u)goto fail;
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
    s->checkpoint_count=count/AAC_CHECKPOINT_FRAMES+1u;
    if(s->checkpoint_count>SIZE_MAX/sizeof(*s->checkpoints)){
        release(s);return NULL;
    }
    s->checkpoints=calloc(s->checkpoint_count,sizeof(*s->checkpoints));
    if(!s->checkpoints){release(s);return NULL;}
    memcpy(s->checkpoints[0].history,s->history,sizeof(s->history));
    s->checkpoints[0].valid=1;
    /* Prime only the first frame. The former full-stream decode on
     * open blocked the SDL UI on long songs (thousands of IMDCT frames),
     * then decoded the same stream again during playback. Future frames
     * are decoded lazily and a failure stops read() rather than inserting
     * invented PCM. Header validation above remains whole-stream. */
    if(!decode_next(s)){release(s);return NULL;}
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
    const size_t target=(size_t)(position/1024u);
    size_t origin=0;
    /* Reuse the nearest known history snapshot. During ordinary playback
     * a checkpoint is saved every 64 frames. Repeated seeks into already
     * decoded regions then replay at most 63 full frames. For a first
     * forward jump to untouched audio, replay is still proportional to
     * distance; a background seek worker remains future work. */
    for(size_t k=target/AAC_CHECKPOINT_FRAMES+1u;k>0;--k){
        const size_t slot=k-1u;
        if(slot<s->checkpoint_count && s->checkpoints[slot].valid){
            memcpy(s->history,s->checkpoints[slot].history,
                   sizeof(s->history));
            origin=slot*AAC_CHECKPOINT_FRAMES;
            break;
        }
    }
    s->next_frame=origin;
    s->cache_frame=1024u;
    for(size_t i=origin;i<target;i++)
        if(!decode_next(s))return 0;
    s->cache_frame=1024u;
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

/* Natywny AAC-LC MONO ADTS (jeden SCE, jeden raw_data_block na ramke): ten sam kontrakt co adapter stereo powyzej, jeden kanal.  Strumien HE-AAC mono (FIL z SBR) jest odrzucany przy
 * otwarciu (rdzen LC nie pomija SBR), wiec rejestr moze miec ten adapter PRZED adapterem HE: HE otworzy sie dopiero, gdy LC odmowi. */
typedef struct {
    SautiAacLcSynthesis history[1];
    unsigned char valid;
} NativeMonoCheckpoint;
typedef struct {
    const uint8_t *bytes;
    size_t bytes_length;
    size_t *offsets; /* frame_count + 1, sentinel at end */
    size_t frame_count;
    size_t next_frame;
    uint64_t position;
    SautiAacLcSynthesis history[1];
    float cache[1024];
    size_t cache_frame;
    NativeMonoCheckpoint *checkpoints;
    size_t checkpoint_count;
} NativeMonoAac;

static int mono_probe(const uint8_t *data,size_t size){
    SautiAacAdts h={0};
    return data && size>=7 &&
           sauti_aac_adts_parse_asm(data,size,&h) &&
           h.core_aot==2u && h.core_channels==1u &&
           h.raw_blocks==1u;
}

static int mono_reset(NativeMonoAac *s){
    if(!sauti_aac_lc_synthesis_init(&s->history[0],0))return 0;
    s->next_frame=0;
    s->cache_frame=1024;
    return 1;
}

static int mono_decode_next(NativeMonoAac *s){
    if(s->next_frame>=s->frame_count)return 0;
    const size_t offset=s->offsets[s->next_frame];
    const size_t length=s->offsets[s->next_frame+1]-offset;
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    if(sauti_aac_lc_adts_mono_decode_ex(s->bytes+offset,length,
                                        &s->history[0],s->cache,&reason)!=1)
        return 0;
    for(size_t i=0;i<1024;i++)
        if(!isfinite(s->cache[i]))return 0;
    s->next_frame++;
    if(s->checkpoints && s->next_frame%AAC_CHECKPOINT_FRAMES==0){
        const size_t k=s->next_frame/AAC_CHECKPOINT_FRAMES;
        if(k<s->checkpoint_count && !s->checkpoints[k].valid){
            memcpy(s->checkpoints[k].history,s->history,
                   sizeof(s->history));
            s->checkpoints[k].valid=1;
        }
    }
    s->cache_frame=0;
    return 1;
}
static void mono_release(void *ctx){
    NativeMonoAac *s=ctx;
    if(s){free(s->checkpoints);free(s->offsets);free(s);}
}
static void *mono_open_native(const uint8_t *data,size_t length,SautiPcmInfo *info){
    if(!info || !mono_probe(data,length))return NULL;
    /* 512 MiB input ceiling; decoded PCM is intentionally not buffered. */
    if(length>512u*1024u*1024u)return NULL;
    size_t offset=0,count=0,capacity=0;
    size_t *offsets=NULL;
    unsigned rate=0;
    while(offset<length){
        SautiAacAdts h={0};
        if(!sauti_aac_adts_parse_asm(data+offset,length-offset,&h) ||
           h.frame_bytes<7 || h.frame_bytes>length-offset ||
           h.core_aot!=2u || h.core_channels!=1u ||
           h.raw_blocks!=1u)goto fail;
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
    NativeMonoAac *s=calloc(1,sizeof(*s));
    if(!s)goto fail;
    s->bytes=data;s->bytes_length=length;
    s->offsets=offsets;s->frame_count=count;
    if(!mono_reset(s)){mono_release(s);return NULL;}
    s->checkpoint_count=count/AAC_CHECKPOINT_FRAMES+1u;
    if(s->checkpoint_count>SIZE_MAX/sizeof(*s->checkpoints)){
        mono_release(s);return NULL;
    }
    s->checkpoints=calloc(s->checkpoint_count,sizeof(*s->checkpoints));
    if(!s->checkpoints){mono_release(s);return NULL;}
    memcpy(s->checkpoints[0].history,s->history,sizeof(s->history));
    s->checkpoints[0].valid=1;
    /* Prime only the first frame. The former full-stream decode on
     * open blocked the SDL UI on long songs (thousands of IMDCT frames),
     * then decoded the same stream again during playback. Future frames
     * are decoded lazily and a failure stops read() rather than inserting
     * invented PCM. Header validation above remains whole-stream. */
    if(!mono_decode_next(s)){mono_release(s);return NULL;}
    s->position=0;
    info->sample_rate=rate;
    info->channels=1u;
    info->total_frames=(uint64_t)count*1024u;
    return s;
fail:
    free(offsets);
    return NULL;
}
static size_t mono_read_native(void *ctx,float *output,size_t frames){
    NativeMonoAac *s=ctx;
    if(!s || !output || !frames)return 0;
    const uint64_t total=(uint64_t)s->frame_count*1024u;
    if(s->position>=total)return 0;
    uint64_t available=total-s->position;
    if((uint64_t)frames>available)frames=(size_t)available;
    size_t written=0;
    while(written<frames){
        if(s->cache_frame==1024u && !mono_decode_next(s))break;
        size_t take=frames-written;
        if(take>1024u-s->cache_frame)take=1024u-s->cache_frame;
        memcpy(output+written,s->cache+s->cache_frame,
               take*sizeof(float));
        s->cache_frame+=take;
        written+=take;
        s->position+=take;
    }
    return written;
}
static int mono_seek_native(void *ctx,uint64_t position){
    NativeMonoAac *s=ctx;
    if(!s || position>(uint64_t)s->frame_count*1024u)return 0;
    if(position==s->position)return 1;
    const size_t target=(size_t)(position/1024u);
    size_t origin=0;
    /* Reuse the nearest known history snapshot. During ordinary playback
     * a checkpoint is saved every 64 frames. Repeated seeks into already
     * decoded regions then replay at most 63 full frames. For a first
     * forward jump to untouched audio, replay is still proportional to
     * distance; a background seek worker remains future work. */
    for(size_t k=target/AAC_CHECKPOINT_FRAMES+1u;k>0;--k){
        const size_t slot=k-1u;
        if(slot<s->checkpoint_count && s->checkpoints[slot].valid){
            memcpy(s->history,s->checkpoints[slot].history,
                   sizeof(s->history));
            origin=slot*AAC_CHECKPOINT_FRAMES;
            break;
        }
    }
    s->next_frame=origin;
    s->cache_frame=1024u;
    for(size_t i=origin;i<target;i++)
        if(!mono_decode_next(s))return 0;
    s->cache_frame=1024u;
    if(position%1024u){
        if(!mono_decode_next(s))return 0;
        s->cache_frame=(size_t)(position%1024u);
    }
    s->position=position;
    return 1;
}
const SautiDecoderOps sauti_decoder_aac_mono_native_ops={
    "aac-lc-native-mono-adts",mono_probe,mono_open_native,mono_read_native,mono_seek_native,mono_release
};

/* The HE-AAC adapters (SBR, parametric stereo) live in sauti_decoder_aac_he.c. */


/* Natywny AAC-LC WIELOKANALOWY ADTS (channel_configuration 3..7: 3.0, 4.0, 5.0, 5.1, 7.1; jeden raw_data_block na ramke, bez CRC): ten sam kontrakt co adaptery
 * mono/stereo.  PCM jest w kolejnosci FFmpeg/WAVE (patrz sauti_aac_lc_multi.h).  Probe przyjmuje tylko >= 3 kanaly, wiec stereo/mono zostaja przy swoich adapterach. */
typedef struct {
    SautiAacLcMulti history;
    unsigned char valid;
} NativeMultiCheckpoint;
typedef struct {
    const uint8_t *bytes;
    size_t bytes_length;
    size_t *offsets; /* frame_count + 1, sentinel at end */
    size_t frame_count;
    size_t next_frame;
    uint64_t position;
    unsigned channels;
    unsigned config;                    /* channel_configuration ADTS (0 = uklad z PCE) */
    SautiAacLcLayout layout;            /* tylko config 0 */
    SautiAacLcMulti history;
    float cache[SAUTI_AAC_LC_MULTI_MAX*1024];
    size_t cache_frame;
    NativeMultiCheckpoint *checkpoints;
    size_t checkpoint_count;
} NativeMultiAac;

static int multi_probe(const uint8_t *data,size_t size){
    SautiAacAdts h={0};
    return data && size>=7 &&
           sauti_aac_adts_parse_asm(data,size,&h) &&
           h.core_aot==2u && (h.core_channels==0u || (h.core_channels>=3u && h.core_channels<=7u)) &&
           h.raw_blocks==1u && !h.crc_present;
}
static int multi_reset(NativeMultiAac *s,unsigned config){
    if(config==0u){ if(!sauti_aac_lc_multi_init_layout(&s->history,&s->layout,0))return 0; }
    else if(!sauti_aac_lc_multi_init(&s->history,config,0))return 0;
    s->next_frame=0;
    s->cache_frame=1024;
    return 1;
}
static int multi_decode_next(NativeMultiAac *s){
    if(s->next_frame>=s->frame_count)return 0;
    const size_t offset=s->offsets[s->next_frame];
    const size_t length=s->offsets[s->next_frame+1]-offset;
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    if(sauti_aac_lc_adts_multi_decode_ex(s->bytes+offset,length,&s->history,s->cache,&reason)!=1)
        return 0;
    for(size_t i=0;i<(size_t)1024u*s->channels;i++)
        if(!isfinite(s->cache[i]))return 0;
    s->next_frame++;
    if(s->checkpoints && s->next_frame%AAC_CHECKPOINT_FRAMES==0){
        const size_t k=s->next_frame/AAC_CHECKPOINT_FRAMES;
        if(k<s->checkpoint_count && !s->checkpoints[k].valid){
            s->checkpoints[k].history=s->history;
            s->checkpoints[k].valid=1;
        }
    }
    s->cache_frame=0;
    return 1;
}
static void multi_release(void *ctx){
    NativeMultiAac *s=ctx;
    if(s){free(s->checkpoints);free(s->offsets);free(s);}
}
static void *multi_open_native(const uint8_t *data,size_t length,SautiPcmInfo *info){
    if(!info || !multi_probe(data,length))return NULL;
    if(length>512u*1024u*1024u)return NULL;
    size_t offset=0,count=0,capacity=0;
    size_t *offsets=NULL;
    unsigned rate=0,config=0;
    while(offset<length){
        SautiAacAdts h={0};
        if(!sauti_aac_adts_parse_asm(data+offset,length-offset,&h) ||
           h.frame_bytes<7 || h.frame_bytes>length-offset ||
           h.core_aot!=2u || h.core_channels==1u || h.core_channels==2u || h.core_channels>7u ||
           h.raw_blocks!=1u || h.crc_present)goto fail;
        if(!count){rate=h.core_sample_rate;config=h.core_channels;}
        if(rate!=h.core_sample_rate || config!=h.core_channels)goto fail;
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
    NativeMultiAac *s=calloc(1,sizeof(*s));
    if(!s)goto fail;
    s->bytes=data;s->bytes_length=length;
    s->offsets=offsets;s->frame_count=count;
    s->config=config;
    if(config==0u){                                        /* uklad z PCE w pierwszej ramce (fdk-aac 7.1 itp.) */
        if(sauti_aac_lc_multi_scan_pce(data+offsets[0],offsets[1]-offsets[0],&s->layout)!=1){multi_release(s);return NULL;}
        s->channels=s->layout.channels;
    }else s->channels=sauti_aac_lc_multi_config_channels(config);
    if(!s->channels || !multi_reset(s,config)){multi_release(s);return NULL;}
    s->checkpoint_count=count/AAC_CHECKPOINT_FRAMES+1u;
    if(s->checkpoint_count>SIZE_MAX/sizeof(*s->checkpoints)){
        multi_release(s);return NULL;
    }
    s->checkpoints=calloc(s->checkpoint_count,sizeof(*s->checkpoints));
    if(!s->checkpoints){multi_release(s);return NULL;}
    s->checkpoints[0].history=s->history;
    s->checkpoints[0].valid=1;
    /* Tylko pierwsza ramka przy open (jak w adapterze stereo); reszta leniwie, blad zatrzymuje read() bez wymyslonego PCM. */
    if(!multi_decode_next(s)){multi_release(s);return NULL;}
    s->position=0;
    info->sample_rate=rate;
    info->channels=s->channels;
    info->total_frames=(uint64_t)count*1024u;
    return s;
fail:
    free(offsets);
    return NULL;
}
static size_t multi_read_native(void *ctx,float *output,size_t frames){
    NativeMultiAac *s=ctx;
    if(!s || !output || !frames)return 0;
    const uint64_t total=(uint64_t)s->frame_count*1024u;
    if(s->position>=total)return 0;
    uint64_t available=total-s->position;
    if((uint64_t)frames>available)frames=(size_t)available;
    size_t written=0;
    while(written<frames){
        if(s->cache_frame==1024u && !multi_decode_next(s))break;
        size_t take=frames-written;
        if(take>1024u-s->cache_frame)take=1024u-s->cache_frame;
        memcpy(output+written*s->channels,s->cache+s->cache_frame*s->channels,
               take*s->channels*sizeof(float));
        s->cache_frame+=take;
        written+=take;
        s->position+=take;
    }
    return written;
}
static int multi_seek_native(void *ctx,uint64_t position){
    NativeMultiAac *s=ctx;
    if(!s || position>(uint64_t)s->frame_count*1024u)return 0;
    if(position==s->position)return 1;
    const size_t target=(size_t)(position/1024u);
    size_t origin=0;
    for(size_t k=target/AAC_CHECKPOINT_FRAMES+1u;k>0;--k){
        const size_t slot=k-1u;
        if(slot<s->checkpoint_count && s->checkpoints[slot].valid){
            s->history=s->checkpoints[slot].history;
            origin=slot*AAC_CHECKPOINT_FRAMES;
            break;
        }
    }
    s->next_frame=origin;
    s->cache_frame=1024u;
    for(size_t i=origin;i<target;i++)
        if(!multi_decode_next(s))return 0;
    s->cache_frame=1024u;
    if(position%1024u){
        if(!multi_decode_next(s))return 0;
        s->cache_frame=(size_t)(position%1024u);
    }
    s->position=position;
    return 1;
}
const SautiDecoderOps sauti_decoder_aac_multi_native_ops={
    "aac-lc-native-multi-adts",multi_probe,multi_open_native,multi_read_native,multi_seek_native,multi_release
};
