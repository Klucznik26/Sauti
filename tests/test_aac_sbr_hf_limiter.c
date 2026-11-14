#include "sauti_aac_sbr_hf_limiter.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define B SAUTI_AAC_SBR_HF_HIGH_BANDS
#define MAXS SAUTI_AAC_SBR_HF_MAX_SLOTS

static void near(double actual,double expected) {
    assert(isfinite(actual) && isfinite(expected));
    assert(fabs(actual-expected)<=2e-5*fmax(1.0,fabs(expected)));
}
static void fixture(unsigned k0,unsigned k2,
                    SautiAacSbrMaster *m,SautiAacSbrTables *t) {
    assert(sauti_aac_sbr_master_linear(k0,k2,1,0,m));
    assert(sauti_aac_sbr_tables_build(m,2,t));
}
static void targets(const SautiAacSbrTables *t,
                    SautiAacSbrEnergyFrame *energy,
                    unsigned n,double value) {
    memset(energy,0,sizeof(*energy));
    energy->env_count=n;
    energy->noise_count=n==1u?1u:2u;
    for(unsigned e=0;e<n;e++){
        energy->freq_res[e]=(uint8_t)(e&1u);
        const unsigned count=energy->freq_res[e]?t->n_high:t->n_low;
        for(unsigned j=0;j<count;j++)
            energy->env_energy[e][j]=value;
    }
}
static void samples(SautiAacSbrComplex *x,unsigned slots,float value) {
    for(unsigned s=0;s<slots;s++)
        for(unsigned k=0;k<B;k++){
            x[s*B+k].re=value;
            x[s*B+k].im=0.0f;
        }
}
static void test_four_gain_modes(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    fixture(16,32,&m,&t);
    assert(t.low[0]==16 && t.low[1]>17);
    SautiAacSbrEnergyFrame energy;
    targets(&t,&energy,1u,4.0);
    const uint8_t env[2]={0,4};
    SautiAacSbrComplex src[4u*B],out[4u*B];
    SautiAacSbrLimiterReport info;
    samples(src,4u,1.0f);
    for(unsigned s=0;s<4u;s++)
        for(unsigned k=t.kx;k<t.kx+t.m;k++)
            src[s*B+k].re=(k&1u)?3.0f:1.0f;

    static const double factors[4]={0.70795,1.0,1.41254,1.0e10};
    double lowmode=0.0;
    for(unsigned mode=0;mode<4u;mode++){
        assert(sauti_aac_sbr_hf_limit(&t,&energy,env,4u,t.low,
                t.n_low,mode,src,4u*B,out,4u*B,&info));
        assert(info.env_count==1u && info.limiter_count==t.n_low);
        const unsigned left=t.low[0],right=t.low[1];
        double sum_src=0.0,sum_target=0.0;
        for(unsigned k=left;k<right;k++){
            sum_src+=(k&1u)?9.0:1.0;
            sum_target+=4.0;
        }
        const double cap=fmin(100000.0,factors[mode]*
                             sqrt(sum_target/sum_src));
        double after=0.0;
        for(unsigned k=left;k<right;k++){
            const double x=(k&1u)?3.0:1.0;
            const double g=fmin(2.0/x,cap);
            after+=x*x*g*g;
        }
        const double boost=fmin(1.584893192,sqrt(sum_target/after));
        near(info.limiter_max[0][0],cap);
        near(info.limiter_boost[0][0],boost);
        for(unsigned s=0;s<4u;s++)
            for(unsigned k=left;k<right;k++){
                const double x=(k&1u)?3.0:1.0;
                near(out[s*B+k].re,x*fmin(2.0/x,cap)*boost);
                near(out[s*B+k].im,0.0);
                near(info.applied_gain[0][k],fmin(2.0/x,cap)*boost);
            }
        if(mode==0u)lowmode=out[16].re;
        if(mode==3u){
            near(out[16].re,2.0);
            near(out[17].re,2.0);
            near(info.limiter_boost[0][0],1.0);
            assert(out[16].re>lowmode);
        }
        for(unsigned s=0;s<4u;s++)
            for(unsigned k=0;k<B;k++)
                if(k<t.kx||k>=t.kx+t.m)
                    assert(memcmp(&src[s*B+k],&out[s*B+k],
                                  sizeof(src[0]))==0);
    }
}

