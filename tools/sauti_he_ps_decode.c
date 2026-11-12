/* PS-5a: strict CLI for real ADTS HE-AAC v2 -> native Sauti PCM.
 * This executable NEVER invokes FFmpeg or an external decoder.
 * Fails closed on the first unsupported/invalid ADTS frame;
 * deletes incomplete output, does not invent a final PS lookahead. */
#include "sauti_aac_he_adts_ps.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ADTS 8191u
static int adts_frame(FILE *in,uint8_t buf[MAX_ADTS],size_t *length){
    const size_t got=fread(buf,1u,7u,in);
    if(!got&&feof(in))return 0;
    if(got!=7u)return -1;
    if(buf[0]!=0xffu||(buf[1]&0xf6u)!=0xf0u||
       ((buf[2]>>6u)&3u)!=1u)return -1;
    const unsigned n=((unsigned)(buf[3]&3u)<<11u)|
                     ((unsigned)buf[4]<<3u)|
                     ((unsigned)buf[5]>>5u);
    if(n<7u||n>MAX_ADTS||(!(buf[1]&1u)&&n<9u))return -1;
    if(fread(buf+7u,1u,n-7u,in)!=n-7u)return -1;
    *length=n;
    return 1;
}
int main(int argc,char **argv){
    if(argc!=3){
        fprintf(stderr,"Usage: %s SOURCE.adts OUT_STEREO.f32le\n"
                "Writes 48/other 2x-core-rate interleaved L/R float32; "
                "one final PS frame remains pending at EOF.\n",argv[0]);
        return 2;
    }
    if(!strcmp(argv[1],argv[2])){
        fprintf(stderr,"Input and output paths must differ\n");
        return 2;
    }
    FILE *in=fopen(argv[1],"rb");
    if(!in){perror(argv[1]);return 2;}
    /* Never overwrite the input; separate output is required.
     * A failed conversion removes any newly created partial output. */
    FILE *out=fopen(argv[2],"wb");
    if(!out){perror(argv[2]);fclose(in);return 2;}
    int result=1;
    SautiAacHeAdtsPsState *state=calloc(1u,sizeof(*state));
    if(!state||!sauti_aac_he_adts_ps_init(state)){
        fprintf(stderr,"Cannot initialize native PS decoder\n");
        goto done;
    }
    uint8_t frame[MAX_ADTS];
    float pcm[4096u];
    size_t len=0u,frames=0u,published=0u,byte_offset=0u;
    for(;;){
        const int fetched=adts_frame(in,frame,&len);
        if(fetched==0)break;
        if(fetched<0){
            fprintf(stderr,"Bad/truncated ADTS at offset %zu\n",byte_offset);
            goto done;
        }
        SautiAacReason reason=SAUTI_AAC_REASON_INVALID_ADTS;
        SautiAacSbrLimiterReport report;
        unsigned ready=0u;
        const int status=sauti_aac_he_adts_ps_decode_ex(
            frame,len,state,pcm,4096u,&ready,&report,&reason);
        if(status!=1){
            fprintf(stderr,"Unsupported/invalid frame=%zu offset=%zu status=%d reason=%d\n",
                    frames,byte_offset,status,(int)reason);
            if(reason==SAUTI_AAC_REASON_UNSUPPORTED_FIL){
                SautiAacHeAdtsMonoState diagnostic_state=state->adts;
                SautiAacSbrPayloadFrame diagnostic_payload;
                SautiAacSbrPayloadStage stage=SAUTI_SBR_PAYLOAD_INPUT;
                SautiAacReason diagnostic_reason=SAUTI_AAC_REASON_OK;
                unsigned parser_entered=0u;
                float core_pcm[1024u];
                const int diagnostic=sauti_aac_he_adts_mono_diagnose_ex(
                    frame,len,&diagnostic_state,core_pcm,
                    &diagnostic_payload,&diagnostic_reason,
                    &stage,&parser_entered);
                fprintf(stderr,
                    "FIL diagnostic: result=%d reason=%d SBR_parser_entered=%u SBR_stage=%u\n",
                    diagnostic,(int)diagnostic_reason,parser_entered,(unsigned)stage);
                /* Re-run on a disposable copy with ISO-valid temporal
                 * borders enabled. Never commit this trial to the
                 * playback state or publish its PCM as SBR output. */
                SautiAacHeAdtsMonoState temporal_state=state->adts;
                SautiAacSbrPayloadFrame temporal_payload;
                SautiAacSbrPayloadStage temporal_stage=SAUTI_SBR_PAYLOAD_INPUT;
                SautiAacReason temporal_reason=SAUTI_AAC_REASON_OK;
                unsigned temporal_seen=0u;
                const int temporal_status=
                    sauti_aac_he_adts_mono_temporal_diagnose_ex(
                        frame,len,&temporal_state,core_pcm,
                        &temporal_payload,&temporal_reason,
                        &temporal_stage,&temporal_seen);
                fprintf(stderr,
                    "Temporal SBR trial: result=%d reason=%d entered=%u stage=%u\n",
                    temporal_status,(int)temporal_reason,
                    temporal_seen,(unsigned)temporal_stage);
                if(temporal_status==1){
                    const SautiAacSbrGrid *g=&temporal_payload.grid;
                    fprintf(stderr,
                        "SBR grid: class=%u envelopes=%u env_first=%u env_last=%u "
                        "noise_first=%u noise_last=%u (SBR units; nominal [0,16])\n",
                        (unsigned)g->frame_class,(unsigned)g->env_count,
                        (unsigned)g->env_border[0],
                        (unsigned)g->env_border[g->env_count],
                        (unsigned)g->noise_border[0],
                        (unsigned)g->noise_border[g->noise_count]);
                    SautiAacSbrTemporalPlan plan;
                    if(sauti_aac_sbr_temporal_plan(&temporal_payload,&plan))
                        fprintf(stderr,
                            "Temporal SBR plan: deferred_QMF_slots=%u "
                            "current_env_segments=%u deferred_env_segments=%u\n",
                            (unsigned)plan.deferred_slots,
                            (unsigned)plan.env_current_count,
                            (unsigned)plan.env_deferred_count);
                    else
                        fprintf(stderr,"Temporal SBR plan: unsupported\n");
                }
            }
            goto done;
        }
        if(ready){
            for(size_t i=0;i<4096u;i++)if(!isfinite(pcm[i])){
                fprintf(stderr,"Nonfinite stereo PCM at frame %zu\n",frames);
                goto done;
            }
            if(fwrite(pcm,sizeof(pcm[0]),4096u,out)!=4096u){
                perror("PCM write");
                goto done;
            }
            published++;
        }
        frames++;byte_offset+=len;
    }
    if(ferror(in)||frames<2u||published!=frames-1u){
        fprintf(stderr,"No complete two-frame PS output; frames=%zu published=%zu\n",
                frames,published);
        goto done;
    }
    if(fflush(out)!=0){perror("PCM flush");goto done;}
    fprintf(stderr,"PASS native PS stream frames=%zu PCM_frames=%zu stereo_samples=%zu "
            "pending_final=1 (no synthesized EOS)\n",
            frames,published,published*2048u);
    result=0;
done:
    free(state);
    if(fclose(in)!=0)result=1;
    if(fclose(out)!=0)result=1;
    if(result)remove(argv[2]);
    return result;
}
