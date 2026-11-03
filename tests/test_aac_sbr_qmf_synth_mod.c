#include "sauti_aac_sbr_qmf_synth_mod.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define B SAUTI_AAC_SBR_QMF_MOD_BANDS
#define V SAUTI_AAC_SBR_QMF_MOD_VALUES
#define N SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS
#define P SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_OUTPUT
#define PI_L 3.1415926535897932384626433832795028841971693993751L

static void near(double actual,double expected){
    assert(isfinite(actual) && isfinite(expected));
    assert(fabs(actual-expected)<=3e-6*fmax(1.0,fabs(expected)));
}
static void near_precise(double actual,double expected){
    assert(isfinite(actual) && isfinite(expected));
    assert(fabs(actual-expected)<=1e-7*fmax(1.0,fabs(expected)));
}
static void fill(SautiAacSbrComplex *x,unsigned slots){
    for(unsigned t=0;t<slots;t++)
        for(unsigned k=0;k<B;k++){
            const int r=(int)((t*13u+k*7u)%23u)-11;
            const int i=(int)((t*5u+k*3u)%19u)-9;
            x[(size_t)t*B+k].re=(float)r*0.125f;
            x[(size_t)t*B+k].im=(float)i*0.0625f;
        }
}
static void one(SautiAacSbrComplex *x,
                unsigned k,float re,float im){
    memset(x,0,B*sizeof(x[0]));
    x[k].re=re;x[k].im=im;
}
static double reference_value(const SautiAacSbrComplex *x,unsigned n){
    /* Independent high-precision reference using long double trig,
     * a different order of products and a separate real accumulation. */
    long double real_sum=0,imag_sum=0;
    const long double omega=(PI_L/128.0L)*
                            (2.0L*(long double)n-63.0L);
    for(unsigned k=0;k<B;k++){
        const long double theta=omega*((long double)k+0.5L);
        real_sum+=(long double)x[k].re*cosl(theta);
        imag_sum+=(long double)x[k].im*sinl(theta);
    }
    return (double)((real_sum-imag_sum)/64.0L);
}
static void test_golden_real_imag_single_bin(void){
    SautiAacSbrComplex x[B];
    float out[V+2];
    const struct {
        unsigned band,n;float re,im;double expected;
    } cases[]={
        {0,0,1,0,0.011183294145059666},
        {0,31,1,0,0.015623823466236633},
        {0,32,1,0,0.015623823466236633},
        {0,63,1,0,0.011183294145059666},
        {0,127,1,0,-0.010912128897015200},
        {0,0,0,1,0.010912128897015202},
        {0,31,0,1,0.00019174278571437383},
        {0,32,0,1,-0.00019174278571437383},
        {0,63,0,1,-0.010912128897015202},
        {17,0,1,0,-0.005444041878897401},
        {17,32,1,0,0.014205749735789412},
        {63,0,0,1,-0.011183294145059648},
        {63,32,0,1,-0.015623823466236633},
        {63,63,0,1,0.011183294145059648}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        const unsigned k=cases[i].band,n=cases[i].n;
        one(x,k,cases[i].re,cases[i].im);
        memset(out,0xa3,sizeof(out));
        float guard[2];memcpy(guard,&out[V],sizeof(guard));
        assert(sauti_aac_sbr_qmf_synth_modulate_64(x,B,1,out,V));
        near_precise(out[n],cases[i].expected);
        assert(memcmp(&out[V],guard,sizeof(guard))==0);
        for(unsigned q=0;q<V;q++)
            near_precise(out[q],reference_value(x,q));
    }
    /* For a real single low band k=0, samples around n=31.5
     * are symmetric. For a purely imaginary one they are opposite. */
    one(x,0,1,0);
    assert(sauti_aac_sbr_qmf_synth_modulate_64(x,B,1,out,V));
    for(unsigned q=0;q<32;q++)
        near_precise(out[q],out[63u-q]);
    one(x,0,0,1);
    assert(sauti_aac_sbr_qmf_synth_modulate_64(x,B,1,out,V));
    for(unsigned q=0;q<32;q++)
        near_precise(out[q],-out[63u-q]);
    puts("SBR-26: real/imag QMF single-bin golden phases and symmetry PASS");
}

