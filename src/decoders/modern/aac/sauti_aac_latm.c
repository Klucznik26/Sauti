/* LOAS/LATM -> ADTS dla AAC (patrz sauti_aac_latm.h).  Kazdy odczyt jest sprawdzany wzgledem rozmiaru elementu; zadnych wskaznikow poza bufor. */
#include "sauti_aac_latm.h"
#include "sauti_aac_er.h"
#include <stdlib.h>
#include <string.h>

typedef struct { const uint8_t *p; size_t nbits,pos; } Br;
static int get(Br *b,unsigned n,uint32_t *v){
    if(n>32u||b->nbits-b->pos<n)return 0;
    uint32_t r=0;
    for(unsigned i=0;i<n;i++){
        const size_t at=b->pos+i;
        r=(r<<1)|(uint32_t)((b->p[at>>3]>>(7u-(unsigned)(at&7u)))&1u);
    }
    b->pos+=n;*v=r;
    return 1;
}
static int skip(Br *b,uint64_t n){
    if(n>b->nbits-b->pos)return 0;
    b->pos+=(size_t)n;return 1;
}
/* LatmGetValue(): 2 bity = liczba bajtow - 1, potem tyle bajtow */
static int latm_value(Br *b,uint32_t *v){
    uint32_t bytes=0,x=0;
    if(!get(b,2,&bytes))return 0;
    *v=0;
    for(uint32_t i=0;i<=bytes;i++){if(!get(b,8,&x))return 0;*v=(*v<<8)|x;}
    return 1;
}
static const unsigned rates[13]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};

