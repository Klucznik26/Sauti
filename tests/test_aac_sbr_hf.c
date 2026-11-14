#include "sauti_aac_sbr_hf.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NLOW SAUTI_AAC_SBR_HF_LOW_BANDS
#define NHIGH SAUTI_AAC_SBR_HF_HIGH_BANDS
#define NSLOTS SAUTI_AAC_SBR_HF_MAX_SLOTS

static void setup(unsigned rate, unsigned k0, unsigned k2,
                  SautiAacSbrMaster *m, SautiAacSbrTables *t,
                  SautiAacSbrPatches *p) {
    assert(sauti_aac_sbr_master_linear(k0,k2,1,0,m));
    assert(sauti_aac_sbr_tables_build(m,1,t));
    assert(sauti_aac_sbr_patch_build(rate,m,t,p));
}
static void low_fill(SautiAacSbrComplex *low,unsigned slots) {
    for(unsigned s=0;s<slots;s++)
        for(unsigned k=0;k<NLOW;k++){
            const unsigned j=s*NLOW+k;
            low[j].re=(float)(17u*s+k+1u)*0.125f;
            low[j].im=-((float)(5u*s+2u*k+3u)*0.0625f);
        }
}
static void expect_transposition(const SautiAacSbrComplex *low,
                                  const SautiAacSbrComplex *high,
                                  unsigned slots,
                                  const SautiAacSbrPatches *p) {
    SautiAacSbrComplex zero={0};
    for(unsigned s=0;s<slots;s++)
        for(unsigned k=0;k<NHIGH;k++){
            const int src=p->source_for_target[k];
            const SautiAacSbrComplex *expected=src>=0
                ? &low[s*NLOW+(unsigned)src] : &zero;
            assert(memcmp(&high[s*NHIGH+k],expected,
                          sizeof(*expected))==0);
        }
}

static void test_reference_and_40_slots(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    setup(48000,16,32,&m,&t,&p);
    assert(p.count==1u && p.start[0]==2u && p.target[0]==16u &&
           p.width[0]==14u);
    SautiAacSbrComplex low[NSLOTS*NLOW];
    SautiAacSbrComplex high[NSLOTS*NHIGH+3u];
    low_fill(low,NSLOTS);
    memset(high,0xcd,sizeof(high));
    SautiAacSbrComplex guard[3];
    memcpy(guard,&high[NSLOTS*NHIGH],sizeof(guard));
    assert(sauti_aac_sbr_hf_transpose(&p,&t,low,NSLOTS*NLOW,NSLOTS,
                                       high,NSLOTS*NHIGH));
    expect_transposition(low,high,NSLOTS,&p);
    assert(memcmp(guard,&high[NSLOTS*NHIGH],sizeof(guard))==0);
    assert(high[0*NHIGH+16].re==low[0*NLOW+2].re);
    assert(high[17*NHIGH+29].im==low[17*NLOW+15].im);
    assert(high[17*NHIGH+30].re==0.0f &&
           high[17*NHIGH+30].im==0.0f);
    printf("SBR-12 golden: %u complex QMF slots, 14 mapped + 2 omitted bins\n",
           NSLOTS);
}

static void test_multi_patches_and_alias(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    setup(96000,16,48,&m,&t,&p);
    assert(p.count==3u);
    const unsigned slots=32u;
    SautiAacSbrComplex low[slots*NLOW];
    SautiAacSbrComplex out[slots*NHIGH];
    low_fill(low,slots);
    assert(sauti_aac_sbr_hf_transpose(&p,&t,low,slots*NLOW,slots,
                                       out,slots*NHIGH));
    expect_transposition(low,out,slots,&p);

    /* Full input/output overlap is allowed; all reads happen before the
     * output commit because reconstruction uses a bounded staging array. */
    SautiAacSbrComplex aliased[slots*NHIGH];
    memset(aliased,0xab,sizeof(aliased));
    memcpy(aliased,low,sizeof(low));
    assert(sauti_aac_sbr_hf_transpose(&p,&t,aliased,slots*NHIGH,slots,
                                       aliased,slots*NHIGH));
    assert(memcmp(out,aliased,sizeof(out))==0);
}

