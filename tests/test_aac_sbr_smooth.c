#include "sauti_aac_sbr_smooth.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W SAUTI_AAC_SBR_SMOOTH_BANDS
#define Q SAUTI_AAC_SBR_SMOOTH_SLOTS
static void close_to(double got,double expected) {
    assert(isfinite(got));
    assert(fabs(got-expected)<=3e-12*fmax(1.0,fabs(expected)));
}
static void setup(SautiAacSbrTables *tables) {
    SautiAacSbrMaster master;
    assert(sauti_aac_sbr_master_linear(16u,32u,1u,0u,&master));
    assert(sauti_aac_sbr_tables_build(&master,2u,tables));
}
static void targets(double g[5][W],double n[5][W],
                    const SautiAacSbrTables *tables,
                    unsigned env_count,const double *values) {
    memset(g,0,sizeof(double)*5u*W);
    memset(n,0,sizeof(double)*5u*W);
    for(unsigned e=0;e<env_count;e++)
        for(unsigned k=tables->kx;k<tables->kx+tables->m;k++){
            g[e][k]=values[e];
            n[e][k]=values[e]*0.5;
        }
}
static void check_slot(const SautiAacSbrSmoothFrame *out,
                       const SautiAacSbrTables *tables,
                       unsigned slot,double gain,double noise) {
    for(unsigned k=0;k<W;k++){
        close_to(out->gain[slot][k],
                 k>=tables->kx && k<tables->kx+tables->m?gain:0.0);
        close_to(out->noise[slot][k],
                 k>=tables->kx && k<tables->kx+tables->m?noise:0.0);
    }
}
static void test_golden_five_tap(void){
    SautiAacSbrTables tables;setup(&tables);
    double g[5][W],n[5][W];
    const double levels[2]={2,10};
    targets(g,n,&tables,2,levels);
    const uint8_t edges[3]={0,4,10},attack[2]={0,0};
    SautiAacSbrSmoothState state={0};
    SautiAacSbrSmoothFrame out;
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,2u,edges,10u,g,n,attack,0u,&state,&out));
    for(unsigned t=0;t<4u;t++)check_slot(&out,&tables,t,2.0,1.0);
    static const double h[5]={
        0.33333333333333,0.30150283239582,0.21816949906249,
        0.11516383427084,0.03183050093751
    };
    for(unsigned j=0;j<5u;j++){
        double expected=0;
        for(unsigned r=0;r<5u;r++)
            expected+=h[r]*(r<=j?10.0:2.0);
        check_slot(&out,&tables,4u+j,expected,expected*0.5);
    }
    check_slot(&out,&tables,9u,10.0,5.0);
    assert(state.ready==1u && state.kx==tables.kx &&
           state.m==tables.m);
    for(unsigned j=0;j<4u;j++)
        for(unsigned k=tables.kx;k<tables.kx+tables.m;k++) {
            close_to(state.gain[j][k],10.0);
            close_to(state.noise[j][k],5.0);
        }
    puts("SBR-21 golden: exact five-tap smoothing across envelope transition");
}
static void test_frame_split_and_attack(void){
    SautiAacSbrTables tables;setup(&tables);
    double g[5][W],n[5][W];
    const double levels[2]={2,10};
    targets(g,n,&tables,2,levels);
    const uint8_t edges[3]={0,4,10},attack[2]={0,0};
    const uint8_t first[2]={0,4},last[2]={0,6},no_attack[1]={0};
    SautiAacSbrSmoothState whole={0},split={0};
    SautiAacSbrSmoothFrame all,a,b;
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,2,edges,10,g,n,attack,0,&whole,&all));
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,1,first,4,g,n,no_attack,0,&split,&a));
    double next_g[5][W],next_n[5][W];
    const double value[1]={10};
    targets(next_g,next_n,&tables,1,value);
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,1,last,6,next_g,next_n,no_attack,0,&split,&b));
    for(unsigned t=0;t<10u;t++)
        for(unsigned k=0;k<W;k++){
            close_to(all.gain[t][k],t<4u?a.gain[t][k]:b.gain[t-4u][k]);
            close_to(all.noise[t][k],t<4u?a.noise[t][k]:b.noise[t-4u][k]);
        }
    assert(memcmp(&whole,&split,sizeof(whole))==0);

    /* An attack envelope bypasses smoothing, yet writes its raw values
     * into history for the following non-attack envelope or frame. */
    const uint8_t attack_second[2]={0,1};
    const uint8_t edge_attack[3]={0,4,8};
    SautiAacSbrSmoothState bypass={0};
    SautiAacSbrSmoothFrame bypassed;
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,2,edge_attack,8,g,n,attack_second,0,&bypass,&bypassed));
    for(unsigned t=0;t<4u;t++)check_slot(&bypassed,&tables,t,2,1);
    for(unsigned t=4u;t<8u;t++)check_slot(&bypassed,&tables,t,10,5);
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,1,last,6,next_g,next_n,no_attack,0,&bypass,&b));
    check_slot(&b,&tables,0u,10.0,5.0);
    puts("SBR-21: split-frame history identical; attack bypass preserves history");
}
static void test_mode_off_and_reenable(void){
    SautiAacSbrTables tables;setup(&tables);
    double g[5][W],n[5][W];
    const double levels[2]={2,10};
    targets(g,n,&tables,2,levels);
    const uint8_t edges[3]={0,3,7},attack[2]={0,0};
    SautiAacSbrSmoothState state={0};
    SautiAacSbrSmoothFrame out;
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,2,edges,7,g,n,attack,1u,&state,&out));
    for(unsigned t=0;t<3;t++)check_slot(&out,&tables,t,2,1);
    for(unsigned t=3;t<7;t++)check_slot(&out,&tables,t,10,5);
    const double level[1]={2};
    double newer_g[5][W],newer_n[5][W];
    targets(newer_g,newer_n,&tables,1,level);
    const uint8_t edges1[2]={0,1},attack1[1]={0};
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,1,edges1,1,newer_g,newer_n,attack1,0u,&state,&out));
    const double expected=0.33333333333333*2.0+
                          (1.0-0.33333333333333)*10.0;
    check_slot(&out,&tables,0,expected,expected*0.5);
}
static void test_atomicity_and_bad_geometry(void){
    SautiAacSbrTables tables;setup(&tables);
    double g[5][W],n[5][W];
    const double values[2]={2,10};
    targets(g,n,&tables,2,values);
    uint8_t edges[3]={0,4,8},attack[2]={0,0};
    SautiAacSbrSmoothFrame output,old_output;
    SautiAacSbrSmoothState state={0},old_state;
    memset(&output,0x77,sizeof(output));
    old_output=output;old_state=state;
#define REJECT(T,NE,E,S,G,N,A,M) do {                                  \
    assert(!sauti_aac_sbr_smooth_coefficients((T),(NE),(E),(S),       \
            (G),(N),(A),(M),&state,&output));                          \
    assert(memcmp(&output,&old_output,sizeof(output))==0);            \
    assert(memcmp(&state,&old_state,sizeof(state))==0);                \
} while(0)
    REJECT(NULL,2,edges,8,g,n,attack,0);
    REJECT(&tables,0,edges,8,g,n,attack,0);
    REJECT(&tables,6,edges,8,g,n,attack,0);
    REJECT(&tables,2,NULL,8,g,n,attack,0);
    REJECT(&tables,2,edges,0,g,n,attack,0);
    REJECT(&tables,2,edges,Q+1,g,n,attack,0);
    REJECT(&tables,2,edges,8,NULL,n,attack,0);
    REJECT(&tables,2,edges,8,g,NULL,attack,0);
    REJECT(&tables,2,edges,8,g,n,NULL,0);
    REJECT(&tables,2,edges,8,g,n,attack,2);
    edges[1]=0;REJECT(&tables,2,edges,8,g,n,attack,0);
    edges[1]=4;edges[2]=7;
    REJECT(&tables,2,edges,8,g,n,attack,0);
    edges[2]=8;attack[1]=2;
    REJECT(&tables,2,edges,8,g,n,attack,0);
    attack[1]=0;g[1][16]=-1.0;
    REJECT(&tables,2,edges,8,g,n,attack,0);
    g[1][16]=INFINITY;
    REJECT(&tables,2,edges,8,g,n,attack,0);
    g[1][16]=10.0;
    n[1][16]=NAN;
    REJECT(&tables,2,edges,8,g,n,attack,0);
    n[1][16]=5.0;
    tables.low[1]=tables.low[0];
    REJECT(&tables,2,edges,8,g,n,attack,0);
    setup(&tables);
