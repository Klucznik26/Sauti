#include "sauti_aac_sbr_hf_predict.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LO SAUTI_AAC_SBR_HF_LOW_BANDS
#define HI SAUTI_AAC_SBR_HF_HIGH_BANDS
#define MAXS SAUTI_AAC_SBR_HF_MAX_SLOTS

static void approx(float actual,double expected) {
    assert(isfinite(actual) && isfinite(expected));
    assert(fabs((double)actual-expected) <
           0.0001*fmax(1.0,fabs(expected)));
}
static void fixture(unsigned rate,unsigned k0,unsigned k2,
                    SautiAacSbrMaster *master,SautiAacSbrTables *tables,
                    SautiAacSbrPatches *patches) {
    assert(sauti_aac_sbr_master_linear(k0,k2,1,0,master));
    assert(sauti_aac_sbr_tables_build(master,1,tables));
    assert(sauti_aac_sbr_patch_build(rate,master,tables,patches));
}
static void samples(SautiAacSbrComplex *low,unsigned slots) {
    for(unsigned t=0;t<slots+2u;t++) for(unsigned k=0;k<LO;k++) {
        low[t*LO+k].re=(float)(t*3u+k+1u)*0.125f;
        low[t*LO+k].im=(float)((int)t-(int)k)*0.0625f;
    }
}
static void identity(const SautiAacSbrPatches *patches,
                     const SautiAacSbrComplex *low,
                     const SautiAacSbrComplex *out,unsigned slots) {
    SautiAacSbrComplex zero={0};
    for(unsigned t=0;t<slots;t++)for(unsigned k=0;k<HI;k++){
        const int p=patches->source_for_target[k];
        const SautiAacSbrComplex *x=p<0?&zero:&low[(t+2u)*LO+(unsigned)p];
        assert(memcmp(x,&out[t*HI+k],sizeof(*x))==0);
    }
}

static void test_chirp_transitions(void) {
    SautiAacSbrChirp state={0};
    const uint8_t modes0[2]={1,3};
    assert(sauti_aac_sbr_chirp_update(modes0,2,&state));
    assert(state.ready==1u && state.count==2u);
    approx(state.bw[0],0.90625*0.6);    /* OFF -> LOW */
    approx(state.bw[1],0.90625*0.98);   /* OFF -> HIGH */
    assert(state.previous_mode[0]==1u && state.previous_mode[1]==3u);
    const float prior=state.bw[0];
    const uint8_t modes1[2]={0,2};
    assert(sauti_aac_sbr_chirp_update(modes1,2,&state));
    approx(state.bw[0],0.90625*0.6+0.09375*prior); /* LOW -> OFF, target exceeds previous smoothed value */
    approx(state.bw[1],0.90625*0.9+0.09375*(0.90625*0.98));
    const uint8_t off[2]={0,0};
    assert(sauti_aac_sbr_chirp_update(off,2,&state));
    approx(state.bw[0],0.25*(0.90625*0.6+0.09375*prior));
    /* Continued zero/zero smoothing eventually kills residual. */
    for(unsigned i=0;i<12;i++)assert(sauti_aac_sbr_chirp_update(off,2,&state));
    assert(state.bw[0]==0.0f && state.bw[1]==0.0f);
    const uint8_t one[1]={2};
    SautiAacSbrChirp fresh={0};
    assert(sauti_aac_sbr_chirp_update(one,1,&fresh));
    approx(fresh.bw[0],0.90625*0.9);
}

static void test_chirp_atomic_errors(void) {
    SautiAacSbrChirp state={0},saved;
    const uint8_t modes[2]={1,3},bad[2]={1,4};
    assert(sauti_aac_sbr_chirp_update(modes,2,&state));
    saved=state;
#define NOCHANGE(M,N) do { assert(!sauti_aac_sbr_chirp_update(M,N,&state)); \
    assert(memcmp(&state,&saved,sizeof(state))==0); } while(0)
    NOCHANGE(NULL,2);
    NOCHANGE(modes,0);
    NOCHANGE(modes,6);
    NOCHANGE(modes,1);
    NOCHANGE(bad,2);
    state.bw[1]=NAN;
    saved=state;
    NOCHANGE(modes,2);
    state.bw[1]=0.8f;
    state.previous_mode[0]=4;
    saved=state;
    NOCHANGE(modes,2);
    state.previous_mode[0]=1u;
    state.ready=2u;
    saved=state;
    NOCHANGE(modes,2);
