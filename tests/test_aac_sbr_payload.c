#include "sauti_aac_sbr_payload.h"
#include "sauti_aac_headers.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {uint8_t data[SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES];size_t bit;} Bits;
static void put(Bits *b,unsigned n,unsigned v) {
    assert(n<=32u && b->bit+n<=sizeof(b->data)*8u);
    for(unsigned i=n;i>0;i--){
        const unsigned x=(v>>(i-1u))&1u;
        b->data[b->bit/8u]|=(uint8_t)(x<<(7u-b->bit%8u));
        b->bit++;
    }
}
static void align_byte(Bits *b){
    while(b->bit&7u)put(b,1,0u);
}
static void choose(SautiAacSbrHeader *h,SautiAacSbrTables *t) {
    SautiAacSbrHeader z={0};
    z.amp_res=1u;z.freq_scale=0u;z.alter_scale=1u;
    z.noise_bands=2u;z.xover_band=0u;
    z.limiter_bands=2u;z.limiter_gains=2u;
    z.interpol_freq=1u;z.smoothing_mode=0u;
    z.extra_1=1u;z.extra_2=1u;
    for(unsigned start=0;start<16u;start++)
        for(unsigned stop=0;stop<16u;stop++){
            SautiAacSbrLimits limits;
            SautiAacSbrMaster master;
            SautiAacSbrPatches patches;
            if(!sauti_aac_sbr_limits_compute(48000u,start,stop,&limits) ||
               !sauti_aac_sbr_master_linear(
                   limits.k0,limits.k2,1u,0u,&master) ||
               !sauti_aac_sbr_tables_build(&master,2u,t) ||
               !sauti_aac_sbr_patch_build(48000u,&master,t,&patches) ||
               t->n_high>20u || t->n_high<3u || t->n_noise>4u)
                continue;
            z.start_freq=start;z.stop_freq=stop;
            *h=z;return;
        }
    assert(!"No feasible header parameters found");
}
static void header_bits(Bits *b,const SautiAacSbrHeader *h) {
    put(b,1,h->amp_res);
    put(b,4,h->start_freq);put(b,4,h->stop_freq);
    put(b,3,h->xover_band);put(b,2,0);
    put(b,1,h->extra_1);put(b,1,h->extra_2);
    if(h->extra_1){
        put(b,2,h->freq_scale);put(b,1,h->alter_scale);
        put(b,2,h->noise_bands);
    }
    if(h->extra_2){
        put(b,2,h->limiter_bands);put(b,2,h->limiter_gains);
        put(b,1,h->interpol_freq);put(b,1,h->smoothing_mode);
    }
}
/* Two-envelope FIXFIX SCE payload, with real AAC SBR Huffman
 * zero-delta codewords from ENV_3_0DB_F / ENV_3_0DB_T and
 * NOISE_3_0DB_T: code 0, length 1 for symbol delta=0.
 */
