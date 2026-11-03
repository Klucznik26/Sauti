#include "sauti_aac_sbr_limiter_bands.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define H SAUTI_AAC_SBR_HF_HIGH_BANDS

static void setup(unsigned k0,unsigned k2,unsigned rate,
                  SautiAacSbrMaster *master,
                  SautiAacSbrTables *tables,
                  SautiAacSbrPatches *patches) {
    assert(sauti_aac_sbr_master_linear(k0,k2,1u,0u,master));
    assert(sauti_aac_sbr_tables_build(master,2u,tables));
    assert(sauti_aac_sbr_patch_build(rate,master,tables,patches));
}
static void check(const SautiAacSbrTables *t,
                  const SautiAacSbrPatches *patch,
                  const SautiAacSbrLimiterBands *bands) {
    assert(bands->count>=1u &&
           bands->count<=SAUTI_AAC_SBR_MAX_LIMITER_BANDS);
    assert(bands->edges[0]==t->kx);
    assert(bands->edges[bands->count]==t->kx+t->m);
    for(unsigned b=1u;b<=bands->count;b++)
        assert(bands->edges[b]>bands->edges[b-1u]);
    for(unsigned p=1u;p<patch->count;p++){
        int found=0;
        for(unsigned b=1u;b<bands->count;b++)
            if(bands->edges[b]==patch->target[p])found=1;
        assert(found);
    }
    for(unsigned b=1u;b<bands->count;b++){
        const unsigned v=bands->edges[b];
        int found=0;
        for(unsigned i=1u;i<t->n_low;i++)
            if(t->low[i]==v)found=1;
        for(unsigned p=1u;p<patch->count;p++)
            if(patch->target[p]==v)found=1;
        assert(found);
    }
}
static void assert_edges(const SautiAacSbrLimiterBands *v,
                         const uint8_t *values,unsigned len) {
    assert(v->count+1u==len);
    assert(memcmp(v->edges,values,len)==0);
}
static void test_golden_modes_and_patch_junctions(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    SautiAacSbrLimiterBands freq;

    setup(16u,32u,48000u,&m,&t,&p);
    assert(p.count==1u && p.width[0]==14u);
    const uint8_t mode0[]={16,32};
    const uint8_t mode1[]={16,24,32};
    const uint8_t mode2[]={16,20,24,32};
    const uint8_t mode3[]={16,20,24,28,32};
    for(unsigned mode=0;mode<4;mode++){
        assert(sauti_aac_sbr_limiter_bands_build(&t,&p,mode,&freq));
        check(&t,&p,&freq);
        switch(mode) {
        case 0:assert_edges(&freq,mode0,sizeof(mode0));break;
        case 1:assert_edges(&freq,mode1,sizeof(mode1));break;
        case 2:assert_edges(&freq,mode2,sizeof(mode2));break;
        case 3:assert_edges(&freq,mode3,sizeof(mode3));break;
        }
    }

    /* At 96kHz a patch junction is 22, not a low-resolution edge.
     * It MUST be included even when the logarithmic ratio is too small. */
    setup(16u,32u,96000u,&m,&t,&p);
    assert(p.count==2u && p.target[1]==22u);
    const uint8_t patch1[]={16,22,32};
    const uint8_t patch2[]={16,22,32};
    const uint8_t patch3[]={16,22,28,32};
    for(unsigned mode=1u;mode<=3u;mode++){
        assert(sauti_aac_sbr_limiter_bands_build(&t,&p,mode,&freq));
        check(&t,&p,&freq);
        if(mode==1u)assert_edges(&freq,patch1,sizeof(patch1));
        if(mode==2u)assert_edges(&freq,patch2,sizeof(patch2));
        if(mode==3u)assert_edges(&freq,patch3,sizeof(patch3));
    }

    /* Multiple patch junctions: 30 and 44 are both protected. */
    setup(16u,48u,48000u,&m,&t,&p);
    assert(p.count==3u &&
           p.target[1]==30u && p.target[2]==44u);
    assert(sauti_aac_sbr_limiter_bands_build(&t,&p,1u,&freq));
    const uint8_t patchm[]={16,30,44,48};
    assert_edges(&freq,patchm,sizeof(patchm));
}

