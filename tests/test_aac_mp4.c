/* AAC w MP4/M4A: demuxer ISO BMFF -> ADTS (sauti_aac_mp4) i adapter aac-native-mp4.  Pliki skladane w pamieci z pudelek; jednostki dostepu to prawdziwe ramki AAC-LC mono (jedno SCE o max_sfb = 0 = cisza),
 * wiec adapter dekoduje je naprawde.  Zgodnosc PCM z FFmpeg na prawdziwych plikach: `make test-aac-mp4-diff`. */
#include "sauti_decoder_aac_native.h"
#include "sauti_aac_mp4.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t b[16384]; size_t n; } Buf;
static void u8(Buf *b,unsigned v){b->b[b->n++]=(uint8_t)v;}
static void u16(Buf *b,unsigned v){u8(b,v>>8);u8(b,v);}
static void u32(Buf *b,uint32_t v){u16(b,v>>16);u16(b,v&0xffffu);}
static void raw(Buf *b,const void *p,size_t n){memcpy(b->b+b->n,p,n);b->n+=n;}
static size_t open_box(Buf *b,const char *type){const size_t at=b->n;u32(b,0);raw(b,type,4);return at;}
static void close_box(Buf *b,size_t at){const uint32_t sz=(uint32_t)(b->n-at);b->b[at]=(uint8_t)(sz>>24);b->b[at+1]=(uint8_t)(sz>>16);b->b[at+2]=(uint8_t)(sz>>8);b->b[at+3]=(uint8_t)sz;}