static void test_linearity_and_mixed_40_slots(void) {
    SautiAacSbrComplex x[N*B],scaled[N*B];
    float a[N*V+3],b[N*V];
    fill(x,N);
    for(unsigned i=0;i<N*B;i++){
        scaled[i].re=x[i].re*0.5f;
        scaled[i].im=x[i].im*0.5f;
    }
    memset(a,0x8d,sizeof(a));
    float tail[3];memcpy(tail,&a[N*V],sizeof(tail));
    assert(sauti_aac_sbr_qmf_synth_modulate_64(x,N*B,N,a,N*V));
    assert(sauti_aac_sbr_qmf_synth_modulate_64(
        scaled,N*B,N,b,N*V));
    assert(memcmp(tail,&a[N*V],sizeof(tail))==0);
    for(unsigned t=0;t<N;t++)
        for(unsigned n=0;n<V;n++){
            near(a[(size_t)t*V+n],reference_value(&x[t*B],n));
            near(a[(size_t)t*V+n]*0.5,
                 b[(size_t)t*V+n]);
        }

    float first[17u*V],remaining[23u*V];
    assert(sauti_aac_sbr_qmf_synth_modulate_64(
        x,17u*B,17u,first,17u*V));
    assert(sauti_aac_sbr_qmf_synth_modulate_64(
        x+17u*B,23u*B,23u,remaining,23u*V));
    assert(memcmp(a,first,sizeof(first))==0);
    assert(memcmp(&a[17u*V],remaining,sizeof(remaining))==0);
    printf("SBR-26: 40 slots x 128 values, high-precision reference, linearity and split PASS\n");
}
static void test_zero_and_alias(void){
    SautiAacSbrComplex x[2u*B]={0};
    float out[2u*V];
    assert(sauti_aac_sbr_qmf_synth_modulate_64(
        x,2u*B,2u,out,2u*V));
    for(unsigned j=0;j<2u*V;j++)assert(out[j]==0.0f);

    fill(x,2u);
    float expected[2u*V];
    assert(sauti_aac_sbr_qmf_synth_modulate_64(
        x,2u*B,2u,expected,2u*V));
    /* In-place is byte-size-compatible: 64 complex pairs -> 128
     * floats per slot. Writes occur ONLY after staging all output. */
    assert(sauti_aac_sbr_qmf_synth_modulate_64(
        x,2u*B,2u,(float *)x,2u*V));
    assert(memcmp((float *)x,expected,sizeof(expected))==0);
}

