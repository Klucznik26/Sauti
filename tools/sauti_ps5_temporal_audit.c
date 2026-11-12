/* PS-5b: stream-wide native SBR temporal ownership audit.
 * Pure side-data validation: never publishes PCM, never uses FFmpeg.
 * Tests the genuine AAC-LC+SBR bitstream, including FIXVAR borders.
 * Does not pretend that temporal HF rendering is implemented. */
#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_sbr_qmf_frame.h"
#include "sauti_aac_ps5_hf_gain.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ADTS 8191u
static int read_frame(FILE *f,uint8_t buf[MAX_ADTS],size_t *count){
    size_t got=fread(buf,1u,7u,f);
    if(got==0u&&feof(f))return 0;
    if(got!=7u || buf[0]!=0xffu || (buf[1]&0xf6u)!=0xf0u ||
       ((buf[2]>>6u)&3u)!=1u)return -1;
    unsigned len=((unsigned)(buf[3]&3u)<<11u)|
                 ((unsigned)buf[4]<<3u)|((unsigned)buf[5]>>5u);
    if(len<7u||len>MAX_ADTS||(!(buf[1]&1u)&&len<9u))return -1;
    if(fread(buf+7u,1u,len-7u,f)!=len-7u)return -1;
    *count=len;
    return 1;
}
int main(int argc,char **argv){
    if(argc!=2){
        fprintf(stderr,"Usage: %s INPUT.aac\n",argv[0]);return 2;
    }
    FILE *in=fopen(argv[1],"rb");
    if(!in){perror(argv[1]);return 2;}
    SautiAacHeAdtsMonoState *decoder=calloc(1u,sizeof(*decoder));
    SautiAacSbrTemporalHistory *history=calloc(1u,sizeof(*history));
    SautiAacSbrPayloadFrame *payload=malloc(sizeof(*payload));
    SautiAacSbrQmfFrameState *qmf_state=calloc(1u,sizeof(*qmf_state));
    SautiAacSbrComplex *raw_qmf=malloc(32u*64u*sizeof(*raw_qmf));
    if(!decoder||!history||!payload||!qmf_state||!raw_qmf||
       !sauti_aac_he_adts_mono_init(decoder)){
        fprintf(stderr,"Cannot allocate/init audit state\n");
        free(decoder);free(history);free(payload);free(qmf_state);free(raw_qmf);fclose(in);
        return 2;
    }
    SautiAacPs5HfGainReport *gain_report=malloc(sizeof(*gain_report));
    if(!gain_report){fprintf(stderr,"Allocation failed\n");free(raw_qmf);free(qmf_state);free(payload);free(history);free(decoder);fclose(in);return 2;}
    size_t missing_sources=0u,inherited=0u;
    double target_power=0.0,raw_power=0.0;
    float pcm[1024u];
    uint8_t data[MAX_ADTS];
    size_t len=0u,offset=0u,frames=0u,temporal_frames=0u;
    int exit_code=1;
    for(;;){
        const int r=read_frame(in,data,&len);
        if(r==0)break;
        if(r<0){fprintf(stderr,"FAIL ADTS frame=%zu offset=%zu\n",frames,offset);goto done;}
        SautiAacReason reason=SAUTI_AAC_REASON_INVALID_ADTS;
        SautiAacSbrPayloadStage stage=SAUTI_SBR_PAYLOAD_INPUT;
        unsigned seen=0u;
        const int status=sauti_aac_he_adts_mono_temporal_diagnose_ex(
            data,len,decoder,pcm,payload,&reason,&stage,&seen);
        if(status!=1){
            fprintf(stderr,
                    "FAIL temporal parser frame=%zu offset=%zu status=%d reason=%d stage=%u entered=%u\n",
                    frames,offset,status,(int)reason,(unsigned)stage,seen);
            goto done;
        }
        if(payload->has_ps!=1u){
            fprintf(stderr,"FAIL missing PS side data frame=%zu offset=%zu\n",
                    frames,offset);
            goto done;
        }
        SautiAacSbrTemporalHandoff handoff;
        if(!sauti_aac_sbr_temporal_history_push(payload,history,&handoff)){
            fprintf(stderr,
                    "FAIL handoff frame=%zu offset=%zu previous_tail=%u current_start=%u\n",
                    frames,offset,(unsigned)history->current_plan.deferred_slots,
                    (unsigned)payload->env_edges[0]);
            goto done;
        }
        SautiAacSbrResolvedFrame resolved;
        if(!sauti_aac_sbr_temporal_resolve(history,&resolved)){
            fprintf(stderr,
                    "FAIL ownership frame=%zu offset=%zu previous_tail=%u current_start=%u overlap=%u\n",
                    frames,offset,(unsigned)handoff.previous_tail_slots,
                    (unsigned)handoff.current_start_slots,
                    (unsigned)handoff.overlapping_slots);
            goto done;
        }
        SautiAacSbrTemporalHfTargets targets;
        if(!sauti_aac_sbr_temporal_hf_targets(history,&targets)){
            fprintf(stderr,"FAIL HF target ownership frame=%zu offset=%zu\n",
                    frames,offset);
            goto done;
        }
        /* First genuine DSP stage: consume the decoded core PCM
         * through continuous 32-band QMF analysis and SBR-12 transpose.
         * Keep HF envelope/ROM/noise/sine processing strictly disabled:
         * temporal-owned HF adjustment still needs implementation. */
        SautiAacSbrQmfStage qmf_stage=SAUTI_SBR_QMF_STAGE_INPUT;
        if(!sauti_aac_sbr_qmf_frame_export_raw(
                &payload->tables,&payload->patches,
                pcm,1024u,raw_qmf,32u*64u,qmf_state,&qmf_stage)){
            fprintf(stderr,"FAIL raw QMF frame=%zu offset=%zu stage=%u\n",
                    frames,offset,(unsigned)qmf_stage);
            goto done;
        }
        /* Check each decoded slot can be paired with its original
         * owner without applying current-frame target to inherited HF. */
        for(unsigned t=0;t<32u;t++){
            const SautiAacSbrTemporalHfSlot *slot=&targets.slot[t];
            if(slot->source>1u||slot->kx>32u||slot->k2>64u||
               slot->kx>=slot->k2){
                fprintf(stderr,"FAIL owned HF geometry frame=%zu slot=%u\n",frames,t);
                goto done;
            }
            for(unsigned k=slot->kx;k<slot->k2;k++){
                const SautiAacSbrComplex q=raw_qmf[t*64u+k];
                if(!isfinite(q.re)||!isfinite(q.im)||
                   !isfinite(slot->energy[k])||
                   !isfinite(slot->noise_factor[k])||
                   slot->energy[k]<0.0||slot->noise_factor[k]<0.0){
                    fprintf(stderr,"FAIL raw QMF / owned HF target frame=%zu slot=%u bin=%u\n",
                            frames,t,k);
                    goto done;
                }
            }
        }
        if(!sauti_aac_ps5_hf_gain_analyze(
                &targets,raw_qmf,32u*64u,gain_report)){
            fprintf(stderr,"FAIL owned QMF gain frame=%zu offset=%zu\n",
                    frames,offset);
            goto done;
        }
        missing_sources+=gain_report->missing_source;
        inherited+=gain_report->inherited_slots;
        target_power+=gain_report->target_power;
        raw_power+=gain_report->raw_power;
        if(!isfinite(target_power)||!isfinite(raw_power)){
            fprintf(stderr,"FAIL nonfinite power totals frame=%zu\n",frames);
            goto done;
        }
        const unsigned tail=history->current_plan.deferred_slots;
        if(tail||handoff.previous_tail_slots){
            fprintf(stderr,
                "FRAME %zu: class=%u env=%u start=%u end=%u previous=%u deferred=%u owner0=%u owner31=%u\n",
                frames,(unsigned)payload->grid.frame_class,
                (unsigned)payload->grid.env_count,
                (unsigned)payload->env_edges[0],
                (unsigned)payload->env_edges[payload->grid.env_count],
                (unsigned)resolved.previous_slots,tail,
                (unsigned)resolved.slot[0].source,
                (unsigned)resolved.slot[31].source);
            temporal_frames++;
        }
        frames++;offset+=len;
    }
    if(ferror(in)||!frames){fprintf(stderr,"FAIL empty/IO\n");goto done;}
    fprintf(stderr,"PASS PS-5c2 diagnostic frames=%zu transitions=%zu inherited_QMF_slots=%zu missing_raw_sources=%zu target_power=%.7g raw_power=%.7g; NO HF/PCM synthesis\n",
            frames,temporal_frames,inherited,missing_sources,target_power,raw_power);
    exit_code=0;
done:
    free(gain_report);free(raw_qmf);free(qmf_state);free(payload);free(history);free(decoder);
    if(fclose(in)!=0)exit_code=1;
    return exit_code;
}
