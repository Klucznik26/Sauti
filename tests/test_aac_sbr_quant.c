#include "sauti_aac_sbr_quant.h"
#include "sauti_aac_sbr_master.h"
#include "sauti_aac_sbr_tables.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Fixed independent symbols checked against the SBR VLC standard:
 * each code here is a real signed Huffman symbol from a different book.
 * Columns are deltas -1, 0 and +1, respectively. */
typedef struct { unsigned n, code; } Codeword;
static const Codeword word[SAUTI_SBR_HUFF_TABLE_COUNT][3]={
    {{2,1},{2,0},{3,4}}, /* envelope 1.5dB time */
    {{2,1},{2,0},{3,4}}, /* envelope 1.5dB frequency */
    {{3,6},{1,0},{2,2}}, /* balanced 1.5dB time */
    {{2,2},{1,0},{3,6}}, /* balanced 1.5dB frequency */
    {{2,2},{1,0},{3,6}}, /* envelope 3.0dB time */
    {{2,2},{1,0},{3,6}}, /* envelope 3.0dB frequency */
    {{3,6},{1,0},{2,2}}, /* balanced 3.0dB time */
    {{2,2},{1,0},{3,6}}, /* balanced 3.0dB frequency */
    {{3,6},{1,0},{2,2}}, /* noise 3.0dB time */
    {{2,2},{1,0},{3,6}}  /* balanced noise 3.0dB time */
};
typedef struct { uint8_t bytes[512]; size_t bit; } Bits;
static void put(Bits *b, unsigned n, unsigned value) {
    assert(n<=32u && b->bit+n<=sizeof(b->bytes)*8u);
    for(unsigned i=n;i>0;i--){
        b->bytes[b->bit/8u] |=
          (uint8_t)(((value>>(i-1u))&1u)<<(7u-b->bit%8u));
        ++b->bit;
    }
}
static void cw(Bits *b,unsigned book,int delta) {
    assert(book<SAUTI_SBR_HUFF_TABLE_COUNT && delta>=-1 && delta<=1);
    const Codeword c=word[book][(unsigned)(delta+1)];
    put(b,c.n,c.code);
}
static size_t bytes_used(const Bits *b) {return (b->bit+7u)/8u;}
static void setup(SautiAacSbrGrid *g, SautiAacSbrTables *t,
                  SautiAacSbrControl *c) {
    SautiAacSbrMaster master;
    assert(sauti_aac_sbr_master_linear(16,32,1,0,&master));
    assert(sauti_aac_sbr_tables_build(&master,2,t));
    assert(t->n_high==8 && t->n_low==4 && t->n_noise==2);
    memset(g,0,sizeof(*g));
    memset(c,0,sizeof(*c));
    g->frame_class=0;g->env_count=2;g->noise_count=2;
    c->env_count=2;c->noise_count=2;c->noise_band_count=2;
}
static void freq_env(Bits *b,unsigned amp,unsigned balance,unsigned n,
                     unsigned initial,int diff) {
    unsigned first_bits=amp?(balance?5:6):(balance?6:7);
    put(b,first_bits,initial);
    unsigned book=amp?(balance?7:5):(balance?3:1);
    for(unsigned j=1;j<n;j++)cw(b,book,diff);
}
static void time_env(Bits *b,unsigned amp,unsigned balance,unsigned n,
                     int delta) {
    unsigned book=amp?(balance?6:4):(balance?2:0);
    for(unsigned j=0;j<n;j++)cw(b,book,delta);
}
static void freq_noise(Bits *b,unsigned balance,unsigned n,
                       unsigned initial,int diff) {
    put(b,5,initial);
    for(unsigned j=1;j<n;j++)cw(b,balance?7:5,diff);
}
static void time_noise(Bits *b,unsigned balance,unsigned n,int delta) {
    for(unsigned j=0;j<n;j++)cw(b,balance?9:8,delta);
}