#undef REJECT
    assert(!sauti_aac_sbr_smooth_coefficients(
        &tables,2,edges,8,g,n,attack,0,&state,NULL));
    assert(!sauti_aac_sbr_smooth_coefficients(
        &tables,2,edges,8,g,n,attack,0,NULL,&output));

    /* Warm state with different kx/m must not silently reuse history. */
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,2,edges,8,g,n,attack,0,&state,&output));
    old_output=output;old_state=state;
    SautiAacSbrMaster master;
    SautiAacSbrTables changed;
    assert(sauti_aac_sbr_master_linear(16,48,1,0,&master));
    assert(sauti_aac_sbr_tables_build(&master,2,&changed));
    REJECT(&changed,2,edges,8,g,n,attack,0);
    state.gain[3][16]=NAN;old_state=state;
    REJECT(&tables,2,edges,8,g,n,attack,0);
}
static void test_40_slots_5_envelopes(void) {
    SautiAacSbrTables tables;setup(&tables);
    double g[5][W],n[5][W];
    const double levels[5]={1,2,3,4,5};
    targets(g,n,&tables,5,levels);
    const uint8_t edges[6]={0,8,16,24,32,40};
    const uint8_t attack[5]={0,0,1,0,0};
    SautiAacSbrSmoothState state={0};
    SautiAacSbrSmoothFrame out;
    assert(sauti_aac_sbr_smooth_coefficients(
        &tables,5,edges,40,g,n,attack,0,&state,&out));
    for(unsigned t=16;t<24;t++)
        check_slot(&out,&tables,t,3,1.5);
    check_slot(&out,&tables,39,5,2.5);
    assert(state.ready);
    printf("SBR-21: 40 QMF slots, 5 envelopes, attack and no-attack smoothing PASS\n");
}
int main(void){
    test_golden_five_tap();
    test_frame_split_and_attack();
    test_mode_off_and_reenable();
    test_atomicity_and_bad_geometry();
    test_40_slots_5_envelopes();
    puts("PASS HE-AAC SBR-21 coefficient smoothing: five taps, history, attacks, rollback");
    return 0;
}