typedef struct {
    unsigned version;
    unsigned aot,rate_index,sample_rate,channel_config,sbr,ps;
    unsigned frame480;                                   /* AAC-LD / ELD (aot 23 / 39): ramka 480 zamiast 512 */
    unsigned sub_frames;
    unsigned other_data;
    uint64_t other_bits;
    int have;
} Config;
static int config_equal(const Config *a,const Config *b){
    return a->version==b->version&&a->aot==b->aot&&a->rate_index==b->rate_index&&a->channel_config==b->channel_config&&
           a->sbr==b->sbr&&a->ps==b->ps&&a->frame480==b->frame480&&a->sub_frames==b->sub_frames&&a->other_data==b->other_data&&a->other_bits==b->other_bits;
}
static int get_aot(Br *b,unsigned *aot){
    uint32_t v=0,e=0;
    if(!get(b,5,&v))return 0;
    if(v==31u){if(!get(b,6,&e))return 0;v=32u+e;}
    *aot=v;return 1;
}
static int get_rate(Br *b,unsigned *index,unsigned *rate){
    uint32_t v=0,r=0;
    if(!get(b,4,&v))return 0;
    *index=v;
    if(v==15u){if(!get(b,24,&r))return 0;*rate=r;return 1;}
    if(v>12u)return 0;
    *rate=rates[v];return 1;
}
/* AudioSpecificConfig do konca (w tym GASpecificConfig i rozszerzenie SBR/PS zgodne wstecz).  Zwraca 1 i wypelnia c; 0 = nieobslugiwane albo blad. */
/* AAC-LD (23) / AAC-ELD (39) bez SBR: konfiguracja wg sauti_aac_er_asc_parse na skopiowanych, wyrownanych bitach; zwraca 1 i przesuwa czytnik o jej dlugosc */
static int parse_asc_er(Br *b,Config *c,unsigned aot){
    uint8_t buf[64];SautiAacErAsc er;size_t i;
    const size_t avail=b->nbits-b->pos;
    const size_t bits=avail<sizeof(buf)*8u?avail:sizeof(buf)*8u;
    memset(buf,0,sizeof(buf));
    { Br t=*b;for(i=0;i<bits;i++){uint32_t v=0;if(!get(&t,1,&v))return 0;if(v)buf[i>>3]|=(uint8_t)(0x80u>>(i&7u));} }
    if(sauti_aac_er_asc_parse(buf,(bits+7u)/8u,&er)!=1||er.ld_sbr||er.bits>bits||er.channel_config<1u||er.channel_config>2u)return 0;
    if(!skip(b,er.bits))return 0;
    c->aot=aot;c->rate_index=er.rate_index;c->sample_rate=rates[er.rate_index];c->channel_config=er.channel_config;c->frame480=er.frame480;c->sbr=c->ps=0;
    return 1;
}
static int parse_asc(Br *b,Config *c){
    unsigned aot=0,ext_index=0,ext_rate=0;
    c->frame480=0;
    { Br peek=*b;unsigned a=0;if(get_aot(&peek,&a)&&(a==23u||a==39u))return parse_asc_er(b,c,a); }
    if(!get_aot(b,&aot)||!get_rate(b,&c->rate_index,&c->sample_rate))return 0;
    if(c->rate_index>12u)return 0;                       /* czestotliwosc jawna: poza tablica ADTS */
    uint32_t chan=0,flag=0;
    if(!get(b,4,&chan))return 0;
    c->channel_config=chan;c->sbr=c->ps=0;
    if(aot==5u||aot==29u){
        c->sbr=1u;c->ps=aot==29u;
        if(!get_rate(b,&ext_index,&ext_rate)||!get_aot(b,&aot))return 0;
        if(aot==22u&&!get(b,4,&flag))return 0;
    }
    if(aot!=2u)return 0;                                 /* tylko AAC-LC (rdzen) */
    c->aot=aot;
    /* GASpecificConfig */
    if(!get(b,1,&flag)||flag)return 0;                   /* frameLengthFlag 1 = 960 */
    if(!get(b,1,&flag))return 0;
    if(flag&&!skip(b,14))return 0;                       /* dependsOnCoreCoder: coreCoderDelay */
    if(!get(b,1,&flag)||flag)return 0;                   /* extensionFlag: nie dla AOT 2 */
    if(c->channel_config==0u||c->channel_config>7u)return 0;   /* PCE w ASC: nieobslugiwane */
    /* rozszerzenie zgodne wstecz: sync 0x2B7, AOT 5, flaga SBR (+ czestotliwosc), sync 0x548, flaga PS */
    if(!c->sbr){
        Br t=*b;uint32_t sync=0,ext_aot=0,sbr=0;
        if(get(&t,11,&sync)&&sync==0x2b7u&&get(&t,5,&ext_aot)&&ext_aot==5u&&get(&t,1,&sbr)){
            c->sbr=sbr;
            if(sbr){
                if(!get_rate(&t,&ext_index,&ext_rate))return 0;
                const size_t save=t.pos;uint32_t s2=0,psf=0;
                if(get(&t,11,&s2)&&s2==0x548u&&get(&t,1,&psf))c->ps=psf;
                else t.pos=save;                                  /* bez PS: drugi sync nie jest czescia ASC */
            }
            *b=t;
        }
    }
    return 1;
}
static int parse_stream_mux_config(Br *b,Config *c){
    uint32_t v=0,a=0,tmp=0,same=0,subs=0,prog=0,layer=0,ftype=0,other=0,crc=0;
    memset(c,0,sizeof(*c));
    if(!get(b,1,&v))return 0;
    c->version=v;
    if(v==1u){if(!get(b,1,&a)||a)return 0;}              /* audioMuxVersionA != 0: zarezerwowane */
    if(v==1u&&!latm_value(b,&tmp))return 0;              /* taraBufferFullness */
    if(!get(b,1,&same)||!same)return 0;                  /* allStreamsSameTimeFraming musi byc 1 */
    if(!get(b,6,&subs)||!get(b,4,&prog)||prog!=0u||!get(b,3,&layer)||layer!=0u)return 0;
    c->sub_frames=subs;
    if(v==0u){if(!parse_asc(b,c))return 0;}
    else{
        uint32_t len=0;
        if(!latm_value(b,&len))return 0;
        const size_t start=b->pos;
        if(!parse_asc(b,c))return 0;
        const size_t used=b->pos-start;
        if(used>len||!skip(b,len-used))return 0;         /* fillBits */
    }
    if(!get(b,3,&ftype)||ftype!=0u||!get(b,8,&tmp))return 0;     /* frameLengthType 0, latmBufferFullness */
    if(!get(b,1,&other))return 0;
    c->other_data=other;
    if(other){
        if(v==1u){uint32_t n=0;if(!latm_value(b,&n))return 0;c->other_bits=n;}
        else{
            uint32_t esc=0,x=0;uint64_t n=0;
            do{if(!get(b,1,&esc)||!get(b,8,&x))return 0;n=(n<<8)|x;if(n>(1u<<24))return 0;}while(esc);
            c->other_bits=n;
        }
    }
    if(!get(b,1,&crc))return 0;
    if(crc&&!get(b,8,&tmp))return 0;
    c->have=1;
    return 1;
}
int sauti_aac_latm_probe(const uint8_t *data,size_t size){
    if(!data||size<6u||data[0]!=0x56u||(data[1]&0xe0u)!=0xe0u)return 0;
    const size_t len=(size_t)((data[1]&0x1fu)<<8)|data[2];
    if(!len||3u+len>size)return 0;
    if(3u+len==size)return 1;
    return size>=3u+len+2u&&data[3u+len]==0x56u&&(data[3u+len+1u]&0xe0u)==0xe0u;
}
int sauti_aac_latm_to_adts(const uint8_t *data,size_t size,uint8_t **adts,size_t *adts_length,SautiAacLatmInfo *info){
    if(!data||!adts||!adts_length||!info||!sauti_aac_latm_probe(data,size))return 0;
    *adts=NULL;*adts_length=0;
    Config cfg;memset(&cfg,0,sizeof(cfg));
    size_t cap=size+size/4u+4096u,w=0;
    uint8_t *out=malloc(cap);
    if(!out)return 0;
    uint64_t frames=0;size_t pos=0;
    while(pos+3u<=size){
        if(data[pos]!=0x56u||(data[pos+1u]&0xe0u)!=0xe0u){pos++;continue;}     /* utrata synchronizacji: szukaj kolejnej */
        const size_t len=(size_t)((data[pos+1u]&0x1fu)<<8)|data[pos+2u];
        if(!len||pos+3u+len>size){break;}                                       /* ucięty ostatni element */
        Br b={data+pos+3u,len*8u,0};
        pos+=3u+len;
        uint32_t same=0;
        if(!get(&b,1,&same))continue;
        if(!same){
            Config c;
            if(!parse_stream_mux_config(&b,&c)){if(!cfg.have){continue;}free(out);return 0;}
            if(!cfg.have)cfg=c;
            else if(!config_equal(&cfg,&c)){free(out);return 0;}                 /* zmiana konfiguracji w trakcie: nieobslugiwane */
        }else if(!cfg.have)continue;                                            /* poczatek strumienia przed pierwsza konfiguracja */
        int ok=1;
        for(unsigned sf=0;sf<=cfg.sub_frames&&ok;sf++){
            uint32_t slot=0,x=255;
            while(x==255u){if(!get(&b,8,&x)){ok=0;break;}slot+=x;}
            if(!ok||!slot||slot>8184u||b.nbits-b.pos<(size_t)slot*8u){ok=0;break;}
            if(w+7u+slot>cap){
                cap=cap*2u+slot;
                uint8_t *t=realloc(out,cap);
                if(!t){free(out);return 0;}
                out=t;
            }
            const unsigned l=7u+slot;
            out[w+0]=0xff;out[w+1]=0xf1;
            out[w+2]=(uint8_t)(0x40u|(cfg.rate_index<<2)|((cfg.channel_config>>2)&1u));
            out[w+3]=(uint8_t)(((cfg.channel_config&3u)<<6)|((l>>11)&3u));
            out[w+4]=(uint8_t)(l>>3);
            out[w+5]=(uint8_t)(((l&7u)<<5)|0x1fu);
            out[w+6]=0xfc;
            for(uint32_t i=0;i<slot;i++){uint32_t byte=0;get(&b,8,&byte);out[w+7u+i]=(uint8_t)byte;}   /* przesuniecie bitowe do granicy bajtu */
            w+=l;frames++;
        }
        if(!ok){free(out);return 0;}
    }
    if(!frames||!cfg.have||cfg.aot==23u||cfg.aot==39u){free(out);return 0;}                  /* AAC-LD / ELD: tylko sciezka strumieniowa silnika (jednostki nie sa ADTS) */
    memset(info,0,sizeof(*info));
    info->aot=cfg.aot;info->rate_index=cfg.rate_index;info->sample_rate=cfg.sample_rate;info->channel_config=cfg.channel_config;
    info->sbr=cfg.sbr;info->ps=cfg.ps;info->frames=frames;
    *adts=out;*adts_length=w;
    return 1;
}

