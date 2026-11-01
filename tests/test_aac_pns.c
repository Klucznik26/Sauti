#include "sauti_aac_pns.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void run(unsigned shortwin){
    SautiAacIcsInfo ics={0};
    ics.window_sequence=shortwin?2u:0u;
    ics.window_count=shortwin?8u:1u;
    ics.group_count=shortwin?2u:1u;
    ics.group_length[0]=shortwin?3u:1u;
    ics.group_length[1]=shortwin?5u:0u;
    ics.max_sfb=2;
    SautiAacSections sec={0};
    sec.group_count=ics.group_count;sec.max_sfb=2;
    SautiAacScaleFactors sf={0};
    for(unsigned g=0;g<ics.group_count;g++){
        SautiAacSection *a=&sec.sections[sec.section_count++];
        a->group=(uint8_t)g;a->codebook=13;a->first_sfb=0;a->end_sfb=1;
        SautiAacSection *b=&sec.sections[sec.section_count++];
        b->group=(uint8_t)g;b->codebook=0;b->first_sfb=1;b->end_sfb=2;
        sf.codebook[g][0]=13;sf.value[g][0]=100;
    }
    float x[1024]={0},y[1024]={0};
    uint32_t seed=0x12345678u,second=seed;
    assert(sauti_aac_pns_apply(&ics,4,&sec,&sf,&seed,x));
    assert(sauti_aac_pns_apply(&ics,4,&sec,&sf,&second,y));
    assert(seed==second && memcmp(x,y,sizeof(x))==0);
    double aggregate=0;
    for(unsigned w=0;w<ics.window_count;w++){
        double energy=0;
        unsigned base=w*(shortwin?128u:1024u);
        for(unsigned i=0;i<4;i++)energy+=(double)x[base+i]*x[base+i];
        assert(fabs(energy-1.0)<0.00001);
        aggregate+=energy;
        for(unsigned i=4;i<(shortwin?128u:1024u);i++)
            assert(x[base+i]==0);
    }
    assert(fabs(aggregate-ics.window_count)<0.0001);
    float prior[1024];memcpy(prior,x,sizeof(x));
    uint32_t old=seed;
    sf.codebook[0][0]=14;
    assert(!sauti_aac_pns_apply(&ics,4,&sec,&sf,&seed,x));
    assert(seed==old && memcmp(x,prior,sizeof(x))==0);
    sf.codebook[0][0]=13;
    sf.value[0][0]=800;
    assert(!sauti_aac_pns_apply(&ics,4,&sec,&sf,&seed,x));
    assert(seed==old && memcmp(x,prior,sizeof(x))==0);
    assert(!sauti_aac_pns_apply(&ics,13,&sec,&sf,&seed,x));
    assert(seed==old && memcmp(x,prior,sizeof(x))==0);
}
int main(void){
    run(0);run(1);
    puts("PASS AAC-19 PNS: normalized energy, short grouping, RNG continuity, rollback");
    return 0;
}
