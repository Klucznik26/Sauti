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
            printf("Etap: %s\n",core_result==1?
                "HF/QMF po poprawnym parsowaniu AAC-LC i SBR":
                "AAC-LC / FIL / parser SBR");
            printf("Wynik dekodera: %s (%d)\n",
                sauti_aac_reason_name(reason),result);
            if(core_result!=1)
                printf("Przyczyna parsera: %s (%d)\n",
                       sauti_aac_reason_name(stage_reason),core_result);
            printf("Udane ramki przed bledem: %zu\n",decoded);
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