/* ---- strumieniowo: jeden element LOAS naraz (patrz sauti_aac_latm.h); ta sama logika co petla w sauti_aac_latm_to_adts ---- */
typedef char latm_state_size_check[sizeof(Config)<=sizeof(SautiAacLatmState)?1:-1];
void sauti_aac_latm_state_init(SautiAacLatmState *st){memset(st,0,sizeof(*st));}
int sauti_aac_latm_convert_element(SautiAacLatmState *st,const uint8_t *payload,size_t len,uint8_t *out,size_t cap,uint16_t *lens,unsigned max_frames,unsigned *n){
    Config *cfg=(Config *)st;
    Br b={payload,len*8u,0};
    uint32_t same=0;
    size_t w=0;
    *n=0;
    if(!payload||!out||!lens||!len)return 0;
    if(!get(&b,1,&same))return 0;
    if(!same){
        Config c;
        if(!parse_stream_mux_config(&b,&c))return cfg->have?-1:0;
        if(!cfg->have)*cfg=c;
        else if(!config_equal(cfg,&c))return -2;
    }else if(!cfg->have)return 0;
    if(cfg->sub_frames+1u>max_frames)return -1;
    for(unsigned sf=0;sf<=cfg->sub_frames;sf++){
        uint32_t slot=0,x=255;
        while(x==255u){if(!get(&b,8,&x))return -1;slot+=x;}
        if(!slot||slot>8184u||b.nbits-b.pos<(size_t)slot*8u)return -1;
        const unsigned l=7u+slot;
        if(w+l>cap)return -1;
        if(cfg->aot==23u||cfg->aot==39u)(void)sauti_aac_er_wrap_header(out+w,cfg->aot,cfg->rate_index,cfg->channel_config,cfg->frame480,slot);     /* AAC-LD / ELD: naglowek wewnetrzny silnika */
        else{
        out[w+0]=0xff;out[w+1]=0xf1;
        out[w+2]=(uint8_t)(0x40u|(cfg->rate_index<<2)|((cfg->channel_config>>2)&1u));
        out[w+3]=(uint8_t)(((cfg->channel_config&3u)<<6)|((l>>11)&3u));
        out[w+4]=(uint8_t)(l>>3);
        out[w+5]=(uint8_t)(((l&7u)<<5)|0x1fu);
        out[w+6]=0xfc;
        }
        for(uint32_t i=0;i<slot;i++){uint32_t byte=0;get(&b,8,&byte);out[w+7u+i]=(uint8_t)byte;}
        lens[sf]=(uint16_t)l;
        w+=l;
    }
    *n=cfg->sub_frames+1u;
    return 1;
}
