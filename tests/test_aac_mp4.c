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
    const size_t mdia=open_box(f,"mdia");
    { size_t md=open_box(f,"mdhd");u32(f,0);u32(f,0);u32(f,0);u32(f,48000);u32(f,sp->samples*1024u);u32(f,0);close_box(f,md); }
    { size_t h=open_box(f,"hdlr");u32(f,0);u32(f,0);raw(f,"soun",4);for(int i=0;i<12;i++){u8(f,0);}
      u8(f,0);close_box(f,h); }
    const size_t minf=open_box(f,"minf");const size_t stbl=open_box(f,"stbl");
    {   size_t sd=open_box(f,"stsd");u32(f,0);u32(f,1);
        size_t en=open_box(f,sp->codec_alac?"alac":"mp4a");
        for(int i=0;i<6;i++){u8(f,0);}
        u16(f,1);u16(f,0);u16(f,0);u32(f,0);u16(f,sp->channel_config?sp->channel_config:1);u16(f,16);u16(f,0);u16(f,0);u32(f,48000u<<16);
        size_t es=open_box(f,"esds");u32(f,0);
        const uint8_t asc[2]={(uint8_t)((2u<<3)|(sp->rate_index>>1)),(uint8_t)(((sp->rate_index&1u)<<7)|(sp->channel_config<<3))};
        u8(f,0x03);u8(f,25);u16(f,0);u8(f,0);                                   /* ES_Descriptor */
        u8(f,0x04);u8(f,17);u8(f,0x40);u8(f,0x15);u8(f,0);u16(f,0);u32(f,0);u32(f,0);   /* DecoderConfigDescriptor */
        u8(f,0x05);u8(f,2);raw(f,asc,2);u8(f,0x06);u8(f,1);u8(f,2);
        close_box(f,es);close_box(f,en);close_box(f,sd); }
    { size_t sz=open_box(f,"stsz");u32(f,0);u32(f,sp->big_au?9000u:0u);u32(f,sp->samples);
      if(!sp->big_au){for(unsigned i=0;i<(sp->stsz_truncated?sp->samples/2u:sp->samples);i++)u32(f,4);}
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
    close_box(f,moov);
    size_t md=open_box(f,"mdat");
    const size_t data_at=f->n;
    if(sp->big_au){for(unsigned i=0;i<9000;i++){u8(f,0);}}
    else{for(unsigned i=0;i<sp->samples;i++){raw(f,au,4);}}
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
    puts("PASS AAC MP4: demuxer ISO BMFF -> ADTS, lista edycji, adapter (dekodowanie, przyciecie, seek), odrzucenia");
    return 0;
}
