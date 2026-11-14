/* HE-AAC SBR-32: diagnostic runner for REAL third-party ADTS files.
 *
 * This tool uses ONLY Sauti's own AAC-LC+SBR+QMF decoder. It does not
 * use the host FFmpeg, FAAD, or any external codec as a fallback.
 * The independent encoder and an external decoder can be used by
 * the developer to GENERATE/VERIFY fixtures, not by this executable.
 *
 * CLI: ./build/sauti_he_aac_probe input.aac [max_decode_frames] [preview.f32le]
 *
 * FIRST pass reads every ADTS header, counts stereo/mono, rates,
 * malformed length/CRC; SECOND pass runs a continuous native decoder
 * until first rejection, without resetting its entropy/overlap history.
 * When the full chain fails, a SIDE-EFFECT-FREE trial of the SBR-30
 * core parser distinguishes parse failure from a later QMF/HF gap.
 *
 * The decoder is experimental. "decoded" means produced finite 2048
 * QMF samples, NOT MPEG-4 HE-AAC conformance-quality sound.
 */
#include "sauti_aac_he_adts_qmf.h"
#include "sauti_aac_sbr_temporal_qmf.h"
#include "sauti_aac_sbr_temporal_power.h"
#include "sauti_aac_sbr_temporal_gain.h"
#include "sauti_aac_sbr_temporal_apply.h"
#include "sauti_aac_sbr_temporal_noise.h"
#include "sauti_aac_sbr_temporal_mix.h"
#include "sauti_aac_sbr_temporal_smooth.h"
#include "sauti_aac_sbr_temporal_pcm_preview.h"
#include "sauti_aac_sbr_temporal_energy_audit.h"
#include "sauti_aac_sbr_temporal_envelope_audit.h"
#include "sauti_aac_sbr_lpc_estimate.h"
#include "sauti_aac_sbr_temporal_harmonic_preview.h"
#include "sauti_aac_sbr_temporal_hf_units.h"
#include "sauti_aac_sbr_temporal_energy_units.h"
#include "sauti_aac_sbr_temporal_reference_gain.h"
#include "sauti_aac_sbr_temporal_gain_bridge.h"
#include "sauti_aac_sbr_hf_phase_probe.h"
#include "sauti_aac_sbr_temporal_source6.h"
#include "sauti_aac_sbr_hf_band_audit.h"
#include "sauti_aac_sbr_hf_patch_route_audit.h"
#include "sauti_aac_sbr_low_source_audit.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_syntax.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ADTS_FRAME 8191u
#define DEFAULT_DECODE_LIMIT 24u

typedef struct {
    size_t frames;
    size_t mono,stereo,other_channels,crc;
    size_t bytes;
    unsigned first_rate,first_channels;
    unsigned max_length;
    size_t offset;
} AdtsSummary;

static int frame_read(FILE *f,uint8_t data[MAX_ADTS_FRAME],
                      size_t *length) {
    const size_t got=fread(data,1u,7u,f);
    if(!got && feof(f))return 0;
    if(got!=7u)return -1;
    if(data[0]!=0xffu || (data[1]&0xf6u)!=0xf0u ||
       ((data[2]>>6u)&3u)!=1u)return -1;
    const unsigned len=((unsigned)(data[3]&3u)<<11u)|
                        ((unsigned)data[4]<<3u)|
                        ((unsigned)data[5]>>5u);
    if(len<7u || len>MAX_ADTS_FRAME ||
       (!(data[1]&1u) && len<9u))
        return -1;
    if(fread(data+7u,1u,len-7u,f)!=len-7u)return -1;
    *length=(size_t)len;
    return 1;
}
static unsigned adts_channels(const uint8_t *frame){
    return ((unsigned)(frame[2]&1u)<<2u)|
            ((unsigned)frame[3]>>6u);
}
static int scan(const char *path,AdtsSummary *out){
    FILE *f=fopen(path,"rb");
    if(!f){fprintf(stderr,"Nie moge otworzyc %s: %s\n",
                   path,strerror(errno));return 0;}
    AdtsSummary next={0};
    uint8_t frame[MAX_ADTS_FRAME];
    size_t len=0;
    for(;;){
        const int r=frame_read(f,frame,&len);
        if(r==0)break;
        if(r<0){
            fprintf(stderr,
                "Nieprawidlowa ramka ADTS lub obciecie przy offset %zu\n",
                next.offset);
            fclose(f);return 0;
        }
        SautiAacAdts header;
        if(!sauti_aac_adts_parse_asm(frame,len,&header) ||
           header.frame_bytes!=len){
            fprintf(stderr,
                "Nieprawidlowy naglowek ADTS przy offset %zu\n",
                next.offset);
            fclose(f);return 0;
        }
        const unsigned channels=adts_channels(frame);
        if(next.frames==0u){
            next.first_rate=header.core_sample_rate;
            next.first_channels=channels;
        }
        if(channels==1u)next.mono++;
        else if(channels==2u)next.stereo++;
        else next.other_channels++;
        if(!(frame[1]&1u))next.crc++;
        if(len>next.max_length)next.max_length=(unsigned)len;
        next.bytes+=len;
        next.offset+=len;
        next.frames++;
    }
    if(ferror(f)){perror(path);fclose(f);return 0;}
    fclose(f);
    if(!next.frames){
        fprintf(stderr,"Plik nie zawiera ramek ADTS\n");
        return 0;
    }
    *out=next;
    return 1;
}
/* Side-effect-free FIL inspection: do not infer HE-AAC/PS from ADTS.
 * Report the first post-SCE element and FIL extension type so an
 * unsupported SBR payload cannot be mistaken for an unknown FIL type. */
static void inspect_fil(const uint8_t *frame,size_t len) {
    SautiAacAdts adts;
    if(!sauti_aac_adts_parse_asm(frame,len,&adts) ||
       adts.header_bytes>adts.frame_bytes ||
       adts.frame_bytes>len) {
        puts("FIL diagnostyka: niepoprawny ADTS");
        return;
    }
    const uint8_t *raw=frame+adts.header_bytes;
    const size_t n=adts.frame_bytes-adts.header_bytes;
    const unsigned rate=(frame[2]>>2u)&15u;
    size_t pos=0u;
    SautiAacLcMonoData sce;
    const int parsed=sauti_aac_lc_mono_parse(raw,n,&pos,rate,&sce);
    if(parsed!=1) {
        printf("FIL diagnostyka: parser SCE zatrzymal sie (%d)\n",parsed);
        return;
    }
    for(unsigned index=0;index<8u;index++) {
        uint32_t id=0u;
        if(!sauti_aac_bits_read_asm(raw,n,&pos,3u,&id)) {
            puts("FIL diagnostyka: brak identyfikatora elementu");
            return;
        }
        if(id==7u) {
            puts("FIL diagnostyka: ID_END (brak dalszego SBR)");
            return;
        }
        if(id!=6u) {
            printf("FIL diagnostyka: element po SCE ID=%u (nie FIL)\n",id);
            return;
        }
        SautiAacFillInfo fill;
        if(!sauti_aac_fill_parse(raw,n,&pos,&fill)) {
            puts("FIL diagnostyka: niepoprawna dlugosc FIL");
            return;
        }
        printf("FIL diagnostyka: element %u, payload=%u B, extension_type=%u, extension_bits=%zu\n",
               index,fill.payload_bytes,fill.extension_type,fill.extension_bits);
        if(fill.extension_type==13u)
            puts("FIL diagnostyka: rozpoznano EXT_SBR_DATA (13); odrzucenie dotyczy skladni/ograniczen SBR");
        else if(fill.extension_type==14u)
            puts("FIL diagnostyka: rozpoznano EXT_SBR_DATA_CRC (14); wymagany poprawny CRC-10 i obslugiwany podzbior SBR");
    }
    puts("FIL diagnostyka: osiagnieto limit 8 elementow");
}
static const char *qmf_stage_name(SautiAacSbrQmfStage stage){
    switch(stage){
        case SAUTI_SBR_QMF_STAGE_INPUT:return "input/argumenty";
        case SAUTI_SBR_QMF_STAGE_MAP:return "mapa patchy";
        case SAUTI_SBR_QMF_STAGE_HISTORY:return "historia QMF";
        case SAUTI_SBR_QMF_STAGE_ANALYSIS:return "analiza QMF 32";
        case SAUTI_SBR_QMF_STAGE_TRANSPOSE:return "transpozycja HF";
        case SAUTI_SBR_QMF_STAGE_PREDICT:return "predyktor HF";
        case SAUTI_SBR_QMF_STAGE_HF_ADJUST:return "regulacja energii HF/limiter";
        case SAUTI_SBR_QMF_STAGE_SYNTHESIS:return "synteza QMF 64";
        case SAUTI_SBR_QMF_STAGE_OK:return "OK";
        default:return "nieznany";
    }
}
/* Recompute only HF source energies on copied QMF history. This
 * verifies a suspected zero-source limiter rejection without changing
 * the production decoder's fail-closed behavior. */
static void diagnose_zero_hf(const float core[1024],
                             const SautiAacSbrPayloadFrame *payload,
                             const SautiAacSbrQmfFrameState *current){
    double core_sum=0.0,core_max=0.0;
    unsigned core_nonzero=0u;
    for(unsigned i=0;i<1024u;i++){
        const double v=core[i];
        if(!isfinite(v)){
            puts("AAC-LC rdzen: niepoprawna probka");
            return;
        }
        if(v!=0.0)core_nonzero++;
        if(fabs(v)>core_max)core_max=fabs(v);
        core_sum+=v*v;
    }
    printf("AAC-LC rdzen: RMS=%.9g, max=%.9g, niezerowe=%u/1024\n",
           sqrt(core_sum/1024.0),core_max,core_nonzero);
    /* The SBR noise floor and add_harmonic flags can carry legitimate
     * HF energy even when the AAC-LC startup frame is all zero.
     * Report the signaling before changing the limiter contract. */
    unsigned harmonic_enabled=0u;
    for(unsigned b=0u;b<payload->tables.n_high;b++)
        if(payload->harmonic[b])harmonic_enabled++;
    double noise_min=INFINITY,noise_max=0.0;
    for(unsigned e=0u;e<payload->energy.noise_count;e++)
        for(unsigned b=0u;b<payload->tables.n_noise;b++){
            const double q=payload->energy.noise_factor[e][b];
            if(q<noise_min)noise_min=q;
            if(q>noise_max)noise_max=q;
        }
    printf("SBR zrodla dodatkowe: harmoniczne=%u/%u, noise Q min=%.9g max=%.9g\n",
           harmonic_enabled,payload->tables.n_high,noise_min,noise_max);
    SautiAacSbrQmfAnalysisState analysis=current->analysis;
    SautiAacSbrComplex low[32u*32u],high[32u*64u];
    const unsigned slots=32u;
    if(!sauti_aac_sbr_qmf_analyze_32(core,1024u,slots,32u,
                                     low,32u*32u,&analysis) ||
       !sauti_aac_sbr_hf_transpose(&payload->patches,&payload->tables,
                                  low,32u*32u,slots,high,32u*64u)){
        puts("HF energia: nie udalo sie odtworzyc analizy/transpozycji");
        return;
    }
    const SautiAacSbrTables *tab=&payload->tables;
    unsigned zeros=0u,positive=0u;
    double max_target=0.0;
    for(unsigned e=0;e<payload->energy.env_count;e++){
        unsigned env_zeros=0u,env_positive=0u;
        const unsigned a=payload->env_edges[e],z=payload->env_edges[e+1u];
        const unsigned resolution=payload->energy.freq_res[e];
        const uint8_t *freq=resolution?tab->high:tab->low;
        const unsigned bands=resolution?tab->n_high:tab->n_low;
        if(a>=z || z>slots) {
            puts("HF energia: niepoprawne granice obwiedni");
            return;
        }
        for(unsigned b=0;b<bands;b++) {
            const double target=payload->energy.env_energy[e][b];
            if(target>max_target)max_target=target;
            for(unsigned k=freq[b];k<freq[b+1u];k++){
                double sum=0.0;
                for(unsigned t=a;t<z;t++){
                    const SautiAacSbrComplex c=high[t*64u+k];
                    sum+=(double)c.re*c.re+(double)c.im*c.im;
                }
                if(target>0.0){
                    positive++;
                    env_positive++;
                    if(sum==0.0){zeros++;env_zeros++;}
                }
            }
        }
        printf("HF energia: obwiednia %u, sloty %u..%u, dodatnie=%u, zerowe=%u\n",
               e,a,z,env_positive,env_zeros);
    }
    printf("HF energia: pasma z dodatnim celem=%u, zerowe zrodla=%u, max cel=%.9g\n",
           positive,zeros,max_target);
    if(zeros)
        puts("HF energia: limiter odrzucilby dodatnia energie bez zrodla (możliwy powod)");
    else
        puts("HF energia: brak zerowych zrodel; problem moze byc w dalszym etapie regulacji");
}
/* Independent continuous LC+SBR pass. It never promotes a frame to
 * HE-AAC/QMF success, and never fabricates QMF state after rejection.
 * The accepted core history is advanced exactly as coded in ADTS. */
static const char *sbr_stage_name(SautiAacSbrPayloadStage stage){
    switch(stage){
        case SAUTI_SBR_PAYLOAD_INPUT:return "argumenty/zakres payloadu";
        case SAUTI_SBR_PAYLOAD_TYPE:return "typ rozszerzenia";
        case SAUTI_SBR_PAYLOAD_HEADER:return "naglowek SBR";
        case SAUTI_SBR_PAYLOAD_TABLES:return "tablice czestotliwosci/patchy";
        case SAUTI_SBR_PAYLOAD_GRID:return "siatka czasowa";
        case SAUTI_SBR_PAYLOAD_BORDERS:return "granice czasowe obwiedni";
        case SAUTI_SBR_PAYLOAD_CONTROL:return "sterowanie dtdf/invf";
        case SAUTI_SBR_PAYLOAD_QUANT:return "entropia obwiedni/szumu";
        case SAUTI_SBR_PAYLOAD_ENERGY:return "dekantyzacja energii";
        case SAUTI_SBR_PAYLOAD_HARMONICS:return "flagi harmonicznych";
        case SAUTI_SBR_PAYLOAD_EXTENDED:return "dane rozszerzone/PS";
        case SAUTI_SBR_PAYLOAD_PADDING:return "wyrownanie/padding";
        case SAUTI_SBR_PAYLOAD_OK:return "OK";
        default:return "nieznany";
    }
}
/* Diagnose SBR on a copy of the PRE-FAILURE continuous history.
 * Never advance decoder state or classify this as successful PCM. */
/* Inspect the real SBR grid without relaxing the strict 0..16
 * synthesis contract. The SBR header is consumed only on a local
 * scratch buffer; previous header is taken from pre-failure state. */
static void diagnose_sbr_grid(const uint8_t *raw,size_t n,
                              const SautiAacFillInfo *fill,
                              const SautiAacSbrPayloadState *prior){
    if(fill->extension_bits<8u ||
       fill->extension_bits>SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES*8u ||
       n>SIZE_MAX/8u ||
       fill->extension_start_bit>n*8u ||
       fill->extension_bits>n*8u-fill->extension_start_bit)return;
    uint8_t packed[SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES]={0};
    for(size_t i=0;i<fill->extension_bits;i++){
        const size_t src=fill->extension_start_bit+i;
        packed[i/8u]|=(uint8_t)(((raw[src/8u]>>(7u-(src&7u)))&1u)
                      <<(7u-(i&7u)));
    }
    const size_t bytes=fill->extension_bits/8u;
    size_t pos=0u;
    uint32_t kind=0u,has_header=0u,extra=0u;
    if(!sauti_aac_bits_read_asm(packed,bytes,&pos,4u,&kind) ||
       kind!=13u ||
       !sauti_aac_bits_read_asm(packed,bytes,&pos,1u,&has_header))
        return;
    SautiAacSbrHeader header=prior->header;
    if(has_header &&
       !sauti_aac_sbr_header_parse(packed,bytes,&pos,&header))return;
    if(!has_header && !prior->initialized)return;
    if(!sauti_aac_bits_read_asm(packed,bytes,&pos,1u,&extra))return;
    if(extra){
        uint32_t reserved=0u;
        if(!sauti_aac_bits_read_asm(packed,bytes,&pos,4u,&reserved))return;
    }
    SautiAacSbrGrid grid;
    if(!sauti_aac_sbr_grid_parse(packed,bytes,&pos,16u,&grid)){
        puts("SBR siatka: sam parser sbr_grid odrzucil ramke");
        return;
    }
    printf("SBR siatka: frame_class=%u, env=%u, noise=%u, pointer=%u, header=%u\n",
           grid.frame_class,grid.env_count,grid.noise_count,
           grid.pointer,has_header);
    printf("SBR granice env:");
    for(unsigned i=0;i<=grid.env_count;i++)
        printf(" %u",grid.env_border[i]);
    printf(" (oczekiwane 0..16)\nSBR granice noise:");
    for(unsigned i=0;i<=grid.noise_count;i++)
        printf(" %u",grid.noise_border[i]);
    putchar('\n');
}
static void diagnose_sbr_rejection(const uint8_t *frame,size_t len,
                                   const SautiAacHeAdtsMonoState *prior){
    SautiAacAdts adts;
    if(!sauti_aac_adts_parse_asm(frame,len,&adts) ||
       adts.header_bytes>adts.frame_bytes || adts.frame_bytes>len)return;
    const uint8_t *raw=frame+adts.header_bytes;
    const size_t n=adts.frame_bytes-adts.header_bytes;
    const unsigned rate=(frame[2]>>2u)&15u;
    size_t pos=0u;
    SautiAacLcMonoData sce;
    if(sauti_aac_lc_mono_parse(raw,n,&pos,rate,&sce)!=1){
        puts("SBR etap: nieudany parser rdzenia AAC-LC");
        return;
    }
    for(unsigned index=0u;index<8u;index++){
        uint32_t id=0u;
        if(!sauti_aac_bits_read_asm(raw,n,&pos,3u,&id) || id==7u)
            return;
        if(id!=6u)return;
        SautiAacFillInfo fill;
        if(!sauti_aac_fill_parse(raw,n,&pos,&fill))return;
        if(fill.extension_type!=13u)continue;
        SautiAacSbrPayloadState trial=prior->sbr;
        SautiAacSbrPayloadFrame parsed;
        SautiAacSbrPayloadStage stage=SAUTI_SBR_PAYLOAD_INPUT;
        const int ok=sauti_aac_sbr_payload_sce_parse_ex(
            raw,n,fill.extension_start_bit,fill.extension_bits,
            adts.core_sample_rate*2u,&trial,&parsed,&stage);
        printf("SBR diagnostyka: FIL %u, etap=%s, wynik=%d\n",
               index,sbr_stage_name(stage),ok);
        if(stage==SAUTI_SBR_PAYLOAD_BORDERS) {
            diagnose_sbr_grid(raw,n,&fill,&prior->sbr);
            SautiAacSbrPayloadState temporal_state=prior->sbr;
            SautiAacSbrPayloadFrame temporal_payload;
            SautiAacSbrPayloadStage temporal_stage=SAUTI_SBR_PAYLOAD_INPUT;
            const int temporal=sauti_aac_sbr_payload_temporal_parse_ex(
                raw,n,fill.extension_start_bit,fill.extension_bits,
                adts.core_sample_rate*2u,
                &temporal_state,&temporal_payload,&temporal_stage);
            printf("SBR temporal side-data: etap=%s, wynik=%d\n",
                   sbr_stage_name(temporal_stage),temporal);
            if(temporal) {
                printf("SBR temporal side-data: env_end=%u, QMF_end=%u; BRAK dekodowania PCM\n",
                       temporal_payload.grid.env_border[temporal_payload.grid.env_count],
                       temporal_payload.env_edges[temporal_payload.grid.env_count]);
                SautiAacSbrTemporalPlan plan;
                if(sauti_aac_sbr_temporal_plan(&temporal_payload,&plan))
                    printf("SBR temporal plan: teraz env=%u noise=%u; odroczone env=%u noise=%u, sloty=%u\n",
                           plan.env_current_count,plan.noise_current_count,
                           plan.env_deferred_count,plan.noise_deferred_count,
                           plan.deferred_slots);
            }
        }
        return;
    }
}
/* SBR-54c diagnostics: isolate the three sources of the abnormal
 * PCM waveform. All component signals pass through their OWN 64-QMF
 * synthesis histories; do not change SBR-54a or final audio PCM.
 * Each active HF mask slot is contiguous and belongs to the emitted
 * (not the current input) frame. Component outputs sum approximately
 * to the SBR-54a preview due to linearity of QMF synthesis. */
