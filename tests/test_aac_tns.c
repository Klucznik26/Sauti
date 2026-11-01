#include "sauti_aac_tns.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sauti_aac_bands.h"
typedef struct{uint8_t bytes[64];size_t bit;}Bits;
static void writebits(Bits *b,unsigned width,unsigned value){
    assert(b->bit+width<=sizeof(b->bytes)*8u);
    while(width--){
        b->bytes[b->bit/8u]|=(uint8_t)(((value>>width)&1u)
                                              <<(7u-b->bit%8u));
        b->bit++;
    }
}
static void run(unsigned shortwin){
    SautiAacIcsInfo ics={0};
    ics.window_sequence=shortwin?2u:0u;
    ics.window_count=shortwin?8u:1u;
    ics.group_count=1;ics.group_length[0]=ics.window_count;
    Bits b={0};
    writebits(&b,shortwin?1:2,1); /* one filter in window 0 */
    writebits(&b,1,1);             /* 4-bit base resolution */
    writebits(&b,shortwin?4:6,2); /* filter length */
    writebits(&b,shortwin?3:5,2); /* filter order */
    writebits(&b,1,1);             /* backwards */
    writebits(&b,1,1);             /* compressed -> 3-bit */
    writebits(&b,3,7);             /* -1 */
    writebits(&b,3,2);             /* +2 */
    for(unsigned win=1;win<ics.window_count;win++)
        writebits(&b,1,0);
    SautiAacTnsData out;memset(&out,0x55,sizeof(out));
    size_t pos=0;
    assert(sauti_aac_tns_parse(b.bytes,(b.bit+7)/8,&pos,&ics,&out));
    assert(pos==b.bit);
    assert(out.filter_count[0]==1);
    assert(out.filter[0][0].length==2);
    assert(out.filter[0][0].order==2);
    assert(out.filter[0][0].direction==1);
    assert(out.filter[0][0].resolution==4);
    assert(out.filter[0][0].compress==1);
    assert(out.filter[0][0].coefficients[0]==-1);
    assert(out.filter[0][0].coefficients[1]==2);
    for(unsigned win=1;win<ics.window_count;win++)
        assert(out.filter_count[win]==0);
    SautiAacTnsData saved=out;
    size_t prior=pos;
    pos=0;
    assert(!sauti_aac_tns_parse(b.bytes,0,&pos,&ics,&out));
    assert(pos==0 && memcmp(&out,&saved,sizeof(out))==0);
    pos=prior;
    ics.window_count=2;
    assert(!sauti_aac_tns_parse(b.bytes,(b.bit+7)/8,&pos,&ics,&out));
    assert(pos==prior && memcmp(&out,&saved,sizeof(out))==0);
}
static void invalid_order(void){
    Bits b={0};
    writebits(&b,2,1);writebits(&b,1,0);
    writebits(&b,6,3);writebits(&b,5,31);
    SautiAacIcsInfo ics={0};ics.window_sequence=0;ics.window_count=1;
    SautiAacTnsData out={0};
    size_t pos=0;
    assert(!sauti_aac_tns_parse(b.bytes,(b.bit+7)/8,&pos,&ics,&out));
    assert(pos==0);
}
static void inverse_filter(unsigned shortwin,unsigned reverse){
    SautiAacIcsInfo ics={0};
    ics.window_sequence=shortwin?2u:0u;
    ics.window_count=shortwin?8u:1u;
    ics.max_sfb=shortwin?12u:41u;
    SautiAacTnsData tns={0};
    tns.filter_count[0]=1;
    SautiAacTnsFilter *f=&tns.filter[0][0];
    f->length=(uint8_t)ics.max_sfb;
    f->order=1;
    f->resolution=4;
    f->coefficients[0]=1;
    f->direction=(uint8_t)reverse;
    const uint16_t *offsets=NULL;
    unsigned bands=0;
    assert(sauti_aac_band_offsets(4,shortwin,&offsets,&bands));
    (void)bands;
    const unsigned cap=shortwin?9u:31u;
    unsigned width=offsets[cap];
    assert(width>=3);
    float spectrum[1024]={0};
    unsigned first=reverse?width-1u:0u;
    unsigned next=reverse?first-1u:first+1u;
    unsigned third=reverse?first-2u:first+2u;
    spectrum[first]=1.0f;
    uint32_t marker=0;
    /* Independent first-order inverse LPC reference. */
    const double pi=3.14159265358979323846264338327950288;
    const double reflection=sin((pi/2.0)/7.5);
    assert(sauti_aac_tns_apply(&tns,&ics,4,spectrum));
    assert(fabs(spectrum[first]-1.0f)<1e-6f);
    assert(fabs(spectrum[next]+reflection)<1e-6f);
    assert(fabs(spectrum[third]-reflection*reflection)<1e-6f);
    assert(spectrum[width]==0);
    float saved[1024];
    memcpy(saved,spectrum,sizeof(saved));
    f->resolution=2; /* invalid resolution */
    assert(!sauti_aac_tns_apply(&tns,&ics,4,spectrum));
    assert(memcmp(saved,spectrum,sizeof(saved))==0);
    f->resolution=4;
    f->order=21; /* larger than AAC long max */
    assert(!sauti_aac_tns_apply(&tns,&ics,4,spectrum));
    assert(memcmp(saved,spectrum,sizeof(saved))==0);
    f->order=1;
    assert(!sauti_aac_tns_apply(&tns,&ics,13,spectrum));
    assert(memcmp(saved,spectrum,sizeof(saved))==0);
    marker++;
    assert(marker==1);
}
int main(void){
    run(0);run(1);invalid_order();
    inverse_filter(0,0);inverse_filter(0,1);
    inverse_filter(1,0);inverse_filter(1,1);
    puts("PASS AAC-24/25 TNS: syntax, inverse LPC, directions, long/short, rollback");
    return 0;
}
