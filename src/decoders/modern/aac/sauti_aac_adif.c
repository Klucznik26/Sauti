/* ADIF -> ADTS dla AAC (patrz sauti_aac_adif.h).  Kazdy odczyt jest sprawdzany wzgledem rozmiaru danych; zadnych wskaznikow poza bufor. */
#include "sauti_aac_adif.h"
#include "sauti_aac_cpe.h"
#include "sauti_aac_lc_frame.h"
#include "sauti_aac_syntax.h"
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
static const unsigned rates[13]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};

typedef struct { unsigned object_type,rate_index,front,front_cpe,other; } Pce;
/* program_config_element(); `*b` wyrownywane do bajtu wzgledem poczatku danych (poczatek naglowka ADIF) */
static int parse_pce(Br *b,Pce *p){
    uint32_t tag=0,obj=0,sfi=0,nf=0,ns=0,nb=0,nl=0,na=0,nc=0,f=0,t=0,x=0;
    memset(p,0,sizeof(*p));
    if(!get(b,4,&tag)||!get(b,2,&obj)||!get(b,4,&sfi)||!get(b,4,&nf)||!get(b,4,&ns)||!get(b,4,&nb)||!get(b,2,&nl)||!get(b,3,&na)||!get(b,4,&nc))return 0;
    for(unsigned k=0;k<3u;k++){                                 /* mono_mixdown, stereo_mixdown (flaga + 4 bity), matrix_mixdown (flaga + 3 bity) */
        if(!get(b,1,&f))return 0;
        if(f&&!skip(b,k<2u?4u:3u))return 0;
    }
    p->object_type=obj;p->rate_index=sfi;p->front=nf;
    for(uint32_t i=0;i<nf;i++){if(!get(b,1,&f)||!get(b,4,&t))return 0;if(i==0u)p->front_cpe=f;}
    for(uint32_t i=0;i<ns+nb;i++){if(!get(b,1,&f)||!get(b,4,&t))return 0;}
    for(uint32_t i=0;i<nl;i++){if(!get(b,4,&t))return 0;}
    for(uint32_t i=0;i<na;i++){if(!get(b,4,&t))return 0;}
    for(uint32_t i=0;i<nc;i++){if(!get(b,1,&f)||!get(b,4,&t))return 0;}
    p->other=ns+nb+nl+na+nc;
    while(b->pos&7u){if(!get(b,1,&x))return 0;}               /* byte_alignment() */
    if(!get(b,8,&x)||!skip(b,(uint64_t)x*8u))return 0;        /* comment_field_bytes + komentarz */
    return 1;
}
int sauti_aac_adif_probe(const uint8_t *data,size_t size){
    return data&&size>=8u&&memcmp(data,"ADIF",4)==0;
}
/* Dlugosc jednego raw_data_block() w bajtach (od `raw`, w danych o dlugosci `length`); 0 = blad albo ucięty. */
static size_t block_length(const uint8_t *raw,size_t length,unsigned rate_index,unsigned channels){
    static SautiAacCpeData cpe;
    static SautiAacLcMonoData mono;
    size_t pos=0;
    int channel_element=0;
    for(;;){
        Br b={raw,length*8u,pos};
        uint32_t id=0;
        if(!get(&b,3,&id))return 0;
        if(id==7u){                                              /* ID_END + byte_alignment() */
            pos=b.pos;
            pos=(pos+7u)&~(size_t)7u;
            return pos<=length*8u?pos/8u:0u;
        }
        SautiAacReason reason=SAUTI_AAC_REASON_OK;
        if(id==6u){                                              /* FIL: dowolne rozszerzenie (takze SBR), pomijane */
            SautiAacFillInfo fi;size_t q=b.pos;
            if(!sauti_aac_fill_parse(raw,length,&q,&fi))return 0;      /* q = koniec ladunku */
            pos=q;continue;
        }
        if(id==4u){                                              /* DSE: tag, flaga wyrownania, dlugosc (+ rozszerzenie), dane */
            uint32_t align=0,cnt=0,esc=0;
            if(!skip(&b,4)||!get(&b,1,&align)||!get(&b,8,&cnt))return 0;
            if(cnt==255u){if(!get(&b,8,&esc))return 0;cnt+=esc;}
            if(align)while(b.pos&7u)b.pos++;
            if(!skip(&b,(uint64_t)cnt*8u))return 0;
            pos=b.pos;continue;
        }
        if(channel_element)return 0;                             /* tylko jeden element czolowy w bloku */
        if(id==0u&&channels==1u){
            size_t q=pos;
            if(sauti_aac_lc_element_parse(raw,length,&q,rate_index,0u,&mono,&reason)!=1)return 0;
            pos=q;channel_element=1;continue;
        }
        if(id==1u&&channels==2u){
            size_t q=pos;
            if(sauti_aac_lc_cpe_parse(raw,length,&q,rate_index,&cpe,&reason)!=1)return 0;
            pos=q;channel_element=1;continue;
        }
        return 0;                                                /* LFE, CCE, PCE w strumieniu, element niezgodny z PCE */
    }
}
int sauti_aac_adif_to_adts(const uint8_t *data,size_t size,uint8_t **adts,size_t *adts_length,SautiAacAdifInfo *info){
    if(!data||!adts||!adts_length||!info||!sauti_aac_adif_probe(data,size))return 0;
    *adts=NULL;*adts_length=0;
    Br b={data,size*8u,32u};
    uint32_t v=0,type=0,rate=0,npce=0;
    if(!get(&b,1,&v))return 0;
    if(v&&!skip(&b,72))return 0;                                 /* copyright_id */
    if(!skip(&b,2)||!get(&b,1,&type)||!get(&b,23,&rate)||!get(&b,4,&npce))return 0;
    if(npce!=0u)return 0;                                        /* jeden program */
    if(type==0u&&!skip(&b,20))return 0;                          /* adif_buffer_fullness */
    Pce pce;
    if(!parse_pce(&b,&pce))return 0;
    if(pce.object_type!=1u||pce.rate_index>12u||pce.front!=1u||pce.other!=0u)return 0;
    const unsigned channels=pce.front_cpe?2u:1u;
    size_t pos=b.pos/8u;
    size_t cap=size+size/4u+4096u,w=0;
    uint8_t *out=malloc(cap);
    if(!out)return 0;
    uint64_t frames=0;
    while(pos<size){
        const size_t len=block_length(data+pos,size-pos,pce.rate_index,channels);
        if(!len)break;                                           /* koniec danych albo ucięty ostatni blok */
        if(len>8184u){free(out);return 0;}
        if(w+7u+len>cap){
            cap=cap*2u+len;
            uint8_t *t=realloc(out,cap);
            if(!t){free(out);return 0;}
            out=t;
        }
        const unsigned l=7u+(unsigned)len;
        out[w+0]=0xff;out[w+1]=0xf1;
        out[w+2]=(uint8_t)(0x40u|(pce.rate_index<<2)|((channels>>2)&1u));
        out[w+3]=(uint8_t)(((channels&3u)<<6)|((l>>11)&3u));
        out[w+4]=(uint8_t)(l>>3);
        out[w+5]=(uint8_t)(((l&7u)<<5)|0x1fu);
        out[w+6]=0xfc;
        memcpy(out+w+7,data+pos,len);
        w+=l;pos+=len;frames++;
    }
    if(!frames){free(out);return 0;}
    memset(info,0,sizeof(*info));
    info->rate_index=pce.rate_index;info->sample_rate=rates[pce.rate_index];info->channel_config=channels;info->bitstream_type=type;info->bitrate=rate;info->frames=frames;
    *adts=out;*adts_length=w;
    return 1;
}
