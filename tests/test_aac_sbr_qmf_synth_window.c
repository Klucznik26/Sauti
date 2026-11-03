#include "sauti_aac_sbr_qmf_synth_window.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define I SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_INPUT
#define O SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_OUTPUT
#define MAXS SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_SLOTS
#define BLOCKS SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_HISTORY

static void close_to(double got,double expected){
    assert(isfinite(got));
    assert(fabs(got-expected)<=3e-6*fmax(1.0,fabs(expected)));
}
static float coeff(unsigned n){
    float v=0;
    assert(sauti_aac_sbr_qmf_synth_window_coefficient(n,&v));
    return v;
}
static void samples(float *x,unsigned slots){
    for(unsigned t=0;t<slots;t++)
        for(unsigned n=0;n<I;n++){
            const int v=(int)((t*7u+n*3u)%47u)-23;
            x[(size_t)t*I+n]=(float)v*0.125f;
        }
}
static void test_golden_coefficients(void){
    close_to(coeff(0u),0.0);
    close_to(coeff(1u),-0.0005525286);
    close_to(coeff(64u),0.0026201758);
    close_to(coeff(320u),0.8537385600);
    close_to(coeff(639u),-0.0005525286);
    assert(coeff(320u)>0.85f);
    /* QMF prototype is symmetric around coefficient 320 except for
     * the extra zero sample at index 0. */
    for(unsigned i=1u;i<640u;i++)
        close_to(coeff(i),coeff(640u-i));
    float untouched=37.25f;
    assert(!sauti_aac_sbr_qmf_synth_window_coefficient(640u,&untouched));
    assert(untouched==37.25f);
    assert(!sauti_aac_sbr_qmf_synth_window_coefficient(0u,NULL));
    puts("SBR-25: 640 QMF prototype taps and symmetry checked");
}
static void test_impulse_overlap_ten_blocks(void){
    float modulation[11u*I]={0};
    /* This pair of nonzero modulation entries allows one simple
     * impulse to visit both the even and odd polyphase positions. */
    modulation[0u]=1.0f;
    modulation[64u]=1.0f;
    float output[11u*O];
    SautiAacSbrQmfSynthWindowState state={0};
    assert(sauti_aac_sbr_qmf_synth_window_64(
        modulation,11u*I,11u,output,11u*O,&state));
    for(unsigned t=0;t<11u;t++)
        for(unsigned n=0;n<O;n++)
            close_to(output[(size_t)t*O+n],
                     n==0u && t<10u?coeff(t*64u):0.0);
    assert(state.ready==1u && state.head<10u);
    puts("SBR-25: impulse visits 10 polyphase blocks, disappears after 10th");
}
static void test_direct_math_and_split_40slots(void) {
    float modulation[MAXS*I],whole[MAXS*O+3u],pieces[MAXS*O];
    samples(modulation,MAXS);
    SautiAacSbrQmfSynthWindowState all={0},split={0};
    memset(whole,0xa5,sizeof(whole));
    float guard[3];
    memcpy(guard,whole+MAXS*O,sizeof(guard));
    assert(sauti_aac_sbr_qmf_synth_window_64(
        modulation,MAXS*I,MAXS,whole,MAXS*O,&all));
    assert(memcmp(guard,whole+MAXS*O,sizeof(guard))==0);

    /* Independent reference without a history ring: each output
     * is reconstructed directly from all preceding modulation blocks.
     * The current block has age zero, and older blocks age 1..9. */
    for(unsigned t=0;t<MAXS;t++)
        for(unsigned n=0;n<O;n++){
            double expected=0;
            for(unsigned age=0;age<10u && age<=t;age++){
                const size_t p=(size_t)(t-age)*I+
                               ((age&1u)?64u:0u)+n;
                expected+=(double)coeff(age*O+n)*
                          (double)modulation[p];
            }
            close_to(whole[(size_t)t*O+n],expected);
        }

    assert(sauti_aac_sbr_qmf_synth_window_64(
        modulation,17u*I,17u,pieces,17u*O,&split));
    assert(sauti_aac_sbr_qmf_synth_window_64(
        modulation+17u*I,23u*I,23u,
        pieces+17u*O,23u*O,&split));
    assert(memcmp(whole,pieces,MAXS*O*sizeof(float))==0);
    assert(memcmp(&all,&split,sizeof(all))==0);
    puts("SBR-25: 40-slot output matches independent 10-tap math and 17+23 split");
}
static void test_many_frames_wrap_history(void){
    float modulation[MAXS*I],out[MAXS*O];
    SautiAacSbrQmfSynthWindowState state={0};
    for(unsigned frame=0;frame<4u;frame++){
        samples(modulation,MAXS);
        for(unsigned i=0;i<MAXS*I;i++)
            modulation[i]+=(float)frame*0.25f;
        assert(sauti_aac_sbr_qmf_synth_window_64(
            modulation,MAXS*I,MAXS,out,MAXS*O,&state));
        assert(state.ready && state.head<10u);
    }
    for(unsigned n=0;n<I;n++)
        close_to(state.history[state.head][n],
                 modulation[(MAXS-1u)*I+n]);
    printf("SBR-25: four 40-slot frames preserve all 10 history blocks\n");
}
static void test_rollback_and_validation(void){
    float modulation[4u*I],output[4u*O],old[4u*O];
    samples(modulation,4u);
    memset(output,0x6b,sizeof(output));memcpy(old,output,sizeof(old));
    SautiAacSbrQmfSynthWindowState state={0},before=state;
#define REJECT(X,IC,N,Y,OC,ST) do { \
    assert(!sauti_aac_sbr_qmf_synth_window_64( \
        (X),(IC),(N),(Y),(OC),(ST))); \
    assert(memcmp(output,old,sizeof(old))==0); \
    assert(memcmp(&state,&before,sizeof(state))==0); \
} while (0)
    REJECT(NULL,4u*I,4u,output,4u*O,&state);
    REJECT(modulation,4u*I,4u,NULL,4u*O,&state);
    REJECT(modulation,4u*I,4u,output,4u*O,NULL);
    REJECT(modulation,4u*I,0u,output,4u*O,&state);
    REJECT(modulation,4u*I,MAXS+1u,output,4u*O,&state);
    REJECT(modulation,4u*I-1u,4u,output,4u*O,&state);
    REJECT(modulation,4u*I,4u,output,4u*O-1u,&state);

    modulation[3u*I+127u]=NAN;
    REJECT(modulation,4u*I,4u,output,4u*O,&state);
    modulation[3u*I+127u]=INFINITY;
    REJECT(modulation,4u*I,4u,output,4u*O,&state);
    modulation[3u*I+127u]=0.0f;

    state.ready=2u;before=state;
    REJECT(modulation,4u*I,4u,output,4u*O,&state);
    state.ready=1u;state.head=10u;before=state;
    REJECT(modulation,4u*I,4u,output,4u*O,&state);
    state.head=0u;state.history[7u][24u]=NAN;before=state;
    REJECT(modulation,4u*I,4u,output,4u*O,&state);
    state=(SautiAacSbrQmfSynthWindowState){0};before=state;
    state.history[0u][4u]=1.0f;before=state;
    REJECT(modulation,4u*I,4u,output,4u*O,&state);
#undef REJECT
    /* Documented rule: source and output must not overlap. */
    float combined[4u*I+4u*O];
    samples(combined,4u);
    state=(SautiAacSbrQmfSynthWindowState){0};
    before=state;
    assert(!sauti_aac_sbr_qmf_synth_window_64(
        combined,4u*I,4u,combined+I,4u*O,&state));
    assert(memcmp(&before,&state,sizeof(before))==0);
    puts("SBR-25: invalid geometry, NaN/Inf, aliasing and history rollback");
}
int main(void){
    test_golden_coefficients();
    test_impulse_overlap_ten_blocks();
    test_direct_math_and_split_40slots();
    test_many_frames_wrap_history();
    test_rollback_and_validation();
    puts("PASS HE-AAC SBR-25 QMF synthesis window: 640 taps, 64 outputs/slot, 10-block overlap");
    return 0;
}