/* jednostka dostepu AAC-LC mono: SCE z max_sfb = 0 (cisza), 4 bajty */
static void make_au(uint8_t out[4]){
    uint32_t bits=0;unsigned n=0;
    #define PUT(w,v) do{bits=(bits<<(w))|((uint32_t)(v)&((1u<<(w))-1u));n+=(w);}while(0)
    PUT(3,0);PUT(4,0);PUT(8,100);PUT(1,0);PUT(2,0);PUT(1,0);PUT(6,0);PUT(1,0);PUT(1,0);PUT(1,0);PUT(1,0);PUT(3,7);
    #undef PUT
    assert(n==32u);
    out[0]=(uint8_t)(bits>>24);out[1]=(uint8_t)(bits>>16);out[2]=(uint8_t)(bits>>8);out[3]=(uint8_t)bits;
}
typedef struct {
    unsigned samples;              /* liczba AU */
    unsigned rate_index;           /* 3 = 48000 */
    unsigned channel_config;
    int elst;                      /* 1 = dodaj liste edycji */
    uint32_t elst_media_time, elst_duration;
    int codec_alac, drop_stco, big_au, stsz_truncated, stco_far, version1_mvhd;
    unsigned stz2;                 /* 4, 8 albo 16: rozmiary w stz2 zamiast stsz */
    int qt_wave;                   /* wersja 1 wpisu mp4a (QuickTime), esds w pudelku wave */
    int asc_ext;                   /* ASC z rozszerzeniem zgodnym wstecz (AOT 2 + 0x2B7 + SBR), jak w plikach Nero */
    unsigned frag;                 /* > 0: fragmentowany MP4, tyle fragmentow po samples/frag AU; stbl bez probek */
    unsigned frag_variant;         /* 0 = base_data_offset + rozmiary w trun; 1 = default_base_is_moof + default_sample_size w tfhd; 2 = rozmiar z trex */
    const char *smpb;              /* tekst iTunSMPB (NULL = brak) */
} Spec;
static void build(Buf *f,const Spec *sp){
    uint8_t au[4];make_au(au);
    f->n=0;
    size_t ft=open_box(f,"ftyp");raw(f,"M4A ",4);u32(f,0);raw(f,"M4A mp42isom",12);close_box(f,ft);
    const size_t moov=open_box(f,"moov");
    { size_t mv=open_box(f,"mvhd");u32(f,0);u32(f,0);u32(f,0);u32(f,1000);u32(f,5000);u32(f,0x10000);u16(f,0x100);for(int i=0;i<70;i++){u8(f,0);}
      close_box(f,mv); }
    const size_t trak=open_box(f,"trak");
    if(sp->elst){ size_t ed=open_box(f,"edts");size_t el=open_box(f,"elst");u32(f,0);u32(f,1);u32(f,sp->elst_duration);u32(f,sp->elst_media_time);u32(f,0x10000);close_box(f,el);close_box(f,ed); }
    if(sp->frag){ size_t th=open_box(f,"tkhd");u32(f,3);u32(f,0);u32(f,0);u32(f,1);u32(f,0);u32(f,0);for(int i=0;i<60;i++){u8(f,0);} close_box(f,th); }
    const size_t mdia=open_box(f,"mdia");
    { size_t md=open_box(f,"mdhd");u32(f,0);u32(f,0);u32(f,0);u32(f,48000);u32(f,sp->samples*1024u);u32(f,0);close_box(f,md); }
    { size_t h=open_box(f,"hdlr");u32(f,0);u32(f,0);raw(f,"soun",4);for(int i=0;i<12;i++){u8(f,0);}
      u8(f,0);close_box(f,h); }
    const size_t minf=open_box(f,"minf");const size_t stbl=open_box(f,"stbl");
    {   size_t sd=open_box(f,"stsd");u32(f,0);u32(f,1);
        size_t en=open_box(f,sp->codec_alac?"alac":"mp4a");
        for(int i=0;i<6;i++){u8(f,0);}
        u16(f,1);u16(f,sp->qt_wave?1u:0u);u16(f,0);u32(f,0);u16(f,sp->channel_config?sp->channel_config:1);u16(f,16);u16(f,0);u16(f,0);u32(f,48000u<<16);
        if(sp->qt_wave){u32(f,1024);u32(f,0);u32(f,0);u32(f,0);}                          /* QuickTime wersja 1: 16 bajtow rozszerzenia */
        size_t wv=0;
        if(sp->qt_wave){wv=open_box(f,"wave");size_t fr=open_box(f,"frma");raw(f,"mp4a",4);close_box(f,fr);}
        size_t es=open_box(f,"esds");u32(f,0);
        uint8_t asc[8]={0};unsigned asc_len=2;
        if(sp->asc_ext){
            /* AOT 2, czestotliwosc rdzenia 24 kHz (indeks 6), kanaly, GASpecificConfig 000, sync 0x2B7, AOT 5, SBR 1, czestotliwosc 48 kHz (indeks 3) */
            Buf t;memset(&t,0,sizeof t);
            #define TB(wd,v) do{for(int k_=(wd)-1;k_>=0;k_--){if(((v)>>k_)&1u)t.b[t.n/8u]|=(uint8_t)(0x80u>>(t.n%8u));t.n++;}}while(0)
            TB(5,2u);TB(4,6u);TB(4,sp->channel_config);TB(3,0u);TB(11,0x2b7u);TB(5,5u);TB(1,1u);TB(4,3u);
            #undef TB
            asc_len=(unsigned)((t.n+7u)/8u);memcpy(asc,t.b,asc_len);
        }else{
            asc[0]=(uint8_t)((2u<<3)|(sp->rate_index>>1));asc[1]=(uint8_t)(((sp->rate_index&1u)<<7)|(sp->channel_config<<3));
        }
        u8(f,0x03);u8(f,23u+asc_len);u16(f,0);u8(f,0);                                   /* ES_Descriptor */
        u8(f,0x04);u8(f,15u+asc_len);u8(f,0x40);u8(f,0x15);u8(f,0);u16(f,0);u32(f,0);u32(f,0);   /* DecoderConfigDescriptor */
        u8(f,0x05);u8(f,asc_len);raw(f,asc,asc_len);u8(f,0x06);u8(f,1);u8(f,2);
        close_box(f,es);
        if(sp->qt_wave){size_t tm=open_box(f,"\0\0\0\0");close_box(f,tm);close_box(f,wv);}
        close_box(f,es);close_box(f,en);close_box(f,sd); }
    if(sp->stz2&&!sp->big_au&&!sp->frag){
        size_t sz=open_box(f,"stz2");u32(f,0);u32(f,sp->stz2);u32(f,sp->samples);                 /* reserved(3) + field_size */
        for(unsigned i=0;i<sp->samples;i++){
            if(sp->stz2==16u)u16(f,4);else if(sp->stz2==8u)u8(f,4);else if(i&1u)f->b[f->n-1]=(uint8_t)(f->b[f->n-1]|5u);else u8(f,4u<<4);   /* 4 bity: rozmiary na przemian 4 i 5 (starszy polbajt = parzysta jednostka) */
        }
        close_box(f,sz);
    }else{ size_t sz=open_box(f,"stsz");u32(f,0);u32(f,sp->big_au?9000u:0u);u32(f,sp->frag?0u:sp->samples);
      if(!sp->big_au&&!sp->frag){for(unsigned i=0;i<(sp->stsz_truncated?sp->samples/2u:sp->samples);i++)u32(f,4);}
      close_box(f,sz); }
    { size_t sc=open_box(f,"stsc");u32(f,0);u32(f,1);u32(f,1);u32(f,sp->samples);u32(f,1);close_box(f,sc); }
    size_t stco_at=0;
    if(!sp->drop_stco){ size_t co=open_box(f,"stco");u32(f,0);u32(f,1);stco_at=f->n;u32(f,0);close_box(f,co); }
    close_box(f,stbl);close_box(f,minf);close_box(f,mdia);close_box(f,trak);
    if(sp->smpb){                                                               /* moov/udta/meta/ilst/----/{mean,name,data} */
        size_t ud=open_box(f,"udta"),me=open_box(f,"meta");u32(f,0);
        size_t il=open_box(f,"ilst"),dd=open_box(f,"----");
        { size_t m=open_box(f,"mean");u32(f,0);raw(f,"com.apple.iTunes",16);close_box(f,m); }
        { size_t n=open_box(f,"name");u32(f,0);raw(f,"iTunSMPB",8);close_box(f,n); }
        { size_t d=open_box(f,"data");u32(f,1);u32(f,0);raw(f,sp->smpb,strlen(sp->smpb));close_box(f,d); }
        close_box(f,dd);close_box(f,il);close_box(f,me);close_box(f,ud);
    }
    if(sp->frag){
        size_t mx=open_box(f,"mvex"),tx=open_box(f,"trex");u32(f,0);u32(f,1);u32(f,1);u32(f,1024);u32(f,sp->frag_variant==2u?4u:0u);u32(f,0);close_box(f,tx);close_box(f,mx);
    }
    close_box(f,moov);
    if(sp->frag){
        const unsigned per=sp->samples/sp->frag;
        for(unsigned fr=0;fr<sp->frag;fr++){
            const size_t moof=open_box(f,"moof");
            { size_t mf=open_box(f,"mfhd");u32(f,0);u32(f,fr+1u);close_box(f,mf); }
            const size_t traf=open_box(f,"traf");
            { size_t th=open_box(f,"tfhd");
              if(sp->frag_variant==0u){u32(f,0x1u);u32(f,1);u32(f,0);u32(f,(uint32_t)moof);}              /* base_data_offset = moof */
              else if(sp->frag_variant==1u){u32(f,0x20010u);u32(f,1);u32(f,4);}                           /* default_base_is_moof + default_sample_size */
              else {u32(f,0x20000u);u32(f,1);}
              close_box(f,th); }
            { size_t tr=open_box(f,"trun");u32(f,sp->frag_variant==0u?0x201u:0x1u);u32(f,per);
              const size_t off_at=f->n;u32(f,0);
              if(sp->frag_variant==0u)for(unsigned i=0;i<per;i++)u32(f,4);
              close_box(f,tr);
              close_box(f,traf);close_box(f,moof);
              const uint32_t doff=(uint32_t)(f->n-moof+8u);
              f->b[off_at]=(uint8_t)(doff>>24);f->b[off_at+1]=(uint8_t)(doff>>16);f->b[off_at+2]=(uint8_t)(doff>>8);f->b[off_at+3]=(uint8_t)doff; }
            size_t md=open_box(f,"mdat");
            for(unsigned i=0;i<per;i++)raw(f,au,4);
            close_box(f,md);
        }
        return;
    }
    size_t md=open_box(f,"mdat");
    const size_t data_at=f->n;
    if(sp->big_au){for(unsigned i=0;i<9000;i++){u8(f,0);}}
    else{for(unsigned i=0;i<sp->samples;i++){raw(f,au,4);if(sp->stz2==4u&&(i&1u))u8(f,0);}}
    close_box(f,md);
    if(!sp->drop_stco){ uint32_t off=sp->stco_far?0x7fffff00u:(uint32_t)data_at;f->b[stco_at]=(uint8_t)(off>>24);f->b[stco_at+1]=(uint8_t)(off>>16);f->b[stco_at+2]=(uint8_t)(off>>8);f->b[stco_at+3]=(uint8_t)off; }
}
int main(void){
    static Buf f;Spec sp;uint8_t *adts=NULL;size_t al=0;SautiAacMp4Info info;
    memset(&sp,0,sizeof sp);sp.samples=5;sp.rate_index=3;sp.channel_config=1;
    /* poprawny plik: naglowki ADTS i tresc jednostek */
    build(&f,&sp);
    assert(sauti_aac_mp4_probe(f.b,f.n) && !sauti_aac_mp4_probe(f.b,8) && !sauti_aac_mp4_probe(NULL,100));
    assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==1);
    assert(al==5u*(7u+4u) && info.aot==2 && info.rate_index==3 && info.sample_rate==48000 && info.channel_config==1 && info.sample_count==5 && info.skip_samples==0 && info.duration_samples==0);
    for(unsigned i=0;i<5;i++){
        const uint8_t *h=adts+i*11u;
        assert(h[0]==0xff && h[1]==0xf1 && (h[2]&0xc0)==0x40 && ((h[2]>>2)&15u)==3u && (((h[2]&1u)<<2)|(h[3]>>6))==1u);
        assert((((h[3]&3u)<<11)|(h[4]<<3)|(h[5]>>5))==11u);
        uint8_t au[4];make_au(au);assert(memcmp(h+7,au,4)==0);
    }
    free(adts);
    /* lista edycji (v0): pominiecie 1024 i dlugosc 3000 ms * 48 = 144000 > dostepne, wiec dlugosc 2000 ms = 96000 */
    sp.elst=1;sp.elst_media_time=1024;sp.elst_duration=2000;
    build(&f,&sp);adts=NULL;
    assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==1 && info.skip_samples==1024 && info.duration_samples==96000);free(adts);
    /* adapter: dekoduje (cisza), przycina: 5 AU = 5120, skip 1024, edycja 3000 ms... */
    sp.elst_duration=60;                                  /* 60 ms = 2880 probek */
    build(&f,&sp);
    {   SautiDecoder dec;const SautiDecoderOps *const reg[]={&sauti_decoder_aac_mp4_native_ops};
        assert(sauti_decoder_open(&dec,reg,1,f.b,f.n));
        assert(dec.info.channels==1 && dec.info.sample_rate==48000 && dec.info.total_frames==2880);
        static float pcm[8192];size_t got=0,all=0;
        while((got=sauti_decoder_read(&dec,pcm,1000))>0){for(size_t i=0;i<got;i++)assert(pcm[i]==0.0f);all+=got;}
        assert(all==2880);
        assert(sauti_decoder_seek(&dec,100)&&sauti_decoder_read(&dec,pcm,5000)==2780);
        assert(!sauti_decoder_seek(&dec,2881) && sauti_decoder_seek(&dec,2880) && sauti_decoder_read(&dec,pcm,10)==0);
        sauti_decoder_close(&dec);
    }
    /* iTunSMPB bez listy edycji: wartosci w probkach rdzenia, adapter skaluje (tu LC: 1:1).  5 AU = 5120 probek; priming 0x400 = 1024, dlugosc 0xBB8 = 3000. */
    {   Spec g=sp;g.elst=0;g.smpb=" 00000000 00000400 00000100 0000000000000BB8 00000000 00000000";
        build(&f,&g);adts=NULL;
        assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==1 && info.gapless==1u && info.gapless_priming==1024u && info.gapless_samples==3000u &&
               info.skip_samples==0u && info.duration_samples==0u);free(adts);
        SautiDecoder dec;const SautiDecoderOps *const reg[]={&sauti_decoder_aac_mp4_native_ops};
        assert(sauti_decoder_open(&dec,reg,1,f.b,f.n));
        assert(dec.info.total_frames==3000);
        static float pcm[8192];size_t got=0,all=0;
        while((got=sauti_decoder_read(&dec,pcm,1000))>0)all+=got;
        assert(all==3000);
        sauti_decoder_close(&dec);
        /* a lista edycji ma pierwszenstwo (dotychczasowe zachowanie, zwalidowane na korpusie): iTunSMPB jest wtedy ignorowany */
        g.elst=1;g.elst_media_time=1024;g.elst_duration=60;
        build(&f,&g);adts=NULL;
        assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==1 && info.gapless==0u && info.skip_samples==1024u);free(adts);
        /* nieprawdopodobny priming (> 2^20) albo uszkodzony tekst: ignorowane, bez ciecia dzwieku */
        g.elst=0;g.smpb=" 00000000 7FFFFFFF 00000100 0000000000000BB8";
        build(&f,&g);adts=NULL;
        assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==1 && info.gapless==0u);free(adts);
        g.smpb=" 00000000 zzzz 00000100";
        build(&f,&g);adts=NULL;
        assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==1 && info.gapless==0u);free(adts);
    }
    /* odrzucenia: kazde daje 0 / NULL bez efektow ubocznych */
    Spec bad;
    bad=sp;bad.codec_alac=1;build(&f,&bad);assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==0 && adts==NULL);
    bad=sp;bad.drop_stco=1;build(&f,&bad);assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==0);
    bad=sp;bad.big_au=1;bad.samples=1;build(&f,&bad);assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==0);          /* AU 9000 B > 8184 */
    bad=sp;bad.stsz_truncated=1;build(&f,&bad);assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==0);   /* tabela krotsza niz liczba */
    bad=sp;bad.stco_far=1;build(&f,&bad);assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==0);         /* offset poza plikiem */
    bad=sp;bad.rate_index=15;build(&f,&bad);assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==0);      /* jawna czestotliwosc: nie w ADTS */
    build(&f,&sp);
    for(size_t cut=8;cut<f.n;cut+=37){assert(sauti_aac_mp4_to_adts(f.b,cut,&adts,&al,&info)==0||adts);free(adts);adts=NULL;}   /* uciecia: bez awarii (ASan) */
    memset(&f.b[4],'x',4);assert(!sauti_aac_mp4_probe(f.b,f.n));
    { SautiDecoder dec;const SautiDecoderOps *const reg[]={&sauti_decoder_aac_mp4_native_ops};
      assert(!sauti_decoder_open(&dec,reg,1,f.b,f.n)); }
    /* fragmentowany MP4 (moov bez probek + moof/traf/trun): trzy sposoby opisu rozmiarow i podstawy offsetow, kilka fragmentow */
    for(unsigned variant=0;variant<3u;variant++){
        Spec g;memset(&g,0,sizeof g);g.samples=6;g.rate_index=3;g.channel_config=1;g.frag=3;g.frag_variant=variant;
        build(&f,&g);adts=NULL;
        assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==1 && info.sample_count==6u && al==6u*11u);
        for(unsigned i=0;i<6;i++){uint8_t au[4];make_au(au);assert(memcmp(adts+i*11u+7u,au,4)==0);}
        free(adts);
        SautiDecoder dec;const SautiDecoderOps *const reg[]={&sauti_decoder_aac_mp4_native_ops};
        assert(sauti_decoder_open(&dec,reg,1,f.b,f.n)&&dec.info.total_frames==6u*1024u);
        sauti_decoder_close(&dec);
        /* ucieta koncowka (ostatni fragment niepelny): kompletne fragmenty zostaja, jak w odtwarzaczach; 6 AU w 3 fragmentach po 2 */
        Buf cut=f;cut.n=f.n-30u;
        adts=NULL;assert(sauti_aac_mp4_to_adts(cut.b,cut.n,&adts,&al,&info)==1 && info.sample_count==4u && al==4u*11u);free(adts);
        /* data_offset wskazujacy poza plik: odrzucenie calego pliku (zadnego odczytu poza buforem) */
        Buf bad=f;
        for(size_t at=12u;at+8u<bad.n;at++)
            if(memcmp(bad.b+at,"trun",4)==0){const size_t o=at+4u+8u;bad.b[o]=0x7f;bad.b[o+1]=0xff;bad.b[o+2]=0xff;bad.b[o+3]=0xf0;if(variant==0u)break;}
        adts=NULL;assert(sauti_aac_mp4_to_adts(bad.b,bad.n,&adts,&al,&info)==0);
    }
    /* zwarte rozmiary (stz2: 4, 8, 16 bitow), esds w pudelku wave (QuickTime), ASC z rozszerzeniem SBR (Nero): ten sam wynik co zwykly plik */
    for(unsigned variant=0;variant<5;variant++){
        Spec g;memset(&g,0,sizeof g);g.samples=6;g.rate_index=3;g.channel_config=1;
        if(variant<3)g.stz2=variant==0?4u:variant==1?8u:16u;
        if(variant==3)g.qt_wave=1;
        if(variant==4){g.asc_ext=1;g.rate_index=6;}
        build(&f,&g);adts=NULL;
        assert(sauti_aac_mp4_to_adts(f.b,f.n,&adts,&al,&info)==1&&info.sample_count==6u&&al==(variant==0?3u*11u+3u*12u:6u*11u));
        const unsigned ri=variant==4?6u:3u;
        for(unsigned i=0,at=0;i<6;i++){uint8_t au[4];make_au(au);const uint8_t *h=adts+at;assert(((h[2]>>2)&15u)==ri&&memcmp(h+7,au,4)==0);at+=(variant==0&&(i&1u))?12u:11u;}
        if(variant==4)assert(info.aot==2u&&info.sample_rate==24000u);
        free(adts);
    }
    puts("PASS AAC MP4: demuxer ISO BMFF -> ADTS, lista edycji, adapter (dekodowanie, przyciecie, seek), odrzucenia");
    return 0;
}
