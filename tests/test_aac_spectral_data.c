#include "sauti_aac_spectral_data.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_tables.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {uint8_t bytes[256];size_t bit;} Bits;
static void put(Bits *b,unsigned width,uint32_t code){
    assert(width<=32 && b->bit+width<=sizeof(b->bytes)*8u);
    for(unsigned j=width;j>0;j--){
        if((code>>(j-1u))&1u)
            b->bytes[b->bit/8u]|=(uint8_t)(1u<<(7u-b->bit%8u));
        b->bit++;
    }
}
static void symbol(Bits *b,unsigned book,unsigned index){
    uint32_t code;uint8_t width;
    assert(sauti_aac_standard_codeword(book,index,&code,&width));
    put(b,width,code);
}
static void add(SautiAacSections *s,unsigned g,unsigned cb,unsigned a,unsigned z){
    SautiAacSection *p=&s->sections[s->section_count++];
    p->group=(uint8_t)g;p->codebook=(uint8_t)cb;
    p->first_sfb=(uint8_t)a;p->end_sfb=(uint8_t)z;
}
static void bands(void){
    for(unsigned short_win=0;short_win<2;short_win++)
        for(unsigned rate=0;rate<13;rate++){
            const uint16_t *off=NULL;unsigned n=0;
            assert(sauti_aac_band_offsets(rate,short_win,&off,&n));
            assert(n>0 && off[0]==0);
            assert(off[n]==(short_win?128u:1024u));
            for(unsigned j=0;j<n;j++)
                assert(off[j]<off[j+1] && (off[j+1]-off[j])%4u==0);
        }
    const uint16_t *off=NULL;unsigned n=0;
    assert(!sauti_aac_band_offsets(13,0,&off,&n));
    assert(!sauti_aac_band_offsets(4,2,&off,&n));
}
static void long_window(void){
    SautiAacIcsInfo ics={0};ics.window_count=1;
    ics.group_count=1;ics.group_length[0]=1;ics.max_sfb=3;
    SautiAacSections s={0};s.group_count=1;s.max_sfb=3;
    add(&s,0,1,0,1);add(&s,0,0,1,2);add(&s,0,5,2,3);
    Bits b={0};put(&b,3,5);
    symbol(&b,1,80); /* 4 coefficients, each +1 */
    symbol(&b,5,80); /* 2 coefficients +4,+4 */
    int32_t spectrum[1024];for(unsigned j=0;j<1024;j++)spectrum[j]=123;
    size_t pos=3;
    assert(sauti_aac_lc_spectral_data_parse(b.bytes,sizeof(b.bytes),
                      &pos,4,&ics,&s,spectrum));
    assert(pos==b.bit);
    for(unsigned j=0;j<4;j++)assert(spectrum[j]==1);
    for(unsigned j=4;j<8;j++)assert(spectrum[j]==0);
    assert(spectrum[8]==4 && spectrum[9]==4);
    for(unsigned j=10;j<1024;j++)assert(spectrum[j]==0);
    int32_t saved[1024];memcpy(saved,spectrum,sizeof(saved));
    pos=3;
    assert(!sauti_aac_lc_spectral_data_parse(b.bytes,1,&pos,
                                      4,&ics,&s,spectrum));
    assert(pos==3 && memcmp(saved,spectrum,sizeof(saved))==0);
    SautiAacSections bad=s;bad.sections[1].first_sfb=0;
    assert(!sauti_aac_lc_spectral_data_parse(b.bytes,sizeof(b.bytes),&pos,
                                         4,&ics,&bad,spectrum));
    assert(pos==3 && memcmp(saved,spectrum,sizeof(saved))==0);
    assert(!sauti_aac_lc_spectral_data_parse(b.bytes,sizeof(b.bytes),&pos,
                                         13,&ics,&s,spectrum));
}
static void short_grouped(void){
    SautiAacIcsInfo ics={0};ics.window_sequence=2;ics.window_count=8;
    ics.group_count=2;ics.group_length[0]=3;ics.group_length[1]=5;
    ics.max_sfb=2;
    SautiAacSections s={0};s.group_count=2;s.max_sfb=2;
    add(&s,0,1,0,1);add(&s,0,13,1,2);
    add(&s,1,0,0,1);add(&s,1,1,1,2);
    Bits b={0};
    for(unsigned w=0;w<3;w++)symbol(&b,1,80);
    for(unsigned w=3;w<8;w++)symbol(&b,1,0);
    int32_t spectrum[1024]={0};size_t pos=0;
    assert(sauti_aac_lc_spectral_data_parse(b.bytes,sizeof(b.bytes),&pos,
                                      4,&ics,&s,spectrum));
    assert(pos==b.bit);
    for(unsigned w=0;w<8;w++)
        for(unsigned j=0;j<128;j++){
            int32_t wanted=w<3 && j<4 ? 1 :
                          w>=3 && j>=4 && j<8 ? -1 : 0;
            assert(spectrum[w*128+j]==wanted);
        }
}
int main(void){
    bands();long_window();short_grouped();
    puts("PASS AAC-LC spectral_data: SFB 1024/128, groups, Huffman, rollback");
    return 0;
}