enum { SBR_COMPONENTS=3, SBR_SAMPLES=2048 };
static const char *component_names[SBR_COMPONENTS]={
    "low","hf","noise"
};
typedef struct {
    SautiAacSbrQmfSynthWindowState synth[SBR_COMPONENTS];
    size_t frames;
    double sum_sq[SBR_COMPONENTS];
    double peak[SBR_COMPONENTS];
} Sbr54cComponents;

static int sbr54c_render(
    const SautiAacSbrComplex low[32u*32u],
    const SautiAacSbrComplex high_gain[32u*64u],
    const SautiAacSbrComplex mixed[32u*64u],
    const SautiAacSbrTemporalNoiseFrame *noise,
    const float full_preview[SBR_SAMPLES],
    Sbr54cComponents *state,
    float pcm[SBR_COMPONENTS][SBR_SAMPLES]){
    if(!low || !high_gain || !mixed || !noise || !full_preview ||
       !state || !pcm)
        return 0;
    SautiAacSbrQmfSynthWindowState staged[SBR_COMPONENTS];
    memcpy(staged,state->synth,sizeof(staged));
    SautiAacSbrComplex bands[SBR_SAMPLES];
    float outputs[SBR_COMPONENTS][SBR_SAMPLES];
    double powers[SBR_COMPONENTS]={0},peaks[SBR_COMPONENTS]={0};
    for(unsigned mode=0u;mode<SBR_COMPONENTS;mode++){
        memset(bands,0,sizeof(bands));
        for(unsigned t=0u;t<32u;t++){
            unsigned kx=64u,ended=0u,started=0u;
            for(unsigned k=0u;k<64u;k++){
                const unsigned ix=t*64u+k;
                const unsigned active=noise->active[ix];
                if(active>1u)return 0;
                if(active){
                    if(ended)return 0;
                    if(!started)kx=k;
                    started=1u;
                }else if(started)ended=1u;
            }
            if(!started || kx<1u || kx>32u)return 0;
            if(mode==0u){
                for(unsigned k=0u;k<kx;k++){
                    const SautiAacSbrComplex x=low[t*32u+k];
                    if(!isfinite(x.re)||!isfinite(x.im))return 0;
                    bands[t*64u+k]=x;
                }
            }else for(unsigned k=0u;k<64u;k++){
                const unsigned ix=t*64u+k;
                const SautiAacSbrComplex g=high_gain[ix],m=mixed[ix];
                if(!isfinite(g.re)||!isfinite(g.im)||
                   !isfinite(m.re)||!isfinite(m.im))return 0;
                if(noise->active[ix]){
                    if(mode==1u)bands[ix]=g;
                    else {
                        bands[ix].re=m.re-g.re;
                        bands[ix].im=m.im-g.im;
                    }
                }
            }
        }
        if(!sauti_aac_sbr_qmf_synth_64(
               bands,SBR_SAMPLES,32u,outputs[mode],SBR_SAMPLES,
               &staged[mode]))return 0;
        for(unsigned i=0u;i<SBR_SAMPLES;i++){
            const double v=outputs[mode][i];
            if(!isfinite(v))return 0;
            powers[mode]+=v*v;
            if(fabs(v)>peaks[mode])peaks[mode]=fabs(v);
        }
    }
    /* Mandatory linearity check against the already accepted 54a
     * frame. Otherwise diagnostic stems could be misaligned or
     * misleading even if they contain finite numbers. Allow only
     * float32 rounding error, never a DSP-shape mismatch. */
    for(unsigned i=0u;i<SBR_SAMPLES;i++){
        const double full=full_preview[i];
        const double recomposed=(double)outputs[0][i]+
            (double)outputs[1][i]+(double)outputs[2][i];
        if(!isfinite(full) || !isfinite(recomposed) ||
           fabs(full-recomposed)>1e-3+fabs(full)*1e-5)
            return 0;
    }
    memcpy(state->synth,staged,sizeof(staged));
    for(unsigned mode=0u;mode<SBR_COMPONENTS;mode++){
        state->sum_sq[mode]+=powers[mode];
        if(peaks[mode]>state->peak[mode])
            state->peak[mode]=peaks[mode];
    }
    state->frames++;
    memcpy(pcm,outputs,sizeof(outputs));
    return 1;
}

static int sbr54c_export(FILE *src,const char *path,size_t expected){
    if(!src || !path ||
       fflush(src)!=0 || fseek(src,0L,SEEK_SET)!=0)return 0;
    FILE *dst=fopen(path,"wb");
    if(!dst){
        fprintf(stderr,"SBR-54c: nie mozna otworzyc %s: %s\n",
                path,strerror(errno));
        return 0;
    }
    unsigned char bytes[4096];
    size_t written=0u,n=0u;
    int ok=1;
    while((n=fread(bytes,1u,sizeof(bytes),src))>0u){
        if(fwrite(bytes,1u,n,dst)!=n){
            ok=0;break;
        }
        written+=n;
    }
    if(ferror(src))ok=0;
    if(fclose(dst)!=0)ok=0;
    if(!ok || written!=expected){
        (void)remove(path);
        return 0;
    }
    return 1;
}

static void sbr54e_accumulate(SautiAacSbrTemporalEnergyAudit *total,
                              const SautiAacSbrTemporalEnergyAudit *value){
    total->active_bins+=value->active_bins;
    total->double_alloc_bins+=value->double_alloc_bins;
    total->harmonic_bins+=value->harmonic_bins;
    total->decoded_target_power+=value->decoded_target_power;
    total->adjusted_hf_power+=value->adjusted_hf_power;
    total->nominal_noise_power+=value->nominal_noise_power;
    total->mixed_hf_power+=value->mixed_hf_power;
    if(value->max_budget_ratio>total->max_budget_ratio)
        total->max_budget_ratio=value->max_budget_ratio;
}


/* SBR-73 optional read-only emitted-HF oscillator slot trace.
 * Enable with SAUTI_SBR73_TRACE=1 and capture stderr to a file. */
static int sbr73_trace_phase(const SautiAacSbrTemporalHfTargets *targets,
                            const uint8_t onset[32],
                            uint64_t frame,unsigned phase_before){
    if(!getenv("SAUTI_SBR73_TRACE"))return 1;
    if(!targets || !onset || phase_before>3u)return 0;
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        if(slot->harmonic[18u]>1u || slot->harmonic[42u]>1u ||
           slot->kx>64u || slot->k2>64u || onset[t]>1u)return 0;
        if(fprintf(stderr,
                   "SBR73_SAUTI,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                   (unsigned long long)frame,t,
                   (phase_before+t)&3u,(phase_before+t+2u)&3u,
                   slot->kx,slot->k2,(unsigned)slot->source,
                   (unsigned)slot->envelope,
                   (unsigned)slot->harmonic[18u],
                   (unsigned)slot->harmonic[42u],
                   (unsigned)onset[t])<0)return 0;
    }
    return fflush(stderr)==0;
}

/* SBR-73 optional complex HF QMF at k18/k42 for phase0 and phase+2.
 * The samples are the experimental preview that enters the sine PCM
 * path. Enable with SAUTI_SBR73_SAUTI_QMF=/path.csv. Unset leaves
 * every buffer untouched. */
static int sbr73_trace_qmf(const SautiAacSbrComplex *phase0,
                           const SautiAacSbrComplex *phase2,
                           uint64_t frame){
    const char *path=getenv("SAUTI_SBR73_SAUTI_QMF");
    static FILE *out;
    static int header_done;
    if(!path)return 1;
    if(!phase0 || !phase2)return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(!header_done){
        if(fprintf(out,
                   "frame,slot,re18,im18,re42,im42,p2re18,p2im18,p2re42,p2im42\n")<0)
            return 0;
        header_done=1;
    }
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrComplex *a18=&phase0[(size_t)t*64u+18u];
        const SautiAacSbrComplex *a42=&phase0[(size_t)t*64u+42u];
        const SautiAacSbrComplex *b18=&phase2[(size_t)t*64u+18u];
        const SautiAacSbrComplex *b42=&phase2[(size_t)t*64u+42u];
        if(!isfinite(a18->re) || !isfinite(a18->im) ||
           !isfinite(a42->re) || !isfinite(a42->im) ||
           !isfinite(b18->re) || !isfinite(b18->im) ||
           !isfinite(b42->re) || !isfinite(b42->im))
            return 0;
        if(fprintf(out,
                   "%llu,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                   (unsigned long long)frame,t,
                   (double)a18->re,(double)a18->im,
                   (double)a42->re,(double)a42->im,
                   (double)b18->re,(double)b18->im,
                   (double)b42->re,(double)b42->im)<0)
            return 0;
    }
    return fflush(out)==0;
}

/* SBR-73 optional gain-adjusted QMF after the per-band tone scale.
 * Enable with SAUTI_SBR73_SAUTI_SCALED=/path.csv. */
static int sbr73_trace_scaled(uint64_t frame,
                              const SautiAacSbrComplex *scaled){
    const char *path=getenv("SAUTI_SBR73_SAUTI_SCALED");
    static FILE *out;
    static int header_done;
    if(!path)return 1;
    if(!scaled)return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(!header_done){
        if(fprintf(out,"frame,slot,re18,im18,re42,im42\n")<0)
            return 0;
        header_done=1;
    }
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrComplex a18=scaled[(size_t)t*64u+18u];
        const SautiAacSbrComplex a42=scaled[(size_t)t*64u+42u];
        if(!isfinite(a18.re) || !isfinite(a18.im) ||
           !isfinite(a42.re) || !isfinite(a42.im))
            return 0;
        if(fprintf(out,"%llu,%u,%.9g,%.9g,%.9g,%.9g\n",
                   (unsigned long long)frame,t,
                   (double)a18.re,(double)a18.im,
                   (double)a42.re,(double)a42.im)<0)
            return 0;
    }
    return fflush(out)==0;
}

/* SBR-73 optional gain-only HF: analysis slot t-6 times the limiter
 * hf_gain times 1024/sqrt(2). SBR-53d adjusted_hf is not rewritten.
 * Enable with SAUTI_SBR73_SAUTI_GAINHF=/path.csv. */
static int sbr73_trace_gainhf(uint64_t frame,
                              const SautiAacSbrComplex *shifted){
    const char *path=getenv("SAUTI_SBR73_SAUTI_GAINHF");
    static FILE *out;
    static int header_done;
    if(!path)return 1;
    if(!shifted)return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(!header_done){
        if(fprintf(out,"frame,slot,re18,im18,re42,im42\n")<0)
            return 0;
        header_done=1;
    }
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrComplex a18=shifted[(size_t)t*64u+18u];
        const SautiAacSbrComplex a42=shifted[(size_t)t*64u+42u];
        if(!isfinite(a18.re) || !isfinite(a18.im) ||
           !isfinite(a42.re) || !isfinite(a42.im))
            return 0;
        if(fprintf(out,"%llu,%u,%.9g,%.9g,%.9g,%.9g\n",
                   (unsigned long long)frame,t,
                   (double)a18.re,(double)a18.im,
                   (double)a42.re,(double)a42.im)<0)
            return 0;
    }
    return fflush(out)==0;
}

/* SBR-73 optional sum of the shifted gain HF and the already scaled
 * tone. Bins whose sine sample is still the raw mix stay on the
 * scaled buffer. Enable with SAUTI_SBR73_SAUTI_SUM=/path.csv. */
static int sbr73_trace_sum(uint64_t frame,
                           const SautiAacSbrComplex *sum){
    const char *path=getenv("SAUTI_SBR73_SAUTI_SUM");
    static FILE *out;
    static int header_done;
    if(!path)return 1;
    if(!sum)return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(!header_done){
        if(fprintf(out,"frame,slot,re18,im18,re42,im42\n")<0)
            return 0;
        header_done=1;
    }
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrComplex a18=sum[(size_t)t*64u+18u];
        const SautiAacSbrComplex a42=sum[(size_t)t*64u+42u];
        if(!isfinite(a18.re) || !isfinite(a18.im) ||
           !isfinite(a42.re) || !isfinite(a42.im))
            return 0;
        if(fprintf(out,"%llu,%u,%.9g,%.9g,%.9g,%.9g\n",
                   (unsigned long long)frame,t,
                   (double)a18.re,(double)a18.im,
                   (double)a42.re,(double)a42.im)<0)
            return 0;
    }
    return fflush(out)==0;
}

/* SBR-73 optional split of gain-adjusted HF and the unsmoothed noise
 * amplitude at k18/k42. Enable with SAUTI_SBR73_SAUTI_PARTS=/path.csv.
 * Unset leaves every buffer untouched. */
static int sbr73_trace_parts(uint64_t frame,
                             const SautiAacSbrComplex *gain,
                             const SautiAacSbrTemporalNoiseFrame *noise,
                             const SautiAacSbrTemporalHfTargets *targets){
    const char *path=getenv("SAUTI_SBR73_SAUTI_PARTS");
    static FILE *out;
    static int header_done;
    if(!path)return 1;
    if(!gain || !noise || !targets)return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(!header_done){
        if(fprintf(out,
                   "frame,slot,gre18,gim18,gre42,gim42,namp18,nre18,nim18,namp42,nre42,nim42,h18,h42\n")<0)
            return 0;
        header_done=1;
    }
    for(unsigned t=0u;t<32u;t++){
        const size_t i18=(size_t)t*64u+18u;
        const size_t i42=(size_t)t*64u+42u;
        const SautiAacSbrComplex g18=gain[i18],g42=gain[i42];
        const SautiAacSbrComplex v18=noise->vector[i18],v42=noise->vector[i42];
        const double a18=noise->raw_amplitude[i18];
        const double a42=noise->raw_amplitude[i42];
        const unsigned h18=targets->slot[t].harmonic[18u];
        const unsigned h42=targets->slot[t].harmonic[42u];
        if(!isfinite(g18.re)||!isfinite(g18.im)||
           !isfinite(g42.re)||!isfinite(g42.im)||
           !isfinite(v18.re)||!isfinite(v18.im)||
           !isfinite(v42.re)||!isfinite(v42.im)||
           !isfinite(a18)||a18<0.0||!isfinite(a42)||a42<0.0||
           h18>1u||h42>1u)
            return 0;
        if(fprintf(out,
                   "%llu,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%u,%u\n",
                   (unsigned long long)frame,t,
                   (double)g18.re,(double)g18.im,(double)g42.re,(double)g42.im,
                   a18,(double)v18.re,(double)v18.im,
                   a42,(double)v42.re,(double)v42.im,h18,h42)<0)
            return 0;
    }
    return fflush(out)==0;
}

/* SBR-73 optional AAC core PCM that the temporal QMF analyzes.
 * 1024 native floats per successfully decoded frame, ingest order.
 * Enable with SAUTI_SBR73_SAUTI_CORE=/path.f32le. */
static int sbr73_trace_core(const float *core){
    const char *path=getenv("SAUTI_SBR73_SAUTI_CORE");
    static FILE *out;
    if(!path)return 1;
    if(!core)return 0;
    for(unsigned i=0u;i<1024u;i++)
        if(!isfinite(core[i]))return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(fwrite(core,sizeof(float),1024u,out)!=1024u)return 0;
    return fflush(out)==0;
}

/* SBR-73 optional pre-gain HF at the bins that dominate the open limiter
 * groups. frame is the ingest index, which source=1 gains apply to.
 * Enable with SAUTI_SBR73_SAUTI_X=/path.csv. */
static int sbr73_trace_x(uint64_t frame, const SautiAacSbrComplex *hf){
    const char *path=getenv("SAUTI_SBR73_SAUTI_X");
    static FILE *out;
    static int header_done;
    static const unsigned ks[7]={18u,19u,35u,36u,40u,41u,42u};
    if(!path)return 1;
    if(!hf)return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(!header_done){
        if(fprintf(out,"frame,slot,k,re,im\n")<0)return 0;
        header_done=1;
    }
    for(unsigned n=0u;n<7u;n++){
        const unsigned k=ks[n];
        for(unsigned t=0u;t<32u;t++){
            const SautiAacSbrComplex x=hf[(size_t)t*64u+k];
            if(!isfinite(x.re) || !isfinite(x.im))return 0;
            if(fprintf(out,"%llu,%u,%u,%.9g,%.9g\n",
                       (unsigned long long)frame,t,k,
                       (double)x.re,(double)x.im)<0)
                return 0;
        }
    }
    return fflush(out)==0;
}

/* SBR-73 optional e_curr of the 6-slot HF-generator window.
 * One row per HF bin of each current-frame envelope. This does not
 * replace the unshifted SBR-53c mean and does not change a gain.
 * Enable with SAUTI_SBR73_SAUTI_HFGEN_POWER=/path.csv. */
static int sbr73_trace_hfgen_power(
    uint64_t frame,
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrComplex *hf){
    const char *path=getenv("SAUTI_SBR73_SAUTI_HFGEN_POWER");
    static FILE *out;
    static int header_done;
    static int have_prev;
    static SautiAacSbrComplex prev[32u*64u];
    if(!path)return 1;
    if(!history || !hf || !history->ready)return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(!header_done){
        if(fprintf(out,"frame,envelope,k,ecurr\n")<0)return 0;
        header_done=1;
    }
    const SautiAacSbrPayloadFrame *payload=&history->current;
    const unsigned kx=payload->tables.kx;
    const unsigned k2=kx+payload->tables.m;
    if(payload->grid.env_count<1u ||
       payload->grid.env_count>SAUTI_AAC_SBR_MAX_ENV ||
       kx<1u || k2<=kx || k2>64u)
        return 0;
    for(unsigned env=0u;env<payload->grid.env_count;env++){
        const unsigned start=payload->env_edges[env];
        const unsigned end=payload->env_edges[env+1u];
        if(end<=start || end>38u || start>32u)return 0;
        if((int)start-6<0 && !have_prev)continue;
        double mean[64];
        if(!sauti_aac_sbr_temporal_power_hfgen_window(
               hf,32u*64u,
               have_prev?prev:NULL,have_prev?32u*64u:0u,
               start,end,kx,k2,mean))
            return 0;
        for(unsigned k=kx;k<k2;k++){
            if(!isfinite(mean[k]) || mean[k]<0.0)return 0;
            if(fprintf(out,"%llu,%u,%u,%.9g\n",
                       (unsigned long long)frame,env,k,mean[k])<0)
                return 0;
        }
    }
    memcpy(prev,hf,sizeof(prev));
    have_prev=1;
    return fflush(out)==0;
}

/* Limiter boosts, post-boost hf_gain, and the pre-gain HF for the
 * last few ingest frames. The sine is emitted one loop later, and a
 * source=0 slot still belongs to the frame before that. The shifted
 * gain reads analysis slot t-6, which also borrows one frame back,
 * so three saved frames are the minimum. */
#define SBR73_BOOST_RING 4u
typedef struct {
    int valid;
    uint64_t frame;
    unsigned envs;
    uint8_t band_ok[SAUTI_AAC_SBR_MAX_ENV][64];
    double boost[SAUTI_AAC_SBR_MAX_ENV][64];
    double hf_gain[SAUTI_AAC_SBR_MAX_ENV][64];
    SautiAacSbrComplex baseline[32u*64u];
} Sbr73BoostSaved;
static Sbr73BoostSaved sbr73_boost_ring[SBR73_BOOST_RING];

static void sbr73_boost_remember(
    uint64_t frame,unsigned envs,
    const uint8_t band_ok[SAUTI_AAC_SBR_MAX_ENV][64],
    const double boost[SAUTI_AAC_SBR_MAX_ENV][64],
    const double hf_gain[SAUTI_AAC_SBR_MAX_ENV][64],
    const SautiAacSbrComplex *baseline){
    memmove(&sbr73_boost_ring[1],&sbr73_boost_ring[0],
            (SBR73_BOOST_RING-1u)*sizeof(sbr73_boost_ring[0]));
    Sbr73BoostSaved *saved=&sbr73_boost_ring[0];
    memset(saved,0,sizeof(*saved));
    saved->valid=1;
    saved->frame=frame;
    saved->envs=envs;
    memcpy(saved->band_ok,band_ok,sizeof(saved->band_ok));
    memcpy(saved->boost,boost,sizeof(saved->boost));
    memcpy(saved->hf_gain,hf_gain,sizeof(saved->hf_gain));
    memcpy(saved->baseline,baseline,sizeof(saved->baseline));
}

