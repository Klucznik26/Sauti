/* HE-AAC SBR-32: diagnostic runner for REAL third-party ADTS files.
 *
 * This tool uses ONLY Sauti's own AAC-LC+SBR+QMF decoder. It does not
 * use the host FFmpeg, FAAD, or any external codec as a fallback.
 * The independent encoder and an external decoder can be used by
 * the developer to GENERATE/VERIFY fixtures, not by this executable.
 *
 * CLI: ./build/sauti_he_aac_probe input.aac [max_decode_frames]
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
            if(temporal)
                printf("SBR temporal side-data: env_end=%u, QMF_end=%u; BRAK dekodowania PCM\n",
                       temporal_payload.grid.env_border[temporal_payload.grid.env_count],
                       temporal_payload.env_edges[temporal_payload.grid.env_count]);
        }
        return;
    }
}
static void diagnose_core_sequence(const char *path,size_t limit){
    FILE *f=fopen(path,"rb");
    if(!f){perror(path);return;}
    SautiAacHeAdtsMonoState core_state={0};
    if(!sauti_aac_he_adts_mono_init(&core_state)){
        fclose(f);return;
    }
    printf("Diagnostyka ciaglosci rdzenia AAC-LC + temporal SBR (%zu ramek, bez HE-AAC PCM):\n",limit);
    size_t decoded=0,first_nonzero=SIZE_MAX;
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
        if(nonzero && first_nonzero==SIZE_MAX)first_nonzero=i;
        printf("  ramka %zu: rdzen RMS=%.9g, max=%.9g, niezerowe=%u/1024, SBR env_end=%u\n",
               i,sqrt(sum/1024.0),max_value,nonzero,
               payload.grid.env_border[payload.grid.env_count]);
        decoded++;
    }
    printf("  Rdzen: udane=%zu, pierwsza niezerowa=",decoded);
    if(first_nonzero==SIZE_MAX)puts("brak w przebadanym zakresie");
    else printf("%zu\n",first_nonzero);
    fclose(f);
}
static int decode(const char *path,size_t max_decode){
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
            diagnose_core_sequence(path,max_decode);
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
    if(argc<2 || argc>3){
        fprintf(stderr,
            "Uzycie: %s nagranie.aac [maks_ramek_do_dekodowania]\n",
            argv[0]);
        return 2;
    }
    size_t limit=DEFAULT_DECODE_LIMIT;
    if(argc==3){
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
    const int status=decode(argv[1],limit);
    if(status==0)
        puts("OK: sprobowane ramki przeszly. To NIE dowodzi zgodnosci brzmienia HE-AAC.");
    else if(status==1)
        puts("Wynik: ograniczenie obecnego dekodera; pozostale ramki NIE byly zastepowane cisza.");
    return status;
}
