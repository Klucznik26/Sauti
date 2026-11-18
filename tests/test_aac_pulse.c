/* Tool pulse AAC-LC: skladnia pulse_data() i zastosowanie amplitud (ISO/IEC 14496-3 4.4.2.7).  Roznicowy test z FFmpeg: `make test-aac-pulse-diff`. */
#include "sauti_aac_lc_frame.h"
#include "sauti_aac_bands.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t b[64]; size_t bits; } Bits;
static void put(Bits *b,unsigned width,unsigned value){
    while(width--){ b->b[b->bits/8]|=(uint8_t)(((value>>width)&1u)<<(7-b->bits%8)); b->bits++; }
}
static void pulse_bits(Bits *b,unsigned count,unsigned start,const unsigned *off,const unsigned *amp){
    memset(b,0,sizeof(*b));
    put(b,2,count-1u);put(b,6,start);
    for(unsigned i=0;i<count;i++){put(b,5,off[i]);put(b,4,amp[i]);}
}
int main(void){
    SautiAacIcsInfo ics={0};ics.window_sequence=0;ics.max_sfb=20;ics.window_count=1;ics.group_count=1;ics.group_length[0]=1;
    const uint16_t *offs=NULL;unsigned bands=0;
    assert(sauti_aac_band_offsets(4,0,&offs,&bands));
    /* parser: pola i granice */
    {
        const unsigned off[4]={31,0,17,5},amp[4]={15,0,9,1};
        Bits b;pulse_bits(&b,4,63,off,amp);
        SautiAacPulseData p;size_t pos=0;
        assert(sauti_aac_pulse_parse(b.b,sizeof(b.b),&pos,&ics,&p)==1);
        assert(pos==2+6+4*9 && p.count==4 && p.start_sfb==63);
        for(unsigned i=0;i<4;i++)assert(p.offset[i]==off[i] && p.amp[i]==amp[i]);
        const size_t before=pos=0;
        SautiAacPulseData keep;memset(&keep,0x5a,sizeof(keep));
        assert(sauti_aac_pulse_parse(b.b,5,&pos,&ics,&keep)==0);       /* ucieta ramka: bez zmiany wyniku i pozycji */
        assert(pos==before && keep.count==0x5a5a5a5au);
        SautiAacIcsInfo shortw=ics;shortw.window_sequence=2;
        pos=0;assert(sauti_aac_pulse_parse(b.b,sizeof(b.b),&pos,&shortw,&p)==0);   /* okna krotkie: bez pulsacji */
        assert(sauti_aac_pulse_parse(NULL,0,&pos,&ics,&p)==0);
    }
    /* zastosowanie: znak, kumulacja pozycji, pasma pominiete */
    {
        SautiAacScaleFactors f;memset(&f,0,sizeof(f));
        for(unsigned i=0;i<20;i++)f.codebook[0][i]=1;
        f.codebook[0][7]=13;                                           /* PNS */
        f.codebook[0][8]=0;                                            /* zerowe */
        int32_t q[1024];memset(q,0,sizeof(q));
        SautiAacPulseData p;memset(&p,0,sizeof(p));
        p.count=3;p.start_sfb=4;p.offset[0]=2;p.offset[1]=1;p.offset[2]=3;p.amp[0]=5;p.amp[1]=7;p.amp[2]=2;
        q[offs[4]+2]=4;q[offs[4]+3]=-4;                                // pierwsza > 0: +amp, druga < 0: -amp, trzecia = 0: -amp
        assert(sauti_aac_lc_pulse_apply(q,4,&ics,&f,&p)==1);
        assert(q[offs[4]+2]==4+5);
        assert(q[offs[4]+3]==-4-7);
        assert(q[offs[4]+6]==0-2);
        unsigned changed=0;for(unsigned i=0;i<1024;i++)if(q[i]!=0)changed++;
        assert(changed==3);
        /* pasmo PNS i zerowe: pulsacja pominieta, zwrot 1 */
        memset(q,0,sizeof(q));
        p.count=1;p.start_sfb=7;p.offset[0]=0;p.amp[0]=9;
        assert(sauti_aac_lc_pulse_apply(q,4,&ics,&f,&p)==1);
        p.start_sfb=8;assert(sauti_aac_lc_pulse_apply(q,4,&ics,&f,&p)==1);
        for(unsigned i=0;i<1024;i++)assert(q[i]==0);
        /* poza zakodowanymi pasmami (>= max_sfb): pominieta */
        p.start_sfb=21;p.offset[0]=1;assert(sauti_aac_lc_pulse_apply(q,4,&ics,&f,&p)==1);
        for(unsigned i=0;i<1024;i++)assert(q[i]==0);
        /* poza zakresem: start_sfb >= liczby pasm albo pozycja >= 1024; wspolczynniki bez zmian */
        p.start_sfb=(uint32_t)bands;assert(sauti_aac_lc_pulse_apply(q,4,&ics,&f,&p)==0);
        p.start_sfb=bands-1u;p.count=4;p.offset[0]=31;p.offset[1]=31;p.offset[2]=31;p.offset[3]=31;
        assert(offs[bands-1u]+124u>=1024u);                            /* ostatnia pulsacja wypada poza 1024 */
        assert(sauti_aac_lc_pulse_apply(q,4,&ics,&f,&p)==0);
        for(unsigned i=0;i<1024;i++)assert(q[i]==0);
        SautiAacIcsInfo shortw=ics;shortw.window_sequence=2;
        p.count=1;p.start_sfb=4;p.offset[0]=0;assert(sauti_aac_lc_pulse_apply(q,4,&shortw,&f,&p)==0);
        p.count=0;assert(sauti_aac_lc_pulse_apply(q,4,&ics,&f,&p)==0);
        p.count=5;assert(sauti_aac_lc_pulse_apply(q,4,&ics,&f,&p)==0);
        assert(sauti_aac_lc_pulse_apply(NULL,4,&ics,&f,&p)==0);
    }
    puts("PASS AAC pulse: skladnia, znak amplitudy, kumulacja pozycji, pasma pominiete, granice");
    return 0;
}
