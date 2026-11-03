#include "sauti_aac_sbr_tables.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static SautiAacSbrMaster make_master(const uint8_t *edges,
                                    unsigned count, unsigned crossover) {
    SautiAacSbrMaster m;
    memset(&m, 0, sizeof(m));
    assert(count <= SAUTI_AAC_SBR_QMF_BANDS && crossover < count);
    m.k0=edges[0]; m.k2=edges[count]; m.count=count;
    m.crossover_band=crossover; m.kx=edges[crossover];
    memcpy(m.edges,edges,count+1u);
    return m;
}

static void test_golden(void) {
    const uint8_t even[]={10,12,14,16,18,20,22,24,26};
    const uint8_t even_hi[]={14,16,18,20,22,24,26};
    const uint8_t even_lo[]={14,18,22,26};
    const uint8_t even_noise[]={14,18,26};
    SautiAacSbrMaster m=make_master(even,8,2);
    SautiAacSbrTables t;
    assert(sauti_aac_sbr_tables_build(&m,2,&t));
    assert(t.kx==14 && t.m==12 && t.n_high==6 &&
           t.n_low==3 && t.n_noise==2);
    assert(memcmp(t.high,even_hi,sizeof(even_hi))==0);
    assert(memcmp(t.low,even_lo,sizeof(even_lo))==0);
    assert(memcmp(t.noise,even_noise,sizeof(even_noise))==0);

    const uint8_t odd[]={12,14,17,20,24,28,32};
    const uint8_t odd_hi[]={14,17,20,24,28,32};
    const uint8_t odd_lo[]={14,17,24,32};
    const uint8_t odd_noise[]={14,17,32};
    m=make_master(odd,6,1);
    assert(sauti_aac_sbr_tables_build(&m,2,&t));
    assert(t.n_high==5 && t.n_low==3 && t.n_noise==2);
    assert(memcmp(t.high,odd_hi,sizeof(odd_hi))==0);
    assert(memcmp(t.low,odd_lo,sizeof(odd_lo))==0);
    assert(memcmp(t.noise,odd_noise,sizeof(odd_noise))==0);
    assert(sauti_aac_sbr_tables_build(&m,0,&t));
    assert(t.n_noise==1 && t.noise[0]==14 && t.noise[1]==32);

    const uint8_t single[]={18,20,24};
    m=make_master(single,2,1);
    assert(sauti_aac_sbr_tables_build(&m,3,&t));
    assert(t.n_high==1 && t.n_low==1 && t.n_noise==1);
    assert(t.high[0]==20 && t.high[1]==24 &&
           t.low[0]==20 && t.low[1]==24 &&
           t.noise[0]==20 && t.noise[1]==24);
}

static void test_fail_closed(void) {
    const uint8_t edges[]={8,9,32};
    SautiAacSbrMaster m=make_master(edges,2,0);
    SautiAacSbrTables t,previous;
    memset(&t,0xa5,sizeof(t)); previous=t;
    assert(!sauti_aac_sbr_tables_build(&m,1,&t)); /* nQ=2 > nLow=1 */
    assert(!sauti_aac_sbr_tables_build(&m,4,&t));
    assert(!sauti_aac_sbr_tables_build(NULL,0,&t));
    assert(!sauti_aac_sbr_tables_build(&m,0,NULL));
    m.kx=9;
    assert(!sauti_aac_sbr_tables_build(&m,0,&t));
    m.kx=8; m.edges[1]=8;
    assert(!sauti_aac_sbr_tables_build(&m,0,&t));
    m.edges[1]=9; m.edges[2]=65;
    assert(!sauti_aac_sbr_tables_build(&m,0,&t));
    m.edges[2]=32; m.crossover_band=8;
    assert(!sauti_aac_sbr_tables_build(&m,0,&t));
    m.crossover_band=0; m.count=3;
    assert(!sauti_aac_sbr_tables_build(&m,0,&t));
    m.count=2; m.kx=33;
    assert(!sauti_aac_sbr_tables_build(&m,0,&t));

    /* More than five noise floors is invalid even with enough low bands. */
    const uint8_t broad[]={8,9,10,11,12,13,14,15,16,17,20,40,64};
    m=make_master(broad,12,0);
    assert(!sauti_aac_sbr_tables_build(&m,3,&t));
    assert(memcmp(&t,&previous,sizeof(t))==0);
}

static void test_chain(void) {
    unsigned accepted=0,rejected=0;
    static const unsigned rates[]={
        16000,22050,24000,32000,44100,48000,64000,88200,96000
    };
    for (unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);++r)
      for (unsigned start=0;start<16;start+=3)
        for (unsigned stop=0;stop<16;stop+=3) {
            SautiAacSbrLimits limits;
            if (!sauti_aac_sbr_limits_compute(rates[r],start,stop,&limits))
                continue;
            for (unsigned scale=0;scale<4;++scale)
              for (unsigned alter=0;alter<2;++alter)
                for (unsigned xover=0;xover<8;++xover) {
                    SautiAacSbrMaster m;
                    int valid=scale==0
                        ? sauti_aac_sbr_master_linear(limits.k0,limits.k2,
                                                      alter,xover,&m)
                        : sauti_aac_sbr_master_log(limits.k0,limits.k2,
                                                   scale,alter,xover,&m);
                    if (!valid) continue;
                    for (unsigned noise=0;noise<4;++noise) {
                        SautiAacSbrTables t,previous;
                        memset(&t,0xa5,sizeof(t)); previous=t;
                        if (!sauti_aac_sbr_tables_build(&m,noise,&t)) {
                            assert(memcmp(&t,&previous,sizeof(t))==0);
                            ++rejected;
                            continue;
                        }
                        ++accepted;
                        assert(t.kx==m.kx && t.m==m.k2-m.kx &&
                               t.n_high==m.count-m.crossover_band &&
                               t.n_low==(t.n_high+1u)/2u &&
                               t.n_noise>=1 && t.n_noise<=5);
                        assert(t.high[0]==t.low[0] &&
                               t.low[0]==t.noise[0]);
                        assert(t.high[t.n_high]==m.k2 &&
                               t.low[t.n_low]==m.k2 &&
                               t.noise[t.n_noise]==m.k2);
                        for(unsigned i=1;i<=t.n_high;i++)
                            assert(t.high[i]>t.high[i-1u]);
                        for(unsigned i=1;i<=t.n_low;i++)
                            assert(t.low[i]>t.low[i-1u]);
                        for(unsigned i=1;i<=t.n_noise;i++)
                            assert(t.noise[i]>t.noise[i-1u]);
                    }
                }
        }
    assert(accepted>100 && rejected>10);
    printf("SBR-05 chain: %u valid, %u rejected safely\n",
           accepted,rejected);
}
int main(void) {
    test_golden();test_fail_closed();test_chain();
    puts("PASS HE-AAC SBR high/low/noise: even/odd, master/limits chain, fail-closed");
    return 0;
}