static void test_40_slots_five_envelopes_and_alias(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    fixture(16,48,&m,&t);
    SautiAacSbrEnergyFrame energy;
    targets(&t,&energy,5u,1.0);
    const uint8_t env[6]={0,8,16,24,32,40};
    for(unsigned e=0;e<5u;e++){
        const unsigned count=energy.freq_res[e]?t.n_high:t.n_low;
        for(unsigned b=0;b<count;b++)
            energy.env_energy[e][b]=(double)(e+1u)*(e+1u);
    }
    SautiAacSbrComplex src[MAXS*B],out[MAXS*B+2u];
    samples(src,MAXS,1.0f);
    memset(out,0xab,sizeof(out));
    SautiAacSbrComplex guard[2];
    memcpy(guard,&out[MAXS*B],sizeof(guard));
    SautiAacSbrLimiterReport info;
    assert(sauti_aac_sbr_hf_limit(&t,&energy,env,MAXS,
            t.low,t.n_low,3u,src,MAXS*B,out,MAXS*B,&info));
    for(unsigned e=0;e<5u;e++)
        for(unsigned s=env[e];s<env[e+1u];s++)
            for(unsigned k=t.kx;k<t.kx+t.m;k++){
                near(out[s*B+k].re,(double)e+1.0);
                near(info.applied_gain[e][k],(double)e+1.0);
            }
    assert(memcmp(guard,&out[MAXS*B],sizeof(guard))==0);

    SautiAacSbrComplex aliased[MAXS*B];
    memcpy(aliased,src,sizeof(src));
    SautiAacSbrLimiterReport report2;
    assert(sauti_aac_sbr_hf_limit(&t,&energy,env,MAXS,
            t.low,t.n_low,3u,aliased,MAXS*B,aliased,MAXS*B,&report2));
    assert(memcmp(out,aliased,sizeof(aliased))==0);
    assert(memcmp(&info,&report2,sizeof(info))==0);
    puts("SBR-16: 40 slots, 5 envelopes, 32 complex high QMF bands");
}

static void test_fail_closed(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    fixture(16,32,&m,&t);
    SautiAacSbrEnergyFrame energy;
    targets(&t,&energy,2u,4.0);
    uint8_t env[3]={0,3,8};
    uint8_t lim[65]={0};
    memcpy(lim,t.low,t.n_low+1u);
    const unsigned slots=8u;
    SautiAacSbrComplex src[8u*B],out[8u*B],saved[8u*B];
    samples(src,slots,1.0f);
    memset(out,0x63,sizeof(out));memcpy(saved,out,sizeof(out));
    SautiAacSbrLimiterReport report,original;
    memset(&report,0x9b,sizeof(report));original=report;
#define REJECT(T,E,Q,N,L,C,G,I,IC,O,OC,R) do {                         \
    assert(!sauti_aac_sbr_hf_limit((T),(E),(Q),(N),(L),(C),(G),       \
                                   (I),(IC),(O),(OC),(R)));          \
    assert(memcmp(out,saved,sizeof(out))==0);                          \
    assert(memcmp(&report,&original,sizeof(report))==0);               \
} while(0)
    REJECT(NULL,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    REJECT(&t,NULL,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    REJECT(&t,&energy,NULL,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    REJECT(&t,&energy,env,slots,NULL,t.n_low,1,src,slots*B,out,slots*B,&report);
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,NULL,slots*B,out,slots*B,&report);
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,NULL,slots*B,&report);
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,NULL);
    REJECT(&t,&energy,env,0,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    REJECT(&t,&energy,env,MAXS+1u,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    REJECT(&t,&energy,env,slots,lim,0,1,src,slots*B,out,slots*B,&report);
    REJECT(&t,&energy,env,slots,lim,t.n_low,4,src,slots*B,out,slots*B,&report);
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B-1u,out,slots*B,&report);
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B-1u,&report);
    lim[1]=lim[0];
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    lim[1]=t.low[1];lim[0]++;
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    lim[0]=t.kx;lim[t.n_low]--;
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    lim[t.n_low]=(uint8_t)(t.kx+t.m);
    env[1]=0;
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    env[1]=3;env[2]=7;
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    env[2]=8;energy.freq_res[1]=2;
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    energy.freq_res[1]=1;energy.env_energy[1][0]=NAN;
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    energy.env_energy[1][0]=4.0;
    src[7u*B+31u].im=INFINITY;
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    src[7u*B+31u].im=0.0f;
    for(unsigned s=0;s<3;s++)
        src[s*B+16u].re=0.0f;
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
    for(unsigned s=0;s<3;s++)src[s*B+16u].re=1.0f;
    t.high[1]=t.high[0];
    REJECT(&t,&energy,env,slots,lim,t.n_low,1,src,slots*B,out,slots*B,&report);