static void test_mixed_resolution_and_history(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrControl c;
    setup(&g,&t,&c);
    g.freq_res[0]=1;g.freq_res[1]=1;
    c.df_env[0]=0;c.df_env[1]=1;
    c.df_noise[0]=0;c.df_noise[1]=1;

    Bits b={0};
    put(&b,3,5); const size_t start=b.bit;
    freq_env(&b,1,0,8,10,0);
    time_env(&b,1,0,8,1);
    freq_noise(&b,0,2,6,1);
    time_noise(&b,0,2,-1);
    SautiAacSbrQuantState state={0};
    SautiAacSbrQuantFrame frame={0};
    size_t pos=start;
    assert(sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                      &g,&t,&c,1,0,&state,&frame));
    assert(pos==b.bit && frame.env_count==2 && frame.noise_count==2);
    for(unsigned j=0;j<8;j++){
        assert(frame.env[0][j]==10u && frame.env[1][j]==11u);
    }
    assert(frame.noise[0][0]==6u && frame.noise[0][1]==7u);
    assert(frame.noise[1][0]==5u && frame.noise[1][1]==6u);
    assert(state.has_env && state.has_noise && state.prev_freq_res==1u);

    /* Frame 2: high -> low -> high time-deltas, plus noise time-deltas. */
    g.freq_res[0]=0;g.freq_res[1]=1;
    c.df_env[0]=1;c.df_env[1]=1;
    c.df_noise[0]=1;c.df_noise[1]=1;
    b=(Bits){0};
    time_env(&b,1,0,4,1);
    time_env(&b,1,0,8,-1);
    time_noise(&b,0,2,1);
    time_noise(&b,0,2,0);
    pos=0;
    assert(sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                      &g,&t,&c,1,0,&state,&frame));
    assert(pos==b.bit);
    for(unsigned j=0;j<4;j++)assert(frame.env[0][j]==12u);
    for(unsigned j=0;j<8;j++)assert(frame.env[1][j]==11u);
    assert(frame.noise[0][0]==6u && frame.noise[0][1]==7u);
    assert(frame.noise[1][0]==6u && frame.noise[1][1]==7u);
}


static void test_odd_band_cross_resolution(void) {
    /* Odd N_high mapping: high j uses low[(j+1)/2], not low[j/2]. */
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrControl c;
    setup(&g,&t,&c);
    SautiAacSbrMaster m;
    assert(sauti_aac_sbr_master_linear(16,32,1,1,&m));
    assert(sauti_aac_sbr_tables_build(&m,1,&t));
    assert(t.n_high==7 && t.n_low==4 && t.n_noise==1);
    g.freq_res[0]=0;g.freq_res[1]=1;
    c.noise_band_count=1;
    c.df_env[0]=0;c.df_env[1]=1;
    Bits b={0};
    freq_env(&b,1,0,4,8,1);
    time_env(&b,1,0,7,0);
    freq_noise(&b,0,1,4,0);
    freq_noise(&b,0,1,5,0);
    SautiAacSbrQuantState state={0};
    SautiAacSbrQuantFrame frame;
    size_t pos=0;
    assert(sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                      &g,&t,&c,1,0,&state,&frame));
    static const uint8_t expected[]={8,9,9,10,10,11,11};
    assert(memcmp(frame.env[1],expected,sizeof(expected))==0);
    assert(pos==b.bit);
}

static void test_balanced(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrControl c;
    setup(&g,&t,&c);
    g.freq_res[0]=1;g.freq_res[1]=1;
    c.df_env[1]=1;c.df_noise[1]=1;
    Bits b={0};
    freq_env(&b,1,1,8,4,1);
    time_env(&b,1,1,8,-1);
    freq_noise(&b,1,2,4,1);
    time_noise(&b,1,2,-1);
    SautiAacSbrQuantState state={0};
    SautiAacSbrQuantFrame frame={0};
    size_t pos=0;
    assert(sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                      &g,&t,&c,1,1,&state,&frame));
    assert(pos==b.bit);
    for(unsigned j=0;j<8;j++){
        assert(frame.env[0][j]==8u+2u*j);
        assert(frame.env[1][j]==6u+2u*j);
    }
    assert(frame.noise[0][0]==8u && frame.noise[0][1]==10u);
    assert(frame.noise[1][0]==6u && frame.noise[1][1]==8u);
    assert(state.balanced==1u);
}

static void test_forced_amp_resolution(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrControl c;
    setup(&g,&t,&c);
    g.env_count=1;g.noise_count=1;
    g.freq_res[0]=0;
    c.env_count=1;c.noise_count=1;
    Bits b={0};
    /* FIXFIX with one envelope forces 1.5dB, despite amp_res=1. */
    freq_env(&b,0,0,4,12,0);
    freq_noise(&b,0,2,8,0);
    SautiAacSbrQuantState state={0};
    SautiAacSbrQuantFrame frame={0};
    size_t pos=0;
    assert(sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                      &g,&t,&c,1,0,&state,&frame));
    assert(pos==b.bit && state.amp_res==0u && frame.env_count==1u);
    for(unsigned j=0;j<4;j++)assert(frame.env[0][j]==12u);
    assert(frame.noise[0][0]==8u && frame.noise[0][1]==8u);
}