static void test_automatic_limiter_integration(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches patches;
    setup(16,48,96000,&m,&t,&patches);
    SautiAacSbrEnergyFrame energy={0};
    energy.env_count=2u;
    energy.noise_count=2u;
    energy.freq_res[0]=0u;energy.freq_res[1]=1u;
    for(unsigned i=0;i<t.n_low;i++)
        energy.env_energy[0][i]=4.0;
    for(unsigned i=0;i<t.n_high;i++)
        energy.env_energy[1][i]=16.0;

    const uint8_t env_edges[3]={0,4,8};
    SautiAacSbrComplex input[8u*H];
    for(unsigned s=0;s<8u;s++)
        for(unsigned k=0;k<H;k++){
            input[s*H+k].re=(float)((k%3u)+1u);
            input[s*H+k].im=(float)(s%3u)*0.25f;
        }

    unsigned cases=0;
    for(unsigned limiter_mode=0;limiter_mode<4u;limiter_mode++){
        SautiAacSbrLimiterBands freq;
        assert(sauti_aac_sbr_limiter_bands_build(
            &t,&patches,limiter_mode,&freq));
        for(unsigned gain_mode=0;gain_mode<4u;gain_mode++){
            SautiAacSbrComplex manual[8u*H],automatic[8u*H];
            SautiAacSbrLimiterReport report_manual,report_auto;
            assert(sauti_aac_sbr_hf_limit(
                &t,&energy,env_edges,8u,freq.edges,freq.count,
                gain_mode,input,8u*H,manual,8u*H,&report_manual));
            assert(sauti_aac_sbr_hf_limit_auto(
                &t,&patches,limiter_mode,&energy,env_edges,8u,
                gain_mode,input,8u*H,automatic,8u*H,&report_auto));
            assert(memcmp(manual,automatic,sizeof(manual))==0);
            assert(memcmp(&report_manual,&report_auto,
                          sizeof(report_manual))==0);
            cases++;
        }
    }
    printf("SBR-17: %u automated/manual limiter pairs exactly match\n",cases);
}
static void test_fail_closed(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    setup(16,32,96000,&m,&t,&p);
    SautiAacSbrLimiterBands output,original;
    memset(&output,0x7c,sizeof(output)); original=output;
#define REJECT(T,P,N) do {                                           \
    assert(!sauti_aac_sbr_limiter_bands_build((T),(P),(N),&output));\
    assert(memcmp(&output,&original,sizeof(output))==0);            \
} while (0)
    REJECT(NULL,&p,1u);
    REJECT(&t,NULL,1u);
    REJECT(&t,&p,4u);
    assert(!sauti_aac_sbr_limiter_bands_build(&t,&p,1u,NULL));
    p.count=0u;
    REJECT(&t,&p,1u);
    p.count=2u;
    p.target[1]=21u;
    REJECT(&t,&p,1u);
    p.target[1]=22u;
    p.source_for_target[16]=100;
    REJECT(&t,&p,1u);
    p.source_for_target[16]=10;
    t.low[1]=17u;
    REJECT(&t,&p,1u);
    t.low[1]=20u;
    t.noise[1]=17u;
    REJECT(&t,&p,1u);
    t.noise[1]=24u; /* restore via setup */
#undef REJECT

    setup(16,32,96000,&m,&t,&p);
    SautiAacSbrEnergyFrame energy={0};
    energy.env_count=1u;energy.noise_count=1u;
    for(unsigned i=0;i<t.n_low;i++)energy.env_energy[0][i]=4.0;
    const uint8_t env[2]={0,2};
    SautiAacSbrComplex source[2u*H],adjusted[2u*H],old[2u*H];
    for(unsigned i=0;i<2u*H;i++){
        source[i].re=1.0f;source[i].im=0.0f;
    }
    memset(adjusted,0xb2,sizeof(adjusted));
    memcpy(old,adjusted,sizeof(old));
    SautiAacSbrLimiterReport stats,old_stats;
    memset(&stats,0xc5,sizeof(stats));old_stats=stats;
    assert(!sauti_aac_sbr_hf_limit_auto(
        &t,&p,4u,&energy,env,2u,1u,source,2u*H,
        adjusted,2u*H,&stats));
    assert(memcmp(old,adjusted,sizeof(old))==0);
    assert(memcmp(&old_stats,&stats,sizeof(stats))==0);
}

static void test_sweep(void) {
    const unsigned rates[]={16000,22050,24000,32000,44100,48000,
                            64000,88200,96000,128000,176400,192000};
    unsigned accepted=0u,rejected=0u,has_multiple=0u;
    for(unsigned ri=0;ri<sizeof(rates)/sizeof(rates[0]);ri++)
      for(unsigned start=0;start<16;start+=3)
        for(unsigned stop=0;stop<16;stop+=3){
            SautiAacSbrLimits lim;
            if(!sauti_aac_sbr_limits_compute(rates[ri],start,stop,&lim))
                continue;
            for(unsigned scale=0;scale<4;scale++)
              for(unsigned alter=0;alter<2;alter++)
                for(unsigned cross=0;cross<8;cross++){
                    SautiAacSbrMaster m;
                    const int ok=scale==0u
                       ? sauti_aac_sbr_master_linear(lim.k0,lim.k2,
                                                     alter,cross,&m)
                       : sauti_aac_sbr_master_log(lim.k0,lim.k2,
                                                   scale,alter,cross,&m);
                    if(!ok)continue;
                    SautiAacSbrTables tables;
                    if(!sauti_aac_sbr_tables_build(&m,2,&tables))continue;
                    SautiAacSbrPatches patches;
                    if(!sauti_aac_sbr_patch_build(rates[ri],&m,
                                                   &tables,&patches)){
                        rejected++;continue;
                    }
                    if(patches.count>1u)has_multiple++;
                    for(unsigned mode=0;mode<4;mode++){
                        SautiAacSbrLimiterBands bands;
                        assert(sauti_aac_sbr_limiter_bands_build(
                            &tables,&patches,mode,&bands));
                        check(&tables,&patches,&bands);
                        if(mode==0u)assert(bands.count==1u);
                        accepted++;
                    }
                }
        }
    assert(accepted>10000u && rejected>10u && has_multiple>100u);
    printf("SBR-17 limiter tables: %u validated, %u rejected patches, %u multi-patch configurations\n",
           accepted,rejected,has_multiple);
}

int main(void){
    test_golden_modes_and_patch_junctions();
    test_automatic_limiter_integration();
    test_fail_closed();
    test_sweep();
    puts("PASS HE-AAC SBR-17 automatic limiter bands: 4 modes, patch junctions, rollback, SBR-16");
    return 0;
}