static const Sbr73BoostSaved *sbr73_boost_find(uint64_t frame){
    for(unsigned i=0u;i<SBR73_BOOST_RING;i++)
        if(sbr73_boost_ring[i].valid &&
           sbr73_boost_ring[i].frame==frame)
            return &sbr73_boost_ring[i];
    return NULL;
}

/* Scale the emitted tone by the boost of the envelope that owns
 * each slot. A missing window leaves that bin at the unscaled
 * preview. phase+2 is not scaled. */
static int sbr73_scale_emitted_tone(
    uint64_t frame,
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *gain,
    const SautiAacSbrComplex *raw,
    const SautiAacSbrComplex *sine,
    SautiAacSbrComplex *scaled){
    if(!targets || !gain || !raw || !sine || !scaled)return 0;
    double boost[32u*64u]={0};
    uint8_t apply[32u*64u]={0};
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        if(slot->source>1u || slot->envelope>=SAUTI_AAC_SBR_MAX_ENV ||
           slot->k2<slot->kx || slot->k2>64u)
            return 0;
        uint64_t owner=frame;
        if(slot->source==0u){
            if(frame==0u)continue;
            owner=frame-1u;
        }
        const Sbr73BoostSaved *saved=sbr73_boost_find(owner);
        if(!saved || slot->envelope>=saved->envs)continue;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            if(!saved->band_ok[slot->envelope][k])continue;
            const unsigned ix=t*64u+k;
            apply[ix]=1u;
            boost[ix]=saved->boost[slot->envelope][k];
        }
    }
    return sauti_aac_sbr_temporal_harmonic_scale(
        gain,32u*64u,raw,32u*64u,sine,32u*64u,
        boost,apply,scaled,32u*64u);
}

/* Gain-only HF for the emitted frame. Slot t reads the saved
 * baseline at t-6 (previous frame, slot t+26, when t<6) and the
 * limiter hf_gain of the envelope that owns the slot. The product
 * uses 1024/sqrt(2). Bins without a saved window stay on fallback,
 * which is the unshifted SBR-53d HF. */
static int sbr73_shift_emitted_gain(
    uint64_t frame,
    const SautiAacSbrTemporalHfTargets *targets,
    const SautiAacSbrComplex *fallback,
    SautiAacSbrComplex *shifted){
    if(!targets || !fallback || !shifted)return 0;
    memcpy(shifted,fallback,32u*64u*sizeof(*shifted));
    const Sbr73BoostSaved *cur=sbr73_boost_find(frame);
    if(!cur)return 1;
    const Sbr73BoostSaved *prev=frame?sbr73_boost_find(frame-1u):NULL;
    double gain[32u*64u]={0};
    uint8_t apply[32u*64u]={0};
    const double domain=1024.0/sqrt(2.0);
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        if(slot->source>1u || slot->envelope>=SAUTI_AAC_SBR_MAX_ENV ||
           slot->k2<slot->kx || slot->k2>64u)
            return 0;
        uint64_t owner=frame;
        if(slot->source==0u){
            if(frame==0u)continue;
            owner=frame-1u;
        }
        const Sbr73BoostSaved *saved=sbr73_boost_find(owner);
        if(!saved || slot->envelope>=saved->envs)continue;
        for(unsigned k=slot->kx;k<slot->k2;k++){
            if(!saved->band_ok[slot->envelope][k])continue;
            const unsigned ix=t*64u+k;
            const double g=saved->hf_gain[slot->envelope][k];
            if(!isfinite(g) || g<0.0)return 0;
            const double scaled_g=g*domain;
            if(!isfinite(scaled_g))return 0;
            apply[ix]=1u;
            gain[ix]=scaled_g;
        }
    }
    return sauti_aac_sbr_temporal_hf_shift_gain(
        cur->baseline,32u*64u,
        prev?prev->baseline:NULL,prev?32u*64u:0u,
        gain,apply,shifted,32u*64u);
}

/* Tone bins: shifted gain HF plus the scaled tone already removed
 * from the old SBR-53d bin. Other bins stay on the scaled buffer,
 * whose remainder is noise rather than a second gain. */
static int sbr73_assemble_shifted_tone(
    const SautiAacSbrComplex *shifted,
    const SautiAacSbrComplex *adjusted,
    const SautiAacSbrComplex *raw,
    const SautiAacSbrComplex *sine,
    const SautiAacSbrComplex *scaled,
    SautiAacSbrComplex *sum){
    if(!shifted || !adjusted || !raw || !sine || !scaled || !sum)
        return 0;
    SautiAacSbrComplex staging[32u*64u];
    for(unsigned ix=0u;ix<32u*64u;ix++){
        const SautiAacSbrComplex s=sine[ix],r=raw[ix];
        if(!isfinite(s.re) || !isfinite(s.im) ||
           !isfinite(r.re) || !isfinite(r.im) ||
           !isfinite(scaled[ix].re) || !isfinite(scaled[ix].im) ||
           !isfinite(shifted[ix].re) || !isfinite(shifted[ix].im) ||
           !isfinite(adjusted[ix].re) || !isfinite(adjusted[ix].im))
            return 0;
        if(s.re==r.re && s.im==r.im){
            staging[ix]=scaled[ix];
            continue;
        }
        const double re=(double)shifted[ix].re
            +((double)scaled[ix].re-(double)adjusted[ix].re);
        const double im=(double)shifted[ix].im
            +((double)scaled[ix].im-(double)adjusted[ix].im);
        if(!isfinite(re) || !isfinite(im))return 0;
        staging[ix].re=(float)re;
        staging[ix].im=(float)im;
    }
    memcpy(sum,staging,sizeof(staging));
    return 1;
}

/* SBR-73 optional limiter fed by the HF-generator window.
 * Envelope energy and window power are raised by 2^30, which is
 * the analysis-domain factor removed by SBR-60. Q is already in
 * that domain. Applied SBR-53d gain stays untouched. The boost
 * table, the post-boost hf_gain, and this frame's pre-gain HF are
 * always kept for the experimental tone scale and the shifted gain.
 * The CSV is written only when SAUTI_SBR73_SAUTI_LIMIT=/path.csv. */