static void payload(Bits *b,const SautiAacSbrHeader *hdr,
                    const SautiAacSbrTables *tab,
                    unsigned new_header,unsigned time_deltas,
                    unsigned harmonic) {
    *b=(Bits){0};
    put(b,4,13u);          /* EXT_SBR_DATA */
    put(b,1,new_header);   /* bs_header_flag */
    if(new_header)header_bits(b,hdr);
    put(b,1,0);            /* bs_data_extra */
    put(b,2,0);put(b,2,1);put(b,1,1); /* FIXFIX 2 env, freq_res=high */
    put(b,1,time_deltas);put(b,1,time_deltas); /* dtdf env */
    put(b,1,time_deltas);put(b,1,time_deltas); /* dtdf noise */
    for(unsigned q=0;q<tab->n_noise;q++)put(b,2,0); /* invf */
    for(unsigned e=0;e<2u;e++){
        if(!time_deltas)put(b,6,5u);
        for(unsigned j=time_deltas?0u:1u;j<tab->n_high;j++)
            put(b,1,0); /* Huffman env delta 0 */
    }
    for(unsigned e=0;e<2u;e++){
        if(!time_deltas)put(b,5,6u);
        for(unsigned j=time_deltas?0u:1u;j<tab->n_noise;j++)
            put(b,1,0); /* Huffman noise delta 0 */
    }
    put(b,1,harmonic);
    if(harmonic)
        for(unsigned j=0;j<tab->n_high;j++)
            put(b,1,j==0u?1u:0u);
    put(b,1,0u); /* bs_extended_data=0: no PS */
    align_byte(b);
}
static void put_payload_with_offset(Bits *dst,const Bits *src,unsigned offset){
    *dst=(Bits){0};
    for(unsigned i=0;i<offset;i++)put(dst,1,i&1u);
    for(size_t i=0;i<src->bit;i++)
        put(dst,1,(src->data[i/8u]>>(7u-i%8u))&1u);
}
static void test_header_grid_entropy_and_harmonics(void) {
    SautiAacSbrHeader header;
    SautiAacSbrTables tab;
    choose(&header,&tab);
    Bits p,unaligned;
    payload(&p,&header,&tab,1u,0u,1u);
    put_payload_with_offset(&unaligned,&p,7u); /* after FIL ID/count */
    SautiAacSbrPayloadState state={0};
    SautiAacSbrPayloadFrame f;
    assert(sauti_aac_sbr_payload_sce_parse(
        unaligned.data,(unaligned.bit+7u)/8u,7u,p.bit,48000u,&state,&f));
    assert(f.header.start_freq==header.start_freq &&
           f.header.stop_freq==header.stop_freq);
    assert(f.grid.frame_class==0u && f.grid.env_count==2u);
    assert(f.grid.noise_count==2u && f.grid.freq_res[0]==1u);
    assert(f.env_edges[0]==0u && f.env_edges[1]==16u &&
           f.env_edges[2]==32u);
    assert(f.noise_edges[0]==0u && f.noise_edges[1]==16u &&
           f.noise_edges[2]==32u);
    assert(f.energy.env_count==2u && f.energy.noise_count==2u);
    assert(f.tables.n_high==tab.n_high &&
           f.tables.n_noise==tab.n_noise);
    assert(f.harmonic[0]==1u && f.transient_env==0u);
    assert(f.bs_smoothing_mode==0u && f.limiter_mode==2u);
    assert(f.quant.env[0][0]==5u && f.quant.env[1][0]==5u);
    assert(f.quant.noise[0][0]==6u && f.quant.noise[1][0]==6u);
    assert(isfinite(f.energy.env_energy[0][0]) &&
           f.energy.env_energy[0][0]>0);
    assert(state.frames==1u && state.initialized==1u &&
           state.quant_state.has_env && state.quant_state.has_noise);
    assert(f.payload_data_bits<=p.bit &&
           p.bit-f.payload_data_bits<8u);

    SautiAacSbrHfJointConfig cfg={0};
    assert(sauti_aac_sbr_payload_make_hf(&f,&cfg));
    assert(cfg.tables==&f.tables && cfg.patches==&f.patches &&
           cfg.energy==&f.energy && cfg.slots==32u &&
           cfg.flag_count==f.tables.n_high &&
           cfg.harmonic_flags[0]==1u && cfg.env_edges[2]==32u);
    printf("SBR-29: full unaligned real SCE syntax, %u high bands, %u noise bands, QMF config PASS\n",
           f.tables.n_high,f.tables.n_noise);

    /* Second FIL may legally omit SBR header and time-delta encode
     * against the previous entropy frame. Confirm bit-aligned state
     * transition and exact energy continuity. */
    Bits second;
    payload(&second,&header,&tab,0u,1u,0u);
    const SautiAacSbrEnergyFrame prior_energy=f.energy;
    assert(sauti_aac_sbr_payload_sce_parse(
        second.data,(second.bit+7u)/8u,0u,second.bit,48000u,&state,&f));
    assert(state.frames==2u && f.harmonic[0]==0u);
    assert(memcmp(&f.energy,&prior_energy,sizeof(prior_energy))==0);
    printf("SBR-29: header reuse, inter-frame Huffman time deltas and energy continuity PASS\n");
}
static void test_atomic_failure_modes(void) {
    SautiAacSbrHeader hdr;
    SautiAacSbrTables tab;
    choose(&hdr,&tab);
    Bits b;
    payload(&b,&hdr,&tab,1u,0u,1u);
    SautiAacSbrPayloadState state={0},prior;
    SautiAacSbrPayloadFrame out;
    memset(&out,0x97,sizeof(out));
    SautiAacSbrPayloadFrame saved=out;
    prior=state;
#define REJECT(DATA,LEN,START,LENGTH,RATE) do { \
    assert(!sauti_aac_sbr_payload_sce_parse( \
        (DATA),(LEN),(START),(LENGTH),(RATE),&state,&out)); \
    assert(memcmp(&state,&prior,sizeof(state))==0); \
    assert(memcmp(&out,&saved,sizeof(out))==0); \
} while(0)
    REJECT(NULL,sizeof(b.data),0u,b.bit,48000u);
    REJECT(b.data,0u,0u,b.bit,48000u);
    REJECT(b.data,sizeof(b.data),0u,0u,48000u);
    REJECT(b.data,sizeof(b.data),0u,3u,48000u);
    REJECT(b.data,sizeof(b.data),0u,b.bit+1u,48000u);
    REJECT(b.data,(b.bit+7u)/8u,1u,b.bit,48000u);
    REJECT(b.data,sizeof(b.data),0u,b.bit,12345u);

    const unsigned orig=b.data[0];
    b.data[0]=(uint8_t)(12u<<4u)|(b.data[0]&0x0fu);
    REJECT(b.data,sizeof(b.data),0u,b.bit,48000u);
    b.data[0]=(uint8_t)(14u<<4u)|(b.data[0]&0x0fu);
    REJECT(b.data,sizeof(b.data),0u,b.bit,48000u);
    b.data[0]=(uint8_t)orig;

    Bits without_header;
    payload(&without_header,&hdr,&tab,0u,0u,0u);
    REJECT(without_header.data,sizeof(without_header.data),
           0u,without_header.bit,48000u);
    Bits wrong_extension=b;
    /* Locate bs_extended_data bit using actual syntax rather than a
     * hardcoded offset. The last content bit before zero-pad. */
    SautiAacSbrPayloadState warmed={0};
    SautiAacSbrPayloadFrame parsed;
    assert(sauti_aac_sbr_payload_sce_parse(
        b.data,sizeof(b.data),0u,b.bit,48000u,&warmed,&parsed));
    const size_t ext_bit=parsed.payload_data_bits-1u;
    wrong_extension.data[ext_bit/8u]^=(uint8_t)(1u<<(7u-ext_bit%8u));
    REJECT(wrong_extension.data,sizeof(wrong_extension.data),
           0u,b.bit,48000u);
    /* Incorrect padding, or a FIL count shortened by one byte. */
    if(parsed.payload_data_bits<b.bit){
        Bits wrong_pad=b;
        wrong_pad.data[(b.bit-1u)/8u]|=1u;
        REJECT(wrong_pad.data,sizeof(wrong_pad.data),0u,b.bit,48000u);
    }
    REJECT(b.data,sizeof(b.data),0u,b.bit-8u,48000u);
