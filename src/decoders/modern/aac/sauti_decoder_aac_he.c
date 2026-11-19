/* SBR-86: HE-AAC (SBR, and parametric stereo for HE-AAC v2) ADTS adapters on top of the standard-structured SBR channel decoder.
 *
 *   mono core, SBR            -> mono PCM at twice the core rate
 *   mono core, SBR + PS       -> stereo PCM (HE-AAC v2), decided from the first frame
 *   stereo core (CPE), SBR    -> stereo PCM, one SBR channel per core channel
 *
 * One stream object holds every piece of state, so a checkpoint is a plain copy and a seek restores the nearest one and decodes forward.
 * Parametric stereo needs six QMF slots after the frame it renders; those are the first slots of the NEXT frame, so in PS mode the PCM of
 * frame k appears when frame k+1 has been decoded, and the last frame is rendered with silence as its lookahead. Frames of a PS stream
 * that carry no PS data reuse the previous frame's parameters (unverified against another decoder).
 * The adapter only opens a stream whose first frame decodes completely with SBR: plain AAC-LC stays with the LC adapters. */
#include "sauti_decoder_aac_native.h"
#include "sauti_aac_he_adts_cpe.h"
#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_he_ps_bridge.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_sbr_channel.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { HE_CHECKPOINT_FRAMES = 256, HE_FRAME_PCM = 2048 };

typedef struct {
    SautiAacHeAdtsMonoState mono;
    SautiAacHeAdtsCpeState cpe;
    SautiAacSbrChannel channel[2];
    SautiAacHePsBridgeState ps;
    SautiAacSbrComplex pending[SAUTI_AAC_SBR_CHANNEL_GRID];       /* PS mode: grid of the frame waiting for its lookahead */
    SautiAacSbrPayloadFrame pending_payload;
    uint32_t pending_valid;
    uint32_t flushed;
    size_t next_frame;                                            /* next ADTS frame to decode */
    uint64_t emitted;                                             /* PCM frames produced so far */
} HeStream;

typedef struct {
    HeStream state;
    unsigned char valid;
} HeCheckpoint;

typedef struct {
    const uint8_t *bytes;
    size_t bytes_length;
    size_t *offsets;                 /* frame_count + 1, sentinel at end */
    size_t frame_count;
    unsigned core_channels;          /* 1 or 2 */
    unsigned out_channels;           /* 1 or 2 */
    unsigned ps_mode;
    uint64_t position;
    HeStream st;
    float cache[2u*HE_FRAME_PCM];
    size_t cache_frame;              /* HE_FRAME_PCM = empty */
    /* scratch owned by this decoder (not shared between decoders or threads, not part of checkpoints) */
    SautiAacSbrComplex scratch_qmf38[38u*64u];
    SautiAacSbrComplex scratch_grid[SAUTI_AAC_SBR_CHANNEL_GRID];
    SautiAacSbrComplex scratch_g2[2][SAUTI_AAC_SBR_CHANNEL_GRID];
    HeCheckpoint *checkpoints;
    size_t checkpoint_count;
} NativeHe;

static int he_stream_init(HeStream *s){
    memset(s,0,sizeof(*s));
    return sauti_aac_he_adts_mono_init(&s->mono) && sauti_aac_he_adts_cpe_init(&s->cpe) &&
           sauti_aac_sbr_channel_init(&s->channel[0]) && sauti_aac_sbr_channel_init(&s->channel[1]);
}

static int he_probe_channels(const uint8_t *data,size_t size,unsigned channels){
    SautiAacAdts h={0};
    return data && size>=7u && sauti_aac_adts_parse_asm(data,size,&h) &&
           h.core_aot==2u && h.core_channels==channels && h.raw_blocks==1u;
}
static int he_probe(const uint8_t *data,size_t size){return he_probe_channels(data,size,1u);}
static int he_cpe_probe(const uint8_t *data,size_t size){return he_probe_channels(data,size,2u);}

/* Parametric stereo of the pending frame; `lookahead` is the next frame's grid, or NULL at the end of the stream (silence). */
static int he_render_ps(NativeHe *n,const SautiAacSbrComplex *lookahead,float *pcm_stereo){
    HeStream *s=&n->st;
    SautiAacSbrComplex *qmf38=n->scratch_qmf38;
    memcpy(qmf38,s->pending,sizeof(s->pending));
    if(lookahead)memcpy(qmf38+32u*64u,lookahead,6u*64u*sizeof(qmf38[0]));
    else memset(qmf38+32u*64u,0,6u*64u*sizeof(qmf38[0]));
    return sauti_aac_he_ps_bridge_render(&s->pending_payload,qmf38,38u*64u,&s->ps,pcm_stereo,4096u);
}

/* A frame whose SBR extension carries no PS data: the same as a ps_data() with no envelopes and no header (num_env = 0) - the last envelope's parameters
 * stay, flat over the whole frame, and the envelope structure of the previous frame is NOT replayed. IPD/OPD are not retained (a frame without the
 * extension has none). `prev` is the previous frame's PS data. */
