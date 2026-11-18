/* Narzedzie testowe (nie produkt): generuje WLASNORECZNIE zbudowane strumienie ADTS AAC-LC z pulse_data (mono SCE i stereo CPE bez common_window) do ROZNICOWEGO testu z FFmpeg
 * (tools/aac_corpus_matrix.py --file).  Widmo jest zerowe (kodowane cb 1), wiec PCM pochodzi wylacznie z pulsacji: q=0 + amp daje -amp (reguła specyfikacji dla q<=0).
 * Uzycie: gen_aac_pulse_fixture <mono|stereo> <wyjscie.aac> */
#include "sauti_aac_bands.h"
#include "sauti_aac_tables.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t b[256]; size_t bits; } Bits;
static void put(Bits *b,unsigned width,unsigned value){
    assert(b->bits+width<=sizeof(b->b)*8u);
    while(width--){ b->b[b->bits/8]|=(uint8_t)(((value>>width)&1u)<<(7-b->bits%8)); b->bits++; }
}
static void symbol(Bits *b,unsigned book,unsigned index){
    uint32_t value=0;uint8_t width=0;
    assert(sauti_aac_standard_codeword(book,index,&value,&width));
    put(b,width,value);
}
typedef struct { unsigned count,start,off[4],amp[4]; } Pulse;
enum { RATE=4, NBANDS=24 };                                   /* indeks 4 = 44100 Hz */
/* jeden kanal: global_gain, ICS (ONLY_LONG, max_sfb=NBANDS, jesli own_ics), sekcja cb 1 na wszystkie pasma (opcjonalnie: pasma >= noise_from jako PNS cb 13), scalefactory, pulse, widmo zerowe */
static void channel(Bits *b,unsigned own_ics,const Pulse *p,unsigned noise_from,unsigned gain){
    const uint16_t *offs=NULL;unsigned bands=0;
    assert(sauti_aac_band_offsets(RATE,0,&offs,&bands) && bands>=NBANDS);
    put(b,8,gain);
    if(own_ics){ put(b,1,0);put(b,2,0);put(b,1,0);put(b,6,NBANDS);put(b,1,0); }
    if(noise_from<NBANDS){
        put(b,4,1);put(b,5,noise_from);                       /* sekcja cb 1: pasma 0..noise_from-1 */
        put(b,4,13);put(b,5,NBANDS-noise_from);               /* sekcja PNS */
    }else{ put(b,4,1);put(b,5,NBANDS); }
    for(unsigned band=0;band<NBANDS;band++){
        if(band<noise_from)symbol(b,0,60);                    /* delta scalefactora 0 */
        else if(band==noise_from)put(b,9,256);                /* pierwsza energia szumu: delta 0 */
        else symbol(b,0,60);
    }
    if(p){ put(b,1,1);put(b,2,p->count-1u);put(b,6,p->start);
           for(unsigned i=0;i<p->count;i++){put(b,5,p->off[i]);put(b,4,p->amp[i]);} }
    else put(b,1,0);
    put(b,1,0);put(b,1,0);                                    /* tns, gain control */
    for(unsigned q=0;q<offs[noise_from<NBANDS?noise_from:NBANDS]/4u;q++)symbol(b,1,40);   /* zerowe kwadruplety cb 1 */
}
static size_t frame(uint8_t *out,int stereo,const Pulse *p0,const Pulse *p1,unsigned noise_from){
    Bits raw={0};
    if(stereo){
        put(&raw,3,1);put(&raw,4,0);put(&raw,1,0);            /* CPE, common_window=0 */
        channel(&raw,1,p0,noise_from,150);channel(&raw,1,p1,noise_from,152);
    }else{
        put(&raw,3,0);put(&raw,4,0);
        channel(&raw,1,p0,noise_from,150);
    }
    put(&raw,3,7);while(raw.bits%8u)put(&raw,1,0);
    const size_t length=7u+raw.bits/8u;
    memset(out,0,length);
    out[0]=0xff;out[1]=0xf1;out[2]=(uint8_t)(0x40u|(RATE<<2)|(stereo?0u:0u));
    out[3]=(uint8_t)((stereo?0x80u:0x40u)|((length>>11u)&3u));
    out[4]=(uint8_t)(length>>3u);out[5]=(uint8_t)(((length&7u)<<5u)|0x1fu);out[6]=0xfcu;
    memcpy(out+7,raw.b,raw.bits/8u);
    return length;
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    const int stereo=!strcmp(argv[1],"stereo");
    FILE *f=fopen(argv[2],"wb");
    if(!f)return 2;
    const Pulse list[]={
        {1,4,{2},{3}},                     /* jedna pulsacja */
        {2,3,{1,5},{15,7}},                /* dwie, kumulacja pozycji */
        {4,2,{0,3,9,17},{1,2,3,4}},        /* cztery */
        {3,10,{4,4,6},{8,8,8}},            /* kolejne pozycje w roznych pasmach */
        {1,0,{0},{15}},                    /* pozycja 0, maksymalna amplituda */
        {2,20,{1,2},{9,9}},                /* gorne pasma */
    };
    uint8_t buf[512];
    for(unsigned round=0;round<2;round++){                    /* round 1: pasma >= 12 jako PNS (pulsacje tam sa pomijane) */
        const unsigned noise_from=round?12u:NBANDS;
        for(unsigned i=0;i<sizeof list/sizeof list[0];i++){
            const size_t n=frame(buf,stereo,&list[i],&list[(i+2)%(sizeof list/sizeof list[0])],noise_from);
            fwrite(buf,1,n,f);
        }
    }
    for(unsigned i=0;i<3;i++){                                  /* ramki bez pulsacji: domkniecie nakladania */
        const size_t n=frame(buf,stereo,NULL,NULL,NBANDS);
        fwrite(buf,1,n,f);
    }
    fclose(f);
    return 0;
}