#undef REJECT

    /* Truncation/corruption must roll back even a warm entropy state.
     * In a live stream the next valid frame must still decode. */
    state=warmed;prior=state;
    memset(&out,0x95,sizeof(out));saved=out;
    b.data[0]=(uint8_t)(14u<<4u)|(b.data[0]&0x0fu);
    assert(!sauti_aac_sbr_payload_sce_parse(
        b.data,sizeof(b.data),0u,b.bit,48000u,&state,&out));
    assert(memcmp(&state,&prior,sizeof(state))==0);
    assert(memcmp(&out,&saved,sizeof(out))==0);
    b.data[0]=(uint8_t)orig;
    Bits second;
    payload(&second,&hdr,&tab,0u,1u,0u);
    assert(sauti_aac_sbr_payload_sce_parse(
        second.data,sizeof(second.data),0u,second.bit,
        48000u,&state,&out));
    assert(state.frames==2u && state.quant_state.has_env);
    puts("SBR-29: CRC/PS/invalid FIL, unsupported rate, truncated payload and warm rollback PASS");
}
/* A legitimate FIXVAR frame may end one SBR time slot after the
 * current AAC frame. Side-data parsing is allowed, but QMF still must
 * refuse until a temporal lookahead buffer is implemented. */
static void test_temporal_fixvar_17_side_data(void){
    SautiAacSbrHeader hdr;SautiAacSbrTables tab;
    choose(&hdr,&tab);
    SautiAacSbrPayloadState state={0};
    SautiAacSbrPayloadFrame out;
    Bits warm,extended={0};
    payload(&warm,&hdr,&tab,1u,0u,0u);
    assert(sauti_aac_sbr_payload_sce_parse(
        warm.data,sizeof(warm.data),0u,warm.bit,48000u,&state,&out));
    put(&extended,4u,13u); /* extension_type */
    put(&extended,1u,0u);  /* reuse SBR header */
    put(&extended,1u,0u);  /* bs_data_extra */
    put(&extended,2u,1u);  /* FIXVAR */
    put(&extended,2u,1u);  /* absolute trailing border 16+1 */
    put(&extended,2u,3u);  /* 3 trailing relative borders: 4 env */
    put(&extended,2u,2u);  /* 17-6 = 11 */
    put(&extended,2u,1u);  /* 11-4 = 7 */
    put(&extended,2u,0u);  /* 7-2 = 5 */
    put(&extended,3u,4u);  /* pointer */
    for(unsigned i=0u;i<4u;i++)put(&extended,1u,1u); /* high resolution */
    for(unsigned i=0u;i<4u;i++)put(&extended,1u,1u); /* time delta env */
    for(unsigned i=0u;i<2u;i++)put(&extended,1u,1u); /* time delta noise */
    for(unsigned i=0u;i<tab.n_noise;i++)put(&extended,2u,0u);
    for(unsigned e=0u;e<4u;e++)
        for(unsigned j=0u;j<tab.n_high;j++)put(&extended,1u,0u);
    for(unsigned e=0u;e<2u;e++)
        for(unsigned j=0u;j<tab.n_noise;j++)put(&extended,1u,0u);
    put(&extended,1u,0u); /* no harmonic */
    put(&extended,1u,0u); /* no extended data */
    align_byte(&extended);

    SautiAacSbrPayloadState prior=state;
    SautiAacSbrPayloadFrame saved=out;
    SautiAacSbrPayloadStage stage=SAUTI_SBR_PAYLOAD_INPUT;
    assert(!sauti_aac_sbr_payload_sce_parse_ex(
        extended.data,sizeof(extended.data),0u,extended.bit,48000u,
        &state,&out,&stage));
    assert(stage==SAUTI_SBR_PAYLOAD_BORDERS);
    assert(memcmp(&state,&prior,sizeof(state))==0);
    assert(memcmp(&out,&saved,sizeof(out))==0);

    assert(sauti_aac_sbr_payload_temporal_parse_ex(
        extended.data,sizeof(extended.data),0u,extended.bit,48000u,
        &state,&out,&stage));
    assert(stage==SAUTI_SBR_PAYLOAD_OK);
    assert(out.grid.frame_class==1u && out.grid.env_count==4u);
    assert(out.grid.env_border[0]==0u &&
           out.grid.env_border[1]==5u &&
           out.grid.env_border[2]==7u &&
           out.grid.env_border[3]==11u &&
           out.grid.env_border[4]==17u);
    assert(out.env_edges[4]==34u);
    assert(out.noise_edges[2]==34u);
    assert(state.frames==2u);
    SautiAacSbrHfJointConfig hf;
    assert(!sauti_aac_sbr_payload_make_hf(&out,&hf));
    puts("SBR-43: FIXVAR 0,5,7,11,17 parsed as side-data; strict PCM path rejects PASS");
}
int main(void){
    test_header_grid_entropy_and_harmonics();
    test_atomic_failure_modes();
    test_temporal_fixvar_17_side_data();
    puts("PASS HE-AAC SBR-29 entropy-coded FIL SCE payload to QMF-frame parameters");
    return 0;
}
