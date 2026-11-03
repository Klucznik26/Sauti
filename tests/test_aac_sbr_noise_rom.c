#include "sauti_aac_sbr_noise_rom.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W SAUTI_AAC_SBR_NOISE_ROM_BANDS
#define T SAUTI_AAC_SBR_NOISE_ROM_MAX_SLOTS

static void near(float observed,double expected){
    assert(isfinite(observed));
    assert(fabs((double)observed-expected)<=1e-6*fmax(1.0,fabs(expected)));
}
static void reference(unsigned idx,double re,double im) {
    SautiAacSbrComplex out;
    assert(sauti_aac_sbr_noise_rom_lookup(idx,&out));
    near(out.re,re);near(out.im,im);
}
static void test_reference_vectors(void){
    reference(0,-0.99948153278296,-0.59483417516607);
    reference(1,0.97113454393991,-0.67528515225647);
    reference(2,0.14130051758487,-0.95090983575689);
    reference(7,-0.91266367957293,-0.11522938140034);
    reference(31,-0.99969370862163,0.98369989360250);
    reference(127,-0.83302725605608,-0.67330410892084);
    reference(255,0.64016792079480,0.15649530836856);
    reference(256,0.99570534804836,0.45844586038111);
    reference(511,0.91599807087376,-0.98147830385781);

    double power=0;
    double min_power=DBL_MAX,max_power=0;
    for(unsigned i=0;i<SAUTI_AAC_SBR_NOISE_ROM_SIZE;i++){
        SautiAacSbrComplex v;
        assert(sauti_aac_sbr_noise_rom_lookup(i,&v));
        const double p=(double)v.re*v.re+(double)v.im*v.im;
        power+=p;
        if(p<min_power)min_power=p;
        if(p>max_power)max_power=p;
    }
    /* Reference ROM has mean power close to 1, but individual vectors
     * are NOT normalized to unit magnitude (unlike SBR-18). */
    assert(min_power<0.03 && max_power>1.8);
    assert(power/512.0>0.95 && power/512.0<1.05);
    printf("SBR-23: 512 normative vectors, mean complex noise power %.9f\n",
           power/512.0);
}

static void test_increment_before_lookup_and_wrap(void){
    SautiAacSbrNoiseRomState state={0};
    SautiAacSbrComplex v;
    assert(sauti_aac_sbr_noise_rom_next(&state,&v));
    reference(state.index,0.97113454393991,-0.67528515225647);
    near(v.re,0.97113454393991);
    assert(state.initialized==1 && state.index==1 && state.emitted==1);
    for(unsigned i=2;i<=511;i++)
        assert(sauti_aac_sbr_noise_rom_next(&state,&v));
    assert(state.index==511 && state.emitted==511);
    near(v.re,0.91599807087376);
    assert(sauti_aac_sbr_noise_rom_next(&state,&v));
    assert(state.index==0 && state.emitted==512);
    near(v.re,-0.99948153278296);
    near(v.im,-0.59483417516607);
    assert(sauti_aac_sbr_noise_rom_next(&state,&v));
    assert(state.index==1 && state.emitted==513);
    near(v.re,0.97113454393991);

    /* Explicit decoder index lets the caller resume after a seek,
     * and the next vector is always the following ROM entry. */
    SautiAacSbrNoiseRomState other={1u,511u,2000u};
    assert(sauti_aac_sbr_noise_rom_next(&other,&v));
    assert(other.index==0u && other.emitted==2001u);
    near(v.im,-0.59483417516607);
}

static void test_bulk_vs_per_sample_and_guards(void){
    SautiAacSbrNoiseRomState whole={0},parts={0};
    SautiAacSbrComplex output[T*W+2u],separate[T*W];
    memset(output,0x9c,sizeof(output));
    SautiAacSbrComplex guard[2];
    memcpy(guard,&output[T*W],sizeof(guard));
    assert(sauti_aac_sbr_noise_rom_sequence(
        &whole,T*W,output,T*W));
    assert(whole.index==0 && whole.emitted==T*W);
    assert(memcmp(&output[T*W],guard,sizeof(guard))==0);
    for(unsigned i=0;i<T*W;i++)
        assert(sauti_aac_sbr_noise_rom_next(&parts,&separate[i]));
    assert(memcmp(output,separate,sizeof(separate))==0);
    assert(memcmp(&whole,&parts,sizeof(whole))==0);
    SautiAacSbrComplex last,next;
    assert(sauti_aac_sbr_noise_rom_lookup(0,&last));
    assert(memcmp(&output[T*W-1u],&last,sizeof(last))==0);
    assert(sauti_aac_sbr_noise_rom_next(&whole,&next));
    near(next.re,0.97113454393991);
    puts("SBR-23: 2560-vector sequence, exact modulo-512 wrap, guards and frame continuity");
}

