/* ISO BMFF -> ADTS dla AAC (patrz sauti_aac_mp4.h).  Kazdy odczyt jest sprawdzany wzgledem rozmiaru pliku; zadnych wskaznikow poza bufor, zadnych przepelnien rozmiarow. */
#include "sauti_aac_mp4.h"
#include "sauti_usac.h"
#include "sauti_aac_er.h"
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
#define MAX_SAMPLES 20000000u
typedef struct { uint64_t off; uint32_t size; } Sample;
static int add_sample(Sample **v,size_t *n,size_t *cap,uint64_t off,uint32_t size){
    if(*n>=MAX_SAMPLES)return 0;
    if(*n==*cap){
        const size_t nc=*cap?*cap*2u:4096u;
        Sample *t=realloc(*v,nc*sizeof(**v));
        if(!t)return 0;
        *v=t;*cap=nc;
    }
    (*v)[*n].off=off;(*v)[*n].size=size;(*n)++;
    return 1;
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
static int parse_esds(View e,uint8_t asc[256],size_t *asc_len){
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
    if(!descriptor(in,&ip,&tag,&dsi)||tag!=0x05u||dsi.n<2u||dsi.n>256u)return 0;
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
/* program_config_element() in an AudioSpecificConfig -> the standard channel_configuration (1..7) with the same element order, 0 otherwise: one front SCE / CPE, then more front CPEs, side and back
 * elements, then the LFE ("S", "C", "SC", "SCS", "SCC", "SCCL", "SCCCL" as in sauti_aac_lcm.c).  `bit` is the position of the PCE and moves past it. */
static unsigned pce_standard_config(const uint8_t *asc,size_t n,size_t *bit){
    unsigned v[12];
    for(unsigned i=0;i<9u;i++){static const uint8_t w[9]={4,2,4,4,4,4,2,3,4};v[i]=getbits(asc,n,bit,w[i]);if(v[i]==0xffffffffu)return 0;}
    const unsigned nf=v[3],ns=v[4],nb=v[5],nl=v[6],na=v[7],ncc=v[8];
    for(unsigned k=0;k<3u;k++){const unsigned f=getbits(asc,n,bit,1);if(f==0xffffffffu)return 0;if(f&&getbits(asc,n,bit,k==2u?3u:4u)==0xffffffffu)return 0;}
    char kinds[16];unsigned count=0;
    for(unsigned part=0;part<3u;part++){
        const unsigned num=part==0u?nf:part==1u?ns:nb;
        for(unsigned i=0;i<num;i++){
            const unsigned is_cpe=getbits(asc,n,bit,1),tag=getbits(asc,n,bit,4);
            if(is_cpe==0xffffffffu||tag==0xffffffffu||count>=15u)return 0;
            kinds[count++]=is_cpe?'C':'S';
        }
    }
    for(unsigned i=0;i<nl;i++){if(getbits(asc,n,bit,4)==0xffffffffu||count>=15u)return 0;kinds[count++]='L';}
    for(unsigned i=0;i<na;i++)if(getbits(asc,n,bit,4)==0xffffffffu)return 0;
    for(unsigned i=0;i<ncc;i++)if(getbits(asc,n,bit,5)==0xffffffffu)return 0;
    kinds[count]=0;
    static const char *const layout[8]={"","S","C","SC","SCS","SCC","SCCL","SCCCL"};
    for(unsigned c=1;c<8u;c++)if(!strcmp(kinds,layout[c]))return c;
    return 0;
}
static int parse_asc(const uint8_t *asc,size_t n,SautiAacMp4Info *info){
    size_t bit=0;
    unsigned aot=getbits(asc,n,&bit,5);
    if(aot==31u){ unsigned e=getbits(asc,n,&bit,6); if(e==0xffffffffu)return 0; aot=32u+e; }
    if(aot==42u){                                                             /* USAC (xHE-AAC): UsacConfig() niesie czestotliwosc, kanaly i uklad ramki; jednostki dostepu nie sa ADTS */
        SautiUsacConfig uc;
        if(sauti_usac_asc_parse(asc,n,&uc)!=1||uc.out_channels<1u||uc.out_channels>SAUTI_USAC_MAX_CHANNELS||uc.channel_config>7u||n>sizeof(info->asc))return 0;
        info->aot=42u;info->rate_index=uc.aac_rate_index;info->sample_rate=uc.sample_rate;info->channel_config=uc.channel_config;info->frame480=0u;
        memcpy(info->asc,asc,n);info->asc_len=(unsigned)n;
        return 1;
    }
    const unsigned ri=getbits(asc,n,&bit,4);
    if(aot==0xffffffffu||ri==0xffffffffu||ri>=13u)return 0;                  /* 15 (jawna czestotliwosc) nie mieści sie w ADTS */
    const unsigned cc=getbits(asc,n,&bit,4);
    if(cc==0xffffffffu||cc>7u)return 0;
    if(aot==17u||aot==19u||aot==23u||aot==39u){                               /* ER AAC LC / LTP, AAC-LD / AAC-ELD: ASC wg sauti_aac_er_asc_parse; jednostki dostepu nie sa ADTS (patrz aac_er_wrap w silniku) */
        SautiAacErAsc er;
        if(sauti_aac_er_asc_parse(asc,n,&er)!=1||(aot==19u&&er.frame480))return 0;                 /* ER AAC LTP nie ma wariantu 960 */
        if(er.ld_sbr&&(aot!=39u||er.channel_config<1u||er.channel_config>2u||n>sizeof(info->asc)))return 0;       /* ELD z SBR niskoopoznieniowym: mono i stereo; konfiguracja (ld_sbr_header) jest czescia ASC */
        info->aot=aot;info->rate_index=er.rate_index;info->sample_rate=rates[er.rate_index];info->channel_config=er.channel_config;info->frame480=er.frame480;
        if(er.ld_sbr){memcpy(info->asc,asc,n);info->asc_len=(unsigned)n;}
        return 1;
    }
    if(aot==3u){                                                              /* AAC SSR: GASpecificConfig (frameLengthFlag 0, no core coder), channel_configuration or a program_config_element that is one of the standard layouts */
        if(getbits(asc,n,&bit,1)!=0u||getbits(asc,n,&bit,1)!=0u||getbits(asc,n,&bit,1)!=0u)return 0;
        unsigned config=cc;
        if(cc==0u&&(config=pce_standard_config(asc,n,&bit))==0u)return 0;
        if(config<1u||config>7u)return 0;
        info->aot=3u;info->rate_index=ri;info->sample_rate=rates[ri];info->channel_config=config;info->frame480=0u;
        return 1;
    }
    if(aot!=1u&&aot!=2u&&aot!=5u&&aot!=29u&&aot!=4u)return 0;                         /* Main, LC, albo HE/HEv2 (rdzen LC + SBR w FIL) */
    info->frame480=0u;
    if(aot==1u||aot==4u){                                                    /* AAC Main / AAC LTP: 1024 linii (frameLengthFlag 1 nie jest obslugiwane), mono i stereo */
        const unsigned flag=getbits(asc,n,&bit,1);
        if(flag==1u&&(aot!=1u||cc<1u||cc>2u))return 0;                         /* 960 linii: tylko Main mono / stereo (AAC LTP nie ma wariantu 960) */
        info->aot=aot;info->rate_index=ri;info->sample_rate=rates[ri];info->channel_config=cc;info->frame480=flag;return cc>=1u&&cc<=7u;      /* wielokanalowy: sauti_aac_lcm */
    }
    if(aot==2u){                                                              /* GASpecificConfig: frameLengthFlag 1 = 960 linii (tylko LC bez SBR) */
        const unsigned flag=getbits(asc,n,&bit,1);
        if(flag==1u){info->aot=aot;info->rate_index=ri;info->sample_rate=rates[ri];info->channel_config=cc;info->frame480=1u;return cc>=1u&&cc<=2u;}
    }
    if(aot==5u||aot==29u){                                                    /* jawny SBR: nastepna jest czestotliwosc wyjsciowa i AOT rdzenia */
        const unsigned eri=getbits(asc,n,&bit,4);
        if(eri==0xffffffffu)return 0;
        if(eri==15u)bit+=24u;
        unsigned core=getbits(asc,n,&bit,5);
        if(core!=2u)return 0;
        if(getbits(asc,n,&bit,1)==1u){                                        /* HE-AAC z ramka 960: rdzen LC 960 bez SBR; AU z SBR odrzuca dekoder (E_UNSUPPORTED_FEATURE), nie dekoduje ich jako 1024 */
            info->aot=2u;info->rate_index=ri;info->sample_rate=rates[ri];info->channel_config=cc;info->frame480=1u;return cc>=1u&&cc<=2u;
        }
    }
    info->aot=aot;info->rate_index=ri;info->sample_rate=rates[ri];info->channel_config=cc;info->frame480=0u;
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
        uint8_t asc[256];size_t asc_len=0;
        /* esds jest dzieckiem wpisu mp4a; QuickTime (wersja 1/2 wpisu, stare M4A/MOV) chowa go w pudelku `wave` (frma, mp4a, esds, terminator) */
        int have_esds=find_child(esds_parent,FOURCC('e','s','d','s'),&esds);
        if(!have_esds){
            View wave;
            have_esds=find_child(esds_parent,FOURCC('w','a','v','e'),&wave)&&find_child(wave,FOURCC('e','s','d','s'),&esds);
        }
        if(!have_esds||!parse_esds(esds,asc,&asc_len))continue;
        SautiAacMp4Info inf;memset(&inf,0,sizeof(inf));
        if(!parse_asc(asc,asc_len,&inf))continue;
        if(inf.aot==1u||inf.aot==3u||inf.aot==4u||inf.aot==17u||inf.aot==19u||inf.aot==23u||inf.aot==39u||inf.aot==42u||(inf.aot==2u&&inf.frame480))continue;                              /* AAC-LD / ELD nie da sie zapisac jako ADTS (sciezka strumieniowa silnika owija je wlasnym naglowkiem) */
        /* Lista jednostek dostepu: z tablic probek (stbl) i z fragmentow (moof/trun) - plik moze miec jedno, drugie albo oba. */
        Sample *list=NULL;size_t nlist=0,caplist=0;
        {
            View stsz,stsc,stco;int co64=0;
            /* rozmiary jednostek: stsz (staly albo tabela 32-bitowa) albo stz2 (zwarta: 4, 8 albo 16 bitow na wpis, Nero i inne) */
            uint32_t fixed=0,count=0,field=32;int have_sizes=0;View table={NULL,0};
            if(find_child(stbl,FOURCC('s','t','s','z'),&stsz)&&stsz.n>=12u){
                fixed=be32(stsz.p+4);count=be32(stsz.p+8);table.p=stsz.p+12;table.n=stsz.n-12u;have_sizes=1;
                if(!fixed&&(size_t)count>table.n/4u)have_sizes=0;
            }else if(find_child(stbl,FOURCC('s','t','z','2'),&stsz)&&stsz.n>=12u){
                field=stsz.p[7];count=be32(stsz.p+8);table.p=stsz.p+12;table.n=stsz.n-12u;
                have_sizes=(field==4u||field==8u||field==16u)&&(size_t)count<=table.n*8u/field;
            }
            if(have_sizes&&find_child(stbl,FOURCC('s','t','s','c'),&stsc)&&
               (find_child(stbl,FOURCC('s','t','c','o'),&stco)||(find_child(stbl,FOURCC('c','o','6','4'),&stco)&&(co64=1)))&&
               stsc.n>=8u&&stco.n>=8u){
                const uint32_t stsc_n=be32(stsc.p+4),chunks=be32(stco.p+4);
                const size_t esz=co64?8u:4u;
                if(count>0u&&count<=MAX_SAMPLES&&
                   stsc_n>0u&&(size_t)stsc_n<=(stsc.n-8u)/12u&&chunks>0u&&(size_t)chunks<=(stco.n-8u)/esz){
                    uint32_t sample=0;int ok=1;
                    for(uint32_t g=0;g<stsc_n&&ok&&sample<count;g++){
                        const uint32_t first=be32(stsc.p+8+(size_t)g*12u),per=be32(stsc.p+12+(size_t)g*12u);
                        const uint32_t last=(g+1u<stsc_n)?be32(stsc.p+8+(size_t)(g+1u)*12u):chunks+1u;
                        if(first<1u||last<first||last>chunks+1u||per==0u){ok=0;break;}
                        for(uint32_t c=first;c<last&&ok&&sample<count;c++){
                            uint64_t off=co64?be64(stco.p+8+(size_t)(c-1u)*8u):be32(stco.p+8+(size_t)(c-1u)*4u);
                            for(uint32_t s2=0;s2<per&&sample<count;s2++,sample++){
                                uint32_t sz=fixed;
                                if(!fixed){
                                    if(field==32u)sz=be32(table.p+(size_t)sample*4u);
                                    else if(field==16u)sz=be16(table.p+(size_t)sample*2u);
                                    else if(field==8u)sz=table.p[sample];
                                    else sz=(sample&1u)?(table.p[sample/2u]&15u):(table.p[sample/2u]>>4);
                                }
                                if(!add_sample(&list,&nlist,&caplist,off,sz)){ok=0;break;}
                                off+=sz;
                            }
                        }
                    }
                    if(!ok||sample!=count){free(list);list=NULL;nlist=caplist=0;}
                }
            }
        }
        {   /* fragmenty: moof na poziomie pliku, traf dla tej sciezki */
            View tkhd,mvex;uint32_t track_id=0,trex_size=0;
            if(find_child(trak,FOURCC('t','k','h','d'),&tkhd)&&tkhd.n>=24u)
                track_id=tkhd.p[0]==1u?(tkhd.n>=28u?be32(tkhd.p+20):0u):be32(tkhd.p+12);
            if(find_child(moov,FOURCC('m','v','e','x'),&mvex)){
                size_t tp=0;Box tb;
                while(next_box(mvex,&tp,&tb))
                    if(tb.type==FOURCC('t','r','e','x')&&tb.end-tb.payload>=24u&&be32(mvex.p+tb.payload+4)==track_id)
                        trex_size=be32(mvex.p+tb.payload+16);
                size_t fp=0;Box fb;
                while(track_id&&next_box(file,&fp,&fb)){
                    if(fb.type!=FOURCC('m','o','o','f'))continue;
                    View moof={data+fb.payload,fb.end-fb.payload};
                    size_t mp=0;Box tf;
                    while(next_box(moof,&mp,&tf)){
                        if(tf.type!=FOURCC('t','r','a','f'))continue;
                        View traf={moof.p+tf.payload,tf.end-tf.payload};
                        size_t hp=0;Box hb;uint64_t base=fb.start,cursor=0;int have_cursor=0;
                        uint32_t def_size=trex_size;int mine=0;
                        while(next_box(traf,&hp,&hb)){
                            const uint8_t *q=traf.p+hb.payload;const size_t qn=hb.end-hb.payload;
                            if(hb.type==FOURCC('t','f','h','d')&&qn>=8u){
                                const uint32_t flags=be32(q)&0xffffffu;size_t o=8u;
                                if(be32(q+4)!=track_id)break;
                                mine=1;
                                if(flags&0x1u){if(qn<o+8u)break;base=be64(q+o);o+=8u;}
                                if(flags&0x2u)o+=4u;
                                if(flags&0x8u)o+=4u;
                                if(flags&0x10u){if(qn<o+4u)break;def_size=be32(q+o);o+=4u;}
                            }else if(hb.type==FOURCC('t','r','u','n')&&mine&&qn>=8u){
                                const uint32_t flags=be32(q)&0xffffffu,cnt=be32(q+4);size_t o=8u;
                                if(flags&0x1u){if(qn<o+4u)break;cursor=(uint64_t)((int64_t)base+(int32_t)be32(q+o));have_cursor=1;o+=4u;}
                                else if(!have_cursor){cursor=base;have_cursor=1;}
                                if(flags&0x4u)o+=4u;
                                const size_t per=((flags&0x100u)?4u:0u)+((flags&0x200u)?4u:0u)+((flags&0x400u)?4u:0u)+((flags&0x800u)?4u:0u);
                                if(per&&(size_t)cnt>(qn-o)/per)break;
                                for(uint32_t i=0;i<cnt;i++){
                                    const uint8_t *r=q+o+(size_t)i*per;size_t ro=0;
                                    if(flags&0x100u)ro+=4u;
                                    uint32_t sz=def_size;
                                    if(flags&0x200u){sz=be32(r+ro);}
                                    if(!add_sample(&list,&nlist,&caplist,cursor,sz)){nlist=0;break;}
                                    cursor+=sz;
                                }
                            }
                        }
                    }
                }
            }
        }
        if(!nlist){free(list);continue;}
        const size_t count=nlist;
        /* wymiar wyjscia: suma (7 + rozmiar AU), AU <= 8184 */
        uint64_t total=0;
        for(size_t i=0;i<count;i++){
            const uint32_t sz=list[i].size;
            if(sz>8184u||sz==0u||list[i].off>size||sz>size-list[i].off){total=0;break;}
            total+=7u+sz;
        }
        if(!total||total>(uint64_t)512u*1024u*1024u){free(list);continue;}
        uint8_t *out=malloc((size_t)total);
        if(!out){free(list);return 0;}
        size_t w=0;
        for(size_t i=0;i<count;i++){
            const uint32_t sz=list[i].size;const unsigned len=7u+sz;
            out[w+0]=0xff;out[w+1]=0xf1;
            out[w+2]=(uint8_t)(0x40u|(inf.rate_index<<2)|((inf.channel_config>>2)&1u));
            out[w+3]=(uint8_t)(((inf.channel_config&3u)<<6)|((len>>11)&3u));
            out[w+4]=(uint8_t)(len>>3);
            out[w+5]=(uint8_t)(((len&7u)<<5)|0x1fu);
            out[w+6]=0xfc;
            memcpy(out+w+7,data+list[i].off,sz);
            w+=len;
        }
        free(list);
        if(w!=(size_t)total){free(out);continue;}
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

/* ---- indeks probek (strumieniowo), patrz sauti_aac_mp4.h: ta sama logika co sauti_aac_mp4_to_adts, na tablicach z moov i pojedynczych moof ---- */
struct SautiAacMp4Index {
    SautiAacMp4Alloc a;
    Sample *list; size_t n,cap;
    uint32_t track_id,trex_size;
    int has_mvex;
};
static int idx_add(SautiAacMp4Index *x,uint64_t off,uint32_t size){
    if(x->n>=MAX_SAMPLES)return 0;
    if(x->n==x->cap){
        const size_t nc=x->cap?x->cap*2u:4096u;
        Sample *t=(Sample *)x->a.alloc(x->a.user,nc*sizeof(*t));
        if(!t)return 0;
        if(x->n)memcpy(t,x->list,x->n*sizeof(*t));
        if(x->list)x->a.free(x->a.user,x->list);
        x->list=t;x->cap=nc;
    }
    x->list[x->n].off=off;x->list[x->n].size=size;x->n++;
    return 1;
}
void sauti_aac_mp4_index_free(SautiAacMp4Index *x){
    if(!x)return;
    SautiAacMp4Alloc a=x->a;
    if(x->list)a.free(a.user,x->list);
    a.free(a.user,x);
}
uint64_t sauti_aac_mp4_index_count(const SautiAacMp4Index *x){return x?x->n:0u;}
void sauti_aac_mp4_index_forget_units(SautiAacMp4Index *x){if(x)x->n=0;}
int sauti_aac_mp4_index_has_fragments(const SautiAacMp4Index *x){return x&&x->has_mvex;}
int sauti_aac_mp4_index_get(const SautiAacMp4Index *x,uint64_t i,uint64_t *offset,uint32_t *size){
    if(!x||i>=x->n)return 0;
    *offset=x->list[i].off;*size=x->list[i].size;
    return 1;
}
SautiAacMp4Index *sauti_aac_mp4_index_new(const SautiAacMp4Alloc *a,const uint8_t *moov_data,size_t moov_size,SautiAacMp4Info *info){
    if(!a||!a->alloc||!a->free||!moov_data||!info)return NULL;
    View moov={moov_data,moov_size};
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
        size_t ep=8u;Box e;View ent={stsd.p,stsd.n};
        if(!next_box(ent,&ep,&e)||e.type!=FOURCC('m','p','4','a'))continue;
        const size_t base=e.payload+8u;
        if(e.end<base+20u)continue;
        const unsigned ver=be16(stsd.p+base);
        size_t extra=ver==1u?16u:(ver==2u?36u:0u);
        const size_t children=base+20u+extra;
        if(children>e.end)continue;
        View esds_parent={stsd.p+children,e.end-children},esds;
        uint8_t asc[256];size_t asc_len=0;
        int have_esds=find_child(esds_parent,FOURCC('e','s','d','s'),&esds);
        if(!have_esds){
            View wave;
            have_esds=find_child(esds_parent,FOURCC('w','a','v','e'),&wave)&&find_child(wave,FOURCC('e','s','d','s'),&esds);
        }
        if(!have_esds||!parse_esds(esds,asc,&asc_len))continue;
        SautiAacMp4Info inf;memset(&inf,0,sizeof(inf));
        if(!parse_asc(asc,asc_len,&inf))continue;
        SautiAacMp4Index *x=(SautiAacMp4Index *)a->alloc(a->user,sizeof(*x));
        if(!x)return NULL;
        memset(x,0,sizeof(*x));x->a=*a;
        {   /* tablice probek stbl */
            View stsz,stsc,stco;int co64=0;
            uint32_t fixed=0,count=0,field=32;int have_sizes=0;View table={NULL,0};
            if(find_child(stbl,FOURCC('s','t','s','z'),&stsz)&&stsz.n>=12u){
                fixed=be32(stsz.p+4);count=be32(stsz.p+8);table.p=stsz.p+12;table.n=stsz.n-12u;have_sizes=1;
                if(!fixed&&(size_t)count>table.n/4u)have_sizes=0;
            }else if(find_child(stbl,FOURCC('s','t','z','2'),&stsz)&&stsz.n>=12u){
                field=stsz.p[7];count=be32(stsz.p+8);table.p=stsz.p+12;table.n=stsz.n-12u;
                have_sizes=(field==4u||field==8u||field==16u)&&(size_t)count<=table.n*8u/field;
            }
            if(have_sizes&&find_child(stbl,FOURCC('s','t','s','c'),&stsc)&&
               (find_child(stbl,FOURCC('s','t','c','o'),&stco)||(find_child(stbl,FOURCC('c','o','6','4'),&stco)&&(co64=1)))&&
               stsc.n>=8u&&stco.n>=8u){
                const uint32_t stsc_n=be32(stsc.p+4),chunks=be32(stco.p+4);
                const size_t esz=co64?8u:4u;
                if(count>0u&&count<=MAX_SAMPLES&&
                   stsc_n>0u&&(size_t)stsc_n<=(stsc.n-8u)/12u&&chunks>0u&&(size_t)chunks<=(stco.n-8u)/esz){
                    uint32_t sample=0;int ok=1;
                    for(uint32_t g=0;g<stsc_n&&ok&&sample<count;g++){
                        const uint32_t first=be32(stsc.p+8+(size_t)g*12u),per=be32(stsc.p+12+(size_t)g*12u);
                        const uint32_t last=(g+1u<stsc_n)?be32(stsc.p+8+(size_t)(g+1u)*12u):chunks+1u;
                        if(first<1u||last<first||last>chunks+1u||per==0u){ok=0;break;}
                        for(uint32_t c=first;c<last&&ok&&sample<count;c++){
                            uint64_t off=co64?be64(stco.p+8+(size_t)(c-1u)*8u):be32(stco.p+8+(size_t)(c-1u)*4u);
                            for(uint32_t s2=0;s2<per&&sample<count;s2++,sample++){
                                uint32_t sz=fixed;
                                if(!fixed){
                                    if(field==32u)sz=be32(table.p+(size_t)sample*4u);
                                    else if(field==16u)sz=be16(table.p+(size_t)sample*2u);
                                    else if(field==8u)sz=table.p[sample];
                                    else sz=(sample&1u)?(table.p[sample/2u]&15u):(table.p[sample/2u]>>4);
                                }
                                if(!idx_add(x,off,sz)){ok=0;break;}
                                off+=sz;
                            }
                        }
                    }
                    if(!ok||sample!=count){x->n=0;}
                }
            }
        }
        {   View tkhd,mvex;
            if(find_child(trak,FOURCC('t','k','h','d'),&tkhd)&&tkhd.n>=24u)
                x->track_id=tkhd.p[0]==1u?(tkhd.n>=28u?be32(tkhd.p+20):0u):be32(tkhd.p+12);
            if(find_child(moov,FOURCC('m','v','e','x'),&mvex)){
                x->has_mvex=1;
                size_t tp=0;Box tb;
                while(next_box(mvex,&tp,&tb))
                    if(tb.type==FOURCC('t','r','e','x')&&tb.end-tb.payload>=24u&&be32(mvex.p+tb.payload+4)==x->track_id)
                        x->trex_size=be32(mvex.p+tb.payload+16);
            }
        }
        /* lista edycji i iTunSMPB */
        {   View edts,elst;
            if(find_child(trak,FOURCC('e','d','t','s'),&edts)&&find_child(edts,FOURCC('e','l','s','t'),&elst)&&elst.n>=8u&&media_timescale){
                const uint32_t n=be32(elst.p+4);const unsigned v=elst.p[0];const size_t es=v==1u?20u:12u;
                for(uint32_t i=0;i<n&&(size_t)(i+1u)*es<=elst.n-8u;i++){
                    const uint8_t *q=elst.p+8+(size_t)i*es;
                    const uint64_t seg=v==1u?be64(q):be32(q);
                    const int64_t mt=v==1u?(int64_t)be64(q+8):(int32_t)be32(q+4);
                    if(mt<0)continue;
                    inf.skip_samples=(uint64_t)mt*inf.sample_rate/media_timescale;
                    if(seg&&movie_timescale)inf.duration_samples=seg*inf.sample_rate/movie_timescale;
                    break;
                }
            }
            if(!inf.skip_samples&&!inf.duration_samples)gapless_parse(moov,&inf);
        }
        *info=inf;
        return x;
    }
    return NULL;
}
int sauti_aac_mp4_index_add_moof(SautiAacMp4Index *x,uint64_t box_start,const uint8_t *payload,size_t payload_size){
    if(!x||!payload||!x->track_id)return 0;
    const size_t before=x->n;
    View moof={payload,payload_size};
    size_t mp=0;Box tf;
    while(next_box(moof,&mp,&tf)){
        if(tf.type!=FOURCC('t','r','a','f'))continue;
        View traf={moof.p+tf.payload,tf.end-tf.payload};
        size_t hp=0;Box hb;uint64_t base=box_start,cursor=0;int have_cursor=0;
        uint32_t def_size=x->trex_size;int mine=0;
        while(next_box(traf,&hp,&hb)){
            const uint8_t *q=traf.p+hb.payload;const size_t qn=hb.end-hb.payload;
            if(hb.type==FOURCC('t','f','h','d')&&qn>=8u){
                const uint32_t flags=be32(q)&0xffffffu;size_t o=8u;
                if(be32(q+4)!=x->track_id)break;
                mine=1;
                if(flags&0x1u){if(qn<o+8u)break;base=be64(q+o);o+=8u;}
                if(flags&0x2u)o+=4u;
                if(flags&0x8u)o+=4u;
                if(flags&0x10u){if(qn<o+4u)break;def_size=be32(q+o);o+=4u;}
            }else if(hb.type==FOURCC('t','r','u','n')&&mine&&qn>=8u){
                const uint32_t flags=be32(q)&0xffffffu,cnt=be32(q+4);size_t o=8u;
                if(flags&0x1u){if(qn<o+4u)break;cursor=(uint64_t)((int64_t)base+(int32_t)be32(q+o));have_cursor=1;o+=4u;}
                else if(!have_cursor){cursor=base;have_cursor=1;}
                if(flags&0x4u)o+=4u;
                const size_t per=((flags&0x100u)?4u:0u)+((flags&0x200u)?4u:0u)+((flags&0x400u)?4u:0u)+((flags&0x800u)?4u:0u);
                if(per&&(size_t)cnt>(qn-o)/per)break;
                for(uint32_t i=0;i<cnt;i++){
                    const uint8_t *r=q+o+(size_t)i*per;size_t ro=0;
                    if(flags&0x100u)ro+=4u;
                    uint32_t sz=def_size;
                    if(flags&0x200u){sz=be32(r+ro);}
                    if(!idx_add(x,cursor,sz)){x->n=before;return 0;}
                    cursor+=sz;
                }
            }
        }
    }
    return 1;
}