#undef NOCHANGE
    assert(!sauti_aac_sbr_chirp_update(modes,2,NULL));
}

static void test_40_slots_and_identity(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    fixture(48000,16,32,&m,&t,&p);
    SautiAacSbrComplex low[(MAXS+2u)*LO];
    SautiAacSbrComplex high[MAXS*HI+3u];
    SautiAacSbrLpcCoeffs coeff={0};
    SautiAacSbrChirp chirp={0};
    uint8_t modes[5]={0};
    assert(sauti_aac_sbr_chirp_update(modes,t.n_noise,&chirp));
    samples(low,MAXS);
    memset(high,0xcd,sizeof(high));
    SautiAacSbrComplex tail[3u];
    memcpy(tail,&high[MAXS*HI],sizeof(tail));
    assert(sauti_aac_sbr_hf_predict(&p,&t,low,(MAXS+2u)*LO,MAXS,
                                     &coeff,&chirp,high,MAXS*HI));
    identity(&p,low,high,MAXS);
    assert(memcmp(tail,&high[MAXS*HI],sizeof(tail))==0);
    assert(high[0*HI+16].re==low[2*LO+2].re);
    assert(high[39*HI+29].im==low[41*LO+15].im);
}

static void test_complex_prediction(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    fixture(96000,16,48,&m,&t,&p);
    SautiAacSbrLpcCoeffs coeff={0};
    for(unsigned k=0;k<LO;k++){
        coeff.alpha0[k]=(SautiAacSbrComplex){0.25f,0.5f};
        coeff.alpha1[k]=(SautiAacSbrComplex){-0.125f,0.375f};
    }
    const unsigned slots=7;
    SautiAacSbrComplex low[(slots+2u)*LO],high[slots*HI];
    samples(low,slots);
    SautiAacSbrChirp chirp={0};
    const uint8_t modes[5]={1,3,2,1,0};
    assert(sauti_aac_sbr_chirp_update(modes,t.n_noise,&chirp));
    assert(sauti_aac_sbr_hf_predict(&p,&t,low,(slots+2u)*LO,slots,
                                     &coeff,&chirp,high,slots*HI));
    for(unsigned k=0;k<HI;k++)for(unsigned j=0;j<slots;j++){
        const int src=p.source_for_target[k];
        if(src<0){
            assert(high[j*HI+k].re==0.0f && high[j*HI+k].im==0.0f);
            continue;
        }
        unsigned g=0;
        while(g+1<t.n_noise && k>=t.noise[g+1u])g++;
        const double bw=chirp.bw[g],bw2=bw*bw;
        const SautiAacSbrComplex x0=low[(j+2u)*LO+(unsigned)src];
        const SautiAacSbrComplex x1=low[(j+1u)*LO+(unsigned)src];
        const SautiAacSbrComplex x2=low[j*LO+(unsigned)src];
        const double re=x0.re+bw*(0.25*x1.re-0.5*x1.im)+
                        bw2*(-0.125*x2.re-0.375*x2.im);
        const double im=x0.im+bw*(0.25*x1.im+0.5*x1.re)+
                        bw2*(-0.125*x2.im+0.375*x2.re);
        approx(high[j*HI+k].re,re);
        approx(high[j*HI+k].im,im);
    }
    /* Even when caller's low/high ranges overlap, staging preserves
     * preceding 2 QMF slots and eliminates feedback contamination. */
    SautiAacSbrComplex alias[slots*HI];
    memset(alias,0xab,sizeof(alias));
    memcpy(alias,low,sizeof(low));
    assert(sauti_aac_sbr_hf_predict(&p,&t,alias,slots*HI,slots,
                                     &coeff,&chirp,alias,slots*HI));
    assert(memcmp(alias,high,sizeof(high))==0);
}