static void test_atomic_rollback(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrControl c;
    setup(&g,&t,&c);
    g.freq_res[0]=1;g.freq_res[1]=1;
    c.df_env[1]=1;c.df_noise[1]=1;
    Bits b={0};
    freq_env(&b,1,0,8,10,0);
    time_env(&b,1,0,8,1);
    freq_noise(&b,0,2,6,1);
    time_noise(&b,0,2,-1);
    SautiAacSbrQuantState state={0},before;
    SautiAacSbrQuantFrame out,old;
    memset(&out,0xab,sizeof(out));old=out;before=state;
    unsigned truncated=0;
    for(size_t bytes=0;bytes<bytes_used(&b);bytes++){
        size_t pos=0;
        assert(!sauti_aac_sbr_quant_decode(b.bytes,bytes,&pos,&g,&t,&c,
                                            1,0,&state,&out));
        assert(pos==0 && memcmp(&state,&before,sizeof(state))==0 &&
               memcmp(&out,&old,sizeof(out))==0);
        truncated++;
    }
    assert(truncated>5u);

    /* A time-coded first envelope without history is invalid. */
    c.df_env[0]=1;
    size_t pos=0;
    assert(!sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                        &g,&t,&c,1,0,&state,&out));
    c.df_env[0]=0;
    c.df_noise[0]=1;
    assert(!sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                        &g,&t,&c,1,0,&state,&out));
    c.df_noise[0]=0;
    assert(memcmp(&state,&before,sizeof(state))==0 &&
           memcmp(&out,&old,sizeof(out))==0 && pos==0);

    /* Reconstruct a valid state then reject incompatible time-coded
     * geometry, preserving history and output. */
    assert(sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                      &g,&t,&c,1,0,&state,&out));
    before=state;old=out;
    SautiAacSbrTables changed=t;
    changed.high[1]=17; /* still increasing, but different */
    c.df_env[0]=1;c.df_noise[0]=1;
    pos=0;
    assert(!sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                        &g,&changed,&c,1,0,&state,&out));
    assert(pos==0 && memcmp(&state,&before,sizeof(state))==0 &&
           memcmp(&out,&old,sizeof(out))==0);
    c.df_env[0]=0;c.df_noise[0]=0;
    assert(!sauti_aac_sbr_quant_decode(NULL,bytes_used(&b),&pos,
                                        &g,&t,&c,1,0,&state,&out));
    assert(!sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),NULL,
                                        &g,&t,&c,1,0,&state,&out));
    assert(!sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                        &g,&t,&c,2,0,&state,&out));
    assert(!sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                        &g,&t,&c,1,2,&state,&out));
    assert(memcmp(&state,&before,sizeof(state))==0 &&
           memcmp(&out,&old,sizeof(out))==0 && pos==0);
    printf("SBR-08: %u truncated frame states rolled back\n",truncated);
}

static void test_value_limits(void) {
    SautiAacSbrGrid g;
    SautiAacSbrTables t;
    SautiAacSbrControl c;
    setup(&g,&t,&c);
    g.freq_res[0]=1;g.freq_res[1]=1;
    SautiAacSbrQuantState state={0},old;
    SautiAacSbrQuantFrame frame,prior;
    memset(&frame,0xa5,sizeof(frame));prior=frame;old=state;
    /* 6-bit starting energy 63, +1 seven times -> <= 127, valid. */
    Bits b={0};
    freq_env(&b,1,0,8,63,1);
    freq_env(&b,1,0,8,0,0);
    freq_noise(&b,0,2,0,0);
    freq_noise(&b,0,2,0,0);
    size_t pos=0;
    assert(sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                       &g,&t,&c,1,0,&state,&frame));
    assert(frame.env[0][7]==70u);
    state=old;frame=prior;
    /* 1.5dB 7-bit start=127 followed by positive frequency delta
     * must reject before committing a partially built envelope. */
    b=(Bits){0};
    freq_env(&b,0,0,8,127,1);
    freq_env(&b,0,0,8,3,0);
    freq_noise(&b,0,2,0,0);
    freq_noise(&b,0,2,0,0);
    pos=0;
    assert(!sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                        &g,&t,&c,0,0,&state,&frame));
    assert(pos==0 && memcmp(&state,&old,sizeof(state))==0 &&
           memcmp(&frame,&prior,sizeof(frame))==0);
    /* 5-bit noise start=31 exceeds the permitted range 0..30. */
    b=(Bits){0};
    freq_env(&b,1,0,8,3,0);
    freq_env(&b,1,0,8,3,0);
    freq_noise(&b,0,2,31,0);
    freq_noise(&b,0,2,0,0);
    pos=0;
    assert(!sauti_aac_sbr_quant_decode(b.bytes,bytes_used(&b),&pos,
                                        &g,&t,&c,1,0,&state,&frame));
    assert(pos==0 && memcmp(&state,&old,sizeof(state))==0 &&
           memcmp(&frame,&prior,sizeof(frame))==0);
}
int main(void) {
    test_mixed_resolution_and_history();
    test_odd_band_cross_resolution();
    test_balanced();
    test_forced_amp_resolution();
    test_atomic_rollback();
    test_value_limits();
    puts("PASS HE-AAC SBR-08 quantized envelopes/noise: time/frequency, coupling, history, rollback");
    return 0;
}
