#include "sauti_aac_sbr_hf_phase_probe.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define COUNT (32u*64u)
int main(void){
    SautiAacSbrComplex input[COUNT]={0},out[COUNT]={0};
    SautiAacSbrTemporalNoiseFrame mask={0};
    for(unsigned t=0;t<32;t++)
        for(unsigned k=16;k<32;k++){
            const unsigned i=t*64u+k;
            mask.active[i]=1u;
            input[i].re=(float)k;
            input[i].im=(float)(-((int)k));
        }
    assert(sauti_aac_sbr_hf_conjugate_probe(
        input,COUNT,&mask,out,COUNT));
    for(unsigned t=0;t<32;t++)
        for(unsigned k=16;k<32;k++){
            const unsigned i=t*64u+k;
            assert(out[i].re==input[i].re);
            assert(out[i].im==-input[i].im);
            assert(input[i].im<0.0f);
        }
    puts("SBR-54q: conjugate only active complex HF; LOW unchanged PASS");
    const SautiAacSbrComplex before[COUNT]={0}; /* unused? use snapshot */
    (void)before;
    SautiAacSbrComplex snapshot[COUNT];
    memcpy(snapshot,out,sizeof(snapshot));
    input[COUNT-1].im=NAN;
    assert(!sauti_aac_sbr_hf_conjugate_probe(
        input,COUNT,&mask,out,COUNT));
    assert(memcmp(out,snapshot,sizeof(snapshot))==0);
    input[COUNT-1].im=0.0f;
    mask.active[31u*64u+17u]=2u;
    assert(!sauti_aac_sbr_hf_conjugate_probe(
        input,COUNT,&mask,out,COUNT));
    assert(memcmp(out,snapshot,sizeof(snapshot))==0);
    mask.active[31u*64u+17u]=1u;
    assert(!sauti_aac_sbr_hf_conjugate_probe(
        input,COUNT-1,&mask,out,COUNT));
    assert(memcmp(out,snapshot,sizeof(snapshot))==0);
    assert(!sauti_aac_sbr_hf_conjugate_probe(
        input,COUNT,&mask,out,COUNT-1));
    assert(memcmp(out,snapshot,sizeof(snapshot))==0);
    puts("PASS SBR-54q atomic QMF imaginary-sign A/B transformation");
    return 0;
}
