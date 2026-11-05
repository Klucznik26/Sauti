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
            puts("FIL diagnostyka: EXT_SBR_DATA_CRC (14) nie jest obslugiwane");
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
    printf("Diagnostyka ciaglosci rdzenia AAC-LC + temporal SBR (%zu ramek, bez HE-AAC PCM):\n",limit);
    size_t decoded=0,first_nonzero=SIZE_MAX;
    SautiAacSbrTemporalHistory temporal_history={0};
    size_t handoff_count=0u;
    unsigned max_tail=0u;
    size_t hf_target_frames=0u;
    size_t qmf_prepared_frames=0u;
    size_t power_frames=0u,power_envelopes=0u,power_handoffs=0u;
    size_t gain_frames=0u,gain_envelopes=0u,gain_noise_only=0u;
    size_t apply_frames=0u,apply_emitted=0u;
    size_t noise_frames=0u,noise_vectors=0u,noise_positive=0u;
    size_t mix_frames=0u,mix_active=0u,mix_noise=0u;
    size_t smooth_frames=0u,smooth_active=0u,smooth_bypasses=0u;
    size_t preview_frames=0u,preview_samples=0u,preview_nonzero=0u;
    size_t preview_first_nonzero=SIZE_MAX;
    SautiAacSbrTemporalPcmPreviewState temporal_pcm={0};
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
        double sum=0.0,max_value=0.0;
        unsigned nonzero=0u;
        for(unsigned k=0;k<1024u;k++){
            const double v=core[k];
            sum+=v*v;
            if(fabs(v)>max_value)max_value=fabs(v);
            if(v!=0.0)nonzero++;
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
        apply_frames++;
        if(apply_report.emitted)apply_emitted++;
        /* SBR-53f: align the canonical noise sequence with the
         * frame actually emitted by SBR-53e, never with a future
         * owner's current SBR side data. Still NO PCM synthesis. */
        SautiAacSbrTemporalNoiseFrame noise_frame;
        SautiAacSbrTemporalNoiseReport noise_report;
        if(!sauti_aac_sbr_temporal_noise_prepare(
               &temporal_history,&apply_report,&temporal_noise,
               &noise_frame,&noise_report)){
            printf("  ramka %zu: SBR-53f kolejnosc ROM noise odrzucona (bez PCM HE-AAC)\n",i);
            break;
        }
        SautiAacSbrComplex preview[32u*64u];
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
            mix_frames++;
            mix_active+=mix_report.active_bins;
            mix_noise+=mix_report.noise_bins;
            noise_frames++;
            noise_vectors+=noise_report.vectors;
            noise_positive+=noise_report.nonzero_amplitudes;
        }
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
                    mix_frames++;
                    mix_active+=last_mix.active_bins;
                    mix_noise+=last_mix.noise_bins;
                    float final_pcm[2048u];
                    SautiAacSbrTemporalPcmPreviewReport final_pcm_report;
                    if(sauti_aac_sbr_temporal_pcm_preview_flush(
                           last_preview,32u*64u,&last_noise,
                           &final_report,&last_mix,&temporal_pcm,
                           final_pcm,2048u,&final_pcm_report)){
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
    printf("  SBR-53c: ciagly pomiar HF=%zu ramek, pelne obwiednie=%zu, dokonczenia=%zu, pozostale sloty=%u (bez finalnego PCM HE-AAC)\n",
           power_frames,power_envelopes,power_handoffs,
           temporal_power.pending?temporal_power.pending_remaining:0u);
    printf("  SBR-53d: limiter HF=%zu ramek, gotowe obwiednie=%zu, pasma noise-only=%zu (bez finalnego PCM HE-AAC)\n",
           gain_frames,gain_envelopes,gain_noise_only);
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
    printf("  SBR-54a: PCM PREVIEW 64-QMF, ramki=%zu, probki=%zu, niezerowe=%zu, pierwsza niezerowa=",
           preview_frames,preview_samples,preview_nonzero);
    if(preview_first_nonzero==SIZE_MAX)puts("brak (niezweryfikowany dzwiek HE-AAC)");
    else printf("%zu (bez potwierdzenia zgodnosci z dekoderem HE-AAC)\n",preview_first_nonzero);
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
    printf("CRC: %zu ramek (na razie nieobslugiwane)\n",summary.crc);
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