static int sbr73_trace_limit(
    uint64_t frame,
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrComplex *hf){
    const char *path=getenv("SAUTI_SBR73_SAUTI_LIMIT");
    static FILE *out;
    static int header_done;
    static int have_prev;
    static SautiAacSbrComplex prev[32u*64u];
    if(!history || !hf || !history->ready)return 0;
    if(path && !out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(path && !header_done){
        if(fprintf(out,"frame,envelope,delta,k,group,boost,sm,g\n")<0)
            return 0;
        header_done=1;
    }
    const SautiAacSbrPayloadFrame *payload=&history->current;
    const SautiAacSbrTables *tab=&payload->tables;
    const unsigned kx=tab->kx;
    const unsigned k2=kx+tab->m;
    if(payload->grid.env_count<1u ||
       payload->grid.env_count>SAUTI_AAC_SBR_MAX_ENV ||
       payload->gain_mode>3u ||
       kx<1u || k2<=kx || k2>64u ||
       tab->n_noise>SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
       tab->n_high>SAUTI_AAC_SBR_MAX_MASTER_BANDS)
        return 0;
    SautiAacSbrLimiterBands bands;
    if(!sauti_aac_sbr_limiter_bands_build(
           tab,&payload->patches,payload->limiter_mode,&bands) ||
       bands.count<1u || bands.count>64u)
        return 0;
    uint8_t band_ok[SAUTI_AAC_SBR_MAX_ENV][64]={0};
    double saved_boost[SAUTI_AAC_SBR_MAX_ENV][64]={0};
    double saved_gain[SAUTI_AAC_SBR_MAX_ENV][64]={0};
    for(unsigned env=0u;env<payload->grid.env_count;env++){
        const unsigned start=payload->env_edges[env];
        const unsigned end=payload->env_edges[env+1u];
        if(end<=start || end>38u || start>32u ||
           env>=payload->grid.env_count ||
           payload->energy.freq_res[env]>1u ||
           payload->attack[env]>1u)
            return 0;
        if((int)start-6<0 && !have_prev)continue;
        double window[64];
        if(!sauti_aac_sbr_temporal_power_hfgen_window(
               hf,32u*64u,
               have_prev?prev:NULL,have_prev?32u*64u:0u,
               start,end,kx,k2,window))
            return 0;
        const unsigned fine=payload->energy.freq_res[env];
        const uint8_t *freq=fine?tab->high:tab->low;
        const unsigned nband=fine?tab->n_high:tab->n_low;
        double energy[64]={0},noise[64]={0},ec[64]={0};
        uint8_t center[64]={0},mapped[64]={0};
        for(unsigned b=0u;b<nband;b++){
            if(freq[b]>=freq[b+1u] || freq[b+1u]>64u)return 0;
            const double value=payload->energy.env_energy[env][b];
            if(!isfinite(value) || value<0.0)return 0;
            const double scaled=ldexp(value,30);
            if(!isfinite(scaled))return 0;
            for(unsigned k=freq[b];k<freq[b+1u];k++)energy[k]=scaled;
        }
        unsigned noise_index=payload->grid.noise_count;
        for(unsigned j=0u;j<payload->grid.noise_count;j++)
            if(start>=payload->noise_edges[j] &&
               end<=payload->noise_edges[j+1u]){
                noise_index=j;break;
            }
        if(noise_index>=payload->grid.noise_count)return 0;
        for(unsigned b=0u;b<tab->n_noise;b++){
            if(tab->noise[b]>=tab->noise[b+1u] ||
               tab->noise[b+1u]>64u)return 0;
            const double value=payload->energy.noise_factor[noise_index][b];
            if(!isfinite(value) || value<0.0)return 0;
            for(unsigned k=tab->noise[b];k<tab->noise[b+1u];k++)
                noise[k]=value;
        }
        for(unsigned b=0u;b<tab->n_high;b++){
            if(payload->harmonic[b]>1u ||
               tab->high[b]>=tab->high[b+1u])return 0;
            if(!payload->harmonic[b])continue;
            const unsigned mid=(tab->high[b]+tab->high[b+1u])/2u;
            if(mid>=64u)return 0;
            center[mid]=1u;
        }
        for(unsigned b=0u;b<nband;b++){
            unsigned present=0u;
            for(unsigned k=freq[b];k<freq[b+1u];k++)
                if(center[k])present=1u;
            if(!present)continue;
            for(unsigned k=freq[b];k<freq[b+1u];k++)mapped[k]=1u;
        }
        for(unsigned k=kx;k<k2;k++){
            ec[k]=ldexp(window[k],30);
            if(!isfinite(ec[k]) || ec[k]<0.0)return 0;
        }
        /* attack[0] stays clear when no onset was signaled, so a
         * one-envelope frame keeps delta=1. A real pointer sets
         * attack on that envelope only. */
        const unsigned delta=payload->attack[env]?0u:1u;
        SautiAacSbrGainLimitedFrame limited;
        if(!sauti_aac_sbr_gain_limit_partition_flags(
               energy,ec,noise,center,mapped,bands.edges,
               bands.count,payload->gain_mode,delta,&limited))
            return 0;
        for(unsigned k=kx;k<k2;k++){
            int group=-1;
            for(unsigned g=0u;g<bands.count;g++)
                if(k>=bands.edges[g] && k<bands.edges[g+1u]){
                    group=(int)g;break;
                }
            if(group<0)return 0;
            const double boost=limited.group_boost[group];
            const double sm=limited.band[k].sine_amplitude;
            const double g=limited.band[k].hf_gain;
            if(!isfinite(boost) || boost<0.0 ||
               !isfinite(sm) || sm<0.0 ||
               !isfinite(g) || g<0.0)return 0;
            saved_boost[env][k]=boost;
            saved_gain[env][k]=g;
            band_ok[env][k]=1u;
            if(path && fprintf(out,"%llu,%u,%u,%u,%d,%.9g,%.9g,%.9g\n",
                       (unsigned long long)frame,env,delta,k,group,
                       boost,sm,g)<0)
                return 0;
        }
    }
    memcpy(prev,hf,sizeof(prev));
    have_prev=1;
    sbr73_boost_remember(frame,payload->grid.env_count,
                         band_ok,saved_boost,saved_gain,hf);
    if(path && fflush(out)!=0)return 0;
    return 1;
}

/* Limiter group containing absolute QMF band k, or -1 when k is outside. */
static int sbr73_limiter_group(const SautiAacSbrLimiterBands *bands,
                               unsigned k){
    if(!bands || k>=64u ||
       bands->count<1u ||
       bands->count>SAUTI_AAC_SBR_MAX_LIMITER_BANDS)return -1;
    for(unsigned g=0u;g<bands->count;g++){
        if(bands->edges[g]>=bands->edges[g+1u])return -1;
        if(k>=bands->edges[g] && k<bands->edges[g+1u])return (int)g;
    }
    return -1;
}

/* SBR-73 optional per-envelope limiter boost for the bands whose tone
 * scale is open. SBR-53d and SBR-54k are both recorded. source=1 gains
 * apply to emitted frame `ingest`; source=0 completes the previous
 * frame's deferred tail (emitted frame ingest-1). Enable with
 * SAUTI_SBR73_SAUTI_BOOST=/path.csv. Unset leaves every buffer untouched. */
static int sbr73_trace_boost(uint64_t ingest,
                             const SautiAacSbrTemporalHistory *history,
                             const SautiAacSbrTemporalGainReport *legacy,
                             const SautiAacSbrTemporalReferenceGainReport *ref){
    const char *path=getenv("SAUTI_SBR73_SAUTI_BOOST");
    static FILE *out;
    static int header_done;
    if(!path)return 1;
    if(!history || !legacy || !ref || !history->frames ||
       legacy->completed_count!=ref->completed_count ||
       legacy->completed_count>SAUTI_AAC_SBR_MAX_ENV+1u)return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(!header_done){
        if(fprintf(out,
                   "ingest,history_frames,applies_to,source,envelope,slots,"
                   "g18,b53_18,b54_18,s54_18,n54_18,"
                   "g42,b53_42,b54_42,s54_42,n54_42\n")<0)
            return 0;
        header_done=1;
    }
    for(unsigned i=0u;i<legacy->completed_count;i++){
        const SautiAacSbrTemporalGainEnvelope *old=&legacy->completed[i];
        const SautiAacSbrTemporalReferenceGainEnvelope *neu=&ref->completed[i];
        if(old->source!=neu->source || old->envelope!=neu->envelope ||
           old->slots!=neu->slots || old->kx!=neu->kx || old->k2!=neu->k2 ||
           old->limiter_count!=neu->limiter_count ||
           old->source>1u ||
           (old->source==0u && (!history->has_previous || ingest==0u)))
            return 0;
        const SautiAacSbrPayloadFrame *frame=
            old->source==0u?&history->previous:&history->current;
        SautiAacSbrLimiterBands bands;
        if(!sauti_aac_sbr_limiter_bands_build(
               &frame->tables,&frame->patches,frame->limiter_mode,&bands) ||
           bands.count!=old->limiter_count)return 0;
        const unsigned ks[2]={18u,42u};
        int group[2];
        double b53[2],b54[2],s54[2],n54[2];
        for(unsigned n=0u;n<2u;n++){
            const unsigned k=ks[n];
            group[n]=-1;
            b53[n]=b54[n]=s54[n]=n54[n]=0.0;
            if(k<old->kx || k>=old->k2)continue;
            group[n]=sbr73_limiter_group(&bands,k);
            if(group[n]<0 || (unsigned)group[n]>=old->limiter_count)return 0;
            b53[n]=old->limiter_boost[group[n]];
            b54[n]=neu->limited.group_boost[group[n]];
            s54[n]=neu->limited.band[k].sine_amplitude;
            n54[n]=neu->limited.band[k].noise_amplitude;
            if(!isfinite(b53[n]) || b53[n]<0.0 ||
               !isfinite(b54[n]) || b54[n]<0.0 ||
               !isfinite(s54[n]) || s54[n]<0.0 ||
               !isfinite(n54[n]) || n54[n]<0.0)return 0;
        }
        const uint64_t applies=old->source==1u?ingest:ingest-1u;
        if(fprintf(out,
                   "%llu,%llu,%llu,%u,%u,%u,"
                   "%d,%.9g,%.9g,%.9g,%.9g,"
                   "%d,%.9g,%.9g,%.9g,%.9g\n",
                   (unsigned long long)ingest,
                   (unsigned long long)history->frames,
                   (unsigned long long)applies,
                   (unsigned)old->source,(unsigned)old->envelope,
                   (unsigned)old->slots,
                   group[0],b53[0],b54[0],s54[0],n54[0],
                   group[1],b53[1],b54[1],s54[1],n54[1])<0)
            return 0;
    }
    return fflush(out)==0;
}

/* Per-bin limiter inputs for the same envelopes as sbr73_trace_boost.
 * E and Q are the normalized envelope values the gain calculators saw.
 * ecurr is the pre-gain mean |X|^2. sine_center is the high-band midpoint;
 * s_mapped is that flag spread across the envelope frequency band, which
 * is the split FFmpeg uses between s_m and the gain equation.
 * Enable with SAUTI_SBR73_SAUTI_BOOST_BINS=/path.csv. */
static int sbr73_trace_boost_bins(
    uint64_t ingest,
    const SautiAacSbrTemporalHistory *history,
    const SautiAacSbrTemporalPowerReport *power,
    const SautiAacSbrTemporalGainReport *legacy,
    const SautiAacSbrTemporalReferenceGainReport *ref){
    const char *path=getenv("SAUTI_SBR73_SAUTI_BOOST_BINS");
    static FILE *out;
    static int header_done;
    if(!path)return 1;
    if(!history || !power || !legacy || !ref || !history->frames ||
       legacy->completed_count!=ref->completed_count ||
       legacy->completed_count!=power->completed_count ||
       legacy->completed_count>SAUTI_AAC_SBR_MAX_ENV+1u)return 0;
    if(!out){
        out=fopen(path,"wb");
        if(!out){
            perror(path);
            return 0;
        }
    }
    if(!header_done){
        if(fprintf(out,
                   "applies_to,source,envelope,delta,transient_env,gain_mode,freq_res,"
                   "k,group,E,Q,ecurr,sine_center,s_mapped,g53,g54,s54,n54\n")<0)
            return 0;
        header_done=1;
    }
    for(unsigned i=0u;i<legacy->completed_count;i++){
        const SautiAacSbrTemporalGainEnvelope *old=&legacy->completed[i];
        const SautiAacSbrTemporalReferenceGainEnvelope *neu=&ref->completed[i];
        const SautiAacSbrTemporalPowerEnvelope *pow=&power->completed[i];
        if(old->source!=neu->source || old->source!=pow->source ||
           old->envelope!=neu->envelope || old->envelope!=pow->envelope ||
           old->kx!=neu->kx || old->kx!=pow->kx ||
           old->k2!=neu->k2 || old->k2!=pow->k2 ||
           old->source>1u || neu->transient_delta>1u ||
           (old->source==0u && (!history->has_previous || ingest==0u)))
            return 0;
        const SautiAacSbrPayloadFrame *frame=
            old->source==0u?&history->previous:&history->current;
        const unsigned env=old->envelope;
        if(env>=frame->grid.env_count ||
           frame->energy.freq_res[env]>1u ||
           frame->gain_mode>3u ||
           frame->transient_env>frame->grid.env_count)return 0;
        SautiAacSbrLimiterBands bands;
        if(!sauti_aac_sbr_limiter_bands_build(
               &frame->tables,&frame->patches,frame->limiter_mode,&bands) ||
           bands.count!=old->limiter_count)return 0;
        const SautiAacSbrTables *tab=&frame->tables;
        const unsigned fine=frame->energy.freq_res[env];
        const uint8_t *freq=fine?tab->high:tab->low;
        const unsigned nband=fine?tab->n_high:tab->n_low;
        const unsigned kx=old->kx,k2=old->k2;
        double energy[64]={0},noise[64]={0};
        uint8_t center[64]={0},mapped[64]={0};
        for(unsigned b=0u;b<nband;b++){
            if(freq[b]>=freq[b+1u] || freq[b+1u]>64u)return 0;
            const double value=frame->energy.env_energy[env][b];
            if(!isfinite(value) || value<0.0)return 0;
            for(unsigned k=freq[b];k<freq[b+1u];k++)energy[k]=value;
        }
        unsigned noise_index=frame->grid.noise_count;
        const unsigned start=frame->env_edges[env];
        const unsigned end=frame->env_edges[env+1u];
        for(unsigned j=0u;j<frame->grid.noise_count;j++)
            if(start>=frame->noise_edges[j] &&
               end<=frame->noise_edges[j+1u]){
                noise_index=j;break;
            }
        if(noise_index>=frame->grid.noise_count ||
           tab->n_noise>SAUTI_AAC_SBR_MAX_NOISE_BANDS)return 0;
        for(unsigned b=0u;b<tab->n_noise;b++){
            if(tab->noise[b]>=tab->noise[b+1u] ||
               tab->noise[b+1u]>64u)return 0;
            const double value=frame->energy.noise_factor[noise_index][b];
            if(!isfinite(value) || value<0.0)return 0;
            for(unsigned k=tab->noise[b];k<tab->noise[b+1u];k++)
                noise[k]=value;
        }
        if(tab->n_high>SAUTI_AAC_SBR_MAX_MASTER_BANDS)return 0;
        for(unsigned b=0u;b<tab->n_high;b++){
            if(frame->harmonic[b]>1u ||
               tab->high[b]>=tab->high[b+1u])return 0;
            if(!frame->harmonic[b])continue;
            const unsigned mid=(tab->high[b]+tab->high[b+1u])/2u;
            if(mid>=64u)return 0;
            center[mid]=1u;
        }
        for(unsigned b=0u;b<nband;b++){
            unsigned present=0u;
            for(unsigned k=freq[b];k<freq[b+1u];k++)
                if(center[k])present=1u;
            if(!present)continue;
            for(unsigned k=freq[b];k<freq[b+1u];k++)mapped[k]=1u;
        }
        const uint64_t applies=old->source==1u?ingest:ingest-1u;
        for(unsigned k=kx;k<k2;k++){
            const int group=sbr73_limiter_group(&bands,k);
            if(group<0)return 0;
            const double ecurr=pow->mean_power[k];
            const double g53=old->applied_gain[k];
            const double g54=neu->limited.band[k].hf_gain;
            const double s54=neu->limited.band[k].sine_amplitude;
            const double n54=neu->limited.band[k].noise_amplitude;
            if(!isfinite(ecurr) || ecurr<0.0 ||
               !isfinite(g53) || g53<0.0 ||
               !isfinite(g54) || g54<0.0 ||
               !isfinite(s54) || s54<0.0 ||
               !isfinite(n54) || n54<0.0)return 0;
            if(fprintf(out,
                       "%llu,%u,%u,%u,%u,%u,%u,"
                       "%u,%d,%.9g,%.9g,%.9g,%u,%u,%.9g,%.9g,%.9g,%.9g\n",
                       (unsigned long long)applies,
                       (unsigned)old->source,(unsigned)old->envelope,
                       (unsigned)neu->transient_delta,
                       (unsigned)frame->transient_env,
                       (unsigned)frame->gain_mode,fine,
                       k,group,energy[k],noise[k],ecurr,
                       (unsigned)center[k],(unsigned)mapped[k],
                       g53,g54,s54,n54)<0)
                return 0;
        }
    }
    return fflush(out)==0;
}

static void diagnose_core_sequence(const char *path,size_t limit,
                                   const char *export_path,
                                   const char *component_prefix){
    FILE *f=fopen(path,"rb");
    if(!f){perror(path);return;}
    SautiAacHeAdtsMonoState core_state={0};
    if(!sauti_aac_he_adts_mono_init(&core_state)){
        fclose(f);return;
    }
    /* Spool all preview samples and publish only after a complete
     * successful decode and final one-frame-delayed QMF flush. */
    FILE *spool=NULL;
    FILE *component_spool[SBR_COMPONENTS]={0};
    char component_path[SBR_COMPONENTS][1024];
    Sbr54cComponents component_state={0};
    int export_io_ok=1,component_io_ok=1;
    if(export_path){
        if(!strcmp(export_path,path)){
            fputs("SBR-54b: plik eksportu nie moze byc plikiem ADTS!\n",stderr);
            fclose(f);return;
        }
        spool=tmpfile();
        if(!spool){
            fprintf(stderr,"SBR-54b: nie mozna utworzyc bufora: %s\n",strerror(errno));
            fclose(f);return;
        }
    }
    if(component_prefix){
        if(!export_path){
            fputs("SBR-54c: eksport skladnikow wymaga takze sciezki PREVIEW\n",stderr);
            fclose(f);if(spool)fclose(spool);return;
        }
        for(unsigned k=0u;k<SBR_COMPONENTS;k++){
            const int n=snprintf(component_path[k],
                 sizeof(component_path[k]),"%s.%s.f32le",
                 component_prefix,component_names[k]);
            if(n<0 || (size_t)n>=sizeof(component_path[k]) ||
               !strcmp(component_path[k],path) ||
               !strcmp(component_path[k],export_path)){
                fputs("SBR-54c: niepoprawna sciezka eksportu skladnikow\n",stderr);
                component_io_ok=0;
                break;
            }
            component_spool[k]=tmpfile();
            if(!component_spool[k]){
                fprintf(stderr,"SBR-54c: brak bufora %s: %s\n",
                        component_names[k],strerror(errno));
                component_io_ok=0;
                break;
            }
        }
        if(!component_io_ok){
            for(unsigned k=0u;k<SBR_COMPONENTS;k++)
                if(component_spool[k])fclose(component_spool[k]);
            if(spool)fclose(spool);
            fclose(f);
            return;
        }
    }
    /* Optional A/B output alongside the unchanged SBR-54a raw PCM. */
    FILE *sine_spool=NULL;
    char sine_path[1024]={0};
    int sine_io_ok=1;
    if(export_path){
        const int n=snprintf(sine_path,sizeof(sine_path),
                             "%s.sine.f32le",export_path);
        if(n<0 || (size_t)n>=sizeof(sine_path) ||
           !strcmp(sine_path,path)){
            fputs("SBR-54h: nazwa pliku kandydata jest niepoprawna\n",stderr);
            if(spool)fclose(spool);
            for(unsigned k=0u;k<SBR_COMPONENTS;k++)
                if(component_spool[k])fclose(component_spool[k]);
            fclose(f);return;
        }
        sine_spool=tmpfile();
        if(!sine_spool){
            fprintf(stderr,"SBR-54h: brak tymczasowego pliku PCM: %s\n",
                    strerror(errno));
            sine_io_ok=0;
        }
    }
    FILE *units_spool=NULL;
    char units_path[1024]={0};
    int units_io_ok=1;
    if(export_path){
        const int n=snprintf(units_path,sizeof(units_path),
                             "%s.units.f32le",export_path);
        if(n<0 || (size_t)n>=sizeof(units_path) ||
           !strcmp(units_path,path) ||
           !strcmp(units_path,sine_path)){
            fputs("SBR-54i: niepoprawna nazwa wyjsciowa PCM\n",stderr);
            if(sine_spool)fclose(sine_spool);
            if(spool)fclose(spool);
            for(unsigned k=0u;k<SBR_COMPONENTS;k++)
                if(component_spool[k])fclose(component_spool[k]);
            fclose(f);return;
        }
        units_spool=tmpfile();
        if(!units_spool){
            fprintf(stderr,"SBR-54i: brak bufora PCM: %s\n",
                    strerror(errno));
            units_io_ok=0;
        }
    }
    FILE *ref_hf_spool=NULL;
    char ref_hf_path[1024]={0};
    int ref_hf_io_ok=1;
    if(export_path){
        const int n=snprintf(ref_hf_path,sizeof(ref_hf_path),
                             "%s.refhf.f32le",export_path);
        if(n<0 || (size_t)n>=sizeof(ref_hf_path) ||
           !strcmp(ref_hf_path,path) ||
           !strcmp(ref_hf_path,sine_path) ||
           !strcmp(ref_hf_path,units_path)){
            fputs("SBR-54m: niedozwolona nazwa PCM\n",stderr);
            if(units_spool)fclose(units_spool);
            if(sine_spool)fclose(sine_spool);
            if(spool)fclose(spool);
            for(unsigned k=0u;k<SBR_COMPONENTS;k++)
                if(component_spool[k])fclose(component_spool[k]);
            fclose(f);return;
        }
        ref_hf_spool=tmpfile();
        if(!ref_hf_spool){
            fprintf(stderr,"SBR-54m: nie moge utworzyc bufora PCM: %s\n",
                    strerror(errno));
            ref_hf_io_ok=0;
        }
    }
    FILE *conj_spool=NULL;
    char conj_path[1024]={0};
    int conj_io_ok=1;
    if(export_path){
        const int n=snprintf(conj_path,sizeof(conj_path),
                             "%s.refhfconj.f32le",export_path);
        if(n<0 || (size_t)n>=sizeof(conj_path) ||
           !strcmp(conj_path,path) ||
           !strcmp(conj_path,sine_path) ||
           !strcmp(conj_path,units_path) ||
           !strcmp(conj_path,ref_hf_path)){
            fputs("SBR-54q: niedozwolona nazwa PCM\n",stderr);
            if(ref_hf_spool)fclose(ref_hf_spool);
            if(units_spool)fclose(units_spool);
            if(sine_spool)fclose(sine_spool);
            if(spool)fclose(spool);
            for(unsigned k=0u;k<SBR_COMPONENTS;k++)
                if(component_spool[k])fclose(component_spool[k]);
            fclose(f);return;
        }
        conj_spool=tmpfile();
        if(!conj_spool){
            fprintf(stderr,"SBR-54q: nie moge utworzyc bufora PCM: %s\n",
                    strerror(errno));
            conj_io_ok=0;
        }
    }
    /* SBR-70 diagnostic-only PCM from an independent phase+2 oscillator.
     * Keep the original SBR-54h file and synthesis state untouched. */
    FILE *phase2_spool=NULL;
    char phase2_path[1024]={0};
    int phase2_io_ok=1;
    if(export_path){
        const int n=snprintf(phase2_path,sizeof(phase2_path),
                             "%s.sinephase2.f32le",export_path);
        if(n<0 || (size_t)n>=sizeof(phase2_path) ||
           !strcmp(phase2_path,path) ||
           !strcmp(phase2_path,export_path)){
            fputs("SBR-70: niedozwolona sciezka PCM phase2\n",stderr);
            phase2_io_ok=0;
        }else{
            phase2_spool=tmpfile();
            if(!phase2_spool){
                fprintf(stderr,"SBR-70: brak bufora phase2 PCM: %s\n",
                        strerror(errno));
                phase2_io_ok=0;
            }
        }
    }
    printf("Diagnostyka ciaglosci rdzenia AAC-LC + temporal SBR (%zu ramek, bez HE-AAC PCM):\n",limit);
    size_t decoded=0,first_nonzero=SIZE_MAX;
    SautiAacSbrTemporalHistory temporal_history={0};
    size_t handoff_count=0u;
    unsigned max_tail=0u;
    size_t hf_target_frames=0u;
    size_t qmf_prepared_frames=0u;
    SautiAacSbrTemporalSource6State source6_state={0};
    SautiAacSbrHfBandAuditState source6_bands={0};
    SautiAacSbrHfPatchRouteAuditState source6_patch_routes={0};
    SautiAacSbrLowSourceAuditState source6_low_audit={0};
    size_t source6_frames=0u,source6_carried=0u;
    size_t source6_missing=0u,source6_prior_owners=0u;
    size_t source6_active=0u,source6_changed=0u;
    double source6_old_energy=0.0,source6_new_energy=0.0;
    double source6_cross=0.0,source6_delta_energy=0.0;
    size_t power_frames=0u,power_envelopes=0u,power_handoffs=0u;
    size_t gain_frames=0u,gain_envelopes=0u,gain_noise_only=0u;
    size_t ref_gain_frames=0u,ref_gain_envelopes=0u;
    size_t ref_gain_changed=0u,ref_gain_noise_positive=0u;
    size_t ref_gain_sine=0u,ref_gain_groups=0u;
    size_t ref_gain_noise_capped=0u,ref_gain_carry=0u;
    double ref_gain_old_power=0.0,ref_gain_new_power=0.0;
    size_t apply_frames=0u,apply_emitted=0u;
    size_t noise_frames=0u,noise_vectors=0u,noise_positive=0u;
    size_t mix_frames=0u,mix_active=0u,mix_noise=0u;
    size_t smooth_frames=0u,smooth_active=0u,smooth_bypasses=0u;
    size_t preview_frames=0u,preview_samples=0u,preview_nonzero=0u;
    size_t energy_audit_frames=0u;
    SautiAacSbrTemporalEnergyAudit energy_budget={0};
    /* SBR-54f owns only read-only diagnostics; it never alters HF,
     * ROM noise, AAC state, QMF histories or emitted PCM. */
    SautiAacSbrTemporalEnvelopeAuditGroup *envelope_groups=NULL;
    SautiAacSbrTemporalEnvelopeAuditState envelope_audit={0};
    if(limit && limit<=SIZE_MAX/SAUTI_AAC_SBR_MAX_ENV){
        envelope_groups=calloc(
            limit*SAUTI_AAC_SBR_MAX_ENV,sizeof(*envelope_groups));
        if(envelope_groups){
            envelope_audit.groups=envelope_groups;
            envelope_audit.max_frames=limit;
        }
    }
    int envelope_audit_ok=envelope_groups!=NULL;
    if(!envelope_audit_ok)
        puts("  SBR-54f: brak pamieci na grupowanie obwiedni, pomijam audit");
    size_t preview_first_nonzero=SIZE_MAX;
    SautiAacSbrTemporalPcmPreviewState temporal_pcm={0};
    SautiAacSbrTemporalPcmPreviewState sine_pcm_state={0};
    SautiAacSbrTemporalPcmPreviewState phase2_pcm_state={0};
    SautiAacSbrTemporalPcmPreviewState units_pcm_state={0};
    SautiAacSbrTemporalPcmPreviewState ref_hf_pcm_state={0};
    SautiAacSbrTemporalPcmPreviewState conj_pcm_state={0};
    size_t conj_pcm_frames=0u,conj_qmf_frames=0u;
    double conj_pcm_energy=0.0,conj_pcm_peak=0.0;
    SautiAacSbrTemporalApplyState ref_hf_apply_state={0};
    size_t ref_hf_frames=0u,ref_hf_qmf_frames=0u;
    double ref_hf_pcm_power=0.0,ref_hf_pcm_peak=0.0;
    size_t units_qmf_frames=0u,units_pcm_frames=0u;
    double units_pcm_power=0.0,units_pcm_peak=0.0;
    SautiAacSbrTemporalHarmonicPreviewState sine_qmf_state={0};
    SautiAacSbrTemporalHarmonicPreviewState phase2_qmf_state={0};
    size_t phase2_qmf_frames=0u,phase2_pcm_frames=0u;
    uint8_t sine_held_onset[32u]={0};
    size_t sine_preview_frames=0u,sine_qmf_frames=0u;
    size_t sine_marked=0u,sine_activated=0u,sine_gated=0u;
    double sine_delta_power=0.0,sine_preview_power=0.0;
    double sine_preview_peak=0.0;
    /* SBR-53h audit: distinguish a genuine FIR slot from deliberate
     * SBR header bypass and envelope-attack bypass. Counting is by
     * ORIGINAL source owner, including any inherited FIXVAR tail. */
    size_t smoothing_headers[2]={0u,0u};
    size_t smoothing_attack_envs=0u;
    size_t smoothing_header_slots=0u,smoothing_attack_only=0u;
    size_t smoothing_both=0u,smoothing_fir_slots=0u;
    SautiAacSbrTemporalSmoothState temporal_smooth={0};
    SautiAacSbrTemporalApplyState temporal_apply={0};
    SautiAacSbrTemporalNoiseState temporal_noise={0};
    SautiAacSbrQmfAnalysisState temporal_analysis={0};
    SautiAacSbrComplex lpc_previous_low[2u*32u]={0};
    SautiAacSbrChirp lpc_previous_chirp={0};
    size_t lpc_estimated_frames=0u,lpc_changed_bins=0u;
    size_t lpc_active_bins=0u,lpc_nonzero_coeffs=0u;
    size_t lpc_nonzero_chirp=0u;
    size_t invf_modes[4]={0u,0u,0u,0u};
    size_t invf_active_frames=0u,invf_total_bands=0u;
    double lpc_delta_power=0.0;
    SautiAacSbrTemporalPowerState temporal_power={0};
    for(size_t i=0;i<limit;i++){
        uint8_t frame[MAX_ADTS_FRAME];
        size_t len=0;
        const int read_result=frame_read(f,frame,&len);
        if(read_result<=0){
            if(read_result<0)puts("  ADTS: niepoprawny odczyt ramki");
            break;
        }
        float core[1024];
        SautiAacSbrPayloadFrame payload;
        SautiAacReason reason=SAUTI_AAC_REASON_OK;
        const int result=sauti_aac_he_adts_mono_temporal_decode_ex(
            frame,len,&core_state,core,&payload,&reason);
        if(result!=1){
            printf("  ramka %zu: odrzucona przez rdzen %s (%d)\n",
                   i,sauti_aac_reason_name(reason),result);
            /* Inspect the actual failing ADTS frame, not just frame 0.
             * A FIL type mismatch and an SBR payload grammar rejection
             * currently share unsupported_fil in the native decoder. */
            inspect_fil(frame,len);
            diagnose_sbr_rejection(frame,len,&core_state);
            break;
        }
        /* SBR-60: the temporal QMF path uses unit-float samples while the
         * decoded envelope remains in reference-QMF energy units. Keep this
         * experimental conversion local to the diagnostic driver; the normal
         * payload and public decoder remain unmodified. */
        SautiAacSbrPayloadFrame normalized_payload;
        if(!sauti_aac_sbr_temporal_payload_normalize_qmf_energy(
               &payload,&normalized_payload)){
            printf("  ramka %zu: SBR-60 normalizacja energii QMF odrzucona\\n",i);
            break;
        }
        payload=normalized_payload;
        double sum=0.0,max_value=0.0;
        unsigned nonzero=0u;
        for(unsigned k=0;k<1024u;k++){
            const double v=core[k];
            sum+=v*v;
            if(fabs(v)>max_value)max_value=fabs(v);
            if(v!=0.0)nonzero++;
        }
        if(!sbr73_trace_core(core)){
            printf("  ramka %zu: SBR-73 core PCM trace odrzucony\n",i);
            break;
        }
        SautiAacSbrTemporalPlan plan;
        if(!sauti_aac_sbr_temporal_plan(&payload,&plan)){
            printf("  ramka %zu: niepoprawny plan czasowy SBR (bez syntezy QMF)\n",i);
            break;
        }
        if(plan.deferred_slots)
            printf("  ramka %zu: odroczone QMF=%u slotow, env=%u, noise=%u (bez PCM HE-AAC)\n",
                   i,plan.deferred_slots,plan.env_deferred_count,
                   plan.noise_deferred_count);
        SautiAacSbrTemporalHandoff handoff;
        if(!sauti_aac_sbr_temporal_history_push(
            &payload,&temporal_history,&handoff)){
            printf("  ramka %zu: blad przekazania historii SBR (bez PCM HE-AAC)\n",i);
            break;
        }
        if(handoff.previous_tail_slots){
            handoff_count++;
            if(handoff.previous_tail_slots>max_tail)
                max_tail=handoff.previous_tail_slots;
            printf("  ramka %zu: historia SBR, poprzedni ogon=%u, poczatek nowej=%u, nakladanie=%u slotow; poprzednia env=%u, noise=%u\n",
                   i,handoff.previous_tail_slots,handoff.current_start_slots,
                   handoff.overlapping_slots,
                   temporal_history.previous.grid.env_count,
                   temporal_history.previous.grid.noise_count);
            if(handoff.current_start_slots==handoff.previous_tail_slots)
                puts("    Ciaglosc SBR: granice dokladnie sie stykaja; brak luki i nakladania");
            else
                puts("    Ciaglosc SBR: obwiednie nakladaja sie; wymagana regulacja w syntezie");
        }
        SautiAacSbrResolvedFrame owners;
        if(!sauti_aac_sbr_temporal_resolve(&temporal_history,&owners)){
            printf("  ramka %zu: niezgodna mapa wlascicieli QMF (bez PCM HE-AAC)\n",i);
            break;
        }
        /* SBR-53a validates real decoded HF target values on all
         * slots, preserving prior-frame origin without producing PCM. */
        SautiAacSbrTemporalHfTargets targets;
        if(!sauti_aac_sbr_temporal_hf_targets(&temporal_history,&targets)){
            printf("  ramka %zu: niepoprawne cele energii/szumu HF SBR-53a (bez PCM HE-AAC)\n",i);
            break;
        }
        hf_target_frames++;
        /* Preserve per-slot ORIGINAL transient onset across the one
         * frame delay. Only the already-parsed SBR owner is accepted.
         * No guessed flags or artificial harmonic activation. */
        uint8_t sine_current_onset[32u]={0};
        int sine_gate_ok=1;
        for(unsigned t=0u;t<32u;t++){
            const SautiAacSbrTemporalHfSlot *owner=&targets.slot[t];
            const SautiAacSbrPayloadFrame *source=
                owner->source==0u?&temporal_history.previous:
                                  &temporal_history.current;
            if(owner->source>1u ||
               owner->envelope>=source->grid.env_count ||
               source->transient_env>source->grid.env_count){
                sine_gate_ok=0;break;
            }
            sine_current_onset[t]=(uint8_t)(
                owner->envelope>=source->transient_env);
        }
        if(!sine_gate_ok){
            printf("  ramka %zu: SBR-54h transient owner niepoprawny\n",i);
            break;
        }
        /* Audit the real source of the smoothing bypass. Do not
         * override any bitstream flag or synthesize alternative PCM. */
        size_t header_slots=0u,attack_only=0u,both=0u,fir=0u;
        int smoothing_policy_ok=payload.bs_smoothing_mode<=1u;
        for(unsigned t=0u;t<32u && smoothing_policy_ok;t++){
            const SautiAacSbrSlotOwner owner=owners.slot[t];
            const SautiAacSbrPayloadFrame *source=
                owner.source==0u?&temporal_history.previous:
                                  &temporal_history.current;
            if(owner.source>1u ||
               owner.envelope>=source->grid.env_count ||
               source->bs_smoothing_mode>1u ||
               source->attack[owner.envelope]>1u){
                smoothing_policy_ok=0;
                break;
            }
            const unsigned head=source->bs_smoothing_mode;
            const unsigned attack=source->attack[owner.envelope];
            if(targets.slot[t].smooth_bypass!=(head||attack)){
                smoothing_policy_ok=0;
                break;
            }
            if(head)header_slots++;
            if(head && attack)both++;
            if(!head && attack)attack_only++;
            if(!head && !attack)fir++;
        }
        if(!smoothing_policy_ok){
            printf("  ramka %zu: SBR-53h-audit niezgodny tryb filtru/ataku (bez PCM)\n",i);
            break;
        }
        smoothing_headers[payload.bs_smoothing_mode]++;
        for(unsigned e=0u;e<payload.grid.env_count;e++)
            smoothing_attack_envs+=payload.attack[e];
        smoothing_header_slots+=header_slots;
        smoothing_attack_only+=attack_only;
        smoothing_both+=both;
        smoothing_fir_slots+=fir;
        /* SBR-53b exercises actual filter-bank analysis and
         * slot-owned HF patch transpose on the real AAC core.
         * Do not infer a playable 64-band HE-AAC waveform. */
        SautiAacSbrComplex low_qmf[32u*32u],baseline_hf[32u*64u];
        if(!sauti_aac_sbr_temporal_qmf_prepare(
                &temporal_history,core,1024u,&temporal_analysis,
                low_qmf,32u*32u,baseline_hf,32u*64u)){
            printf("  ramka %zu: SBR-53b QMF analiza/transpozycja HF odrzucona (bez PCM HE-AAC)\n",i);
            break;
        }
        qmf_prepared_frames++;
        if(!sbr73_trace_x(i,baseline_hf)){
            printf("  ramka %zu: SBR-73 pre-gain X trace odrzucony\n",i);
            break;
        }
        if(!sbr73_trace_hfgen_power(i,&temporal_history,baseline_hf)){
            printf("  ramka %zu: SBR-73 okno energii HF odrzucone\n",i);
            break;
        }
        if(!sbr73_trace_limit(i,&temporal_history,baseline_hf)){
            printf("  ramka %zu: SBR-73 limiter okna HF odrzucony\n",i);
            break;
        }

        /* SBR-54s: read-only six-slot SOURCE-QMF lookback W[i-6],
         * independent of SBR's 2/6 FIXVAR original side-data owner.
         * Do not feed this comparison to the normal HF gains or PCM. */
        SautiAacSbrComplex source6_low[32u*32u];
        SautiAacSbrComplex source6_high[32u*64u];
        SautiAacSbrTemporalSource6Report source6_report;
        if(!sauti_aac_sbr_temporal_source6_prepare(
               &temporal_history,low_qmf,32u*32u,
               &source6_state,source6_low,32u*32u,
               source6_high,32u*64u,&source6_report)){
            printf("  ramka %zu: SBR-54s historia 6 slotow QMF odrzucona\n",i);
            break;
        }
        if(!sauti_aac_sbr_low_source_audit_add(
               source6_low,32u*32u,&source6_report,&source6_low_audit)){
            printf("  ramka %zu: SBR-54v LOW source audit odrzucony\n",i);
            break;
        }
        if(!sauti_aac_sbr_hf_patch_route_audit_add(
               &temporal_history,&source6_patch_routes)){
            printf("  ramka %zu: SBR-54u oryginalne patch routes HF odrzucone\n",i);
            break;
        }
        if(!sauti_aac_sbr_hf_band_audit_add(
               baseline_hf,32u*64u,source6_high,32u*64u,
               &source6_report,&source6_bands)){
            printf("  ramka %zu: SBR-54t rozklad pasm HF QMF odrzucony\n",i);
            break;
        }
        source6_frames++;
        source6_carried+=source6_report.carried_from_previous;
        source6_missing+=source6_report.missing_preroll;
        source6_prior_owners+=source6_report.previous_owner_slots;
        for(unsigned t=source6_report.missing_preroll;t<32u;t++)
            for(unsigned k=0u;k<64u;k++){
                const unsigned ix=t*64u+k;
                const SautiAacSbrComplex old=baseline_hf[ix];
                const SautiAacSbrComplex newer=source6_high[ix];
                if(old.re==0.0f && old.im==0.0f &&
                   newer.re==0.0f && newer.im==0.0f)continue;
                const double orr=old.re,oi=old.im;
                const double nr=newer.re,ni=newer.im;
                source6_active++;
                source6_changed+=(old.re!=newer.re ||
                                  old.im!=newer.im);
                source6_old_energy+=orr*orr+oi*oi;
                source6_new_energy+=nr*nr+ni*ni;
                source6_cross+=orr*nr+oi*ni;
                const double dr=orr-nr,di=oi-ni;
                source6_delta_energy+=dr*dr+di*di;
            }

        /* SBR-54g: experimental native complex LPC(2) estimator,
         * connected to the already existing SBR-13 predictor. This
         * runs SIDE-BY-SIDE with direct transpose; it does NOT feed
         * the original 53c/d/e/54a PCM production/preview path.
         * Prior low/QMF and chirp states advance per AAC core frame.
         */
        SautiAacSbrComplex lpc_input[34u*32u];
        memcpy(lpc_input,lpc_previous_low,
               sizeof(lpc_previous_low));
        memcpy(lpc_input+2u*32u,low_qmf,
               32u*32u*sizeof(low_qmf[0]));
        /* Count the ORIGINAL parsed inverse filtering modes, not
         * inferred chirp values or a made-up override. Zero modes
         * correctly disable the SBR-13 predictor correction. */
        unsigned frame_active_invf=0u;
        if(temporal_history.current.control.noise_band_count!=
           temporal_history.current.tables.n_noise){
            printf("  ramka %zu: SBR-54g invf count niezgodny\n",i);
            break;
        }
        int invf_ok=1;
        for(unsigned j=0u;j<temporal_history.current.tables.n_noise;j++){
            const unsigned mode=temporal_history.current.control.invf_mode[j];
            if(mode>3u){invf_ok=0;break;}
            invf_modes[mode]++;
            invf_total_bands++;
            if(mode)frame_active_invf=1u;
        }
        if(!invf_ok){
            printf("  ramka %zu: SBR-54g invalid invf_mode\n",i);
            break;
        }
        invf_active_frames+=frame_active_invf;
        SautiAacSbrLpcCoeffs lpc_coeff;
        SautiAacSbrChirp next_chirp=lpc_previous_chirp;
        if(next_chirp.ready &&
           next_chirp.count!=temporal_history.current.tables.n_noise)
            memset(&next_chirp,0,sizeof(next_chirp));
        if(!sauti_aac_sbr_lpc_estimate_2(
               lpc_input,34u*32u,32u,&lpc_coeff) ||
           !sauti_aac_sbr_chirp_update(
               temporal_history.current.control.invf_mode,
               temporal_history.current.tables.n_noise,
               &next_chirp)){
            printf("  ramka %zu: SBR-54g estymacja LPC/chirp odrzucona (bez zmiany PCM)\n",i);
            break;
        }
        SautiAacSbrComplex predicted_check[32u*64u];
        unsigned lpc_good=1u;
        for(unsigned t=0u;t<32u;t++){
            const SautiAacSbrTemporalHfSlot *owner=&targets.slot[t];
            const SautiAacSbrPayloadFrame *f=
                owner->source==0u?&temporal_history.previous:
                                  &temporal_history.current;
            const SautiAacSbrChirp *chirp=
                owner->source==0u?&lpc_previous_chirp:&next_chirp;
            if(!sauti_aac_sbr_hf_predict(
                   &f->patches,&f->tables,
                   lpc_input+t*32u,3u*32u,1u,
                   &lpc_coeff,chirp,
                   predicted_check+t*64u,64u)){
                lpc_good=0u;break;
            }
        }
        if(!lpc_good){
            printf("  ramka %zu: SBR-54g brak pelnej ramki predykcji, przerwano\n",i);
            break;
        }
        for(unsigned k=0u;k<32u;k++){
            const SautiAacSbrComplex a=lpc_coeff.alpha0[k];
            const SautiAacSbrComplex b=lpc_coeff.alpha1[k];
            if(a.re!=0.0f || a.im!=0.0f ||
               b.re!=0.0f || b.im!=0.0f)lpc_nonzero_coeffs++;
        }
        for(unsigned t=0u;t<32u;t++){
            const SautiAacSbrTemporalHfSlot *owner=&targets.slot[t];
            const SautiAacSbrPayloadFrame *f=
                owner->source==0u?&temporal_history.previous:
                                  &temporal_history.current;
            const SautiAacSbrChirp *chirp=
                owner->source==0u?&lpc_previous_chirp:&next_chirp;
            for(unsigned j=0u;j<f->tables.n_noise;j++)
                if(chirp->bw[j]>0.0f)lpc_nonzero_chirp++;
            for(unsigned k=owner->kx;k<owner->k2;k++){
                const unsigned ix=t*64u+k;
                const double re=(double)predicted_check[ix].re-
                                 (double)baseline_hf[ix].re;
                const double im=(double)predicted_check[ix].im-
                                 (double)baseline_hf[ix].im;
                const double p=re*re+im*im;
                if(!isfinite(p)){
                    lpc_good=0u;break;
                }
                lpc_delta_power+=p;
                lpc_changed_bins+=(p>1e-14);
                lpc_active_bins++;
            }
            if(!lpc_good)break;
        }
        if(!lpc_good){
            printf("  ramka %zu: SBR-54g niepoprawna energia roznicy QMF\n",i);
            break;
        }
        memcpy(lpc_previous_low,low_qmf+30u*32u,
               sizeof(lpc_previous_low));
        lpc_previous_chirp=next_chirp;
        lpc_estimated_frames++;

        SautiAacSbrTemporalPowerReport power_report;
        if(!sauti_aac_sbr_temporal_power_measure(
               &temporal_history,baseline_hf,32u*64u,
               &temporal_power,&power_report)){
            printf("  ramka %zu: SBR-53c pomiar energii HF odrzucony (bez PCM HE-AAC)\n",i);
            break;
        }
        power_frames++;
        power_envelopes+=power_report.completed_count;
        SautiAacSbrTemporalGainReport gain_report;
        if(!sauti_aac_sbr_temporal_gain_calculate(
               &temporal_history,&power_report,&gain_report)){
            printf("  ramka %zu: SBR-53d limiter pelnych obwiedni odrzucony (bez PCM HE-AAC)\n",i);
            break;
        }
        /* SBR-54l: compare the tested 54j+54k gain model
         * against SBR-53d on the SAME completed original envelopes.
         * Only counters change; no PCM/QMF gain is replaced yet. */
        SautiAacSbrTemporalReferenceGainReport reference_gain;
        if(!sauti_aac_sbr_temporal_reference_gain_calculate(
               &temporal_history,&power_report,&reference_gain) ||
           reference_gain.completed_count!=gain_report.completed_count){
            printf("  ramka %zu: SBR-54l referencyjny limiter obwiedni odrzucony (bez PCM)\n",i);
            break;
        }
        ref_gain_frames++;
        ref_gain_envelopes+=reference_gain.completed_count;
        ref_gain_changed+=reference_gain.gain_changed_bins;
        ref_gain_noise_positive+=reference_gain.noise_positive_bins;
        ref_gain_sine+=reference_gain.sine_bins;
        ref_gain_groups+=reference_gain.group_count;
        ref_gain_noise_capped+=reference_gain.noise_capped_bins;
        ref_gain_old_power+=reference_gain.old_gain_sq_sum;
        ref_gain_new_power+=reference_gain.new_gain_sq_sum;
        for(unsigned j=0u;j<reference_gain.completed_count;j++)
            ref_gain_carry+=reference_gain.completed[j].source==0u;
        if(!sbr73_trace_boost(i,&temporal_history,
                              &gain_report,&reference_gain) ||
           !sbr73_trace_boost_bins(i,&temporal_history,&power_report,
                                   &gain_report,&reference_gain)){
            printf("  ramka %zu: SBR-73 boost trace odrzucony\n",i);
            break;
        }

        gain_frames++;
        gain_envelopes+=gain_report.completed_count;
        for(unsigned e=0u;e<gain_report.completed_count;e++)
            gain_noise_only+=gain_report.completed[e].noise_only_bands;
        /* SBR-53e applies SBR-53d gains only after COMPLETE
         * cross-frame envelope powers are known. QMF-only output. */
        SautiAacSbrComplex adjusted_hf[32u*64u];
        SautiAacSbrTemporalApplyReport apply_report;
        if(!sauti_aac_sbr_temporal_gain_apply(
               &temporal_history,baseline_hf,32u*64u,&gain_report,
               &temporal_apply,adjusted_hf,32u*64u,&apply_report)){
            printf("  ramka %zu: SBR-53e buforowany gain HF odrzucony (bez PCM HE-AAC)\n",i);
            break;
        }
        /* SBR-54m: compare new HF G ONLY, without noise/sine.
         * Reuse SBR-53e time alignment with an INDEPENDENT state. */
        SautiAacSbrTemporalGainReport ref_bridge;
        SautiAacSbrComplex ref_adjusted_hf[32u*64u];
        SautiAacSbrTemporalApplyReport ref_apply_report;
        if(!sauti_aac_sbr_temporal_gain_bridge(
               &reference_gain,&ref_bridge) ||
           !sauti_aac_sbr_temporal_gain_apply(
               &temporal_history,baseline_hf,32u*64u,
               &ref_bridge,&ref_hf_apply_state,
               ref_adjusted_hf,32u*64u,&ref_apply_report) ||
           ref_apply_report.emitted!=apply_report.emitted ||
           (apply_report.emitted &&
            ref_apply_report.frame_index!=apply_report.frame_index)){
            printf("  ramka %zu: SBR-54m niezalezny gain G QMF odrzucony\n",i);
            break;
        }
        if(ref_apply_report.emitted)ref_hf_qmf_frames++;
        apply_frames++;
        if(apply_report.emitted)apply_emitted++;
        /* SBR-53f: align the canonical noise sequence with the
         * frame actually emitted by SBR-53e, never with a future
         * owner's current SBR side data. Still NO PCM synthesis. */
        SautiAacSbrTemporalHfTargets emitted_targets={0};
        if(temporal_noise.ready)
            emitted_targets=temporal_noise.held_targets;
        SautiAacSbrTemporalNoiseFrame noise_frame;
        SautiAacSbrTemporalNoiseReport noise_report;
        if(!sauti_aac_sbr_temporal_noise_prepare(
               &temporal_history,&apply_report,&temporal_noise,
               &noise_frame,&noise_report)){
            printf("  ramka %zu: SBR-53f kolejnosc ROM noise odrzucona (bez PCM HE-AAC)\n",i);
            break;
        }
        SautiAacSbrComplex conjugated_hf[32u*64u];
        if(noise_report.emitted){
            if(!sauti_aac_sbr_hf_conjugate_probe(
                   ref_adjusted_hf,32u*64u,&noise_frame,
                   conjugated_hf,32u*64u)){
                printf("  ramka %zu: SBR-54q HF conjugation odrzucona\n",i);
                break;
            }
            conj_qmf_frames++;
        }
        SautiAacSbrComplex preview[32u*64u];
        SautiAacSbrComplex sine_high[32u*64u];
        SautiAacSbrComplex scaled_sine[32u*64u];
        SautiAacSbrComplex summed_hf[32u*64u];
        const SautiAacSbrComplex *sine_pcm_qmf=NULL;
        SautiAacSbrComplex phase2_high[32u*64u];
        SautiAacSbrComplex units_high[32u*64u];
        SautiAacSbrTemporalMixReport mix_report={0};
        if(noise_report.emitted){
            SautiAacSbrTemporalSmoothFrame smooth_frame;
            SautiAacSbrTemporalSmoothReport smooth_report;
            if(!sauti_aac_sbr_temporal_noise_smooth(
                   &noise_frame,&noise_report,&temporal_smooth,
                   &smooth_frame,&smooth_report)){
                printf("  ramka %zu: SBR-53h czasowe wygladzanie szumu odrzucone (bez PCM)\n",i);
                break;
            }
            smooth_frames++;
            smooth_active+=smooth_report.active_bins;
            smooth_bypasses+=smooth_report.bypassed_slots;
            if(!sauti_aac_sbr_temporal_noise_mix_preview(
                   adjusted_hf,32u*64u,&noise_frame,
                   &apply_report,&noise_report,
                   preview,32u*64u,&mix_report)){
                printf("  ramka %zu: SBR-53g podglad ROM noise + HF odrzucony (bez PCM)\n",i);
                break;
            }
            SautiAacSbrTemporalEnergyAudit energy_entry;
            if(!sauti_aac_sbr_temporal_energy_audit(
                   &emitted_targets,adjusted_hf,32u*64u,
                   &noise_frame,preview,32u*64u,
                   &apply_report,&noise_report,&energy_entry)){
                printf("  ramka %zu: SBR-54e audit bilansu energii odrzucony (bez zmiany PCM)\n",i);
                break;
            }
            energy_audit_frames++;
            sbr54e_accumulate(&energy_budget,&energy_entry);
            if(envelope_audit_ok &&
               !sauti_aac_sbr_temporal_envelope_audit_push(
                   &emitted_targets,adjusted_hf,32u*64u,
                   &noise_frame,&apply_report,&noise_report,
                   &envelope_audit)){
                printf("  ramka %zu: SBR-54f audit pelnych obwiedni odrzucony\n",i);
                envelope_audit_ok=0;
            }
            if(energy_entry.frame_index==2u)
                printf("  SBR-54e ramka 2: target=%.9g, HF=%.9g, NOISE_nom=%.9g, nadbudzet=%u/%u (przed synteza, nie normatywnie)\n",
                       energy_entry.decoded_target_power,
                       energy_entry.adjusted_hf_power,
                       energy_entry.nominal_noise_power,
                       energy_entry.double_alloc_bins,
                       energy_entry.active_bins);
            SautiAacSbrTemporalHarmonicPreviewReport tone_report;
            const unsigned sbr73_phase_before=sine_qmf_state.phase;
            if(!sauti_aac_sbr_temporal_harmonic_preview(
                   &emitted_targets,sine_held_onset,
                   &noise_frame,&noise_report,
                   preview,32u*64u,sine_high,32u*64u,
                   &sine_qmf_state,&tone_report)){
                printf("  ramka %zu: SBR-54h harmonic preview odrzucony\n",i);
                break;
            }
            SautiAacSbrTemporalHarmonicPreviewReport phase2_report;
            if(!sauti_aac_sbr_temporal_harmonic_preview_phase2(
                   &emitted_targets,sine_held_onset,
                   &noise_frame,&noise_report,
                   preview,32u*64u,phase2_high,32u*64u,
                   &phase2_qmf_state,&phase2_report) ||
               phase2_report.frame_index!=tone_report.frame_index ||
               phase2_report.active_tones!=tone_report.active_tones ||
               phase2_qmf_state.frames!=sine_qmf_state.frames ||
               phase2_qmf_state.phase!=sine_qmf_state.phase ||
               memcmp(phase2_qmf_state.previous,
                      sine_qmf_state.previous,
                      sizeof(phase2_qmf_state.previous))!=0){
                printf("  ramka %zu: SBR-70 phase2 QMF odrzucona\n",i);
                break;
            }
            phase2_qmf_frames++;
            if(!sbr73_trace_phase(&emitted_targets,sine_held_onset,
                                  tone_report.frame_index,sbr73_phase_before) ||
               !sbr73_trace_qmf(sine_high,phase2_high,tone_report.frame_index) ||
               !sbr73_trace_parts(tone_report.frame_index,adjusted_hf,
                                  &noise_frame,&emitted_targets)){
                printf("  ramka %zu: SBR-73 trace odrzucony\n",i);
                break;
            }
            SautiAacSbrComplex shifted_hf[32u*64u];
            if(!sbr73_scale_emitted_tone(
                   tone_report.frame_index,&emitted_targets,
                   adjusted_hf,preview,sine_high,scaled_sine) ||
               !sbr73_trace_scaled(tone_report.frame_index,scaled_sine)){
                printf("  ramka %zu: SBR-73 skala tonu odrzucona\n",i);
                break;
            }
            if(!sbr73_shift_emitted_gain(
                   tone_report.frame_index,&emitted_targets,
                   adjusted_hf,shifted_hf) ||
               !sbr73_trace_gainhf(tone_report.frame_index,shifted_hf) ||
               !sbr73_assemble_shifted_tone(
                   shifted_hf,adjusted_hf,preview,sine_high,scaled_sine,
                   summed_hf) ||
               !sbr73_trace_sum(tone_report.frame_index,summed_hf)){
                printf("  ramka %zu: SBR-73 przesuniete HF odrzucone\n",i);
                break;
            }
            /* Listening PCM is the shifted gain plus the scaled tone.
             * The units path stays on the unscaled sine. */
            sine_pcm_qmf=summed_hf;
            if(!sauti_aac_sbr_temporal_hf_units_float(
                   sine_high,32u*64u,&noise_frame,
                   units_high,32u*64u)){
                printf("  ramka %zu: SBR-54i konwersja domeny HF odrzucona\n",i);
                break;
            }
            units_qmf_frames++;
            sine_qmf_frames++;
            sine_marked+=tone_report.marked_bins;
            sine_activated+=tone_report.active_tones;
            sine_gated+=tone_report.gated_noise_bins;
            mix_frames++;
            mix_active+=mix_report.active_bins;
            mix_noise+=mix_report.noise_bins;
            noise_frames++;
            noise_vectors+=noise_report.vectors;
            noise_positive+=noise_report.nonzero_amplitudes;
        }
        /* One-frame delayed emission has used the prior source
         * onset mask; stage the CURRENT one for the next emission. */
        memcpy(sine_held_onset,sine_current_onset,
               sizeof(sine_held_onset));
        /* The SBR-54c decomposition consumes the LOW bands of the
         * prior frame. Snapshot before SBR-54a replaces its buffer. */
        SautiAacSbrComplex emitted_low[32u*32u];
        if(component_prefix && noise_report.emitted)
            memcpy(emitted_low,temporal_pcm.held_low,sizeof(emitted_low));
        /* SBR-54a: delay the LOW QMF by exactly the same frame as
         * SBR-53e HF, then synthesize the full 64-band preview.
         * This is a float waveform, NOT conformance HE-AAC PCM. */
        float preview_pcm[2048u];
        SautiAacSbrTemporalPcmPreviewReport pcm_report;
        if(!sauti_aac_sbr_temporal_pcm_preview_push(
               low_qmf,32u*32u,
               noise_report.emitted?preview:NULL,
               noise_report.emitted?32u*64u:0u,
               noise_report.emitted?&noise_frame:NULL,
               &apply_report,
               noise_report.emitted?&mix_report:NULL,
               &temporal_pcm,preview_pcm,2048u,&pcm_report)){
            printf("  ramka %zu: SBR-54a PCM preview 64-QMF odrzucony (bez normatywnego HE-AAC)\n",i);
            break;
        }
        float conj_pcm[2048u];
        SautiAacSbrTemporalPcmPreviewReport conj_pcm_report;
        if(!sauti_aac_sbr_temporal_pcm_preview_push(
               low_qmf,32u*32u,
               noise_report.emitted?conjugated_hf:NULL,
               noise_report.emitted?32u*64u:0u,
               noise_report.emitted?&noise_frame:NULL,
               &ref_apply_report,
               noise_report.emitted?&mix_report:NULL,
               &conj_pcm_state,conj_pcm,2048u,
               &conj_pcm_report) ||
           conj_pcm_report.emitted!=pcm_report.emitted ||
           (pcm_report.emitted &&
            conj_pcm_report.frame_index!=pcm_report.frame_index)){
            printf("  ramka %zu: SBR-54q conjugated HF PCM odrzucony\n",i);
            break;
        }
        if(conj_pcm_report.emitted){
            conj_pcm_frames++;
            for(unsigned j=0u;j<2048u;j++){
                const double v=conj_pcm[j];
                conj_pcm_energy+=v*v;
                if(fabs(v)>conj_pcm_peak)conj_pcm_peak=fabs(v);
            }
            if(conj_spool &&
               fwrite(conj_pcm,sizeof(float),2048u,conj_spool)!=2048u)
                conj_io_ok=0;
        }
        float ref_hf_pcm[2048u];
        SautiAacSbrTemporalPcmPreviewReport ref_hf_pcm_report;
        if(!sauti_aac_sbr_temporal_pcm_preview_push(
               low_qmf,32u*32u,
               noise_report.emitted?ref_adjusted_hf:NULL,
               noise_report.emitted?32u*64u:0u,
               noise_report.emitted?&noise_frame:NULL,
               &ref_apply_report,
               noise_report.emitted?&mix_report:NULL,
               &ref_hf_pcm_state,ref_hf_pcm,2048u,
               &ref_hf_pcm_report) ||
           ref_hf_pcm_report.emitted!=pcm_report.emitted ||
           (pcm_report.emitted &&
            ref_hf_pcm_report.frame_index!=pcm_report.frame_index)){
            printf("  ramka %zu: SBR-54m odrebny PCM ref HF odrzucony\n",i);
            break;
        }
        if(ref_hf_pcm_report.emitted){
            ref_hf_frames++;
            for(unsigned j=0u;j<2048u;j++){
                const double v=ref_hf_pcm[j];
                ref_hf_pcm_power+=v*v;
                if(fabs(v)>ref_hf_pcm_peak)ref_hf_pcm_peak=fabs(v);
            }
            if(ref_hf_spool &&
               fwrite(ref_hf_pcm,sizeof(float),2048u,ref_hf_spool)!=2048u)
                ref_hf_io_ok=0;
        }
        float sine_pcm[2048u];
        SautiAacSbrTemporalPcmPreviewReport sine_pcm_report;
        if(!sauti_aac_sbr_temporal_pcm_preview_push(
               low_qmf,32u*32u,
               noise_report.emitted?sine_pcm_qmf:NULL,
               noise_report.emitted?32u*64u:0u,
               noise_report.emitted?&noise_frame:NULL,
               &apply_report,
               noise_report.emitted?&mix_report:NULL,
               &sine_pcm_state,sine_pcm,2048u,&sine_pcm_report) ||
           sine_pcm_report.emitted!=pcm_report.emitted ||
           (pcm_report.emitted &&
            sine_pcm_report.frame_index!=pcm_report.frame_index)){
            printf("  ramka %zu: SBR-54h niezalezna synteza 64-QMF odrzucona\n",i);
            break;
        }
        if(sine_pcm_report.emitted){
            sine_preview_frames++;
            for(unsigned j=0u;j<2048u;j++){
                const double v=sine_pcm[j],d=v-preview_pcm[j];
                sine_preview_power+=v*v;
                sine_delta_power+=d*d;
                if(fabs(v)>sine_preview_peak)sine_preview_peak=fabs(v);
            }
            if(sine_spool &&
               fwrite(sine_pcm,sizeof(float),2048u,sine_spool)!=2048u)
                sine_io_ok=0;
        }
        float phase2_pcm[2048u];
        SautiAacSbrTemporalPcmPreviewReport phase2_pcm_report;
        if(!sauti_aac_sbr_temporal_pcm_preview_push(
               low_qmf,32u*32u,
               noise_report.emitted?phase2_high:NULL,
               noise_report.emitted?32u*64u:0u,
               noise_report.emitted?&noise_frame:NULL,
               &apply_report,
               noise_report.emitted?&mix_report:NULL,
               &phase2_pcm_state,phase2_pcm,2048u,&phase2_pcm_report) ||
           phase2_pcm_report.emitted!=sine_pcm_report.emitted ||
           (sine_pcm_report.emitted &&
            phase2_pcm_report.frame_index!=sine_pcm_report.frame_index)){
            printf("  ramka %zu: SBR-70 osobna synteza QMF phase2 odrzucona\n",i);
            break;
        }
        if(phase2_pcm_report.emitted){
            phase2_pcm_frames++;
            if(phase2_spool &&
               fwrite(phase2_pcm,sizeof(float),2048u,
                      phase2_spool)!=2048u)
                phase2_io_ok=0;
        }
        float units_pcm[2048u];
        SautiAacSbrTemporalPcmPreviewReport units_pcm_report;
        if(!sauti_aac_sbr_temporal_pcm_preview_push(
               low_qmf,32u*32u,
               noise_report.emitted?units_high:NULL,
               noise_report.emitted?32u*64u:0u,
               noise_report.emitted?&noise_frame:NULL,
               &apply_report,
               noise_report.emitted?&mix_report:NULL,
               &units_pcm_state,units_pcm,2048u,&units_pcm_report) ||
           units_pcm_report.emitted!=pcm_report.emitted ||
           (pcm_report.emitted &&
            units_pcm_report.frame_index!=pcm_report.frame_index)){
            printf("  ramka %zu: SBR-54i PCM alternatywny odrzucony\n",i);
            break;
        }
        if(units_pcm_report.emitted){
            units_pcm_frames++;
            for(unsigned j=0u;j<2048u;j++){
                const double v=units_pcm[j];
                units_pcm_power+=v*v;
                if(fabs(v)>units_pcm_peak)units_pcm_peak=fabs(v);
            }
            if(units_spool &&
               fwrite(units_pcm,sizeof(float),2048u,units_spool)!=2048u)
                units_io_ok=0;
        }
        if(pcm_report.emitted){
            if(component_prefix){
                float components[SBR_COMPONENTS][SBR_SAMPLES];
                if(!sbr54c_render(emitted_low,adjusted_hf,preview,
                                 &noise_frame,preview_pcm,
                                 &component_state,components)){
                    puts("  SBR-54c: rozdzielenie pasm QMF odrzucone");
                    component_io_ok=0;
                    break;
                }
                for(unsigned c=0u;c<SBR_COMPONENTS;c++)
                    if(fwrite(components[c],sizeof(float),SBR_SAMPLES,
                              component_spool[c])!=SBR_SAMPLES)
                        component_io_ok=0;
                if(!component_io_ok)break;
            }
            if(spool && fwrite(preview_pcm,sizeof(float),2048u,spool)!=2048u)
                export_io_ok=0;
            preview_frames++;
            preview_samples+=pcm_report.samples;
            preview_nonzero+=pcm_report.nonzero;
            if(pcm_report.nonzero && preview_first_nonzero==SIZE_MAX)
                preview_first_nonzero=pcm_report.frame_index;
            if(pcm_report.frame_index==2u)
                printf("  SBR-54a: ramka 2 PCM PREVIEW=%u probek, RMS=%.9g, max=%.9g, niezerowe=%u (NIE zweryfikowano HE-AAC)\n",
                       pcm_report.samples,pcm_report.rms,
                       pcm_report.peak,pcm_report.nonzero);
        }
        if(power_report.completed_count &&
           power_report.completed[0].source==0u){
            power_handoffs++;
            printf("  ramka %zu: SBR-53c zakonczono poprzednia obwiednie HF, slotow=%u (bez PCM)\n",
                   i,power_report.completed[0].slots);
        }
        if(owners.previous_slots){
            printf("  ramka %zu: SBR QMF 0..%u z poprzedniej ramki (env=%u,noise=%u); %u..31 z biezacej\n",
                   i,(unsigned)owners.previous_slots-1u,
                   owners.slot[0].envelope,owners.slot[0].noise,
                   owners.previous_slots);
        }
        if(nonzero && first_nonzero==SIZE_MAX)first_nonzero=i;
        printf("  ramka %zu: rdzen RMS=%.9g, max=%.9g, niezerowe=%u/1024, SBR env_end=%u\n",
               i,sqrt(sum/1024.0),max_value,nonzero,
               payload.grid.env_border[payload.grid.env_count]);
        decoded++;
    }
    if(decoded==limit && temporal_apply.ready){
        SautiAacSbrComplex final_hf[32u*64u];
        SautiAacSbrTemporalApplyReport final_report;
        if(sauti_aac_sbr_temporal_gain_apply_flush(
               &temporal_apply,final_hf,32u*64u,&final_report)){
            apply_emitted++;
            SautiAacSbrComplex final_ref_hf[32u*64u];
            SautiAacSbrTemporalApplyReport final_ref_report;
            const int final_ref_ok=
                sauti_aac_sbr_temporal_gain_apply_flush(
                    &ref_hf_apply_state,final_ref_hf,32u*64u,
                    &final_ref_report) &&
                final_ref_report.emitted &&
                final_ref_report.frame_index==final_report.frame_index;
            if(final_ref_ok)ref_hf_qmf_frames++;
            else{
                puts("  SBR-54m: finalny G-only gain flush odrzucony");
                ref_hf_io_ok=0;
            }
            SautiAacSbrTemporalNoiseFrame last_noise;
            SautiAacSbrTemporalNoiseReport last_report;
            if(sauti_aac_sbr_temporal_noise_flush(
                   &final_report,&temporal_noise,
                   &last_noise,&last_report)){
                SautiAacSbrTemporalSmoothFrame last_smooth;
                SautiAacSbrTemporalSmoothReport last_smooth_report;
                if(sauti_aac_sbr_temporal_noise_smooth(
                       &last_noise,&last_report,&temporal_smooth,
                       &last_smooth,&last_smooth_report)){
                    smooth_frames++;
                    smooth_active+=last_smooth_report.active_bins;
                    smooth_bypasses+=last_smooth_report.bypassed_slots;
                }else
                    puts("  SBR-53h: finalne wygladzanie ROM noise odrzucone (bez PCM)");
                SautiAacSbrComplex last_preview[32u*64u];
                SautiAacSbrTemporalMixReport last_mix;
                if(sauti_aac_sbr_temporal_noise_mix_preview(
                       final_hf,32u*64u,&last_noise,
                       &final_report,&last_report,
                       last_preview,32u*64u,&last_mix)){
                    SautiAacSbrTemporalEnergyAudit final_energy;
                    if(sauti_aac_sbr_temporal_energy_audit(
                           &temporal_noise.held_targets,
                           final_hf,32u*64u,&last_noise,
                           last_preview,32u*64u,&final_report,
                           &last_report,&final_energy)){
                        energy_audit_frames++;
                        sbr54e_accumulate(&energy_budget,&final_energy);
                        if(envelope_audit_ok &&
                           !sauti_aac_sbr_temporal_envelope_audit_push(
                               &temporal_noise.held_targets,
                               final_hf,32u*64u,
                               &last_noise,&final_report,&last_report,
                               &envelope_audit)){
                            puts("  SBR-54f: finalny audit obwiedni odrzucony");
                            envelope_audit_ok=0;
                        }
                    }else
                        puts("  SBR-54e: finalny bilans energii odrzucony (bez zmiany PCM)");
                    SautiAacSbrComplex final_sine_hf[32u*64u];
                    SautiAacSbrComplex final_scaled_sine[32u*64u];
                    const SautiAacSbrComplex *final_sine_pcm_qmf=NULL;
                    SautiAacSbrTemporalHarmonicPreviewReport
                        final_sine_qmf_report;
                    const unsigned sbr73_final_phase_before=sine_qmf_state.phase;
                    const int final_sine_ok=
                        sauti_aac_sbr_temporal_harmonic_preview(
                            &temporal_noise.held_targets,
                            sine_held_onset,
                            &last_noise,&last_report,
                            last_preview,32u*64u,
                            final_sine_hf,32u*64u,
                            &sine_qmf_state,&final_sine_qmf_report);
                    SautiAacSbrComplex final_phase2_hf[32u*64u];
                    SautiAacSbrTemporalHarmonicPreviewReport
                        final_phase2_qmf_report;
                    const int final_phase2_ok=
                        sauti_aac_sbr_temporal_harmonic_preview_phase2(
                            &temporal_noise.held_targets,
                            sine_held_onset,&last_noise,&last_report,
                            last_preview,32u*64u,
                            final_phase2_hf,32u*64u,
                            &phase2_qmf_state,&final_phase2_qmf_report);
                    if(final_phase2_ok && final_sine_ok &&
                       final_phase2_qmf_report.frame_index==
                           final_sine_qmf_report.frame_index &&
                       final_phase2_qmf_report.active_tones==
                           final_sine_qmf_report.active_tones &&
                       phase2_qmf_state.frames==sine_qmf_state.frames &&
                       phase2_qmf_state.phase==sine_qmf_state.phase &&
                       memcmp(phase2_qmf_state.previous,
                              sine_qmf_state.previous,
                              sizeof(phase2_qmf_state.previous))==0)
                        phase2_qmf_frames++;
                    else{
                        puts("  SBR-70: finalny QMF phase2 odrzucony");
                        phase2_io_ok=0;
                    }
                    if(final_sine_ok && final_phase2_ok && phase2_io_ok &&
                       (!sbr73_trace_phase(&temporal_noise.held_targets,
                                           sine_held_onset,
                                           final_sine_qmf_report.frame_index,
                                           sbr73_final_phase_before) ||
                        !sbr73_trace_qmf(final_sine_hf,final_phase2_hf,
                                         final_sine_qmf_report.frame_index) ||
                        !sbr73_trace_parts(final_sine_qmf_report.frame_index,
                                           final_hf,&last_noise,
                                           &temporal_noise.held_targets))){
                        puts("  SBR-73: finalny trace odrzucony");
                        phase2_io_ok=0;
                    }
                    SautiAacSbrComplex final_shifted_hf[32u*64u];
                    SautiAacSbrComplex final_summed_hf[32u*64u];
                    if(final_sine_ok &&
                       sbr73_scale_emitted_tone(
                           final_sine_qmf_report.frame_index,
                           &temporal_noise.held_targets,
                           final_hf,last_preview,final_sine_hf,
                           final_scaled_sine) &&
                       sbr73_trace_scaled(
                           final_sine_qmf_report.frame_index,
                           final_scaled_sine))
                        final_sine_pcm_qmf=final_scaled_sine;
                    else if(final_sine_ok){
                        puts("  SBR-73: finalna skala tonu odrzucona");
                        sine_io_ok=0;
                        final_sine_pcm_qmf=final_sine_hf;
                    }
                    if(final_sine_pcm_qmf==final_scaled_sine){
                        if(sbr73_shift_emitted_gain(
                               final_sine_qmf_report.frame_index,
                               &temporal_noise.held_targets,
                               final_hf,final_shifted_hf) &&
                           sbr73_trace_gainhf(
                               final_sine_qmf_report.frame_index,
                               final_shifted_hf) &&
                           sbr73_assemble_shifted_tone(
                               final_shifted_hf,final_hf,last_preview,
                               final_sine_hf,final_scaled_sine,
                               final_summed_hf) &&
                           sbr73_trace_sum(
                               final_sine_qmf_report.frame_index,
                               final_summed_hf))
                            final_sine_pcm_qmf=final_summed_hf;
                        else{
                            puts("  SBR-73: finalne przesuniete HF odrzucone");
                            sine_io_ok=0;
                        }
                    }
                    SautiAacSbrComplex final_units_high[32u*64u];
                    const int final_units_ok=final_sine_ok &&
                        sauti_aac_sbr_temporal_hf_units_float(
                            final_sine_hf,32u*64u,&last_noise,
                            final_units_high,32u*64u);
                    if(final_units_ok)
                        units_qmf_frames++;
                    else{
                        puts("  SBR-54i: finalna konwersja HF odrzucona");
                        units_io_ok=0;
                    }
                    if(final_sine_ok){
                        sine_qmf_frames++;
                        sine_marked+=final_sine_qmf_report.marked_bins;
                        sine_activated+=final_sine_qmf_report.active_tones;
                        sine_gated+=final_sine_qmf_report.gated_noise_bins;
                    }else{
                        puts("  SBR-54h: finalne harmoniczne QMF odrzucone");
                        sine_io_ok=0;
                    }
                    mix_frames++;
                    mix_active+=last_mix.active_bins;
                    mix_noise+=last_mix.noise_bins;
                    float final_pcm[2048u];
                    SautiAacSbrTemporalPcmPreviewReport final_pcm_report;
                    if(sauti_aac_sbr_temporal_pcm_preview_flush(
                           last_preview,32u*64u,&last_noise,
                           &final_report,&last_mix,&temporal_pcm,
                           final_pcm,2048u,&final_pcm_report)){
                        if(final_ref_ok){
                            SautiAacSbrComplex final_conj[32u*64u];
                            if(sauti_aac_sbr_hf_conjugate_probe(
                                   final_ref_hf,32u*64u,&last_noise,
                                   final_conj,32u*64u)){
                                conj_qmf_frames++;
                                float final_conj_pcm[2048u];
                                SautiAacSbrTemporalPcmPreviewReport
                                    final_conj_report;
                                if(sauti_aac_sbr_temporal_pcm_preview_flush(
                                       final_conj,32u*64u,
                                       &last_noise,&final_ref_report,
                                       &last_mix,&conj_pcm_state,
                                       final_conj_pcm,2048u,
                                       &final_conj_report) &&
                                   final_conj_report.emitted &&
                                   final_conj_report.frame_index==
                                       final_pcm_report.frame_index){
                                    conj_pcm_frames++;
                                    for(unsigned j=0u;j<2048u;j++){
                                        const double v=final_conj_pcm[j];
                                        conj_pcm_energy+=v*v;
                                        if(fabs(v)>conj_pcm_peak)
                                            conj_pcm_peak=fabs(v);
                                    }
                                    if(conj_spool &&
                                       fwrite(final_conj_pcm,sizeof(float),
                                              2048u,conj_spool)!=2048u)
                                        conj_io_ok=0;
                                }else{
                                    puts("  SBR-54q: finalny PCM conjugated HF odrzucony");
                                    conj_io_ok=0;
                                }
                            }else{
                                puts("  SBR-54q: finalny QMF conjugation odrzucony");
                                conj_io_ok=0;
                            }
                            float final_ref_pcm[2048u];
                            SautiAacSbrTemporalPcmPreviewReport
                                final_ref_pcm_report;
                            if(sauti_aac_sbr_temporal_pcm_preview_flush(
                                   final_ref_hf,32u*64u,
                                   &last_noise,&final_ref_report,&last_mix,
                                   &ref_hf_pcm_state,final_ref_pcm,2048u,
                                   &final_ref_pcm_report) &&
                               final_ref_pcm_report.emitted &&
                               final_ref_pcm_report.frame_index==
                                   final_pcm_report.frame_index){
                                ref_hf_frames++;
                                for(unsigned j=0u;j<2048u;j++){
                                    const double v=final_ref_pcm[j];
                                    ref_hf_pcm_power+=v*v;
                                    if(fabs(v)>ref_hf_pcm_peak)
                                        ref_hf_pcm_peak=fabs(v);
                                }
                                if(ref_hf_spool &&
                                   fwrite(final_ref_pcm,sizeof(float),2048u,
                                          ref_hf_spool)!=2048u)
                                    ref_hf_io_ok=0;
                            }else{
                                puts("  SBR-54m: finalny HF-only PCM odrzucony");
                                ref_hf_io_ok=0;
                            }
                        }
                        if(final_sine_ok){
                            float final_sine_pcm[2048u];
                            SautiAacSbrTemporalPcmPreviewReport
                                final_sine_pcm_report;
                            if(sauti_aac_sbr_temporal_pcm_preview_flush(
                                   final_sine_pcm_qmf?final_sine_pcm_qmf:
                                       final_sine_hf,
                                   32u*64u,&last_noise,
                                   &final_report,&last_mix,
                                   &sine_pcm_state,final_sine_pcm,2048u,
                                   &final_sine_pcm_report) &&
                               final_sine_pcm_report.emitted &&
                               final_sine_pcm_report.frame_index==
                                   final_pcm_report.frame_index){
                                sine_preview_frames++;
                                for(unsigned j=0u;j<2048u;j++){
                                    const double v=final_sine_pcm[j];
                                    const double d=v-final_pcm[j];
                                    sine_preview_power+=v*v;
                                    sine_delta_power+=d*d;
                                    if(fabs(v)>sine_preview_peak)
                                        sine_preview_peak=fabs(v);
                                }
                                if(sine_spool &&
                                   fwrite(final_sine_pcm,sizeof(float),2048u,
                                          sine_spool)!=2048u)
                                    sine_io_ok=0;
                            }else{
                                puts("  SBR-54h: finalny tonal PCM 64-QMF odrzucony");
                                sine_io_ok=0;
                            }
                        }
                        if(final_phase2_ok && phase2_io_ok){
                            float final_phase2_pcm[2048u];
                            SautiAacSbrTemporalPcmPreviewReport
                                final_phase2_pcm_report;
                            if(sauti_aac_sbr_temporal_pcm_preview_flush(
                                   final_phase2_hf,32u*64u,&last_noise,
                                   &final_report,&last_mix,
                                   &phase2_pcm_state,final_phase2_pcm,2048u,
                                   &final_phase2_pcm_report) &&
                               final_phase2_pcm_report.emitted &&
                               final_phase2_pcm_report.frame_index==
                                   final_pcm_report.frame_index){
                                phase2_pcm_frames++;
                                if(phase2_spool &&
                                   fwrite(final_phase2_pcm,sizeof(float),
                                          2048u,phase2_spool)!=2048u)
                                    phase2_io_ok=0;
                            }else{
                                puts("  SBR-70: finalny PCM phase2 odrzucony");
                                phase2_io_ok=0;
                            }
                        }
                        if(final_units_ok){
                            float final_units_pcm[2048u];
                            SautiAacSbrTemporalPcmPreviewReport
                                final_units_report;
                            if(sauti_aac_sbr_temporal_pcm_preview_flush(
                                   final_units_high,32u*64u,
                                   &last_noise,&final_report,&last_mix,
                                   &units_pcm_state,final_units_pcm,2048u,
                                   &final_units_report) &&
                               final_units_report.emitted &&
                               final_units_report.frame_index==
                                   final_pcm_report.frame_index){
                                units_pcm_frames++;
                                for(unsigned j=0u;j<2048u;j++){
                                    const double v=final_units_pcm[j];
                                    units_pcm_power+=v*v;
                                    if(fabs(v)>units_pcm_peak)
                                        units_pcm_peak=fabs(v);
                                }
                                if(units_spool &&
                                   fwrite(final_units_pcm,sizeof(float),2048u,
                                          units_spool)!=2048u)
                                    units_io_ok=0;
                            }else{
                                puts("  SBR-54i: finalny PCM domeny float odrzucony");
                                units_io_ok=0;
                            }
                        }
                        if(component_prefix){
                            float components[SBR_COMPONENTS][SBR_SAMPLES];
                            if(!sbr54c_render(temporal_pcm.held_low,
                                             final_hf,last_preview,
                                             &last_noise,final_pcm,
                                             &component_state,components)){
                                puts("  SBR-54c: finalne skladniki QMF odrzucone");
                                component_io_ok=0;
                            }else for(unsigned c=0u;c<SBR_COMPONENTS;c++)
                                if(fwrite(components[c],sizeof(float),SBR_SAMPLES,
                                          component_spool[c])!=SBR_SAMPLES)
                                    component_io_ok=0;
                        }
                        if(spool && fwrite(final_pcm,sizeof(float),2048u,spool)!=2048u)
                            export_io_ok=0;
                        preview_frames++;
                        preview_samples+=final_pcm_report.samples;
                        preview_nonzero+=final_pcm_report.nonzero;
                        if(final_pcm_report.nonzero &&
                           preview_first_nonzero==SIZE_MAX)
                            preview_first_nonzero=final_pcm_report.frame_index;
                        if(final_pcm_report.frame_index==2u)
                            printf("  SBR-54a: ramka 2 PCM PREVIEW=%u probek, RMS=%.9g, max=%.9g, niezerowe=%u (NIE zweryfikowano HE-AAC)\n",
                                   final_pcm_report.samples,
                                   final_pcm_report.rms,
                                   final_pcm_report.peak,
                                   final_pcm_report.nonzero);
                    }else
                        puts("  SBR-54a: finalny PCM preview 64-QMF odrzucony");
                }else
                    puts("  SBR-53g: finalny podglad HF + ROM noise odrzucony (bez PCM)");
                noise_frames++;
                noise_vectors+=last_report.vectors;
                noise_positive+=last_report.nonzero_amplitudes;
            }else{
                puts("  SBR-53f: nie udalo sie zakonczyc sekwencji ROM noise (bez PCM)");
            }
        }else if(temporal_apply.pending_slots){
            printf("  SBR-53e: koncowa obwiednia oczekuje na nastepna ramke, nierozwiazane sloty=%u (bez PCM)\n",
                   temporal_apply.pending_slots);
        }else{
            puts("  SBR-53e: odrzucono finalizacje HF QMF (bez PCM)");
        }
    }
    printf("  Historia SBR: zapisano=%llu, przekazania=%zu, najdluzszy ogon=%u slotow (bez PCM HE-AAC)\n",
           (unsigned long long)temporal_history.frames,handoff_count,max_tail);
    printf("  SBR-53a: cele HF energia/szum/harmoniczne sprawdzone dla %zu ramek (bez syntezy HE-AAC PCM)\n",hf_target_frames);
    printf("  SBR-53b: ciagla analiza QMF 32 + HF transpose dla %zu ramek (bez finalnego PCM HE-AAC)\n",qmf_prepared_frames);
    printf("  SBR-54s: realne HF QMF W[i-6] audit: ramki=%zu, carry_src_slot=%zu, missing_preroll=%zu, prior_FIXVAR_owner_slot=%zu, aktywne_slot-biny=%zu, zmienione=%zu, complex_corr=%.9g, RMS_roznicy_QMF=%.9g (TYLKO POROWNANIE, NIE PCM)\n",
           source6_frames,source6_carried,source6_missing,
           source6_prior_owners,source6_active,source6_changed,
           source6_old_energy>0.0 && source6_new_energy>0.0?
             source6_cross/sqrt(source6_old_energy*source6_new_energy):0.0,
           source6_active?sqrt(source6_delta_energy/
                               (double)source6_active):0.0);
    printf("  SBR-54t: per-band HF audit ramki=%llu, aktywne=%llu, zmienione=%llu, peak_delta=%.9g na ramce=%llu slot=%u band=%u (TYLKO SAUTI vs SAUTI, NIE FFmpeg)\n",
           (unsigned long long)source6_bands.frames,
           (unsigned long long)source6_bands.active_total,
           (unsigned long long)source6_bands.changed_total,
           source6_bands.peak_difference,
           (unsigned long long)source6_bands.peak_frame,
           (unsigned)source6_bands.peak_slot,
           (unsigned)source6_bands.peak_band);
    /* Highest cumulative complex-QMF discrepancy by destination band.
     * Select deterministically; each band can appear at most once. */
    uint8_t selected_source6_bands[64]={0};
    for(unsigned rank=0u;rank<4u;rank++){
        int best=-1;
        double best_delta=0.0;
        for(unsigned k=0u;k<64u;k++)
            if(!selected_source6_bands[k] &&
               source6_bands.difference[k]>best_delta){
                best_delta=source6_bands.difference[k];
                best=(int)k;
            }
        if(best<0)break;
        const unsigned k=(unsigned)best;
        selected_source6_bands[k]=1u;
        const double e_old=source6_bands.old_energy[k];
        const double e_new=source6_bands.new_energy[k];
        const double corr=e_old>0.0 && e_new>0.0?
            source6_bands.cross[k]/sqrt(e_old*e_new):0.0;
        const double rms=source6_bands.active[k]?
            sqrt(best_delta/(double)source6_bands.active[k]):0.0;
        const double relative=e_old>0.0?
            sqrt(best_delta/e_old):0.0;
        printf("  SBR-54t: TOP%u band=%u aktywne=%llu changed=%llu corr=%.9g diff_RMS=%.9g diff_rel=%.9g (wzgledem bezposredniego HF QMF)\n",
               rank+1u,k,
               (unsigned long long)source6_bands.active[k],
               (unsigned long long)source6_bands.changed[k],
               corr,rms,relative);
    }
    printf("  SBR-54v: LOW QMF W[i-6] audit ramki=%llu (Sauti-only; NOT FFmpeg/PCM)\n",
           (unsigned long long)source6_low_audit.frames);
    for(unsigned b=0u;b<4u;b++){
        const double e=source6_low_audit.energy[b];
        const double pr=source6_low_audit.adjacent_norm_prev[b];
        const double pn=source6_low_audit.adjacent_norm_next[b];
        const double norm=pr>0.0 && pn>0.0?sqrt(pr*pn):0.0;
        printf("  SBR-54v: LOW=%u count=%llu energy=%.9g RMS=%.9g ReE=%.9g ImE=%.9g adjacent_phase_re=%.9g adjacent_phase_im=%.9g peak=%.9g frame=%llu slot=%u\n",
               b+2u,(unsigned long long)source6_low_audit.count[b],e,
               source6_low_audit.count[b]?sqrt(e/(double)source6_low_audit.count[b]):0.0,
               source6_low_audit.real_energy[b],source6_low_audit.imag_energy[b],
               norm?source6_low_audit.adjacent_real[b]/norm:0.0,
               norm?source6_low_audit.adjacent_imag[b]/norm:0.0,
               source6_low_audit.max_magnitude[b],
               (unsigned long long)source6_low_audit.peak_frame[b],
               (unsigned)source6_low_audit.peak_slot[b]);
    }
    printf("  SBR-54u: patch HF original owner profile, ramki=%llu (Sauti, bez FFmpeg/PCM)\n",
           (unsigned long long)source6_patch_routes.frames);
    for(unsigned b=0u;b<4u;b++){
        const unsigned k=SAUTI_AAC_SBR_ROUTE_BAND_FIRST+b;
        const uint64_t frames=source6_patch_routes.frames;
        const uint64_t mapped=source6_patch_routes.mapped[b];
        printf("  SBR-54u: band=%u mapped=%llu/%llu outside=%llu unmapped=%llu previous_owner=%llu",
               k,(unsigned long long)mapped,
               (unsigned long long)(frames*32u),
               (unsigned long long)source6_patch_routes.outside[b],
               (unsigned long long)source6_patch_routes.unmapped[b],
               (unsigned long long)source6_patch_routes.previous_owner[b]);
        uint8_t printed[32]={0};
        for(unsigned rank=0u;rank<3u;rank++){
            int best=-1;
            uint64_t count=0u;
            for(unsigned src=0u;src<32u;src++)
                if(!printed[src] &&
                   source6_patch_routes.sources[b][src]>count){
                    best=(int)src;
                    count=source6_patch_routes.sources[b][src];
                }
            if(best<0)break;
            printed[(unsigned)best]=1u;
            printf(" src%u=%llu",(unsigned)best,(unsigned long long)count);
        }
        puts(" (tylko mapowanie patch, NIE widmo)");
    }
    printf("  SBR-54g: LPC(2)+chirp alternatywa: ramki=%zu, niezerowe_zestawy_alpha=%zu, dodatni_chirp_slot-band=%zu, roznice_z_transpose=%zu/%zu, RMS_roznicy_QMF=%.9g (wylacznie HF QMF, nie PCM)\n",
           lpc_estimated_frames,lpc_nonzero_coeffs,lpc_nonzero_chirp,
           lpc_changed_bins,lpc_active_bins,
           lpc_active_bins?sqrt(lpc_delta_power/(double)lpc_active_bins):0.0);
    printf("  SBR-54g invf_mode bitstream: OFF=%zu, LOW=%zu, MID=%zu, HIGH=%zu; pasma=%zu; ramki_z_aktywnym_INVF=%zu/%zu (bez wymuszania chirp)\n",
           invf_modes[0],invf_modes[1],invf_modes[2],invf_modes[3],
           invf_total_bands,invf_active_frames,lpc_estimated_frames);
    printf("  SBR-53c: ciagly pomiar HF=%zu ramek, pelne obwiednie=%zu, dokonczenia=%zu, pozostale sloty=%u (bez finalnego PCM HE-AAC)\n",
           power_frames,power_envelopes,power_handoffs,
           temporal_power.pending?temporal_power.pending_remaining:0u);
    printf("  SBR-53d: limiter HF=%zu ramek, gotowe obwiednie=%zu, pasma noise-only=%zu (bez finalnego PCM HE-AAC)\n",
           gain_frames,gain_envelopes,gain_noise_only);
    printf("  SBR-54l: realny limiter porownany: ramki=%zu, pelne_env=%zu, odziedziczone_FIXVAR=%zu, grupy=%zu, roznice_gain=%zu, szum_dodatni=%zu, harmoniczne=%zu, ograniczony_szum=%zu, old_HF_power=%.9g, new_HF_power=%.9g (diagnoza OBWIEDNI, nie PCM)\n",
           ref_gain_frames,ref_gain_envelopes,ref_gain_carry,
           ref_gain_groups,ref_gain_changed,
           ref_gain_noise_positive,ref_gain_sine,
           ref_gain_noise_capped,ref_gain_old_power,ref_gain_new_power);
    printf("  SBR-53e: QMF gain zastosowany z opoznieniem: wejscia=%zu, gotowe ramki HF=%zu, oczekujace sloty=%u (bez koncowego PCM HE-AAC)\n",
           apply_frames,apply_emitted,
           temporal_apply.pending_slots);
    printf("  SBR-53f: ROM noise gotowe ramki=%zu, wektory=%zu, dodatnia amplituda=%zu, index=%u (bez finalnego PCM HE-AAC)\n",
           noise_frames,noise_vectors,noise_positive,
           temporal_noise.rom.index);
    printf("  SBR-53g: HF + ROM noise PODGLAD, ramki=%zu, aktywne pasma-sloty=%zu, dodatnia amplituda=%zu (bez normatywnego PCM HE-AAC)\n",
           mix_frames,mix_active,mix_noise);
    printf("  SBR-53h: wygladzanie amplitud ROM, ramki=%zu, aktywne pasma-sloty=%zu, bypass transient=%zu (bez normatywnego PCM HE-AAC)\n",
           smooth_frames,smooth_active,smooth_bypasses);
    printf("  SBR-53h-audit: naglowki smoothing_mode=0: %zu, =1: %zu; obwiednie attack=%zu; sloty header-bypass=%zu, attack-only=%zu, wspolne=%zu, aktywny FIR=%zu; bypass zgodny=%s (bez PCM)\n",
           smoothing_headers[0],smoothing_headers[1],
           smoothing_attack_envs,smoothing_header_slots,
           smoothing_attack_only,smoothing_both,smoothing_fir_slots,
           smooth_frames==decoded &&
           smoothing_header_slots+smoothing_attack_only==smooth_bypasses &&
           smoothing_header_slots+smoothing_attack_only+smoothing_fir_slots==32u*decoded
               ? "TAK":"NIE");
    printf("  SBR-54e: audit bilansu: ramki=%zu, target=%.9g, HF=%.9g, NOISE_nom=%.9g, HF_plus_NOISE/target=%.9g, nadbudzet=%u/%u, harmonic_bins=%u, max_ratio=%.9g (diagnoza przed QMF, nie normatywne HE-AAC)\n",
           energy_audit_frames,
           energy_budget.decoded_target_power,
           energy_budget.adjusted_hf_power,
           energy_budget.nominal_noise_power,
           energy_budget.decoded_target_power>0.0?
               (energy_budget.adjusted_hf_power+
                energy_budget.nominal_noise_power)/
                energy_budget.decoded_target_power:0.0,
           energy_budget.double_alloc_bins,
           energy_budget.active_bins,energy_budget.harmonic_bins,
           energy_budget.max_budget_ratio);
    SautiAacSbrTemporalEnvelopeAuditReport complete_env={0};
    if(envelope_audit_ok &&
       sauti_aac_sbr_temporal_envelope_audit_report(
           &envelope_audit,&complete_env) &&
       complete_env.completed_frames==preview_frames &&
       complete_env.total_slot_bins==energy_budget.active_bins){
        const double total_budget=complete_env.target_total>0.0?
            (complete_env.hf_total+complete_env.noise_total)/
            complete_env.target_total:0.0;
        const int same_totals=
            fabs(complete_env.target_total-energy_budget.decoded_target_power)
                <=1e-7*fmax(1.0,energy_budget.decoded_target_power) &&
            fabs(complete_env.hf_total-energy_budget.adjusted_hf_power)
                <=1e-7*fmax(1.0,energy_budget.adjusted_hf_power) &&
            fabs(complete_env.noise_total-energy_budget.nominal_noise_power)
                <=1e-7*fmax(1.0,energy_budget.nominal_noise_power);
        printf("  SBR-54f: obwiednie=%u, pasma-obwiednie=%u, sloty-biny=%llu, budzet_sredni=%.9g, nadbudzet_sredni=%u/%u, ponad_2x=%u, ponad_10x=%u, max_sredni_ratio=%.9g (ramka=%llu, env=%u, pasmo=%u, sloty=%u), zgodnosc_sum_54e=%s (nie normatywny HE-AAC)\n",
               complete_env.envelopes,complete_env.active_envelope_bins,
               (unsigned long long)complete_env.total_slot_bins,
               total_budget,complete_env.above_target,
               complete_env.active_envelope_bins,complete_env.above_twice,
               complete_env.above_tenfold,complete_env.max_ratio,
               (unsigned long long)complete_env.worst_frame,
               complete_env.worst_envelope,complete_env.worst_bin,
               complete_env.worst_slots,same_totals?"TAK":"NIE");
    }else
        printf("  SBR-54f: NIEPELNY audit pelnych obwiedni (%llu/%zu wyemitowanych ramek)\n",
               (unsigned long long)envelope_audit.next_emitted_frame,
               preview_frames);
    free(envelope_groups);
    printf("  SBR-54a: PCM PREVIEW 64-QMF, ramki=%zu, probki=%zu, niezerowe=%zu, pierwsza niezerowa=",
           preview_frames,preview_samples,preview_nonzero);
    if(preview_first_nonzero==SIZE_MAX)puts("brak (niezweryfikowany dzwiek HE-AAC)");
    else printf("%zu (bez potwierdzenia zgodnosci z dekoderem HE-AAC)\n",preview_first_nonzero);
    printf("  SBR-54h: tonal A/B PCM: QMF ramki=%zu, PCM ramki=%zu, marked_slot_bands=%zu, active_tones=%zu, gated_noise=%zu, PCM_RMS=%.9g, PCM_peak=%.9g, RMS_roznicy_vs_54a=%.9g (EKSPERYMENT, nie zgodnosc HE-AAC)\n",
           sine_qmf_frames,sine_preview_frames,sine_marked,
           sine_activated,sine_gated,
           sine_preview_frames?
             sqrt(sine_preview_power/((double)sine_preview_frames*2048.0)):0.0,
           sine_preview_peak,
           sine_preview_frames?
             sqrt(sine_delta_power/((double)sine_preview_frames*2048.0)):0.0);
    printf("  SBR-54i: HF QMF domain A/B: ramki=%zu, PCM=%zu, RMS=%.9g, peak=%.9g, factor=32768 (DIAGNOSTYKA domeny, nie normatywny HE-AAC)\n",
           units_qmf_frames,units_pcm_frames,
           units_pcm_frames?
             sqrt(units_pcm_power/((double)units_pcm_frames*2048.0)):0.0,
           units_pcm_peak);
    printf("  SBR-54q: HF imaginary sign A/B: QMF ramki=%zu, PCM ramki=%zu, RMS=%.9g, peak=%.9g; tylko HF CONJ, LOW bez zmian, bez noise/sine (EKSPERYMENT)\n",
           conj_qmf_frames,conj_pcm_frames,
           conj_pcm_frames?
             sqrt(conj_pcm_energy/((double)conj_pcm_frames*2048.0)):0.0,
           conj_pcm_peak);
    printf("  SBR-54m: nowy G-only QMF+PCM: HF ramki=%zu, PCM ramki=%zu, RMS=%.9g, peak=%.9g; NOISE/SINE OMITTED (eksp. A/B, NIE normatywny HE-AAC)\n",
           ref_hf_qmf_frames,ref_hf_frames,
           ref_hf_frames?sqrt(ref_hf_pcm_power/
                             ((double)ref_hf_frames*2048.0)):0.0,
           ref_hf_pcm_peak);
    printf("  Rdzen: udane=%zu, pierwsza niezerowa=",decoded);
    if(first_nonzero==SIZE_MAX)puts("brak w przebadanym zakresie");
    else printf("%zu\n",first_nonzero);
    if(component_prefix){
        const int complete=component_io_ok && decoded==limit &&
            preview_frames==limit && component_state.frames==limit &&
            temporal_pcm.closed==1u;
        if(!complete){
            fprintf(stderr,"SBR-54c: eksport pominiety: niepelne skladniki %zu/%zu\n",
                    component_state.frames,limit);
        }else for(unsigned c=0u;c<SBR_COMPONENTS;c++){
            if(sbr54c_export(component_spool[c],component_path[c],
                             limit*SBR_SAMPLES*sizeof(float))){
                const double rms=sqrt(component_state.sum_sq[c]/
                                      ((double)limit*SBR_SAMPLES));
                printf("  SBR-54c: %s: %s, RMS=%.9g, peak=%.9g, ramki=%zu\n",
                       component_names[c],component_path[c],rms,
                       component_state.peak[c],component_state.frames);
            }else{
                fprintf(stderr,"SBR-54c: nie zapisano %s\n",
                        component_path[c]);
            }
        }
        for(unsigned c=0u;c<SBR_COMPONENTS;c++)
            if(component_spool[c])fclose(component_spool[c]);
    }
    if(conj_spool){
        const int complete=conj_io_ok && decoded==limit &&
            preview_frames==limit && conj_qmf_frames==limit &&
            conj_pcm_frames==limit &&
            conj_pcm_state.closed==1u &&
            conj_pcm_state.emitted_frames==limit;
        if(!complete)
            fprintf(stderr,"SBR-54q: POMINIETY niepelny eksport PCM (%zu/%zu)\n",
                    conj_pcm_frames,limit);
        else if(sbr54c_export(conj_spool,conj_path,
                              limit*2048u*sizeof(float)))
            printf("  SBR-54q: zapisano A/B conjugated HF PCM f32le: %s, ramki=%zu, probki=%zu (DO WALIDACJI)\n",
                   conj_path,conj_pcm_frames,limit*2048u);
        else
            fprintf(stderr,"SBR-54q: blad zapisu %s\n",conj_path);
        fclose(conj_spool);
    }
    if(ref_hf_spool){
        const int complete=ref_hf_io_ok && decoded==limit &&
            preview_frames==limit && ref_hf_qmf_frames==limit &&
            ref_hf_frames==limit && ref_hf_apply_state.closed==1u &&
            ref_hf_pcm_state.closed==1u &&
            ref_hf_pcm_state.emitted_frames==limit;
        if(!complete)
            fprintf(stderr,"SBR-54m: POMINIETY niepelny eksport HF-only PCM (%zu/%zu)\n",
                    ref_hf_frames,limit);
        else if(sbr54c_export(ref_hf_spool,ref_hf_path,
                              limit*2048u*sizeof(float)))
            printf("  SBR-54m: zapisano A/B nowy HF G-only PCM f32le: %s, ramki=%zu, probki=%zu (DO WALIDACJI)\n",
                   ref_hf_path,ref_hf_frames,limit*2048u);
        else
            fprintf(stderr,"SBR-54m: blad zapisu %s\n",ref_hf_path);
        fclose(ref_hf_spool);
    }
    if(units_spool){
        const int complete=units_io_ok && decoded==limit &&
            preview_frames==limit && units_qmf_frames==limit &&
            units_pcm_frames==limit && units_pcm_state.closed==1u &&
            units_pcm_state.emitted_frames==limit;
        if(!complete)
            fprintf(stderr,"SBR-54i: eksport POMINIETY: niepelny PCM (%zu/%zu)\n",
                    units_pcm_frames,limit);
        else if(sbr54c_export(units_spool,units_path,
                              limit*2048u*sizeof(float)))
            printf("  SBR-54i: zapisano A/B HF units PCM f32le: %s, ramki=%zu, probki=%zu (DO WALIDACJI)\n",
                   units_path,units_pcm_frames,limit*2048u);
        else
            fprintf(stderr,"SBR-54i: nie mozna zapisac %s\n",units_path);
        fclose(units_spool);
    }
    if(sine_spool){
        const int sine_complete=sine_io_ok && decoded==limit &&
            sine_qmf_frames==limit && sine_preview_frames==limit &&
            preview_frames==limit && sine_pcm_state.closed==1u &&
            sine_pcm_state.emitted_frames==limit &&
            sine_qmf_state.frames==limit;
        if(!sine_complete)
            fprintf(stderr,"SBR-54h: pomijam NIEPELNY eksport tonal PCM (%zu/%zu ramek)\n",
                    sine_preview_frames,limit);
        else if(sbr54c_export(sine_spool,sine_path,
                              limit*2048u*sizeof(float)))
            printf("  SBR-54h: zapisano A/B tonal PCM f32le: %s, ramki=%zu, probki=%zu (NIE potwierdzono HE-AAC)\n",
                   sine_path,sine_preview_frames,limit*2048u);
        else
            fprintf(stderr,"SBR-54h: blad eksportu tonal PCM: %s\n",
                    sine_path);
        fclose(sine_spool);
    }
    if(phase2_spool){
        const int complete=phase2_io_ok && decoded==limit &&
            preview_frames==limit && sine_preview_frames==limit &&
            phase2_qmf_frames==limit && phase2_pcm_frames==limit &&
            phase2_qmf_state.frames==limit &&
            phase2_pcm_state.closed==1u &&
            phase2_pcm_state.emitted_frames==limit;
        if(!complete)
            fprintf(stderr,
                    "SBR-70: POMINIETY niepelny phase2 PCM (%zu/%zu ramek)\n",
                    phase2_pcm_frames,limit);
        else if(sbr54c_export(phase2_spool,phase2_path,
                              limit*2048u*sizeof(float)))
            printf("  SBR-70: zapisano oddzielny phase+2 HF PCM: %s, ramki=%zu, probki=%zu (NIE normatywny HE-AAC)\n",
                   phase2_path,phase2_pcm_frames,limit*2048u);
        else
            fprintf(stderr,"SBR-70: blad eksportu phase2 PCM: %s\n",
                    phase2_path);
        fclose(phase2_spool);
    }
    if(spool){
        const int complete=export_io_ok && decoded==limit &&
            preview_frames==limit && preview_samples==limit*2048u &&
            temporal_pcm.closed==1u && temporal_pcm.emitted_frames==limit &&
            fflush(spool)==0 && fseek(spool,0L,SEEK_SET)==0;
        if(!complete){
            fprintf(stderr,"SBR-54b: eksport POMINIETY: niepelny PCM PREVIEW (%zu/%zu ramek)\n",
                    preview_frames,limit);
        }else{
            FILE *dst=fopen(export_path,"wb");
            int copied=dst!=NULL;
            uint8_t bytes[4096];
            size_t written=0u,n=0u;
            while(copied && (n=fread(bytes,1u,sizeof(bytes),spool))>0u){
                if(fwrite(bytes,1u,n,dst)!=n){
                    copied=0;break;
                }
                written+=n;
            }
            if(ferror(spool))copied=0;
            if(dst && fclose(dst)!=0)copied=0;
            if(copied && written==preview_samples*sizeof(float)){
                printf("  SBR-54b: zapisano PCM PREVIEW f32le: %s, probki=%zu, bytes=%zu (NIE potwierdzono jakosci HE-AAC)\n",
                       export_path,preview_samples,written);
            }else{
                /* Never remove a pre-existing path if fopen failed
                 * before this run could create/truncate its file. */
                if(dst)(void)remove(export_path);
                fprintf(stderr,"SBR-54b: blad eksportu PCM PREVIEW: %s\n",
                        export_path);
            }
        }
        fclose(spool);
    }
    fclose(f);
}
static int decode(const char *path,size_t max_decode,
                  const char *export_path,
                  const char *component_prefix){
    FILE *f=fopen(path,"rb");
    if(!f){perror(path);return 2;}
    SautiAacHeAdtsQmfState state={0};
    if(!sauti_aac_he_adts_qmf_init(&state)){
        fclose(f);return 2;
    }
    size_t decoded=0;
    double max_abs=0.0,sum_square=0.0;
    for(size_t frame_index=0;frame_index<max_decode;frame_index++){
        uint8_t frame[MAX_ADTS_FRAME];
        size_t len=0;
        const int r=frame_read(f,frame,&len);
        if(r==0)break;
        if(r<0){fclose(f);return 2;}
        SautiAacReason reason=SAUTI_AAC_REASON_OK;
        float pcm[2048];
        SautiAacSbrLimiterReport report;
        const int result=sauti_aac_he_adts_qmf_decode_ex(
            frame,len,&state,pcm,&report,&reason);
        if(result!=1){
            /* Try parsing the same frame on a COPY of the current
             * pre-failure ADTS state, and NEVER commit that copy. */
            SautiAacHeAdtsMonoState trial=state.adts;
            float core[1024];
            SautiAacSbrPayloadFrame payload;
            SautiAacReason stage_reason=SAUTI_AAC_REASON_OK;
            const int core_result=sauti_aac_he_adts_mono_decode_ex(
                frame,len,&trial,core,&payload,&stage_reason);
            printf("Pierwsza odrzucona ramka: %zu\n",frame_index);
            inspect_fil(frame,len);
            printf("Etap: %s\n",core_result==1?
                "HF/QMF po poprawnym parsowaniu AAC-LC i SBR":
                "AAC-LC / FIL / parser SBR");
            printf("Wynik dekodera: %s (%d)\n",
                sauti_aac_reason_name(reason),result);
            if(core_result!=1)
                printf("Przyczyna parsera: %s (%d)\n",
                       sauti_aac_reason_name(stage_reason),core_result);
            else {
                SautiAacSbrHfJointConfig hf;
                if(!sauti_aac_sbr_payload_make_hf(&payload,&hf)) {
                    puts("HF diagnostyka: niepoprawna geometria/konfiguracja payloadu");
                } else {
                    const unsigned kx=payload.tables.kx;
                    const unsigned k2=kx+payload.tables.m;
                    unsigned unmapped=0u;
                    for(unsigned k=kx;k<k2;k++)
                        if(payload.patches.source_for_target[k]<0)
                            unmapped++;
                    printf("HF diagnostyka: kx=%u, k2=%u, patches=%u, niepokryte pasma=%u\n",
                           kx,k2,(unsigned)payload.patches.count,unmapped);
                    diagnose_zero_hf(core,&payload,&state.qmf);
                    if(!sauti_aac_sbr_hf_map_valid(&payload.patches,&payload.tables))
                        puts("HF diagnostyka: mapa patchy odrzucona przed QMF");
                    else
                        {
                            const SautiAacSbrQmfFrameConfig cfg={
                                .hf=&hf,.attack=payload.attack,
                                .bs_smoothing_mode=payload.bs_smoothing_mode,
                                .lpc=NULL,.chirp=NULL
                            };
                            SautiAacSbrQmfFrameState qmf_trial=state.qmf;
                            SautiAacSbrLimiterReport stats;
                            float qmf_pcm[2048];
                            SautiAacSbrQmfStage qstage=SAUTI_SBR_QMF_STAGE_INPUT;
                            const int ok=sauti_aac_sbr_qmf_frame_process_ex(
                                &cfg,core,1024u,qmf_pcm,2048u,
                                &qmf_trial,&stats,&qstage);
                            printf("QMF diagnostyka: etap=%s, wynik=%d\n",
                                   qmf_stage_name(qstage),ok);
                        }
                }
            }
            printf("Udane ramki przed bledem: %zu\n",decoded);
            diagnose_core_sequence(path,max_decode,export_path,component_prefix);
            fclose(f);
            return 1; /* anticipated unsupported/invalid file */
        }
        for(unsigned k=0;k<2048u;k++){
            const double v=pcm[k];
            if(!isfinite(v)){fclose(f);return 2;}
            if(fabs(v)>max_abs)max_abs=fabs(v);
            sum_square+=v*v;
        }
        decoded++;
        if(frame_index<3u)
            printf("  Ramka %zu: 2048 probek; stan QMF OK\n",
                   frame_index);
    }
    fclose(f);
    printf("Udane ramki w probie: %zu\n",decoded);
    if(decoded)
        printf("PCM QMF: RMS %.9g, max abs %.9g\n",
               sqrt(sum_square/((double)decoded*2048.0)),
               max_abs);
    return 0;
}

int main(int argc,char **argv){
    if(argc<2 || argc>5){
        fprintf(stderr,
            "Uzycie: %s nagranie.aac [maks_ramek_do_dekodowania] [PCM_PREVIEW.f32le] [DIAGNOSTIC_PREFIX]\n",
            argv[0]);
        return 2;
    }
    size_t limit=DEFAULT_DECODE_LIMIT;
    if(argc>=3){
        errno=0;
        char *end=NULL;
        const unsigned long n=strtoul(argv[2],&end,10);
        if(errno || !argv[2][0] || !end || *end!='\0' ||
           n==0u || n>10000u){
            fprintf(stderr,"Limit powinien miescic sie w 1..10000\n");
            return 2;
        }
        limit=(size_t)n;
    }
    AdtsSummary summary;
    if(!scan(argv[1],&summary))return 2;
    printf("HE-AAC / ADTS diagnostyka Sauti (bez zewnetrznych kodekow)\n");
    printf("Plik: %s\n",argv[1]);
    printf("ADTS: %zu ramek, %zu bajtow, max ramka %u B\n",
           summary.frames,summary.bytes,summary.max_length);
    printf("Kanaly: %zu mono, %zu stereo, %zu pozostale\n",
           summary.mono,summary.stereo,summary.other_channels);
    printf("CRC: %zu ramek (weryfikowane dla jedno-RDB SCE/CPE)\n",summary.crc);
    printf("Naglowek pierwszej ramki: rdzen %u Hz, %u kanal(y)\n",
           summary.first_rate,summary.first_channels);
    if(summary.first_channels!=1u){
        puts("UWAGA: SBR-31 obsluguje tylko mono. Stereo wymaga CPE.");
    }
    if(limit>summary.frames)limit=summary.frames;
    printf("Dekodowanie pierwszych %zu ramek (ciagly stan):\n",limit);
    const int status=decode(argv[1],limit,argc>=4?argv[3]:NULL,
                            argc==5?argv[4]:NULL);
    if(status==0)
        puts("OK: sprobowane ramki przeszly. To NIE dowodzi zgodnosci brzmienia HE-AAC.");
    else if(status==1)
        puts("Wynik: ograniczenie obecnego dekodera; pozostale ramki NIE byly zastepowane cisza.");
    return status;
}
