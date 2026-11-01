#include "sauti_aac_pns.h"
#include "sauti_aac_bands.h"
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

/* AAC-29B: each independent PNS SFB/window must have exactly its
 * signaled spectral energy; changing gain scales amplitude, not shape.
 * The RNG must advance once per generated spectral coefficient. */
static void energy_matrix(unsigned shortwin){
    SautiAacIcsInfo ics={0};
    ics.window_sequence=shortwin?2u:0u;
    ics.window_count=shortwin?8u:1u;
    ics.group_count=shortwin?2u:1u;
    ics.group_length[0]=shortwin?3u:1u;
    if(shortwin)ics.group_length[1]=5u;
    ics.max_sfb=3u;
    SautiAacSections sec={0};
    sec.group_count=ics.group_count;
    sec.max_sfb=3u;
    SautiAacScaleFactors sf={0};
    for(unsigned g=0;g<ics.group_count;g++){
        SautiAacSection *part=&sec.sections[sec.section_count++];
        part->group=(uint8_t)g;
        part->first_sfb=0;part->end_sfb=2;part->codebook=13;
        part=&sec.sections[sec.section_count++];
        part->group=(uint8_t)g;
        part->first_sfb=2;part->end_sfb=3;part->codebook=0;
        sf.codebook[g][0]=13;sf.value[g][0]=96+(int32_t)(g*4u);
        sf.codebook[g][1]=13;sf.value[g][1]=104+(int32_t)(g*4u);
        sf.codebook[g][2]=0;
    }
    const uint16_t *offsets=NULL;
    unsigned bands=0;
    assert(sauti_aac_band_offsets(4,shortwin,&offsets,&bands));
    assert(bands>=3);
    float original[1024]={0},a[1024]={0},b[1024]={0};
    for(unsigned i=0;i<1024;i++)original[i]=a[i]=b[i]=0.25f;
    uint32_t first=0x1f2e3d4cu,second=first;
    assert(sauti_aac_pns_apply(&ics,4,&sec,&sf,&first,a));
    assert(sauti_aac_pns_apply(&ics,4,&sec,&sf,&second,b));
    assert(first==second);
    assert(memcmp(a,b,sizeof(a))==0);
    /* FFmpeg float NOISE_BT applies a negative spectral gain.
     * First LCG sample from 0x1f2e3d4c is positive: output must
     * therefore be negative, regardless of long/short window mode. */
    assert(a[0]<0.0f);
    unsigned start_window=0;
    uint32_t expected=0x1f2e3d4cu;
    for(unsigned g=0;g<ics.group_count;g++){
        for(unsigned band=0;band<2;band++){
            const unsigned from=offsets[band],to=offsets[band+1u];
            const double amplitude=exp2(((double)sf.value[g][band]-100.0)/4.0);
            for(unsigned w=start_window;w<start_window+ics.group_length[g];w++){
                const unsigned base=w*(shortwin?128u:1024u);
                double energy=0;
                for(unsigned i=from;i<to;i++)
                    energy+=(double)a[base+i]*a[base+i];
                assert(fabs(energy-amplitude*amplitude)<
                       0.00003*(1.0+amplitude*amplitude));
                for(unsigned i=from;i<to;i++)
                    expected=expected*1664525u+1013904223u;
            }
        }
        start_window+=ics.group_length[g];
    }
    assert(first==expected && start_window==ics.window_count);
    for(unsigned w=0;w<ics.window_count;w++){
        const unsigned base=w*(shortwin?128u:1024u);
        for(unsigned i=offsets[2];i<(shortwin?128u:1024u);i++)
            assert(a[base+i]==original[base+i]);
    }
    /* A different initial seed must change noise, but preserve its energy. */
    uint32_t changed=0x76543210u;
    float other[1024];memcpy(other,original,sizeof(other));
    assert(sauti_aac_pns_apply(&ics,4,&sec,&sf,&changed,other));
    assert(memcmp(other,a,sizeof(a))!=0);
    /* A rejected gain leaves all 1024 lines and the seed intact. */
    sf.value[0][0]=513;
    float saved[1024];memcpy(saved,a,sizeof(saved));
    uint32_t prior=first;
    assert(!sauti_aac_pns_apply(&ics,4,&sec,&sf,&first,a));
    assert(first==prior && memcmp(a,saved,sizeof(a))==0);
}

static void continuous_stereo_stream(void){
    SautiAacIcsInfo ics={0};
    ics.window_sequence=0;ics.window_count=1;
    ics.group_count=1;ics.group_length[0]=1;ics.max_sfb=1;
    SautiAacSections sec={0};
    sec.group_count=1;sec.max_sfb=1;sec.section_count=1;
    sec.sections[0].codebook=13;
    sec.sections[0].end_sfb=1;
    SautiAacScaleFactors sf={0};
    sf.codebook[0][0]=13;sf.value[0][0]=100;
    float left[1024]={0},right[1024]={0};
    uint32_t seed=0x1f2e3d4cu;
    assert(sauti_aac_pns_apply(&ics,4,&sec,&sf,&seed,left));
    const uint32_t after_left=seed;
    assert(sauti_aac_pns_apply(&ics,4,&sec,&sf,&seed,right));
    assert(seed!=after_left);
    const uint16_t *off=NULL;unsigned bands=0;
    assert(sauti_aac_band_offsets(4,0,&off,&bands));
    double dot=0,le=0,re=0;
    for(unsigned i=off[0];i<off[1];i++){
        dot+=(double)left[i]*right[i];
        le+=(double)left[i]*left[i];
        re+=(double)right[i]*right[i];
    }
    assert(fabs(le-1.0)<1e-5 && fabs(re-1.0)<1e-5);
    assert(fabs(dot)<0.999); /* not accidentally identical noise */
}
int main(void){
    run(0);run(1);
    energy_matrix(0);energy_matrix(1);
    continuous_stereo_stream();
    puts("PASS AAC-19/29B PNS: multi-band energies, grouping, RNG, stereo independence, rollback");
    return 0;
}