static void he_ps_hold(const SautiAacPsFrame *prev,SautiAacPsFrame *out){
    const unsigned last=prev->resolved_num_env?prev->resolved_num_env-1u:0u;
    memset(out,0,sizeof(*out));
    out->header=prev->header;
    out->resolved_num_env=1u;out->border_position[0]=31u;
    out->iid_count=prev->iid_count;out->icc_count=prev->icc_count;
    memcpy(out->iid[0],prev->iid[last],sizeof(out->iid[0]));
    memcpy(out->icc[0],prev->icc[last],sizeof(out->icc[0]));
}

/* Produce the next PCM frame into n->cache. 1 = produced, 0 = end of stream or error. */
static int he_produce(NativeHe *n){
    HeStream *s=&n->st;
    for(;;){
        if(n->ps_mode && s->pending_valid && s->next_frame>=n->frame_count){
            if(s->flushed)return 0;
            if(!he_render_ps(n,NULL,n->cache))return 0;
            s->pending_valid=0;s->flushed=1;
            goto produced;
        }
        if(s->next_frame>=n->frame_count)return 0;
        const size_t offset=n->offsets[s->next_frame];
        const size_t length=n->offsets[s->next_frame+1u]-offset;
        SautiAacReason reason=SAUTI_AAC_REASON_OK;
        if(n->core_channels==1u){
            float core[1024];SautiAacSbrPayloadFrame payload;
            SautiAacSbrComplex *grid=n->scratch_grid;
            if(sauti_aac_he_adts_mono_temporal_decode_ex(n->bytes+offset,length,&s->mono,core,&payload,&reason)!=1)return 0;
            if(n->ps_mode){
                if(!payload.has_ps){
                    if(!s->pending_payload.has_ps)return 0;                  /* nothing to reuse */
                    he_ps_hold(&s->pending_payload.ps,&payload.ps);payload.has_ps=1u;   /* no PS data in this frame: keep the last parameters */
                }
            }
            if(!sauti_aac_sbr_channel_frame(&s->channel[0],core,&payload,grid))return 0;
            s->next_frame++;
            if(!n->ps_mode){
                if(!sauti_aac_sbr_channel_synthesize(&s->channel[0],grid,n->cache))return 0;
                goto produced;
            }
            if(s->pending_valid){
                float st[4096];
                if(!he_render_ps(n,grid,st))return 0;
                memcpy(n->cache,st,sizeof(st));
                memcpy(s->pending,grid,sizeof(s->pending));
                s->pending_payload=payload;
                goto produced;
            }
            memcpy(s->pending,grid,sizeof(s->pending));
            s->pending_payload=payload;s->pending_valid=1u;
            continue;
        }else{
            float core2[2u*1024u],c[2][1024],pcm2[2][HE_FRAME_PCM];
            SautiAacSbrPayloadCpeFrame pl;
            SautiAacSbrComplex (*g2)[SAUTI_AAC_SBR_CHANNEL_GRID]=n->scratch_g2;
            if(sauti_aac_he_adts_cpe_decode_ex(n->bytes+offset,length,&s->cpe,core2,&pl,&reason)!=1)return 0;
            for(unsigned i=0;i<1024u;i++){c[0][i]=core2[2u*i];c[1][i]=core2[2u*i+1u];}
            for(unsigned k=0;k<2u;k++)
                if(!sauti_aac_sbr_channel_frame(&s->channel[k],c[k],&pl.channel[k],g2[k]) ||
                   !sauti_aac_sbr_channel_synthesize(&s->channel[k],g2[k],pcm2[k]))return 0;
            for(unsigned i=0;i<HE_FRAME_PCM;i++){n->cache[2u*i]=pcm2[0][i];n->cache[2u*i+1u]=pcm2[1][i];}
            s->next_frame++;
            goto produced;
        }
    }
produced:
    for(size_t i=0;i<(size_t)HE_FRAME_PCM*n->out_channels;i++)if(!isfinite(n->cache[i]))return 0;
    s->emitted++;
    n->cache_frame=0u;
    if(n->checkpoints && s->emitted%HE_CHECKPOINT_FRAMES==0u){
        const size_t k=(size_t)(s->emitted/HE_CHECKPOINT_FRAMES);
        if(k<n->checkpoint_count && !n->checkpoints[k].valid){n->checkpoints[k].state=*s;n->checkpoints[k].valid=1u;}
    }
    return 1;
}

static void he_release(void *ctx){
    NativeHe *n=ctx;
    if(n){free(n->checkpoints);free(n->offsets);free(n);}
}

