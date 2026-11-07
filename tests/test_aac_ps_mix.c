#include "sauti_aac_ps_mix.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define N (32u*71u)
static int near(float a,float b){ return fabsf(a-b)<0.00001f; }

int main(void){
    SautiAacPsHybridSchedule schedule={0};
    SautiAacPsMatrix matrix={0};
    SautiAacSbrComplex direct[N],decorrelated[N],left[N],right[N],saved[N];
    schedule.parameter_bands=20u;
    schedule.hybrid_bands=71u;
    schedule.env_count=2u;
    matrix.bands=20u;
    matrix.env_count=2u;
    for(unsigned t=0u;t<32u;t++)schedule.slot_to_env[t]=(uint8_t)(t>=16u);
    for(unsigned k=0u;k<71u;k++){
        schedule.hybrid_to_parameter[k]=(uint8_t)(k%20u);
        schedule.phase_imaginary_sign[k]=1;
    }
    schedule.phase_imaginary_sign[0]=-1;
    for(unsigned e=0u;e<2u;e++)for(unsigned b=0u;b<20u;b++){
        matrix.coefficient[e][b]=(SautiAacPsMatrixCoefficients){1.0f,0.0f,0.0f,1.0f};
    }
    matrix.coefficient[1][3]=(SautiAacPsMatrixCoefficients){1.0f,2.0f,3.0f,4.0f};
    for(unsigned i=0u;i<N;i++){
        direct[i]=(SautiAacSbrComplex){(float)i*0.01f,-(float)i*0.02f};
        decorrelated[i]=(SautiAacSbrComplex){0.5f,-0.25f};
        left[i]=(SautiAacSbrComplex){99.0f,99.0f};
        right[i]=(SautiAacSbrComplex){99.0f,99.0f};
    }
    assert(sauti_aac_ps_mix_real_apply(&schedule,&matrix,direct,N,decorrelated,N,left,N,right,N));
    assert(near(left[0].re,direct[0].re)&&near(left[0].im,direct[0].im)&&
           near(right[0].re,decorrelated[0].re)&&near(right[0].im,decorrelated[0].im));
    const unsigned ix=20u*71u+3u;
    assert(near(left[ix].re,direct[ix].re+1.0f)&&near(left[ix].im,direct[ix].im-0.5f)&&
           near(right[ix].re,3.0f*direct[ix].re+2.0f)&&near(right[ix].im,3.0f*direct[ix].im-1.0f));
    memcpy(saved,left,sizeof(saved));
    direct[N-1u].re=NAN;
    assert(!sauti_aac_ps_mix_real_apply(&schedule,&matrix,direct,N,decorrelated,N,left,N,right,N));
    assert(memcmp(left,saved,sizeof(saved))==0);

    SautiAacPsComplexMatrix complex_matrix={0};
    complex_matrix.bands=20u;complex_matrix.env_count=2u;complex_matrix.has_phase=1u;
    for(unsigned e=0u;e<2u;e++)for(unsigned b=0u;b<20u;b++){
        complex_matrix.coefficient[e][b]=(SautiAacPsComplexMatrixCoefficients){
            {1.0f,0.0f},{0.0f,0.0f},{0.0f,0.0f},{1.0f,0.0f}};
    }
    /* k=2 has a positive phase sign and maps to parameter band 2. */
    complex_matrix.coefficient[0][2]=(SautiAacPsComplexMatrixCoefficients){
        {0.0f,1.0f},{1.0f,0.0f},{1.0f,0.0f},{0.0f,1.0f}};
    for(unsigned i=0u;i<N;i++){
        direct[i]=(SautiAacSbrComplex){1.0f,0.0f};
        decorrelated[i]=(SautiAacSbrComplex){0.0f,1.0f};
    }
    assert(sauti_aac_ps_mix_complex_apply(&schedule,&complex_matrix,direct,N,decorrelated,N,left,N,right,N));
    assert(near(left[2].re,0.0f)&&near(left[2].im,2.0f)&&
           near(right[2].re,0.0f)&&near(right[2].im,0.0f));
    /* k=0 negates imaginary coefficients before the complex multiply. */
    complex_matrix.coefficient[0][0]=(SautiAacPsComplexMatrixCoefficients){
        {0.0f,1.0f},{0.0f,0.0f},{0.0f,0.0f},{1.0f,0.0f}};
    assert(sauti_aac_ps_mix_complex_apply(&schedule,&complex_matrix,direct,N,decorrelated,N,left,N,right,N));
    assert(near(left[0].re,0.0f)&&near(left[0].im,-1.0f));
    puts("PASS AAC PS ASM mix: complex matrix, envelope routing and atomicity");
    return 0;
}
