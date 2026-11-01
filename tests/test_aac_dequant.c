#include "sauti_aac_dequant.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(float a,float b){
    return fabsf(a-b)<=1e-4f*fmaxf(1.0f,fabsf(b));
}
static void section(SautiAacSections *s,unsigned g,unsigned cb,
                    unsigned first,unsigned end){
    SautiAacSection *x=&s->sections[s->section_count++];
    x->group=(uint8_t)g;x->codebook=(uint8_t)cb;
    x->first_sfb=(uint8_t)first;x->end_sfb=(uint8_t)end;
}
static void long_case(void){
    SautiAacIcsInfo ics={0};
    ics.window_count=1;ics.group_count=1;ics.group_length[0]=1;
    ics.max_sfb=3;
    SautiAacSections sec={0};sec.group_count=1;sec.max_sfb=3;
    section(&sec,0,5,0,1);section(&sec,0,0,1,2);
    section(&sec,0,11,2,3);
    SautiAacScaleFactors sf={0};
    sf.codebook[0][0]=5;sf.value[0][0]=100;
    sf.codebook[0][1]=0;
    sf.codebook[0][2]=11;sf.value[0][2]=104;
    int32_t q[1024]={0};
    q[0]=1;q[1]=-8;q[2]=16;q[3]=-27;
    q[8]=8191;q[9]=-8191;q[10]=2;q[11]=0;
    float out[1024];for(unsigned i=0;i<1024;i++)out[i]=99;
    assert(sauti_aac_lc_dequantize(q,4,&ics,&sec,&sf,out));
    assert(near(out[0],1)&&near(out[1],-16)&&near(out[2],powf(16,4.0f/3)));
    assert(near(out[3],-81));
    for(unsigned i=4;i<8;i++)assert(out[i]==0);
    assert(near(out[8],2*powf(8191,4.0f/3)));
    assert(near(out[9],-2*powf(8191,4.0f/3)));
    assert(near(out[10],2*powf(2,4.0f/3)));
    for(unsigned i=12;i<1024;i++)assert(out[i]==0);
    float saved[1024];memcpy(saved,out,sizeof(out));
    q[8]=8192;
    assert(!sauti_aac_lc_dequantize(q,4,&ics,&sec,&sf,out));
    assert(memcmp(saved,out,sizeof(out))==0);
    q[8]=8191;
    sf.value[0][2]=256;
    assert(!sauti_aac_lc_dequantize(q,4,&ics,&sec,&sf,out));
    assert(memcmp(saved,out,sizeof(out))==0);
    sf.value[0][2]=104;
    sec.sections[1].first_sfb=0;
    assert(!sauti_aac_lc_dequantize(q,4,&ics,&sec,&sf,out));
    assert(memcmp(saved,out,sizeof(out))==0);
}
static void short_case(void){
    SautiAacIcsInfo ics={0};
    ics.window_sequence=2;ics.window_count=8;
    ics.group_count=2;ics.group_length[0]=3;ics.group_length[1]=5;
    ics.max_sfb=2;
    SautiAacSections sec={0};sec.group_count=2;sec.max_sfb=2;
    section(&sec,0,1,0,1);section(&sec,0,13,1,2);
    section(&sec,1,0,0,1);section(&sec,1,2,1,2);
    SautiAacScaleFactors sf={0};
    sf.codebook[0][0]=1;sf.value[0][0]=96;
    sf.codebook[0][1]=13;
    sf.codebook[1][0]=0;
    sf.codebook[1][1]=2;sf.value[1][1]=108;
    int32_t q[1024]={0};
    for(unsigned win=0;win<8;win++){
        for(unsigned i=0;i<4;i++)q[win*128+i]=4;
        for(unsigned i=4;i<8;i++)q[win*128+i]=-8;
    }
    float out[1024]={0};
    assert(sauti_aac_lc_dequantize(q,4,&ics,&sec,&sf,out));
    for(unsigned win=0;win<8;win++)
        for(unsigned i=0;i<128;i++){
            float value=win<3&&i<4 ? 0.5f*powf(4,4.0f/3) :
                        win>=3&&i>=4&&i<8 ? -4.0f*16.0f : 0;
            assert(near(out[win*128+i],value));
        }
}
int main(void){
    long_case();short_case();
    puts("PASS AAC-LC dequant: pow4/3, scalefactor, NASM signs, groups, rollback");
    return 0;
}