#undef REJECT
}

static void test_zero_target_and_huge_gain(void) {
    SautiAacSbrMaster m; SautiAacSbrTables t;
    fixture(16,32,&m,&t);
    SautiAacSbrEnergyFrame energy;
    targets(&t,&energy,1u,4.0);
    const uint8_t env[2]={0,2};
    SautiAacSbrComplex src[2u*B],out[2u*B];
    SautiAacSbrLimiterReport report;
    samples(src,2u,1.0f);
    energy.env_energy[0][0]=0.0;
    assert(sauti_aac_sbr_hf_limit(&t,&energy,env,2u,
            t.low,t.n_low,1u,src,2u*B,out,2u*B,&report));
    for(unsigned s=0;s<2;s++)
        for(unsigned k=t.low[0];k<t.low[1];k++){
            assert(out[s*B+k].re==0.0f);
            assert(report.applied_gain[0][k]==0.0);
        }
    energy.env_energy[0][0]=1.0e40;
    assert(sauti_aac_sbr_hf_limit(&t,&energy,env,2u,
            t.low,t.n_low,3u,src,2u*B,out,2u*B,&report));
    near(report.limiter_max[0][0],100000.0);
    near(report.limiter_boost[0][0],1.584893192);
    near(out[16].re,100000.0*1.584893192);
}

static void test_frequency_chain(void) {
    static const unsigned rates[]={16000,22050,24000,32000,44100,48000,
                                   64000,88200,96000,128000,176400,192000};
    SautiAacSbrComplex src[2u*B],out[2u*B];
    samples(src,2u,1.0f);
    const uint8_t env[3]={0,1,2};
    unsigned accepted=0u;
    for(unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);r++)
      for(unsigned start=0;start<16;start+=3)
        for(unsigned stop=0;stop<16;stop+=3) {
            SautiAacSbrLimits lim;
            if(!sauti_aac_sbr_limits_compute(rates[r],start,stop,&lim))
                continue;
            for(unsigned scale=0;scale<4;scale++)
              for(unsigned alter=0;alter<2;alter++)
                for(unsigned cross=0;cross<8;cross++){
                    SautiAacSbrMaster master;
                    const int ok=scale==0
                        ?sauti_aac_sbr_master_linear(lim.k0,lim.k2,
                                                    alter,cross,&master)
                        :sauti_aac_sbr_master_log(lim.k0,lim.k2,
                                                   scale,alter,cross,&master);
                    if(!ok)continue;
                    SautiAacSbrTables t;
                    if(!sauti_aac_sbr_tables_build(&master,2,&t))
                        continue;
                    SautiAacSbrEnergyFrame energy;
                    targets(&t,&energy,2u,4.0);
                    SautiAacSbrLimiterReport report;
                    const unsigned mode=scale;
                    assert(sauti_aac_sbr_hf_limit(&t,&energy,env,2u,
                        t.low,t.n_low,mode,src,2u*B,out,2u*B,&report));
                    accepted++;
                    for(unsigned s=0;s<2;s++)
                        for(unsigned k=t.kx;k<t.kx+t.m;k++)
                            near(out[s*B+k].re,2.0);
                }
        }
    assert(accepted>1000u);
    printf("SBR-16 gain-limiter sweep: %u valid envelope/limiter configurations\n",
           accepted);
}

int main(void){
    test_four_gain_modes();
    test_40_slots_five_envelopes_and_alias();
    test_fail_closed();
    test_zero_target_and_huge_gain();
    test_frequency_chain();
    puts("PASS HE-AAC SBR-16 capped HF gain and boost: 4 modes, 40 slots, rollback");
    return 0;
}
