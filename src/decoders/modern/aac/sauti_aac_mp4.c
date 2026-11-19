/* ISO BMFF -> ADTS dla AAC (patrz sauti_aac_mp4.h).  Kazdy odczyt jest sprawdzany wzgledem rozmiaru pliku; zadnych wskaznikow poza bufor, zadnych przepelnien rozmiarow. */
#include "sauti_aac_mp4.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *p; size_t n; } View;
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t be64(const uint8_t *p){return (uint64_t)be32(p)<<32|be32(p+4);}
static uint16_t be16(const uint8_t *p){return (uint16_t)(p[0]<<8|p[1]);}

/* Nastepny box w widoku [*pos, v.n): zwraca 1 i ustawia type, payload (po naglowku) i koniec; 0 = koniec albo blad. */
typedef struct { uint32_t type; size_t start,payload,end; } Box;
static int next_box(View v,size_t *pos,Box *b){
    if(*pos>v.n||v.n-*pos<8u)return 0;
    const uint8_t *q=v.p+*pos;
    uint64_t size=be32(q);const uint32_t type=be32(q+4);
    size_t header=8u;
    if(size==1u){ if(v.n-*pos<16u)return 0; size=be64(q+8);header=16u; }
    else if(size==0u)size=v.n-*pos;
    if(size<header||size>v.n-*pos)return 0;
    b->type=type;b->start=*pos;b->payload=*pos+header;b->end=*pos+(size_t)size;
    *pos=b->end;
    return 1;
}
#define FOURCC(a,b,c,d) ((uint32_t)(a)<<24|(uint32_t)(b)<<16|(uint32_t)(c)<<8|(uint32_t)(d))
static int find_child(View v,uint32_t type,View *out){
    size_t pos=0;Box b;
    while(next_box(v,&pos,&b))
        if(b.type==type){out->p=v.p+b.payload;out->n=b.end-b.payload;return 1;}
    return 0;
}
int sauti_aac_mp4_probe(const uint8_t *data,size_t size){
    return data&&size>=12u&&memcmp(data+4,"ftyp",4)==0;
}
static const unsigned rates[13]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};

