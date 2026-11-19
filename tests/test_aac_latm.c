/* AAC w LOAS/LATM: demuxer (sauti_aac_latm) i adapter aac-native-latm.  Strumienie skladane w pamieci bit po bicie; jednostki dostepu to prawdziwe ramki AAC-LC mono (jedno SCE o max_sfb = 0 =
 * cisza), wiec adapter dekoduje je naprawde.  Zgodnosc z FFmpeg na prawdziwych strumieniach: scripts/test_he_adapter_vs_ffmpeg.sh PLIK.loas. */
#include "sauti_decoder_aac_native.h"
#include "sauti_aac_latm.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t b[16384]; size_t bit; } Bw;
static void put(Bw *w,unsigned n,uint32_t v){
    for(unsigned i=0;i<n;i++){
        const unsigned bit=(v>>(n-1u-i))&1u;
        if(bit)w->b[w->bit>>3]|=(uint8_t)(0x80u>>(w->bit&7u));
        w->bit++;
    }
}
static void align(Bw *w){while(w->bit&7u)put(w,1,0);}
static void make_au(uint8_t out[4]){
    uint32_t bits=0;unsigned n=0;
    #define PUT(wd,v) do{bits=(bits<<(wd))|((uint32_t)(v)&((1u<<(wd))-1u));n+=(wd);}while(0)
    PUT(3,0);PUT(4,0);PUT(8,100);PUT(1,0);PUT(2,0);PUT(1,0);PUT(6,0);PUT(1,0);PUT(1,0);PUT(1,0);PUT(1,0);PUT(3,7);
    #undef PUT
    assert(n==32u);
    out[0]=(uint8_t)(bits>>24);out[1]=(uint8_t)(bits>>16);out[2]=(uint8_t)(bits>>8);out[3]=(uint8_t)bits;
}
typedef struct {
    unsigned version;            /* audioMuxVersion 0 / 1 */
    unsigned channel_config;     /* ASC */
    unsigned aot;                /* 2 = LC, 5 = explicit SBR, 17 = unsupported */
    unsigned implicit_sbr;       /* AOT 2 + backward compatible 0x2B7 extension (SBR flag 1, 24 kHz -> 48 kHz) */
    unsigned sub_frames;         /* numSubFrames */
    unsigned frame_length_type;
    unsigned other_data;         /* otherDataLenBits = 13, with the escape syntax */
    unsigned crc;
    unsigned config_every;       /* repeat StreamMuxConfig every n elements (0 = only the first) */
    unsigned drop_first;         /* number of leading elements that carry useSameStreamMux=1 without any config */
    unsigned change_at;          /* element index where the config changes (channel_config + 1), 0 = never */
    unsigned elements;
    unsigned frame960;           /* GASpecificConfig frameLengthFlag = 1 (960-sample frames): unsupported */
} Spec;
static void asc(Bw *w,const Spec *s,unsigned channel_config){
    if(s->aot==5u){put(w,5,5);put(w,4,6);put(w,4,channel_config);put(w,4,3);put(w,5,2);put(w,3,0);}       /* AOT 5, core 24 kHz, ext 48 kHz, AOT 2, GAS */
    else{put(w,5,s->aot);put(w,4,s->implicit_sbr?6u:3u);put(w,4,channel_config);put(w,1,s->frame960);put(w,2,0);
         if(s->implicit_sbr){put(w,11,0x2b7);put(w,5,5);put(w,1,1);put(w,4,3);}}
}
static void stream_mux_config(Bw *w,const Spec *s,unsigned channel_config){
    put(w,1,s->version);
    if(s->version==1){put(w,1,0);put(w,2,0);put(w,8,0);}                          /* audioMuxVersionA, taraBufferFullness = LatmGetValue() (1 byte) */
    put(w,1,1);put(w,6,s->sub_frames);put(w,4,0);put(w,3,0);                      /* allStreamsSameTimeFraming, numSubFrames, numProgram, numLayer */
    if(s->version==0)asc(w,s,channel_config);
    else{
        Bw t;memset(&t,0,sizeof t);asc(&t,s,channel_config);
        const unsigned bits=(unsigned)t.bit;
        put(w,2,0);put(w,8,bits+4u);                                               /* ascLen = ASC bits + 4 fill bits */
        for(unsigned i=0;i<bits;i++)put(w,1,(t.b[i>>3]>>(7u-(i&7u)))&1u);
        put(w,4,0);
    }
    put(w,3,s->frame_length_type);put(w,8,0xff);                                   /* frameLengthType, latmBufferFullness */
    put(w,1,s->other_data);
    if(s->other_data){if(s->version==1){put(w,2,0);put(w,8,13);}                    /* otherDataLenBits = LatmGetValue() */
                      else{put(w,1,0);put(w,8,13);}}                                /* otherDataLenEsc = 0, otherDataLenTmp = 13 */
    put(w,1,s->crc);if(s->crc)put(w,8,0x5a);
}
static size_t build(uint8_t *out,const Spec *s){
    uint8_t au[4];make_au(au);
    size_t n=0;
    for(unsigned e=0;e<s->elements;e++){
        Bw w;memset(&w,0,sizeof w);
        const unsigned have_config=e>=s->drop_first&&(e==s->drop_first||(s->config_every&&((e-s->drop_first)%s->config_every)==0)||(s->change_at&&e==s->change_at));
        put(&w,1,have_config?0u:1u);
        if(have_config)stream_mux_config(&w,s,(s->change_at&&e>=s->change_at)?s->channel_config+1u:s->channel_config);
        for(unsigned sf=0;sf<=s->sub_frames;sf++){put(&w,8,4);for(unsigned i=0;i<4;i++)put(&w,8,au[i]);}   /* PayloadLengthInfo (4 bytes) + PayloadMux */
        if(s->other_data)for(unsigned i=0;i<13;i++)put(&w,1,1);
        align(&w);
        const size_t len=w.bit/8u;
        out[n++]=0x56;out[n++]=(uint8_t)(0xe0u|((len>>8)&0x1fu));out[n++]=(uint8_t)len;
        memcpy(out+n,w.b,len);n+=len;
    }
    return n;
}
static void check_au(const uint8_t *adts,unsigned index,unsigned rate_index,unsigned chan){
    uint8_t au[4];make_au(au);
    const uint8_t *h=adts+(size_t)index*11u;
    assert(h[0]==0xff&&h[1]==0xf1&&((h[2]>>2)&15u)==rate_index&&(((h[2]&1u)<<2)|(h[3]>>6))==chan);
    assert((((h[3]&3u)<<11)|(h[4]<<3)|(h[5]>>5))==11u&&memcmp(h+7,au,4)==0);
}
int main(void){
    static uint8_t f[65536];uint8_t *adts=NULL;size_t al=0;SautiAacLatmInfo info;
    Spec s;memset(&s,0,sizeof s);s.aot=2;s.channel_config=1;s.elements=5;
    /* konfiguracja tylko w pierwszym elemencie, potem useSameStreamMux; jednostki sa przesuniete bitowo (konfiguracja nie ma dlugosci w bajtach) */
    size_t n=build(f,&s);
    assert(sauti_aac_latm_probe(f,n)&&!sauti_aac_latm_probe(f,5)&&!sauti_aac_latm_probe(NULL,100));
    assert(sauti_aac_latm_to_adts(f,n,&adts,&al,&info)==1&&info.frames==5u&&al==5u*11u&&info.aot==2u&&info.rate_index==3u&&info.sample_rate==48000u&&
           info.channel_config==1u&&!info.sbr&&!info.ps);
    for(unsigned i=0;i<5;i++)check_au(adts,i,3,1);
    free(adts);
    /* warianty skladni */
    for(unsigned variant=0;variant<8;variant++){
        Spec v=s;
        switch(variant){
            case 0:v.version=1;break;                         /* audioMuxVersion 1: ascLen, taraBufferFullness */
            case 1:v.sub_frames=1;v.elements=3;break;         /* dwie subramki na element */
            case 2:v.config_every=2;break;                    /* konfiguracja powtarzana */
            case 3:v.other_data=1;break;                      /* otherData: pominiete */
            case 4:v.crc=1;break;                             /* crcCheckPresent */
            case 5:v.drop_first=2;break;                      /* poczatek strumienia przed pierwsza konfiguracja: pomijany */
            case 6:v.version=1;v.sub_frames=2;v.other_data=1;v.crc=1;v.config_every=1;v.elements=4;break;
            default:v.channel_config=2;break;                 /* stereo wg ASC */
        }
        n=build(f,&v);adts=NULL;
        const unsigned expect=(v.elements-v.drop_first)*(v.sub_frames+1u);
        assert(sauti_aac_latm_to_adts(f,n,&adts,&al,&info)==1&&info.frames==expect&&al==(size_t)expect*11u);
        for(unsigned i=0;i<expect;i++)check_au(adts,i,3,v.channel_config);
        free(adts);
    }
    /* HE-AAC: jawnie (AOT 5) i niejawnie (rozszerzenie 0x2B7); rdzen 24 kHz = indeks 6 */
    for(unsigned mode=0;mode<2;mode++){
        Spec v=s;if(mode==0)v.aot=5;else v.implicit_sbr=1;
        n=build(f,&v);adts=NULL;
        assert(sauti_aac_latm_to_adts(f,n,&adts,&al,&info)==1&&info.frames==5u&&info.sbr==1u&&info.rate_index==6u&&info.sample_rate==24000u);
        for(unsigned i=0;i<5;i++)check_au(adts,i,6,1);
        free(adts);
    }
    /* nieobslugiwane: inne AOT, frameLengthType != 0, PCE (channelConfiguration 0), zmiana konfiguracji w trakcie; bez PCM zastepczego */
    {   Spec v=s;v.aot=17;n=build(f,&v);adts=NULL;assert(sauti_aac_latm_to_adts(f,n,&adts,&al,&info)==0&&!adts);}
    {   Spec v=s;v.frame_length_type=1;n=build(f,&v);adts=NULL;assert(sauti_aac_latm_to_adts(f,n,&adts,&al,&info)==0&&!adts);}
    {   Spec v=s;v.frame960=1;n=build(f,&v);adts=NULL;assert(sauti_aac_latm_to_adts(f,n,&adts,&al,&info)==0&&!adts);}
    {   Spec v=s;v.channel_config=0;n=build(f,&v);adts=NULL;assert(sauti_aac_latm_to_adts(f,n,&adts,&al,&info)==0&&!adts);}
    {   Spec v=s;v.change_at=3;n=build(f,&v);adts=NULL;assert(sauti_aac_latm_to_adts(f,n,&adts,&al,&info)==0&&!adts);}
    /* ucinanie: kompletne elementy zostaja; uszkodzony naglowek dlugosci i losowe dane nie daja pliku */
    n=build(f,&s);
    assert(sauti_aac_latm_to_adts(f,n-3u,&adts,&al,&info)==1&&info.frames==4u);free(adts);
    { uint8_t g[200];for(unsigned i=0;i<sizeof g;i++)g[i]=(uint8_t)(i*37u+11u);assert(!sauti_aac_latm_probe(g,sizeof g)&&sauti_aac_latm_to_adts(g,sizeof g,&adts,&al,&info)==0);}
    /* adapter: dekoduje (cisza), seek */
    n=build(f,&s);
    {   SautiDecoder dec;const SautiDecoderOps *const reg[]={&sauti_decoder_aac_latm_native_ops};
        assert(sauti_legacy_decoder_open(&dec,reg,1,f,n));
        assert(dec.info.channels==1&&dec.info.sample_rate==48000u&&dec.info.total_frames==5u*1024u&&dec.ops==&sauti_decoder_aac_latm_native_ops);
        static float pcm[8192];size_t got,all=0;
        while((got=sauti_legacy_decoder_read(&dec,pcm,1000))>0){for(size_t i=0;i<got;i++)assert(pcm[i]==0.0f);all+=got;}
        assert(all==5u*1024u);
        assert(sauti_legacy_decoder_seek(&dec,1500)&&sauti_legacy_decoder_read(&dec,pcm,8000)==5u*1024u-1500u);
        sauti_legacy_decoder_close(&dec);
    }
    puts("PASS AAC LATM: LOAS -> ADTS, wersje 0/1, subramki, powtorzona konfiguracja, otherData/CRC, HE jawne i niejawne, odrzucenia, adapter");
    return 0;
}
