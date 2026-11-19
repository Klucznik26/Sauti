/* AAC-LC wielokanalowy (sauti_aac_lc_multi): mapy kanalow, petla elementow (SCE/CPE/LFE/DSE/PCE/FIL/END), odrzucenia fail-closed i atomowosc.  Ramki skladane bit po bicie z elementow o max_sfb = 0 (cisza),
 * wiec test jest hermetyczny; zgodnosc PCM z FFmpeg sprawdza `make test-aac-multi-diff`. */
#include "sauti_aac_lc_multi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t b[512]; size_t bits; } Bits;
static void put(Bits *b,unsigned width,unsigned value){
    while(width--){ b->b[b->bits/8]|=(uint8_t)(((value>>width)&1u)<<(7-b->bits%8)); b->bits++; }
}
static void ics_body(Bits *b){                      /* global_gain..gain_control: jedno ICS bez pasm */
    put(b,8,100); put(b,1,0); put(b,2,0); put(b,1,0); put(b,6,0); put(b,1,0); /* gain, reserved, ONLY_LONG, SIN, max_sfb 0, predictor */
    put(b,1,0); put(b,1,0); put(b,1,0);                                         /* pulse, tns, gain control */
}
static void sce(Bits *b,unsigned id){ put(b,3,id); put(b,4,0); ics_body(b); }
static void cpe(Bits *b){ put(b,3,1); put(b,4,0); put(b,1,0); ics_body(b); ics_body(b); }
static void dse(Bits *b,unsigned bytes){ put(b,3,4); put(b,4,0); put(b,1,1); put(b,8,bytes); while(b->bits%8)put(b,1,0); for(unsigned i=0;i<bytes;i++)put(b,8,0xA5); }
static void pce(Bits *b){                           /* 1 front (SCE), 0 side/back/lfe, bez mixdownow, komentarz 3 bajty */
    put(b,3,5); put(b,4,0); put(b,2,1); put(b,4,4); put(b,4,1); put(b,4,0); put(b,4,0); put(b,2,0); put(b,3,0); put(b,4,0);
    put(b,1,0); put(b,1,0); put(b,1,0);
    put(b,1,0); put(b,4,0);
    while(b->bits%8)put(b,1,0);
    put(b,8,3); put(b,24,0x414243u);
}
static void fil(Bits *b){ put(b,3,6); put(b,4,2); put(b,4,0); put(b,4,0); put(b,8,0); } /* ID_FIL, 2 bajty: EXT_FILL (typ 0, nibble 0) + bajt zer */
static size_t finish(Bits *b,uint8_t *frame,unsigned config,int crc){
    put(b,3,7); while(b->bits%8)put(b,1,0);
    const size_t hdr=crc?9u:7u,total=hdr+b->bits/8;
    memset(frame,0,total);
    frame[0]=0xff; frame[1]=(uint8_t)(0xf0|(crc?0:1));
    frame[2]=(uint8_t)(0x40|(4u<<2)|((config>>2)&1u));
    frame[3]=(uint8_t)(((config&3u)<<6)|((total>>11)&3u));
    frame[4]=(uint8_t)(total>>3); frame[5]=(uint8_t)(((total&7u)<<5)|0x1fu); frame[6]=0xfc;
    memcpy(frame+hdr,b->b,b->bits/8);
    return total;
}
/* PCE z dowolnym ukladem: kolejno przod, bok, tyl (maska bitow: bit i = element i jest CPE), liczba LFE, liczba CCE */
static void pce_layout(Bits *b,unsigned nf,unsigned cf,unsigned ns,unsigned cs,unsigned nb,unsigned cb,unsigned nl,unsigned ncc){
    put(b,3,5); put(b,4,0); put(b,2,1); put(b,4,4); put(b,4,nf); put(b,4,ns); put(b,4,nb); put(b,2,nl); put(b,3,0); put(b,4,ncc);
    put(b,1,0); put(b,1,0); put(b,1,0);
    for(unsigned i=0;i<nf;i++){put(b,1,(cf>>i)&1u);put(b,4,i);}
    for(unsigned i=0;i<ns;i++){put(b,1,(cs>>i)&1u);put(b,4,i);}
    for(unsigned i=0;i<nb;i++){put(b,1,(cb>>i)&1u);put(b,4,i);}
    for(unsigned i=0;i<nl;i++)put(b,4,i);
    for(unsigned i=0;i<ncc;i++){put(b,1,0);put(b,4,i);}
    while(b->bits%8)put(b,1,0);
    put(b,8,0);
}
static int scan(const Bits *src,unsigned config,SautiAacLcLayout *l){
    Bits b=*src; uint8_t frame[600];
    const size_t n=finish(&b,frame,config,0);
    return sauti_aac_lc_multi_scan_pce(frame,n,l);
}
static int run(unsigned config,const Bits *src,SautiAacLcMulti *st,float *pcm,SautiAacReason *rs,int crc){
    Bits b=*src; uint8_t frame[600];
    const size_t n=finish(&b,frame,config,crc);
    return sauti_aac_lc_adts_multi_decode_ex(frame,n,st,pcm,rs);
}
int main(void){
    /* konfiguracje i mapy */
    static const unsigned want[8]={0,1,2,3,4,5,6,8};
    for(unsigned c=0;c<8;c++)assert(sauti_aac_lc_multi_config_channels(c)==want[c]);
    assert(sauti_aac_lc_multi_config_channels(8)==0 && sauti_aac_lc_multi_config_channels(15)==0);
    uint8_t map[8];
    for(unsigned c=1;c<=7;c++){
        assert(sauti_aac_lc_multi_output_map(c,map));
        unsigned seen=0;
        for(unsigned i=0;i<want[c];i++){ assert(map[i]<want[c]); seen|=1u<<map[i]; }
        assert(seen==(1u<<want[c])-1u);               /* permutacja */
    }
    assert(!sauti_aac_lc_multi_output_map(0,map) && !sauti_aac_lc_multi_output_map(8,map) && !sauti_aac_lc_multi_output_map(6,NULL));
    assert(sauti_aac_lc_multi_output_map(6,map) && map[0]==1 && map[1]==2 && map[2]==0 && map[3]==5 && map[4]==3 && map[5]==4); /* 5.1: FL FR FC LFE BL BR */
    SautiAacLcMulti st; assert(!sauti_aac_lc_multi_init(&st,0,0) && !sauti_aac_lc_multi_init(&st,8,0) && !sauti_aac_lc_multi_init(NULL,3,0) && !sauti_aac_lc_multi_init(&st,3,2));
    float pcm[8*1024],before[8*1024]; SautiAacReason rs;
    /* 3.0 = SCE + CPE, 5.1 = SCE + CPE + CPE + LFE: cisza, poprawna liczba kanalow */
    Bits b; memset(&b,0,sizeof b); sce(&b,0); cpe(&b);
    assert(sauti_aac_lc_multi_init(&st,3,0)); for(unsigned i=0;i<3072;i++)pcm[i]=7.0f;
    assert(run(3,&b,&st,pcm,&rs,0)==1 && rs==SAUTI_AAC_REASON_OK);
    for(unsigned i=0;i<3072;i++)assert(pcm[i]==0.0f);
    memset(&b,0,sizeof b); sce(&b,0); cpe(&b); cpe(&b); sce(&b,3);
    assert(sauti_aac_lc_multi_init(&st,6,0)); for(unsigned i=0;i<6144;i++)pcm[i]=7.0f;
    assert(run(6,&b,&st,pcm,&rs,0)==1);
    for(unsigned i=0;i<6144;i++)assert(pcm[i]==0.0f);
    /* DSE, PCE i FIL pomijane w dowolnym miejscu */
    memset(&b,0,sizeof b); pce(&b); sce(&b,0); dse(&b,5); fil(&b); cpe(&b); dse(&b,0);
    assert(sauti_aac_lc_multi_init(&st,3,0)); assert(run(3,&b,&st,pcm,&rs,0)==1);
    /* niezgodnosc liczby kanalow: za malo / za duzo elementow / zla konfiguracja: nic sie nie zmienia */
    memset(&b,0,sizeof b); sce(&b,0);
    assert(sauti_aac_lc_multi_init(&st,3,0)); SautiAacLcMulti saved=st; memset(pcm,0x5a,sizeof pcm); memcpy(before,pcm,sizeof pcm);
    assert(run(3,&b,&st,pcm,&rs,0)==0 && memcmp(&saved,&st,sizeof st)==0 && memcmp(before,pcm,sizeof pcm)==0);
    memset(&b,0,sizeof b); sce(&b,0); cpe(&b); sce(&b,0);
    assert(run(3,&b,&st,pcm,&rs,0)==0 && memcmp(&saved,&st,sizeof st)==0 && memcmp(before,pcm,sizeof pcm)==0);
    memset(&b,0,sizeof b); sce(&b,0); cpe(&b);
    assert(run(4,&b,&st,pcm,&rs,0)==-1 && rs==SAUTI_AAC_REASON_UNSUPPORTED_ADTS && memcmp(&saved,&st,sizeof st)==0);   /* ADTS mowi 4, stan ma 3 */
    /* CCE (id 2) i CRC w ADTS: jawnie nieobslugiwane */
    Bits cc; memset(&cc,0,sizeof cc); sce(&cc,0); put(&cc,3,2);
    assert(run(3,&cc,&st,pcm,&rs,0)==-1 && rs==SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT && memcmp(&saved,&st,sizeof st)==0);
    assert(run(3,&b,&st,pcm,&rs,1)==-1 && rs==SAUTI_AAC_REASON_UNSUPPORTED_ADTS && memcmp(&saved,&st,sizeof st)==0);
    /* ucieta ramka i DSE dluzszy niz ramka */
    { Bits t=b; uint8_t frame[600]; size_t n=finish(&t,frame,3,0);
      assert(sauti_aac_lc_adts_multi_decode_ex(frame,n-1,&st,pcm,&rs)==0 && memcmp(&saved,&st,sizeof st)==0); }
    memset(&b,0,sizeof b); sce(&b,0); put(&b,3,4); put(&b,4,0); put(&b,1,0); put(&b,8,200);
    assert(run(3,&b,&st,pcm,&rs,0)==0 && memcmp(&saved,&st,sizeof st)==0);
    /* stan niezainicjalizowany i bledne argumenty */
    SautiAacLcMulti zero; memset(&zero,0,sizeof zero); zero.channels=3; zero.config=3;
    memset(&b,0,sizeof b); sce(&b,0); cpe(&b);
    assert(run(3,&b,&zero,pcm,&rs,0)==-1 && rs==SAUTI_AAC_REASON_UNSUPPORTED_HISTORY);
    assert(sauti_aac_lc_adts_multi_decode_ex(NULL,0,&st,pcm,&rs)==0 && sauti_aac_lc_adts_multi_decode_ex((const uint8_t *)"",0,&st,pcm,NULL)==0);
    /* channel_configuration 0: uklad z PCE */
    {
        SautiAacLcLayout l; Bits p;
        memset(&p,0,sizeof p); pce_layout(&p,2,0x2,0,0,1,0x1,1,0);                  /* przod SCE+CPE, tyl CPE, LFE = 5.1 */
        assert(scan(&p,0,&l)==1 && l.channels==6);
        static const uint8_t m51[6]={1,2,0,5,3,4};
        assert(memcmp(l.map,m51,6)==0);
        memset(&p,0,sizeof p); pce_layout(&p,2,0x2,0,0,2,0x3,1,0);                  /* przod SCE+CPE, DWIE pary tylne, LFE (fdk 7.1) */
        assert(scan(&p,0,&l)==1 && l.channels==8);
        static const uint8_t m71[8]={1,2,0,7,5,6,3,4};                              /* pierwsza para tylna = SL SR, druga = BL BR */
        assert(memcmp(l.map,m71,8)==0);
        memset(&p,0,sizeof p); pce_layout(&p,2,0x2,1,0x1,1,0x1,1,0);                /* przod SCE+CPE, bok CPE, tyl CPE, LFE */
        assert(scan(&p,0,&l)==1 && l.channels==8);
        static const uint8_t m71b[8]={1,2,0,7,5,6,3,4};
        assert(memcmp(l.map,m71b,8)==0);
        memset(&p,0,sizeof p); pce_layout(&p,1,0x1,0,0,0,0,0,0);                    /* sam stereo przod */
        assert(scan(&p,0,&l)==1 && l.channels==2 && l.map[0]==0 && l.map[1]==1);
        /* odrzucenia: CCE, dwa SCE przodu, ponad 8 kanalow, brak PCE, PCE przy konfiguracji != 0 */
        memset(&p,0,sizeof p); pce_layout(&p,1,0x1,0,0,0,0,0,1);
        assert(scan(&p,0,&l)==-1);
        memset(&p,0,sizeof p); pce_layout(&p,2,0x0,0,0,0,0,0,0);
        assert(scan(&p,0,&l)==-1);
        memset(&p,0,sizeof p); pce_layout(&p,3,0x7,0,0,0,0,0,0);
        assert(scan(&p,0,&l)==-1);
        memset(&p,0,sizeof p); sce(&p,0);
        assert(scan(&p,0,&l)==0);
        memset(&p,0,sizeof p); pce_layout(&p,1,0x1,0,0,0,0,0,0);
        assert(scan(&p,2,&l)==0);                                                     /* ADTS mowi stereo, nie PCE */
        /* dekodowanie ramki z PCE: stan z ukladu, cisza w poprawnej liczbie kanalow */
        SautiAacLcMulti st; SautiAacLcLayout l6;
        memset(&p,0,sizeof p); pce_layout(&p,2,0x2,0,0,1,0x1,1,0); assert(scan(&p,0,&l6)==1);
        sce(&p,0); cpe(&p); cpe(&p); sce(&p,3);
        assert(sauti_aac_lc_multi_init_layout(&st,&l6,0) && st.config==0 && st.channels==6);
        for(unsigned i=0;i<6144;i++)pcm[i]=7.0f;
        assert(run(0,&p,&st,pcm,&rs,0)==1);
        for(unsigned i=0;i<6144;i++)assert(pcm[i]==0.0f);
        assert(run(3,&p,&st,pcm,&rs,0)==-1);                                          /* ADTS 3 przy stanie z PCE */
        SautiAacLcLayout bad=l6; bad.channels=9;
        assert(!sauti_aac_lc_multi_init_layout(&st,&bad,0) && !sauti_aac_lc_multi_init_layout(&st,NULL,0));
    }
    puts("PASS AAC-LC multi: mapy, SCE/CPE/LFE, DSE/PCE/FIL, uklad z PCE (config 0), niezgodnosc kanalow, CCE i CRC odrzucone, atomowosc");
    return 0;
}