static void setup(SautiAacSbrTables *t){
    SautiAacSbrMaster m;
    assert(sauti_aac_sbr_master_linear(16u,32u,1u,0u,&m));
    assert(sauti_aac_sbr_tables_build(&m,2u,t));
    assert(t->kx==16u && t->m==16u);
}
static void fill(SautiAacSbrComplex *v,unsigned slots){
    for(unsigned t=0;t<slots;t++)
        for(unsigned k=0;k<W;k++) {
            v[t*W+k].re=(float)(k+1u)*0.25f;
            v[t*W+k].im=(float)((int)t-(int)k)*0.125f;
        }
}
static void levels(double gain[T][W],double noise[T][W],double g,double n){
    for(unsigned t=0;t<T;t++)for(unsigned k=0;k<W;k++){
        gain[t][k]=g;
        noise[t][k]=n;
    }
}
static void test_qmf_render_and_split(void){
    SautiAacSbrTables tab;setup(&tab);
    SautiAacSbrComplex in[T*W],out[T*W],split[T*W],alias[T*W];
    double gain[T][W],noise[T][W];
    fill(in,T);levels(gain,noise,0.5,1.25);
    SautiAacSbrNoiseRomState total={0},sections={0},aliased={0};
    assert(sauti_aac_sbr_noise_rom_apply_levels(
        &tab,T,gain,noise,in,T*W,out,T*W,&total));
    assert(total.index==((T*tab.m)&511u) &&
           total.emitted==(uint64_t)T*tab.m);
    /* Independently compute each output sample from direct ROM lookup. */
    unsigned phase=0;
    for(unsigned t=0;t<T;t++)
        for(unsigned k=0;k<W;k++){
            const size_t idx=(size_t)t*W+k;
            if(k<tab.kx || k>=tab.kx+tab.m){
                assert(memcmp(&in[idx],&out[idx],sizeof(in[0]))==0);
                continue;
            }
            phase=(phase+1u)&511u;
            SautiAacSbrComplex nr;
            assert(sauti_aac_sbr_noise_rom_lookup(phase,&nr));
            near(out[idx].re,
                 (double)in[idx].re*0.5+(double)nr.re*1.25);
            near(out[idx].im,
                 (double)in[idx].im*0.5+(double)nr.im*1.25);
        }
    assert(phase==total.index);

    /* Reset QMF input to identical frame halves; ROM history must span
     * a pair of calls without consuming extra vectors at boundary. */
    assert(sauti_aac_sbr_noise_rom_apply_levels(
        &tab,17u,gain,noise,in,17u*W,split,17u*W,&sections));
    assert(sauti_aac_sbr_noise_rom_apply_levels(
        &tab,23u,gain,noise,in+17u*W,23u*W,
        split+17u*W,23u*W,&sections));
    assert(memcmp(out,split,sizeof(out))==0);
    assert(memcmp(&total,&sections,sizeof(total))==0);
    memcpy(alias,in,sizeof(alias));
    assert(sauti_aac_sbr_noise_rom_apply_levels(
        &tab,T,gain,noise,alias,T*W,alias,T*W,&aliased));
    assert(memcmp(alias,out,sizeof(out))==0);
    assert(memcmp(&total,&aliased,sizeof(total))==0);

    /* Zero noise still consumes the normative ROM vectors. */
    levels(gain,noise,1.0,0.0);
    SautiAacSbrNoiseRomState muted={0};
    assert(sauti_aac_sbr_noise_rom_apply_levels(
        &tab,T,gain,noise,in,T*W,out,T*W,&muted));
    assert(memcmp(in,out,sizeof(in))==0);
    assert(muted.index==total.index && muted.emitted==total.emitted);
}