static void test_error_atomicity(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    setup(48000,16,32,&m,&t,&p);
    const unsigned slots=30;
    SautiAacSbrComplex low[slots*NLOW];
    SautiAacSbrComplex high[slots*NHIGH],saved[slots*NHIGH];
    low_fill(low,slots);
    memset(high,0xa7,sizeof(high));memcpy(saved,high,sizeof(saved));
#define REJECT(P,T,LP,LN,S,HP,HN) do {                               \
    assert(!sauti_aac_sbr_hf_transpose((P),(T),(LP),(LN),(S),         \
                                       (HP),(HN)));                   \
    assert(memcmp(saved,high,sizeof(saved))==0);                     \
} while(0)
    REJECT(NULL,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    REJECT(&p,NULL,low,slots*NLOW,slots,high,slots*NHIGH);
    REJECT(&p,&t,NULL,slots*NLOW,slots,high,slots*NHIGH);
    REJECT(&p,&t,low,slots*NLOW,slots,NULL,slots*NHIGH);
    REJECT(&p,&t,low,slots*NLOW,0,high,slots*NHIGH);
    REJECT(&p,&t,low,slots*NLOW,NSLOTS+1u,high,slots*NHIGH);
    REJECT(&p,&t,low,slots*NLOW-1u,slots,high,slots*NHIGH);
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH-1u);
    p.count=0;
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    p.count=1;p.target[0]=17u;
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    p.target[0]=16u;p.width[0]=15u;
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    p.width[0]=14u;
    p.source_for_target[17]=-2;
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    p.source_for_target[17]=3;
    p.start[0]=32u;
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    p.start[0]=2u;
    t.kx=33u;
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    t.kx=16u;
    t.low[1]=17;
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    /* restore a known-good original layout */
    setup(48000,16,32,&m,&t,&p);

    /* A single bad complex sample in the last requested time slot
     * must leave the ENTIRE output unchanged. */
    const int src=p.source_for_target[29];
    assert(src>=0);
    const unsigned index=(slots-1u)*NLOW+(unsigned)src;
    const float old=low[index].im;
    low[index].im=NAN;
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    low[index].im=INFINITY;
    REJECT(&p,&t,low,slots*NLOW,slots,high,slots*NHIGH);
    low[index].im=old;

    /* Unused low bins do not contribute and need not be finite. */
    low[0].re=NAN;
    assert(sauti_aac_sbr_hf_transpose(&p,&t,low,slots*NLOW,slots,
                                       high,slots*NHIGH));
    assert(high[16].re==low[2].re);
#undef REJECT
}
static void test_1_slot(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    setup(48000,16,32,&m,&t,&p);
    SautiAacSbrComplex low[NLOW],out[NHIGH];
    low_fill(low,1);
    assert(sauti_aac_sbr_hf_transpose(&p,&t,low,NLOW,1,out,NHIGH));
    expect_transposition(low,out,1,&p);
}

static void test_frequency_chain(void) {
    static const unsigned rates[]={16000u,22050u,24000u,32000u,
                                   44100u,48000u,64000u,88200u,
                                   96000u,128000u,176400u,192000u};
    SautiAacSbrComplex low[2u*NLOW],high[2u*NHIGH];
    low_fill(low,2u);
    unsigned valid=0,rejected=0;
    for(unsigned ri=0;ri<sizeof(rates)/sizeof(rates[0]);ri++)
      for(unsigned start=0;start<16u;start+=3u)
        for(unsigned stop=0;stop<16u;stop+=3u) {
            SautiAacSbrLimits lim;
            if(!sauti_aac_sbr_limits_compute(rates[ri],start,stop,&lim))
                continue;
            for(unsigned scale=0;scale<4u;scale++)
              for(unsigned alter=0;alter<2u;alter++)
                for(unsigned xover=0;xover<8u;xover++) {
                    SautiAacSbrMaster m;
                    int ok=scale==0u
                        ? sauti_aac_sbr_master_linear(lim.k0,lim.k2,
                                                      alter,xover,&m)
                        : sauti_aac_sbr_master_log(lim.k0,lim.k2,
                                                   scale,alter,xover,&m);
                    if(!ok)continue;
                    SautiAacSbrTables t;
                    if(!sauti_aac_sbr_tables_build(&m,2,&t))continue;
                    SautiAacSbrPatches p;
                    if(!sauti_aac_sbr_patch_build(rates[ri],&m,&t,&p)){
                        rejected++;continue;
                    }
                    assert(sauti_aac_sbr_hf_transpose(
                            &p,&t,low,2u*NLOW,2u,high,2u*NHIGH));
                    expect_transposition(low,high,2u,&p);
                    valid++;
                }
        }
    assert(valid>500u && rejected>10u);
    printf("SBR-12 QMF mapping chain: %u valid, %u patch configurations rejected\n",
           valid,rejected);
}

int main(void) {
    test_reference_and_40_slots();
    test_multi_patches_and_alias();
    test_error_atomicity();
    test_1_slot();
    test_frequency_chain();
    puts("PASS HE-AAC SBR-12 complex QMF baseline: 40-slot, multi-patch, overlap, rollback, no external codec");
    return 0;
}