static void test_full_synthesis_chain_and_history(void) {
    SautiAacSbrComplex bands[N*B];
    fill(bands,N);
    float modulated[N*V],manual[N*P],automatic[N*P+2u],split[N*P];
    SautiAacSbrQmfSynthWindowState reference={0},state={0},halves={0};
    assert(sauti_aac_sbr_qmf_synth_modulate_64(
        bands,N*B,N,modulated,N*V));
    assert(sauti_aac_sbr_qmf_synth_window_64(
        modulated,N*V,N,manual,N*P,&reference));
    memset(automatic,0x4e,sizeof(automatic));
    float guard[2];memcpy(guard,&automatic[N*P],sizeof(guard));
    assert(sauti_aac_sbr_qmf_synth_64(
        bands,N*B,N,automatic,N*P,&state));
    assert(memcmp(automatic,manual,sizeof(manual))==0);
    assert(memcmp(&reference,&state,sizeof(state))==0);
    assert(memcmp(&automatic[N*P],guard,sizeof(guard))==0);

    assert(sauti_aac_sbr_qmf_synth_64(
        bands,17u*B,17u,split,17u*P,&halves));
    assert(sauti_aac_sbr_qmf_synth_64(
        bands+17u*B,23u*B,23u,split+17u*P,23u*P,&halves));
    assert(memcmp(split,manual,sizeof(split))==0);
    assert(memcmp(&reference,&halves,sizeof(halves))==0);

    /* First frame / first slot is independent of any prior overlap:
     * only filter coefficient C[n] times current modulation V[n]. */
    SautiAacSbrQmfSynthWindowState zero={0};
    float first[P];
    assert(sauti_aac_sbr_qmf_synth_64(
        bands,B,1,first,P,&zero));
    for(unsigned n=0;n<P;n++){
        float coeff;
        assert(sauti_aac_sbr_qmf_synth_window_coefficient(n,&coeff));
        near(first[n],(double)coeff*modulated[n]);
    }
    puts("SBR-26: QMF complex -> modulated V -> 640-tap synthesis (64 samples/slot) PASS");
}
static void test_transactional_failures(void) {
    SautiAacSbrComplex bands[4u*B];fill(bands,4u);
    float modulation[4u*V],old_mod[4u*V];
    memset(modulation,0x63,sizeof(modulation));
    memcpy(old_mod,modulation,sizeof(modulation));
#define BAD_MOD(X,BC,S,O,OC) do { \
    assert(!sauti_aac_sbr_qmf_synth_modulate_64( \
        (X),(BC),(S),(O),(OC))); \
    assert(memcmp(modulation,old_mod,sizeof(modulation))==0); \
} while(0)
    BAD_MOD(NULL,4u*B,4u,modulation,4u*V);
    BAD_MOD(bands,4u*B,4u,NULL,4u*V);
    BAD_MOD(bands,4u*B,0u,modulation,4u*V);
    BAD_MOD(bands,4u*B,N+1u,modulation,4u*V);
    BAD_MOD(bands,4u*B-1u,4u,modulation,4u*V);
    BAD_MOD(bands,4u*B,4u,modulation,4u*V-1u);
    bands[3u*B+63u].re=NAN;
    BAD_MOD(bands,4u*B,4u,modulation,4u*V);
    bands[3u*B+63u].re=INFINITY;
    BAD_MOD(bands,4u*B,4u,modulation,4u*V);
    bands[3u*B+63u].re=0.0f;
#undef BAD_MOD

    float output[4u*P],old_out[4u*P];
    memset(output,0xa7,sizeof(output));
    memcpy(old_out,output,sizeof(output));
    SautiAacSbrQmfSynthWindowState state={0},before=state;
#define BAD_CHAIN(X,BC,S,O,OC,ST) do { \
    assert(!sauti_aac_sbr_qmf_synth_64( \
        (X),(BC),(S),(O),(OC),(ST))); \
    assert(memcmp(output,old_out,sizeof(output))==0); \
    assert(memcmp(&state,&before,sizeof(state))==0); \
} while(0)
    BAD_CHAIN(NULL,4u*B,4u,output,4u*P,&state);
    BAD_CHAIN(bands,4u*B,4u,NULL,4u*P,&state);
    BAD_CHAIN(bands,4u*B,4u,output,4u*P,NULL);
    BAD_CHAIN(bands,4u*B,0u,output,4u*P,&state);
    BAD_CHAIN(bands,4u*B,N+1u,output,4u*P,&state);
    BAD_CHAIN(bands,4u*B-1u,4u,output,4u*P,&state);
    BAD_CHAIN(bands,4u*B,4u,output,4u*P-1u,&state);
    bands[3u*B+63u].im=NAN;
    BAD_CHAIN(bands,4u*B,4u,output,4u*P,&state);
    bands[3u*B+63u].im=0.0f;
    state.ready=2u;before=state;
    BAD_CHAIN(bands,4u*B,4u,output,4u*P,&state);
    state.ready=1u;state.head=10u;before=state;
    BAD_CHAIN(bands,4u*B,4u,output,4u*P,&state);
    state=(SautiAacSbrQmfSynthWindowState){0};
    state.history[0u][127u]=NAN;before=state;
    BAD_CHAIN(bands,4u*B,4u,output,4u*P,&state);
#undef BAD_CHAIN
    puts("SBR-26: late NaN/Inf, invalid state and buffer bounds rollback PASS");
}
int main(void){
    test_golden_real_imag_single_bin();
    test_linearity_and_mixed_40_slots();
    test_zero_and_alias();
    test_full_synthesis_chain_and_history();
    test_transactional_failures();
    puts("PASS HE-AAC SBR-26 complex QMF synthesis modulation and atomic 640-tap window chain");
    return 0;
}