static void test_rejection_and_transactionality(void) {
    SautiAacSbrNoiseRomState state={0},old_state;
    SautiAacSbrComplex sentinel={17.0f,-23.0f},saved=sentinel;
    old_state=state;
#define FAIL_NEXT(ST) do{ \
    assert(!sauti_aac_sbr_noise_rom_next((ST),&sentinel)); \
    assert(memcmp(&sentinel,&saved,sizeof(saved))==0); \
    assert(memcmp(&state,&old_state,sizeof(state))==0); \
}while(0)
    assert(!sauti_aac_sbr_noise_rom_lookup(512u,&sentinel));
    assert(!sauti_aac_sbr_noise_rom_lookup(0,NULL));
    assert(memcmp(&sentinel,&saved,sizeof(saved))==0);
    assert(!sauti_aac_sbr_noise_rom_next(NULL,&sentinel));
    state.initialized=2u;old_state=state;FAIL_NEXT(&state);
    state.initialized=1u;state.index=512u;old_state=state;
    FAIL_NEXT(&state);
    state.index=0u;state.emitted=UINT64_MAX;old_state=state;
    FAIL_NEXT(&state);
#undef FAIL_NEXT
    state=(SautiAacSbrNoiseRomState){0};old_state=state;

    SautiAacSbrComplex small[4u],before[4u];
    memset(small,0xb5,sizeof(small));memcpy(before,small,sizeof(small));
#define REJECT_SEQUENCE(ST,C,O,N) do{ \
    assert(!sauti_aac_sbr_noise_rom_sequence((ST),(C),(O),(N))); \
    assert(memcmp(small,before,sizeof(small))==0); \
    assert(memcmp(&state,&old_state,sizeof(state))==0); \
}while(0)
    REJECT_SEQUENCE(NULL,2u,small,4u);
    REJECT_SEQUENCE(&state,0u,small,4u);
    REJECT_SEQUENCE(&state,3u,small,2u);
    REJECT_SEQUENCE(&state,T*W+1u,small,4u);
    REJECT_SEQUENCE(&state,2u,NULL,4u);
#undef REJECT_SEQUENCE

    SautiAacSbrTables tab;setup(&tab);
    const unsigned slots=4u;
    SautiAacSbrComplex in[slots*W],out[slots*W],backup[slots*W];
    double gain[T][W],noise[T][W];
    fill(in,slots);levels(gain,noise,1.0,1.0);
    memset(out,0x6a,sizeof(out));memcpy(backup,out,sizeof(out));
    state=(SautiAacSbrNoiseRomState){0};old_state=state;
#define INVALID(A,G,N,I,IC,O,OC,ST) do{ \
    assert(!sauti_aac_sbr_noise_rom_apply_levels( \
        (A),slots,(G),(N),(I),(IC),(O),(OC),(ST))); \
    assert(memcmp(out,backup,sizeof(out))==0); \
    assert(memcmp(&state,&old_state,sizeof(state))==0); \
}while(0)
    INVALID(NULL,gain,noise,in,slots*W,out,slots*W,&state);
    INVALID(&tab,NULL,noise,in,slots*W,out,slots*W,&state);
    INVALID(&tab,gain,NULL,in,slots*W,out,slots*W,&state);
    INVALID(&tab,gain,noise,NULL,slots*W,out,slots*W,&state);
    INVALID(&tab,gain,noise,in,slots*W-1u,out,slots*W,&state);
    INVALID(&tab,gain,noise,in,slots*W,out,slots*W-1u,&state);
    INVALID(&tab,gain,noise,in,slots*W,out,slots*W,NULL);
    gain[3][16u]=NAN;
    INVALID(&tab,gain,noise,in,slots*W,out,slots*W,&state);
    gain[3][16u]=-1.0;
    INVALID(&tab,gain,noise,in,slots*W,out,slots*W,&state);
    gain[3][16u]=1.0;
    noise[3][31u]=INFINITY;
    INVALID(&tab,gain,noise,in,slots*W,out,slots*W,&state);
    noise[3][31u]=1.0;
    in[3u*W+31u].re=NAN;
    INVALID(&tab,gain,noise,in,slots*W,out,slots*W,&state);
    in[3u*W+31u].re=8.0f;
    tab.high[1]=tab.high[0];
    INVALID(&tab,gain,noise,in,slots*W,out,slots*W,&state);
    setup(&tab);
    state.emitted=UINT64_MAX;old_state=state;
    INVALID(&tab,gain,noise,in,slots*W,out,slots*W,&state);
#undef INVALID
}
int main(void){
    test_reference_vectors();
    test_increment_before_lookup_and_wrap();
    test_bulk_vs_per_sample_and_guards();
    test_qmf_render_and_split();
    test_rejection_and_transactionality();
    puts("PASS HE-AAC SBR-23 normative 512-vector noise ROM: golden, wrap, streaming, rollback");
    return 0;
}
