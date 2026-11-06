#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Independent published SBR stop-frequency landmark tables (0..13).
 * Sorting their first differences yields expected bs_stop_freq boundaries. */
static const struct {
    unsigned rate;
    uint8_t stops[14];
} golden[] = {
    {16000,{48,49,50,51,52,54,55,56,57,59,60,61,63,64}},
    {22050,{35,37,38,40,42,44,46,48,51,53,56,58,61,64}},
    {24000,{32,34,36,38,40,42,44,46,49,52,55,58,61,64}},
    {32000,{32,34,36,38,40,42,44,46,49,52,55,58,61,64}},
    {44100,{23,25,27,29,32,34,37,40,43,47,51,55,59,64}},
    {48000,{21,23,25,27,30,32,35,38,42,45,49,54,59,64}},
    {64000,{20,22,24,26,29,31,34,37,41,45,49,54,59,64}},
    {88200,{15,17,19,21,23,26,29,33,37,41,46,51,57,64}},
    {96000,{13,15,17,19,21,24,27,31,35,39,44,50,57,64}},
    {192000,{7,8,10,12,14,16,19,23,27,32,38,46,54,64}}
};

static void test_stops(void) {
    for (unsigned r=0; r<sizeof(golden)/sizeof(golden[0]); ++r) {
        int widths[13];
        for(unsigned i=0;i<13;i++)
            widths[i] = golden[r].stops[i+1]-golden[r].stops[i];
        for(unsigned i=1;i<13;i++) {
            int val=widths[i];unsigned j=i;
            while(j && widths[j-1]>val){widths[j]=widths[j-1];--j;}
            widths[j]=val;
        }
        unsigned expect=golden[r].stops[0];
        for(unsigned stop=0;stop<14;++stop) {
            if(stop)expect+=(unsigned)widths[stop-1];
            SautiAacSbrLimits out;
            unsigned successes=0;
            for(unsigned start=0;start<16;++start) {
                int ok=sauti_aac_sbr_limits_compute(golden[r].rate,start,stop,&out);
                if(ok) {
                    ++successes;
                    if(out.k2!=expect) {
                        fprintf(stderr,"stop mismatch rate %u, start %u stop %u: %u vs %u\n",
                                golden[r].rate,start,stop,out.k2,expect);
                        assert(0);
                    }
                }
            }
            (void)successes; /* Some encoded stops have no valid k0 at a rate. */
        }
    }
}

static void test_examples(void) {
    SautiAacSbrLimits out;
    assert(sauti_aac_sbr_limits_compute(32000,0,0,&out));
    assert(out.k0==10 && out.k2==32 && out.sample_rate==32000);
    assert(sauti_aac_sbr_limits_compute(44100,4,0,&out));
    assert(out.k0==13 && out.k2==23);
    assert(sauti_aac_sbr_limits_compute(48000,5,0,&out));
    assert(out.k0==13 && out.k2==21);
    assert(sauti_aac_sbr_limits_compute(96000,2,0,&out));
    assert(out.k0==7 && out.k2==13);
    assert(sauti_aac_sbr_limits_compute(32000,0,14,&out));
    assert(out.k0==10 && out.k2==20);
    assert(sauti_aac_sbr_limits_compute(32000,0,15,&out));
    assert(out.k0==10 && out.k2==30);
}

static void test_fail_closed(void) {
    SautiAacSbrLimits out, original;
    memset(&out,0xa5,sizeof(out)); original=out;
    static const unsigned invalid_rates[] = {0,8000,12000,16001,44101,384000,UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid_rates)/sizeof(invalid_rates[0]);++i) {
        assert(!sauti_aac_sbr_limits_compute(invalid_rates[i],0,0,&out));
        assert(memcmp(&out,&original,sizeof(out))==0);
    }
    assert(!sauti_aac_sbr_limits_compute(32000,16,0,&out));
    assert(!sauti_aac_sbr_limits_compute(32000,0,16,&out));
    assert(!sauti_aac_sbr_limits_compute(32000,0,0,NULL));
    assert(memcmp(&out,&original,sizeof(out))==0);
}
static void test_sweep(void) {
    unsigned accepted=0,rejected=0;
    static const unsigned rates[] = {16000,22050,24000,32000,44100,48000,64000,88200,96000,128000,176400,192000};
    for(unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);++r)
      for(unsigned start=0;start<=16;++start)
        for(unsigned stop=0;stop<=16;++stop) {
            SautiAacSbrLimits out,saved;
            memset(&out,0x5a,sizeof(out));saved=out;
            int ok=sauti_aac_sbr_limits_compute(rates[r],start,stop,&out);
            if(!ok) {
                assert(memcmp(&out,&saved,sizeof(out))==0);
                rejected++; continue;
            }
            accepted++;
            assert(out.sample_rate==rates[r] && out.k0>0 &&
                   out.k0<out.k2 && out.k2<=64u);
            unsigned widthmax=rates[r]<=32000?48:rates[r]==44100?35:32;
            assert(out.k2-out.k0<=widthmax);
        }
    printf("SBR limits sweep: %u accepted, %u rejected safely\n",accepted,rejected);
    assert(accepted>1000 && rejected>100);
}
int main(void){
    test_stops();test_examples();test_fail_closed();test_sweep();
    puts("PASS HE-AAC SBR start/stop limits: known rates, golden stop tables, fail-closed");
    return 0;
}
