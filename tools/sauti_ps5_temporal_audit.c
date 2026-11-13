/* Native SBR temporal ownership audit with opt-in PCM isolation previews.
 * Default is side-data validation without PCM; never uses FFmpeg.
 * Tests the genuine AAC-LC+SBR bitstream, including FIXVAR borders.
 * Does not pretend that temporal HF rendering is implemented. */
#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_sbr_qmf_frame.h"
#include "sauti_aac_ps5_hf_gain.h"
#include "sauti_aac_ps5_hf_envelope.h"
#include "sauti_aac_ps5_cross.h"
#include "sauti_aac_he_ps_delay.h"
#include "sauti_aac_ps_map.h"
#include "sauti_aac_ps_matrix.h"
#include "sauti_aac_sbr_noise_rom.h"
#include <float.h>
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
    if(argc!=2 && argc!=4){
        fprintf(stderr,"Usage: %s INPUT.aac [--experimental-pcm | --experimental-pcm-no-noise | --experimental-pcm-unscaled | --experimental-pcm-unit-scale | --experimental-pcm-raw-hf | --experimental-pcm-low-only | --experimental-pcm-noise-25 | --experimental-pcm-noise-50 | --experimental-pcm-noise-75] OUT.f32le\n",argv[0]);
        return 2;
    }
    const int experimental=argc==4;
    const int low_only=experimental &&
        strcmp(argv[2],"--experimental-pcm-low-only")==0;
    const int unit_scale=experimental &&
        strcmp(argv[2],"--experimental-pcm-unit-scale")==0;
    const int unscaled=experimental &&
        strcmp(argv[2],"--experimental-pcm-unscaled")==0;
    const int raw_hf=experimental &&
        strcmp(argv[2],"--experimental-pcm-raw-hf")==0;
    const int no_noise=experimental &&
        strcmp(argv[2],"--experimental-pcm-no-noise")==0;
    const int noise_25=experimental &&
        strcmp(argv[2],"--experimental-pcm-noise-25")==0;
    const int noise_50=experimental &&
        strcmp(argv[2],"--experimental-pcm-noise-50")==0;
    const int noise_75=experimental &&
        strcmp(argv[2],"--experimental-pcm-noise-75")==0;
    const int full=experimental &&
        strcmp(argv[2],"--experimental-pcm")==0;
    if(experimental&&(!(low_only||raw_hf||no_noise||full||unit_scale||unscaled||noise_25||noise_50||noise_75)||
        strcmp(argv[1],argv[3])==0)){
        fprintf(stderr,"Invalid experimental PCM arguments\n");
        return 2;
    }
    FILE *in=fopen(argv[1],"rb");
    if(!in){perror(argv[1]);return 2;}
    SautiAacHeAdtsMonoState *decoder=calloc(1u,sizeof(*decoder));
    SautiAacSbrTemporalHistory *history=calloc(1u,sizeof(*history));
    SautiAacSbrPayloadFrame *payload=malloc(sizeof(*payload));
    SautiAacSbrQmfFrameState *qmf_state=calloc(1u,sizeof(*qmf_state));
    SautiAacSbrComplex *raw_qmf=malloc(32u*64u*sizeof(*raw_qmf));
    SautiAacSbrComplex *previous_raw=malloc(32u*64u*sizeof(*previous_raw));
    if(!decoder||!history||!payload||!qmf_state||!raw_qmf||!previous_raw||
       !sauti_aac_he_adts_mono_init(decoder)){
        fprintf(stderr,"Cannot allocate/init audit state\n");
        free(decoder);free(history);free(payload);free(qmf_state);free(raw_qmf);free(previous_raw);fclose(in);
        return 2;
    }
    SautiAacPs5HfGainReport *gain_report=malloc(sizeof(*gain_report));
    if(!gain_report){fprintf(stderr,"Allocation failed\n");free(previous_raw);free(raw_qmf);free(qmf_state);free(payload);free(history);free(decoder);fclose(in);return 2;}
    SautiAacPs5HfEnvelopeReport *envelope=malloc(sizeof(*envelope));
    if(!envelope){fprintf(stderr,"Allocation failed\n");free(gain_report);free(previous_raw);free(raw_qmf);free(qmf_state);free(payload);free(history);free(decoder);fclose(in);return 2;}
    double signal_budget=0.0,noise_budget=0.0;
    size_t envelope_missing=0u;
    unsigned cross_frames=0u;
    double cross_mean_power=0.0;
    size_t missing_sources=0u,inherited=0u;
    double target_power=0.0,raw_power=0.0;
    FILE *preview=NULL;
    SautiAacHePsDelayState *ps_state=NULL;
    SautiAacSbrNoiseRomState noise_state={0};
    SautiAacSbrComplex *adjusted=NULL;
    if(experimental){
        ps_state=calloc(1u,sizeof(*ps_state));
        adjusted=malloc(32u*64u*sizeof(*adjusted));
        if(!ps_state||!adjusted){
            fprintf(stderr,"Experimental PCM allocation failed\n");
            free(adjusted);free(ps_state);
            free(envelope);free(gain_report);free(previous_raw);
            free(raw_qmf);free(qmf_state);free(payload);free(history);
            free(decoder);fclose(in);return 2;
        }
        preview=fopen(argv[3],"wb");
        if(!preview){
            perror(argv[3]);free(adjusted);free(ps_state);
            free(envelope);free(gain_report);free(previous_raw);
            free(raw_qmf);free(qmf_state);free(payload);free(history);
            free(decoder);fclose(in);return 2;
        }
    }
    double hf_raw_power=0.0,hf_signal_power=0.0,hf_noise_power=0.0;
    double hf_peak_gain=0.0;
    size_t hf_bins=0u;
    unsigned published_pcm=0u;
    float stereo_pcm[4096u];
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
        /* Diagnose the PS side data before attributing all problems to
         * SBR. At least one stereo parameter must vary with time on
         * the independent two-tone source. Print bounded snapshots. */
        if(frames<5u){
            const SautiAacPsFrame *ps=&payload->ps;
            fprintf(stderr,
                "PS-SIDE frame=%zu header=%u iid_enabled=%u mode=%u count=%u "
                "icc_enabled=%u mode=%u count=%u env=%u ext=%u "
                "iid0=%d iid1=%d icc0=%d icc1=%d\n",
                frames,(unsigned)ps->header_present,
                (unsigned)ps->header.enable_iid,
                (unsigned)ps->header.iid_mode,
                (unsigned)ps->iid_count,
                (unsigned)ps->header.enable_icc,
                (unsigned)ps->header.icc_mode,
                (unsigned)ps->icc_count,
                (unsigned)ps->resolved_num_env,
                (unsigned)ps->header.enable_ext,
                (int)ps->iid[0][0],(int)ps->iid[0][1],
                (int)ps->icc[0][0],(int)ps->icc[0][1]);
        }
        if(frames==4u){
            SautiAacPsMappedFrame mapped;
            SautiAacPsMatrix coefficients;
            if(!sauti_aac_ps_map_frame(&payload->ps,&mapped)||
               !sauti_aac_ps_matrix_build(&payload->ps,&mapped,&coefficients)){
                fprintf(stderr,"FAIL PS-DETAIL matrix frame=%zu\n",frames);
                goto done;
            }
            for(unsigned b=0;b<mapped.bands;b++){
                const SautiAacPsMatrixCoefficients c=
                    coefficients.coefficient[0][b];
                fprintf(stderr,
                    "PS-DETAIL frame=%zu band=%u iid=%d icc=%d "
                    "h11=%.6g h12=%.6g h21=%.6g h22=%.6g\n",
                    frames,b,(int)mapped.iid[0][b],
                    (int)mapped.icc[0][b],
                    (double)c.h11,(double)c.h12,
                    (double)c.h21,(double)c.h22);
            }
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
        const unsigned inherited_slots=handoff.previous_tail_slots;
        if(!sauti_aac_sbr_qmf_frame_export_raw_owned(
                &payload->tables,&payload->patches,
                inherited_slots?&history->previous.tables:NULL,
                inherited_slots?&history->previous.patches:NULL,
                inherited_slots,pcm,1024u,raw_qmf,32u*64u,
                qmf_state,&qmf_stage)){
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
        if(!sauti_aac_ps5_hf_envelope_analyze(
                &targets,raw_qmf,32u*64u,envelope)){
            fprintf(stderr,"FAIL PS-5c3 owned envelope frame=%zu offset=%zu\n",
                    frames,offset);
            goto done;
        }
        signal_budget+=envelope->signal_budget;
        noise_budget+=envelope->noise_budget;
        envelope_missing+=envelope->missing_source;
        if(!isfinite(signal_budget)||!isfinite(noise_budget)){
            fprintf(stderr,"FAIL nonfinite PS-5c3 budget frame=%zu\n",frames);
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
        if(handoff.previous_tail_slots){
            fprintf(stderr,"OWNER-GEOMETRY frame=%zu inherited=%u previous_kx=%u current_kx=%u\n",frames,inherited_slots,(unsigned)history->previous.tables.kx,(unsigned)payload->tables.kx);
            SautiAacPs5CrossReport cross;
            if(!frames||!sauti_aac_ps5_cross_envelope_analyze(
                    history,&targets,previous_raw,32u*64u,
                    raw_qmf,32u*64u,&cross)){
                fprintf(stderr,"FAIL PS-5c4a cross envelope frame=%zu offset=%zu\n",
                        frames,offset);
                goto done;
            }
            cross_frames++;
            for(unsigned k=history->previous.tables.kx;
                k<history->previous.tables.kx+history->previous.tables.m;k++)
                cross_mean_power+=cross.mean_source_power[k];
            if(!isfinite(cross_mean_power)){
                fprintf(stderr,"FAIL PS-5c4a power overflow\n");
                goto done;
            }
            fprintf(stderr,
                "CROSS frame=%zu previous_slots=%u inherited=%u full_envelope_slots=%u missing_bins=%u\n",
                frames,cross.previous_slots_in_envelope,
                cross.deferred_slots,cross.total_slots_in_envelope,
                cross.missing_source_bins);
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
        if(experimental){
            /* EXPERIMENTAL OUTPUT ONLY: preliminary envelope mean
             * gain, unfiltered ROM noise and no harmonic injection.
             * The QMF geometry comes from the true temporal owner.
             * DO NOT treat the samples as ISO-conformant HE-AAC v2. */
            memcpy(adjusted,raw_qmf,32u*64u*sizeof(*raw_qmf));
            for(unsigned t=0;t<32u;t++){
                const SautiAacSbrTemporalHfSlot *slot=&targets.slot[t];
                for(unsigned k=slot->kx;k<slot->k2;k++){
                    /* ROM advances in both full and no-noise modes,
                     * so the toggle does not shift later phase state. */
                    SautiAacSbrComplex rom={0};
                    if(!low_only &&
                       !sauti_aac_sbr_noise_rom_next(&noise_state,&rom)){
                        fprintf(stderr,"FAIL experimental noise ROM frame=%zu\n",frames);
                        goto done;
                    }
                    const double e=slot->energy[k];
                    const double q=slot->noise_factor[k];
                    /* Quantized SBR envelope energy uses an internal 16-bit
                     * amplitude convention whereas native QMF uses
                     * normalized PCM. Match amplitude domains before
                     * multiplying a derived gain or injecting ROM noise.
                     * The old inconsistent preview is isolated in the
                     * explicit --experimental-pcm-unscaled control.
                     * This is still not a normative SBR HF limiter. */
                    const double unit=unscaled?1.0:1.0/32768.0;
                    const double signal=low_only?0.0:raw_hf?1.0:
                        envelope->provisional_gain[t][k]*unit;
                    /* Three diagnostic controls: all HF, signal HF
                     * without ROM injection, or low-QMF only. No mode
                     * is a normative SBR synthesis implementation. */
                    const double noise_fraction=noise_25?0.25:
                        noise_50?0.50:noise_75?0.75:
                        (full||unit_scale||unscaled)?1.0:0.0;
                    const double noise=noise_fraction*
                        sqrt(e*(q/(1.0+q)))*unit;
                    const SautiAacSbrComplex x=raw_qmf[t*64u+k];
                    const double re=(double)x.re*signal+(double)rom.re*noise;
                    const double im=(double)x.im*signal+(double)rom.im*noise;
                    const double power=(double)x.re*x.re+(double)x.im*x.im;
                    hf_raw_power+=power;
                    hf_signal_power+=power*signal*signal;
                    hf_noise_power+=noise*noise*((double)rom.re*rom.re+
                                                 (double)rom.im*rom.im);
                    if(signal>hf_peak_gain)hf_peak_gain=signal;
                    hf_bins++;
                    if(!isfinite(hf_raw_power)||!isfinite(hf_signal_power)||
                       !isfinite(hf_noise_power)){
                        fprintf(stderr,"FAIL experimental HF power overflow frame=%zu\\n",frames);
                        goto done;
                    }
                    if(!isfinite(re)||!isfinite(im)||
                       fabs(re)>FLT_MAX||fabs(im)>FLT_MAX){
                        fprintf(stderr,"FAIL experimental HF frame=%zu slot=%u bin=%u\n",
                                frames,t,k);goto done;
                    }
                    adjusted[t*64u+k]=(SautiAacSbrComplex){(float)re,(float)im};
                }
            }
            unsigned pcm_ready=0u;
            if(!sauti_aac_he_ps_delay_push(
                    payload,adjusted,32u*64u,ps_state,
                    stereo_pcm,4096u,&pcm_ready)){
                fprintf(stderr,"FAIL experimental PS bridge frame=%zu\n",frames);
                goto done;
            }
            if(pcm_ready){
                for(unsigned i=0;i<4096u;i++)
                    if(!isfinite(stereo_pcm[i])){
                        fprintf(stderr,"FAIL nonfinite experimental stereo frame=%zu\n",frames);
                        goto done;
                    }
                if(fwrite(stereo_pcm,sizeof(*stereo_pcm),4096u,preview)!=4096u){
                    perror("Experimental PCM write");goto done;
                }
                published_pcm++;
            }
        }
        memcpy(previous_raw,raw_qmf,32u*64u*sizeof(*raw_qmf));
        frames++;offset+=len;
    }
    if(ferror(in)||!frames){fprintf(stderr,"FAIL empty/IO\n");goto done;}
    fprintf(stderr,"PASS PS-5b side-data audit frames=%zu temporal_transitions=%zu pending_tail=%u; raw QMF validated; NO PCM produced\n",
            frames,temporal_frames,(unsigned)history->current_plan.deferred_slots);
    fprintf(stderr,"PASS PS-5c2 diagnostic frames=%zu transitions=%zu inherited_QMF_slots=%zu missing_raw_sources=%zu target_power=%.7g raw_power=%.7g; NO HF/PCM synthesis\n",
            frames,temporal_frames,inherited,missing_sources,target_power,raw_power);
    fprintf(stderr,
            "PASS PS-5c3 envelope audit frames=%zu signal_budget=%.7g noise_budget=%.7g absent_envelope_sources=%zu; NO HF/PCM synthesis\n",
            frames,signal_budget,noise_budget,envelope_missing);
    fprintf(stderr,"PASS PS-5c4a full cross-envelope frames=%u mean_power_sum=%.9g; NO HF/PCM synthesis\n",cross_frames,cross_mean_power);
    if(experimental){
        if(published_pcm!=frames-1u||fflush(preview)!=0){
            fprintf(stderr,"FAIL incomplete experimental stereo preview\n");
            goto done;
        }
        fprintf(stderr,
            "HF-POWER mode=%s bins=%zu raw_power=%.9g signal_power=%.9g "
            "rom_noise_power=%.9g peak_gain=%.9g signal/raw=%.9g\n",
            low_only?"low-only":raw_hf?"raw-hf":unit_scale?"unit-scale":no_noise?"no-noise":unscaled?"unscaled":noise_25?"noise-25":noise_50?"noise-50":noise_75?"noise-75":"full",
            hf_bins,hf_raw_power,hf_signal_power,hf_noise_power,hf_peak_gain,
            hf_raw_power>0.0?hf_signal_power/hf_raw_power:0.0);
        fprintf(stderr,
            "EXPERIMENTAL PCM mode=%s frames=%zu published=%u samples_per_channel=%zu "
            "not normative SBR (no limiter/smoothing/harmonics); last PS frame pending\n",
            low_only?"low-only":raw_hf?"raw-hf":no_noise?"no-noise":"full",
            frames,published_pcm,(size_t)published_pcm*2048u);
    }
    exit_code=0;
done:
    free(adjusted);
    free(ps_state);
    if(preview && fclose(preview)!=0)exit_code=1;
    if(exit_code&&experimental)remove(argv[3]);
    free(envelope);free(gain_report);free(previous_raw);
    free(raw_qmf);free(qmf_state);free(payload);free(history);
    free(decoder);
    if(fclose(in)!=0)exit_code=1;
    return exit_code;
}