static void *he_open_common(const uint8_t *data,size_t length,SautiPcmInfo *info,unsigned core_channels){
    if(!info || !he_probe_channels(data,length,core_channels) || length>512u*1024u*1024u)return NULL;
    size_t offset=0,count=0,capacity=0;
    size_t *offsets=NULL;
    unsigned rate=0;
    while(offset<length){
        SautiAacAdts h={0};
        if(!sauti_aac_adts_parse_asm(data+offset,length-offset,&h) || h.frame_bytes<7u || h.frame_bytes>length-offset ||
           h.core_aot!=2u || h.core_channels!=core_channels || h.raw_blocks!=1u)goto fail;
        if(!count)rate=h.core_sample_rate;
        if(rate!=h.core_sample_rate || rate>UINT32_MAX/2u || count>=SIZE_MAX/HE_FRAME_PCM)goto fail;
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
    if(!count || (uint64_t)count>UINT64_MAX/HE_FRAME_PCM)goto fail;
    offsets[count]=length;
    NativeHe *n=calloc(1,sizeof(*n));
    if(!n)goto fail;
    n->bytes=data;n->bytes_length=length;n->offsets=offsets;n->frame_count=count;n->core_channels=core_channels;
    if(!he_stream_init(&n->st)){he_release(n);return NULL;}
    n->checkpoint_count=count/HE_CHECKPOINT_FRAMES+2u;
    if(n->checkpoint_count>SIZE_MAX/sizeof(*n->checkpoints)){he_release(n);return NULL;}
    n->checkpoints=calloc(n->checkpoint_count,sizeof(*n->checkpoints));
    if(!n->checkpoints){he_release(n);return NULL;}
    /* A valid ADTS header does not make a stream HE-AAC: the first frame must decode completely with SBR. For a mono core, parametric
     * stereo in that first frame turns the output into stereo. */
    n->out_channels=core_channels;
    if(core_channels==1u){
        float core[1024];SautiAacSbrPayloadFrame payload;SautiAacReason reason=SAUTI_AAC_REASON_OK;
        SautiAacHeAdtsMonoState probe;
        if(!sauti_aac_he_adts_mono_init(&probe) ||
           sauti_aac_he_adts_mono_temporal_decode_ex(data+offsets[0],offsets[1]-offsets[0],&probe,core,&payload,&reason)!=1){he_release(n);return NULL;}
        if(payload.has_ps){n->ps_mode=1u;n->out_channels=2u;}
    }
    n->checkpoints[0].state=n->st;n->checkpoints[0].valid=1u;
    if(!he_produce(n)){he_release(n);return NULL;}        /* the whole first frame, SBR included, must decode */
    n->st=n->checkpoints[0].state;n->cache_frame=HE_FRAME_PCM;n->position=0;
    info->sample_rate=(uint32_t)rate*2u;
    info->channels=n->out_channels;
    info->total_frames=(uint64_t)count*HE_FRAME_PCM;
    return n;
fail:
    free(offsets);
    return NULL;
}
static void *he_open_native(const uint8_t *data,size_t length,SautiPcmInfo *info){return he_open_common(data,length,info,1u);}
static void *he_cpe_open_native(const uint8_t *data,size_t length,SautiPcmInfo *info){return he_open_common(data,length,info,2u);}

static size_t he_read_native(void *ctx,float *output,size_t frames){
    NativeHe *n=ctx;
    if(!n || !output || !frames)return 0;
    const uint64_t total=(uint64_t)n->frame_count*HE_FRAME_PCM;
    if(n->position>=total)return 0;
    if((uint64_t)frames>total-n->position)frames=(size_t)(total-n->position);
    size_t written=0;
    while(written<frames){
        if(n->cache_frame==HE_FRAME_PCM && !he_produce(n))break;
        size_t take=frames-written;
        if(take>HE_FRAME_PCM-n->cache_frame)take=HE_FRAME_PCM-n->cache_frame;
        memcpy(output+written*n->out_channels,n->cache+n->cache_frame*n->out_channels,take*n->out_channels*sizeof(float));
        n->cache_frame+=take;written+=take;n->position+=take;
    }
    return written;
}
static int he_seek_native(void *ctx,uint64_t position){
    NativeHe *n=ctx;
    if(!n || position>(uint64_t)n->frame_count*HE_FRAME_PCM)return 0;
    if(position==n->position)return 1;
    const uint64_t target=position/HE_FRAME_PCM;
    for(size_t k=(size_t)(target/HE_CHECKPOINT_FRAMES)+1u;k>0;--k){
        const size_t slot=k-1u;
        if(slot<n->checkpoint_count && n->checkpoints[slot].valid){n->st=n->checkpoints[slot].state;break;}
    }
    n->cache_frame=HE_FRAME_PCM;
    while(n->st.emitted<target)if(!he_produce(n))return 0;
    n->cache_frame=HE_FRAME_PCM;
    if(position<(uint64_t)n->frame_count*HE_FRAME_PCM){            /* the frame that holds `position`, then the offset inside it */
        if(!he_produce(n))return 0;
        n->cache_frame=(size_t)(position%HE_FRAME_PCM);
    }
    n->position=position;
    return 1;
}

const SautiDecoderOps sauti_decoder_aac_he_native_ops={
    "aac-he-native-adts",he_probe,he_open_native,he_read_native,he_seek_native,he_release
};
const SautiDecoderOps sauti_decoder_aac_he_cpe_native_ops={
    "aac-he-cpe-native-adts",he_cpe_probe,he_cpe_open_native,he_read_native,he_seek_native,he_release
};