static void test_errors_no_output_changes(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    fixture(48000,16,32,&m,&t,&p);
    const unsigned slots=5;
    SautiAacSbrComplex low[(slots+2u)*LO],out[slots*HI],saved[slots*HI];
    samples(low,slots);
    memset(out,0x7e,sizeof(out));
    memcpy(saved,out,sizeof(saved));
    SautiAacSbrLpcCoeffs coeff={0};
    SautiAacSbrChirp chirp={0};
    const uint8_t modes[5]={3,1};
    assert(sauti_aac_sbr_chirp_update(modes,t.n_noise,&chirp));
#define REJECT(P,T,L,LEN,S,C,B,O,ON) do {                           \
    assert(!sauti_aac_sbr_hf_predict((P),(T),(L),(LEN),(S),         \
                                   (C),(B),(O),(ON)));              \
    assert(memcmp(out,saved,sizeof(saved))==0);                     \
} while(0)
    REJECT(NULL,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    REJECT(&p,NULL,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    REJECT(&p,&t,NULL,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    REJECT(&p,&t,low,(slots+2u)*LO,slots,NULL,&chirp,out,slots*HI);
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,NULL,out,slots*HI);
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,NULL,slots*HI);
    REJECT(&p,&t,low,(slots+2u)*LO,0,&coeff,&chirp,out,slots*HI);
    REJECT(&p,&t,low,(slots+2u)*LO,MAXS+1u,&coeff,&chirp,out,slots*HI);
    REJECT(&p,&t,low,(slots+2u)*LO-1u,slots,&coeff,&chirp,out,slots*HI);
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI-1u);
    p.source_for_target[16]=99;
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    p.source_for_target[16]=2;
    coeff.alpha0[2].im=NAN;
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    coeff.alpha0[2].im=0;
    coeff.alpha1[2].re=4.0f; /* norm^2 must stay < 16 */
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    coeff.alpha1[2].re=0;
    chirp.bw[0]=INFINITY;
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    chirp.bw[0]=0.7f;
    chirp.ready=0;
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    chirp.ready=1u;
    chirp.count=t.n_noise+1u;
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    chirp.count=t.n_noise;
    const unsigned source=p.source_for_target[29];
    assert(source<LO);
    low[(slots+1u)*LO+source].re=NAN;
    REJECT(&p,&t,low,(slots+2u)*LO,slots,&coeff,&chirp,out,slots*HI);
    low[(slots+1u)*LO+source].re=0.0f;
    /* Nonmapped low QMF bins do not influence the output. */
    low[0].im=NAN;
    assert(sauti_aac_sbr_hf_predict(&p,&t,low,(slots+2u)*LO,slots,
                                     &coeff,&chirp,out,slots*HI));
#undef REJECT
}

static void test_frequency_chain(void) {
    const unsigned rates[]={16000u,22050u,24000u,32000u,
                            44100u,48000u,64000u,88200u,
                            96000u,128000u,176400u,192000u};
    SautiAacSbrComplex low[4*LO],high[2*HI];
    SautiAacSbrLpcCoeffs coeff={0};
    samples(low,2);
    unsigned accepted=0,denied=0;
    for(unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);r++)
      for(unsigned start=0;start<16;start+=3)
        for(unsigned stop=0;stop<16;stop+=3){
            SautiAacSbrLimits limits;
            if(!sauti_aac_sbr_limits_compute(rates[r],start,stop,&limits))
                continue;
            for(unsigned scale=0;scale<4;scale++)
              for(unsigned alter=0;alter<2;alter++)
                for(unsigned cross=0;cross<8;cross++){
                    SautiAacSbrMaster master;
                    const int ok=scale==0
                      ?sauti_aac_sbr_master_linear(limits.k0,limits.k2,
                                                    alter,cross,&master)
                      :sauti_aac_sbr_master_log(limits.k0,limits.k2,
                                                 scale,alter,cross,&master);
                    if(!ok)continue;
                    SautiAacSbrTables tables;
                    if(!sauti_aac_sbr_tables_build(&master,2,&tables))
                        continue;
                    SautiAacSbrPatches patches;
                    if(!sauti_aac_sbr_patch_build(rates[r],&master,
                                                  &tables,&patches)){
                        denied++;continue;
                    }
                    const uint8_t modes[5]={2,3,1,0,2};
                    SautiAacSbrChirp chirp={0};
                    assert(sauti_aac_sbr_chirp_update(modes,tables.n_noise,
                                                      &chirp));
                    assert(sauti_aac_sbr_hf_predict(
                        &patches,&tables,low,4u*LO,2u,&coeff,&chirp,
                        high,2u*HI));
                    identity(&patches,low,high,2u);
                    accepted++;
                }
        }
    assert(accepted>500u && denied>10u);
    printf("SBR-13 chain: %u complex HF predictions, %u patch rejects\n",
           accepted,denied);
}

int main(void) {
    test_chirp_transitions();
    test_chirp_atomic_errors();
    test_40_slots_and_identity();
    test_complex_prediction();
    test_errors_no_output_changes();
    test_frequency_chain();
    puts("PASS HE-AAC SBR-13 chirp and complex HF predictor: 2-lag, noise bands, 40 slots, rollback");
    return 0;
}
