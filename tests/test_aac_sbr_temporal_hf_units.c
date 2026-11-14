/* SBR-54i: test diagnostic input-unit-domain conversion only. */
#include "sauti_aac_sbr_temporal_hf_units.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define COUNT (32u*64u)

int main(void){
    SautiAacSbrComplex high[COUNT]={0},out[COUNT]={0},saved[COUNT];
    SautiAacSbrTemporalNoiseFrame noise={0};
    for(unsigned t=0u;t<32u;t++){
        const unsigned idx=t*64u+16u;
        noise.active[idx]=1u;
        high[idx].re=16384.0f;
        high[idx].im=-32768.0f;
    }
    assert(sauti_aac_sbr_temporal_hf_units_float(
        high,COUNT,&noise,out,COUNT));
    for(unsigned t=0u;t<32u;t++){
        const unsigned idx=t*64u+16u;
        assert(out[idx].re==0.5f && out[idx].im==-1.0f);
        assert(high[idx].re==16384.0f && high[idx].im==-32768.0f);
        assert(out[t*64u+15u].re==0.0f &&
               out[t*64u+17u].re==0.0f);
    }
    puts("SBR-54i: 16-bit amplitude to normalized HF float exact PASS");

    memcpy(saved,out,sizeof(saved));
    high[31u*64u+16u].re=NAN;
    assert(!sauti_aac_sbr_temporal_hf_units_float(
        high,COUNT,&noise,out,COUNT));
    assert(memcmp(saved,out,sizeof(saved))==0);
    high[31u*64u+16u].re=16384.0f;
    noise.active[31u*64u+16u]=0u;
    assert(!sauti_aac_sbr_temporal_hf_units_float(
        high,COUNT,&noise,out,COUNT));
    assert(memcmp(saved,out,sizeof(saved))==0);
    noise.active[31u*64u+16u]=1u;
    noise.active[31u*64u+17u]=2u;
    assert(!sauti_aac_sbr_temporal_hf_units_float(
        high,COUNT,&noise,out,COUNT));
    assert(memcmp(saved,out,sizeof(saved))==0);
    noise.active[31u*64u+17u]=0u;
    assert(!sauti_aac_sbr_temporal_hf_units_float(
        high,COUNT-1u,&noise,out,COUNT));
    assert(memcmp(saved,out,sizeof(saved))==0);
    assert(!sauti_aac_sbr_temporal_hf_units_float(
        high,COUNT,&noise,out,COUNT-1u));
    assert(memcmp(saved,out,sizeof(saved))==0);
    puts("PASS SBR-54i high-band normalization A/B (no normative HE-AAC)");
    return 0;
}