/* Deskryptory MPEG-4 (esds): tag + rozmiar zmiennej dlugosci (7 bitow na bajt). */
static int descriptor(View v,size_t *pos,unsigned *tag,View *body){
    if(*pos>=v.n)return 0;
    *tag=v.p[(*pos)++];
    size_t len=0;
    for(unsigned i=0;i<4;i++){
        if(*pos>=v.n)return 0;
        const uint8_t b=v.p[(*pos)++];
        len=(len<<7)|(b&0x7fu);
        if(!(b&0x80u))break;
        if(i==3u)return 0;
    }
    if(len>v.n-*pos)return 0;
    body->p=v.p+*pos;body->n=len;*pos+=len;
    return 1;
}
/* esds -> AudioSpecificConfig (kopia bajtow w asc[], dlugosc w *asc_len). */
static int parse_esds(View e,uint8_t asc[64],size_t *asc_len){
    if(e.n<4u)return 0;
    View d={e.p+4,e.n-4};size_t pos=0;unsigned tag;View body;
    if(!descriptor(d,&pos,&tag,&body)||tag!=0x03u||body.n<3u)return 0;       /* ES_Descriptor: ES_ID(2), flagi(1) [opcjonalne pola] */
    size_t p=3;const unsigned flags=body.p[2];
    if(flags&0x80u){ if(body.n-p<2u)return 0; p+=2; }                         /* streamDependenceFlag */
    if(flags&0x40u){ if(body.n-p<1u)return 0; const unsigned l=body.p[p]; if(body.n-p<1u+l)return 0; p+=1u+l; }   /* URL */
    if(flags&0x20u){ if(body.n-p<2u)return 0; p+=2; }                         /* OCRstreamFlag */
    View sub={body.p+p,body.n-p};size_t sp=0;View dcd;
    if(!descriptor(sub,&sp,&tag,&dcd)||tag!=0x04u||dcd.n<13u)return 0;       /* DecoderConfigDescriptor */
    if(dcd.p[0]!=0x40u)return 0;                                              /* MPEG-4 Audio */
    View in={dcd.p+13,dcd.n-13};size_t ip=0;View dsi;
    if(!descriptor(in,&ip,&tag,&dsi)||tag!=0x05u||dsi.n<2u||dsi.n>64u)return 0;
    memcpy(asc,dsi.p,dsi.n);*asc_len=dsi.n;
    return 1;
}
static unsigned getbits(const uint8_t *p,size_t n,size_t *bit,unsigned count){
    unsigned v=0;
    for(unsigned i=0;i<count;i++){
        const size_t byte=*bit>>3;
        if(byte>=n)return 0xffffffffu;
        v=(v<<1)|((p[byte]>>(7u-(*bit&7u)))&1u);(*bit)++;
    }
    return v;
}
static int parse_asc(const uint8_t *asc,size_t n,SautiAacMp4Info *info){
    size_t bit=0;
    unsigned aot=getbits(asc,n,&bit,5);
    if(aot==31u){ unsigned e=getbits(asc,n,&bit,6); if(e==0xffffffffu)return 0; aot=32u+e; }
    const unsigned ri=getbits(asc,n,&bit,4);
    if(aot==0xffffffffu||ri==0xffffffffu||ri>=13u)return 0;                  /* 15 (jawna czestotliwosc) nie mieści sie w ADTS */
    const unsigned cc=getbits(asc,n,&bit,4);
    if(cc==0xffffffffu||cc>7u)return 0;
    if(aot!=2u&&aot!=5u&&aot!=29u)return 0;                                   /* LC, albo HE/HEv2 (rdzen LC + SBR w FIL) */
    if(aot==5u||aot==29u){                                                    /* jawny SBR: nastepna jest czestotliwosc wyjsciowa i AOT rdzenia */
        const unsigned eri=getbits(asc,n,&bit,4);
        if(eri==0xffffffffu)return 0;
        if(eri==15u)bit+=24u;
        unsigned core=getbits(asc,n,&bit,5);
        if(core!=2u)return 0;
    }
    info->aot=aot;info->rate_index=ri;info->sample_rate=rates[ri];info->channel_config=cc;
    return 1;
}
/* iTunSMPB: ----/mean/name("iTunSMPB")/data(text): " 00000000 <priming> <remainder> <samples> ..." (hex fields).  Searched as bytes inside moov, bounded. */
static int hex_field(const uint8_t *p,size_t n,size_t *pos,uint64_t *out){
    while(*pos<n&&p[*pos]==' ')(*pos)++;
    uint64_t v=0;unsigned digits=0;
    while(*pos<n&&digits<17u){
        const uint8_t c=p[*pos];unsigned d;
        if(c>='0'&&c<='9')d=(unsigned)(c-'0');else if(c>='a'&&c<='f')d=(unsigned)(c-'a'+10);else if(c>='A'&&c<='F')d=(unsigned)(c-'A'+10);else break;
        v=(v<<4)|d;digits++;(*pos)++;
    }
    if(!digits||digits>16u)return 0;
    *out=v;return 1;
}
static void gapless_parse(View moov,SautiAacMp4Info *inf){
    static const char key[]="iTunSMPB";
    for(size_t i=0;i+sizeof(key)+24u<moov.n;i++){
        if(memcmp(moov.p+i,key,sizeof(key)-1u))continue;
        for(size_t j=i+8u;j+16u<moov.n&&j<i+8u+48u;j++){
            if(memcmp(moov.p+j,"data",4))continue;
            size_t pos=j+4u+8u;                                   /* 'data': version+flags(4), locale(4), then the text */
            uint64_t zero,priming,remainder,samples;
            if(!hex_field(moov.p,moov.n,&pos,&zero)||!hex_field(moov.p,moov.n,&pos,&priming)||
               !hex_field(moov.p,moov.n,&pos,&remainder)||!hex_field(moov.p,moov.n,&pos,&samples))return;
            (void)zero;(void)remainder;
            if(priming>(1u<<20))return;                           /* not plausible: ignore rather than cut audio */
            inf->gapless=1u;inf->gapless_priming=priming;inf->gapless_samples=samples;
            return;
        }
        return;
    }
}
int sauti_aac_mp4_to_adts(const uint8_t *data,size_t size,uint8_t **adts,size_t *adts_length,SautiAacMp4Info *info){
    if(!data||!adts||!adts_length||!info||!sauti_aac_mp4_probe(data,size))return 0;
    *adts=NULL;*adts_length=0;
    View file={data,size},moov;
    if(!find_child(file,FOURCC('m','o','o','v'),&moov))return 0;
    uint32_t movie_timescale=1000;
    {View mvhd;if(find_child(moov,FOURCC('m','v','h','d'),&mvhd)&&mvhd.n>=20u){
        movie_timescale=mvhd.p[0]==1u?(mvhd.n>=28u?be32(mvhd.p+20):0u):be32(mvhd.p+12);}}
    size_t pos=0;Box b;
    while(next_box(moov,&pos,&b)){
        if(b.type!=FOURCC('t','r','a','k'))continue;
        View trak={moov.p+b.payload,b.end-b.payload},mdia,hdlr,minf,stbl,stsd,mdhd;
        if(!find_child(trak,FOURCC('m','d','i','a'),&mdia)||!find_child(mdia,FOURCC('h','d','l','r'),&hdlr)||hdlr.n<12u||
           be32(hdlr.p+8)!=FOURCC('s','o','u','n'))continue;
        if(!find_child(mdia,FOURCC('m','i','n','f'),&minf)||!find_child(minf,FOURCC('s','t','b','l'),&stbl)||
           !find_child(stbl,FOURCC('s','t','s','d'),&stsd)||!find_child(mdia,FOURCC('m','d','h','d'),&mdhd))continue;
        uint32_t media_timescale=0;
        if(mdhd.n>=24u)media_timescale=mdhd.p[0]==1u?(mdhd.n>=32u?be32(mdhd.p+20):0u):be32(mdhd.p+12);
        if(stsd.n<16u)continue;
        /* stsd: wersja/flagi(4), liczba wpisow(4), wpis: rozmiar(4) typ(4) ... */
        size_t ep=8u;Box e;View ent={stsd.p,stsd.n};
        if(!next_box(ent,&ep,&e)||e.type!=FOURCC('m','p','4','a'))continue;   /* ALAC i inne: nie nasze */
        /* AudioSampleEntry: 6 zarezerwowane + 2 dref + wersja(2) ... Wersja 1/2 (QuickTime) maja dluzszy naglowek. */
        const size_t base=e.payload+8u;
        if(e.end<base+20u)continue;
        const unsigned ver=be16(stsd.p+base);
        size_t extra=ver==1u?16u:(ver==2u?36u:0u);
        const size_t children=base+20u+extra;
        if(children>e.end)continue;
        View esds_parent={stsd.p+children,e.end-children},esds;
        uint8_t asc[64];size_t asc_len=0;
        if(!find_child(esds_parent,FOURCC('e','s','d','s'),&esds)||!parse_esds(esds,asc,&asc_len))continue;
        SautiAacMp4Info inf;memset(&inf,0,sizeof(inf));
        if(!parse_asc(asc,asc_len,&inf))continue;
        /* tablice probek */
        View stsz,stsc,stco;int co64=0;
        if(!find_child(stbl,FOURCC('s','t','s','z'),&stsz)||!find_child(stbl,FOURCC('s','t','s','c'),&stsc))continue;
        if(!find_child(stbl,FOURCC('s','t','c','o'),&stco)){ if(!find_child(stbl,FOURCC('c','o','6','4'),&stco))continue; co64=1; }
        if(stsz.n<12u||stsc.n<8u||stco.n<8u)continue;
        const uint32_t fixed=be32(stsz.p+4),count=be32(stsz.p+8);
        if(count==0u||count>50000000u)continue;
        if(!fixed&&(size_t)count>(stsz.n-12u)/4u)continue;
        const uint32_t stsc_n=be32(stsc.p+4);
        if((size_t)stsc_n>(stsc.n-8u)/12u||stsc_n==0u)continue;
        const uint32_t chunks=be32(stco.p+4);
        const size_t esz=co64?8u:4u;
        if((size_t)chunks>(stco.n-8u)/esz||chunks==0u)continue;
        /* wymiar wyjscia: suma (7 + rozmiar AU), AU <= 8184 */
        uint64_t total=0;
        for(uint32_t i=0;i<count;i++){
            const uint32_t sz=fixed?fixed:be32(stsz.p+12+(size_t)i*4u);
            if(sz>8184u||sz==0u){total=0;break;}
            total+=7u+sz;
        }
        if(!total||total>(uint64_t)512u*1024u*1024u)continue;
        uint8_t *out=malloc((size_t)total);
        if(!out)return 0;
        size_t w=0;uint32_t sample=0;int ok=1;
        for(uint32_t g=0;g<stsc_n&&ok&&sample<count;g++){
            const uint32_t first=be32(stsc.p+8+(size_t)g*12u),per=be32(stsc.p+12+(size_t)g*12u);
            const uint32_t last=(g+1u<stsc_n)?be32(stsc.p+8+(size_t)(g+1u)*12u):chunks+1u;
            if(first<1u||last<first||last>chunks+1u||per==0u){ok=0;break;}
            for(uint32_t c=first;c<last&&ok&&sample<count;c++){
                uint64_t off=co64?be64(stco.p+8+(size_t)(c-1u)*8u):be32(stco.p+8+(size_t)(c-1u)*4u);
                for(uint32_t s=0;s<per&&sample<count;s++,sample++){
                    const uint32_t sz=fixed?fixed:be32(stsz.p+12+(size_t)sample*4u);
                    if(off>size||sz>size-off){ok=0;break;}
                    const unsigned len=7u+sz;
                    out[w+0]=0xff;out[w+1]=0xf1;
                    out[w+2]=(uint8_t)(0x40u|(inf.rate_index<<2)|((inf.channel_config>>2)&1u));
                    out[w+3]=(uint8_t)(((inf.channel_config&3u)<<6)|((len>>11)&3u));
                    out[w+4]=(uint8_t)(len>>3);
                    out[w+5]=(uint8_t)(((len&7u)<<5)|0x1fu);
                    out[w+6]=0xfc;
                    memcpy(out+w+7,data+off,sz);
                    w+=len;off+=sz;
                }
            }
        }
        if(!ok||sample!=count||w!=(size_t)total){free(out);continue;}
        inf.sample_count=count;
        /* lista edycji: pierwszy wpis niepusty */
        View edts,elst;
        if(find_child(trak,FOURCC('e','d','t','s'),&edts)&&find_child(edts,FOURCC('e','l','s','t'),&elst)&&elst.n>=8u&&media_timescale){
            const uint32_t n=be32(elst.p+4);const unsigned v=elst.p[0];const size_t es=v==1u?20u:12u;
            for(uint32_t i=0;i<n&&(size_t)(i+1u)*es<=elst.n-8u;i++){
                const uint8_t *q=elst.p+8+(size_t)i*es;
                const uint64_t seg=v==1u?be64(q):be32(q);
                const int64_t mt=v==1u?(int64_t)be64(q+8):(int32_t)be32(q+4);       /* v0: segment_duration(4) media_time(4) rate(4); v1: 8 + 8 + 4 */
                if(mt<0)continue;                                             /* pusta edycja (cisza na poczatku): pomijamy */
                inf.skip_samples=(uint64_t)mt*inf.sample_rate/media_timescale;
                if(seg&&movie_timescale)inf.duration_samples=seg*inf.sample_rate/movie_timescale;
                break;
            }
        }
        if(!inf.skip_samples&&!inf.duration_samples)gapless_parse(moov,&inf);
        *adts=out;*adts_length=w;*info=inf;
        return 1;
    }
    return 0;
}
