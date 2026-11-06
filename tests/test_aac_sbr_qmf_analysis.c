#include "sauti_aac_sbr_qmf_analysis.h"
#include "sauti_aac_sbr_qmf_synth_window.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define S SAUTI_AAC_SBR_QMF_ANALYSIS_SLOTS
#define I SAUTI_AAC_SBR_QMF_ANALYSIS_INPUT
#define B SAUTI_AAC_SBR_QMF_ANALYSIS_BANDS
#define H SAUTI_AAC_SBR_QMF_ANALYSIS_HISTORY
#define PI_L 3.1415926535897932384626433832795028841971693993751L

static void near(double actual,double expected){
    assert(isfinite(actual) && isfinite(expected));
    const double scale=fmax(1e-4,fabs(expected));
    assert(fabs(actual-expected)<=2e-5*scale);
}
static void fill(float *samples,unsigned slots){
    for(unsigned t=0;t<slots;t++)
        for(unsigned j=0;j<I;j++){
            const int v=(int)((t*13u+j*7u)%37u)-18;
            samples[(size_t)t*I+j]=(float)v*0.0625f;
        }
}
static float coeff(unsigned idx){
    float c=123.0f;
    assert(idx<320 && sauti_aac_sbr_qmf_synth_window_coefficient(
        idx*2u,&c));
    return c;
}
static void test_first_slot_reference_vectors(void){
    float pcm[I]={0};
    pcm[30]=1.0f; /* newest-first analysis x[1] */
    SautiAacSbrComplex out[B+2];
    memset(out,0xa5,sizeof(out));
    SautiAacSbrComplex guard[2];
    memcpy(guard,out+B,sizeof(guard));
    SautiAacSbrQmfAnalysisState state={0};
    assert(sauti_aac_sbr_qmf_analyze_32(
        pcm,I,1u,32u,out,B,&state));
    assert(memcmp(out+B,guard,sizeof(guard))==0);
    assert(state.ready==1u && state.samples_consumed==32u);
    near(state.history[1],1.0);
    const struct {
        unsigned k;double re,im;
    } golden[]={
        {0u,-0.0011227770730645788,-0.00004135432837197760},
        {1u,-0.0011166926468252952,-0.0001238388824274377},
        {7u,-0.0009565302514931807,-0.0005893966527330746},
        {31u,0.0007646813561015068,-0.0008231652081479880}
    };
    for(unsigned j=0;j<sizeof(golden)/sizeof(golden[0]);j++){
        near(out[golden[j].k].re,golden[j].re);
        near(out[golden[j].k].im,golden[j].im);
    }
    for(unsigned k=0;k<B;k++){
        const long double theta=(PI_L/64.0L)*
              ((long double)k+0.5L)*1.5L;
        const long double tap=(long double)coeff(1u);
        near(out[k].re,(double)(2.0L*tap*cosl(theta)));
        near(out[k].im,(double)(2.0L*tap*sinl(theta)));
    }
    puts("SBR-27: impulse x[1] matches four fixed complex reference vectors");
}
static void test_impulse_10_slot_polyphase_history(void){
    float pcm[11u*I]={0};
    pcm[30u]=1.0f;
    SautiAacSbrComplex out[11u*B];
    SautiAacSbrQmfAnalysisState state={0};
    assert(sauti_aac_sbr_qmf_analyze_32(
        pcm,11u*I,11u,32u,out,11u*B,&state));
    for(unsigned t=0;t<11u;t++){
        const unsigned age=1u+32u*t;
        const unsigned n=age%64u;
        for(unsigned k=0;k<B;k++){
            const double theta=(double)PI_L/64.0*
                           ((double)k+0.5)*
                           (2.0*(double)n-0.5);
            const double factor=age>=320u?0.0:
                                2.0*(double)coeff(age);
            near(out[(size_t)t*B+k].re,factor*cos(theta));
            near(out[(size_t)t*B+k].im,factor*sin(theta));
        }
    }
    for(unsigned k=0;k<B;k++){
        assert(out[10u*B+k].re==0.0f);
        assert(out[10u*B+k].im==0.0f);
    }
    puts("SBR-27: 320-sample impulse tail spans 10 analysis slots");
}
static void reference_40(const float *pcm,unsigned slots,
                         SautiAacSbrComplex *out){
    long double hist[H]={0};
    for(unsigned t=0;t<slots;t++){
        memmove(hist+32u,hist,(H-32u)*sizeof(hist[0]));
        for(unsigned n=0;n<32u;n++)
            hist[31u-n]=(long double)pcm[(size_t)t*I+n];
        long double u[64]={0};
        for(unsigned n=0;n<64u;n++)
            for(unsigned j=0;j<5u;j++){
                const unsigned ix=n+64u*j;
                u[n]+=hist[ix]*(long double)coeff(ix);
            }
        for(unsigned k=0;k<B;k++){
            long double re=0,im=0;
            for(unsigned n=0;n<64u;n++){
                const long double theta=(PI_L/64.0L)*
                    ((long double)k+0.5L)*
                    (2.0L*(long double)n-0.5L);
                re+=2.0L*u[n]*cosl(theta);
                im+=2.0L*u[n]*sinl(theta);
            }
            out[(size_t)t*B+k].re=(float)re;
            out[(size_t)t*B+k].im=(float)im;
        }
    }
}
static void test_40_slots_independent_math_and_split(void){
    float pcm[S*I];
    fill(pcm,S);
    SautiAacSbrComplex actual[S*B+2u],expected[S*B],
                       split[S*B];
    SautiAacSbrQmfAnalysisState state={0},parts={0};
    memset(actual,0x7a,sizeof(actual));
    SautiAacSbrComplex guard[2];
    memcpy(guard,actual+S*B,sizeof(guard));
    assert(sauti_aac_sbr_qmf_analyze_32(
        pcm,S*I,S,32u,actual,S*B,&state));
    reference_40(pcm,S,expected);
    assert(memcmp(actual+S*B,guard,sizeof(guard))==0);
    for(unsigned j=0;j<S*B;j++){
        near(actual[j].re,expected[j].re);
        near(actual[j].im,expected[j].im);
    }

    assert(sauti_aac_sbr_qmf_analyze_32(
        pcm,17u*I,17u,32u,split,17u*B,&parts));
    assert(sauti_aac_sbr_qmf_analyze_32(
        pcm+17u*I,23u*I,23u,32u,
        split+17u*B,23u*B,&parts));
    assert(memcmp(actual,split,S*B*sizeof(actual[0]))==0);
    assert(memcmp(&state,&parts,sizeof(state))==0);
    assert(state.samples_consumed==(uint64_t)S*32u);
    puts("SBR-27: 40 slots x 32 complex QMF match long-double reference and 17+23 split");
}
static void test_cutoff_channel_isolation_and_alias(void){
    float pcm[2u*I];
    fill(pcm,2u);
    SautiAacSbrComplex full[2u*B],half[2u*B],
                       other[2u*B],reference[2u*B];
    SautiAacSbrQmfAnalysisState a={0},b={0},c={0};
    assert(sauti_aac_sbr_qmf_analyze_32(
        pcm,2u*I,2u,32u,full,2u*B,&a));
    assert(sauti_aac_sbr_qmf_analyze_32(
        pcm,2u*I,2u,17u,half,2u*B,&b));
    for(unsigned t=0;t<2u;t++)
        for(unsigned k=0;k<B;k++){
            const size_t i=(size_t)t*B+k;
            if(k<17u)
                assert(memcmp(&full[i],&half[i],sizeof(full[i]))==0);
            else {
                assert(half[i].re==0.0f && half[i].im==0.0f);
            }
        }
    assert(memcmp(&a,&b,sizeof(a))==0);
    float silent[2u*I]={0};
    assert(sauti_aac_sbr_qmf_analyze_32(
        silent,2u*I,2u,32u,other,2u*B,&c));
    for(unsigned j=0;j<2u*B;j++)
        assert(other[j].re==0.0f && other[j].im==0.0f);

    /* The same source bytes can occupy the prefix of a larger
     * output allocation. No write occurs until the analysis ends. */
    union {
        float pcm_storage[2u*B*2u];
        SautiAacSbrComplex qmf_storage[2u*B];
    } overlay;
    memcpy(overlay.pcm_storage,pcm,2u*I*sizeof(float));
    SautiAacSbrQmfAnalysisState alias_state={0};
    assert(sauti_aac_sbr_qmf_analyze_32(
        overlay.pcm_storage,2u*I,2u,32u,
        overlay.qmf_storage,2u*B,&alias_state));
    memcpy(reference,full,sizeof(full));
    assert(memcmp(overlay.qmf_storage,reference,sizeof(reference))==0);
    assert(memcmp(&alias_state,&a,sizeof(a))==0);
}
static void test_invalid_data_atomicity(void){
    float pcm[4u*I];fill(pcm,4u);
    SautiAacSbrComplex out[4u*B],before_out[4u*B];
    memset(out,0x75,sizeof(out));
    memcpy(before_out,out,sizeof(out));
    SautiAacSbrQmfAnalysisState state={0},before=state;
#define BAD(X,IC,SL,AB,Y,OC,ST) do {                             \
    assert(!sauti_aac_sbr_qmf_analyze_32(                       \
        (X),(IC),(SL),(AB),(Y),(OC),(ST)));                      \
    assert(memcmp(out,before_out,sizeof(out))==0);               \
    assert(memcmp(&state,&before,sizeof(state))==0);             \
} while(0)
    BAD(NULL,4u*I,4u,32u,out,4u*B,&state);
    BAD(pcm,4u*I,4u,32u,NULL,4u*B,&state);
    BAD(pcm,4u*I,4u,32u,out,4u*B,NULL);
    BAD(pcm,4u*I,0u,32u,out,4u*B,&state);
    BAD(pcm,4u*I,S+1u,32u,out,4u*B,&state);
    BAD(pcm,4u*I,4u,0u,out,4u*B,&state);
    BAD(pcm,4u*I,4u,33u,out,4u*B,&state);
    BAD(pcm,4u*I-1u,4u,32u,out,4u*B,&state);
    BAD(pcm,4u*I,4u,32u,out,4u*B-1u,&state);
    pcm[3u*I+31u]=NAN;
    BAD(pcm,4u*I,4u,32u,out,4u*B,&state);
    pcm[3u*I+31u]=INFINITY;
    BAD(pcm,4u*I,4u,32u,out,4u*B,&state);
    pcm[3u*I+31u]=0.0f;
    state.ready=2u;before=state;
    BAD(pcm,4u*I,4u,32u,out,4u*B,&state);
    state.ready=0u;state.samples_consumed=32u;before=state;
    BAD(pcm,4u*I,4u,32u,out,4u*B,&state);
    state=(SautiAacSbrQmfAnalysisState){0};
    state.history[319u]=1.0f;before=state;
    BAD(pcm,4u*I,4u,32u,out,4u*B,&state);
    state=(SautiAacSbrQmfAnalysisState){0};
    state.ready=1u;state.history[12u]=NAN;before=state;
    BAD(pcm,4u*I,4u,32u,out,4u*B,&state);
    state=(SautiAacSbrQmfAnalysisState){0};
    state.ready=1u;state.samples_consumed=UINT64_MAX-31u;
    before=state;
    BAD(pcm,4u*I,4u,32u,out,4u*B,&state);
#undef BAD
    puts("SBR-27: corrupted history, late NaN/Inf, overflow and buffer rollback PASS");
}
int main(void){
    test_first_slot_reference_vectors();
    test_impulse_10_slot_polyphase_history();
    test_40_slots_independent_math_and_split();
    test_cutoff_channel_isolation_and_alias();
    test_invalid_data_atomicity();
    puts("PASS HE-AAC SBR-27 32-band complex QMF analysis: 320-tap window, streaming, rollback");
    return 0;
}
