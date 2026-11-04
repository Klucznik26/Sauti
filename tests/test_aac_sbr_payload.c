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
    SautiAacSbrTemporalPlan plan;
    assert(sauti_aac_sbr_temporal_plan(&out,&plan));
    assert(plan.env_current_count==4u &&
           plan.env_deferred_count==1u &&
           plan.noise_current_count==2u &&
           plan.noise_deferred_count==1u &&
           plan.deferred_slots==2u);
    assert(plan.env_current[0].start==0u &&
           plan.env_current[0].end==10u &&
           plan.env_current[0].envelope==0u);
    assert(plan.env_current[1].start==10u &&
           plan.env_current[1].end==14u &&
           plan.env_current[1].envelope==1u);
    assert(plan.env_current[2].start==14u &&
           plan.env_current[2].end==22u &&
           plan.env_current[2].envelope==2u);
    assert(plan.env_current[3].start==22u &&
           plan.env_current[3].end==32u &&
           plan.env_current[3].envelope==3u);
    assert(plan.env_deferred[0].start==0u &&
           plan.env_deferred[0].end==2u &&
           plan.env_deferred[0].envelope==3u);
    assert(plan.noise_deferred[0].start==0u &&
           plan.noise_deferred[0].end==2u &&
           plan.noise_deferred[0].envelope==1u);
    const SautiAacSbrTemporalPlan snapshot=plan;
    const uint8_t saved_end=out.env_edges[4];
    out.env_edges[4]=39u; /* > maximum 3 future SBR slots */
    assert(!sauti_aac_sbr_temporal_plan(&out,&plan));
    assert(memcmp(&snapshot,&plan,sizeof(plan))==0);
    out.env_edges[4]=saved_end;

    /* SBR-49: retain the complete deferred payload across the next
     * frame, without silently truncating or synthesizing the tail. */
    SautiAacSbrTemporalHistory history={0};
    SautiAacSbrTemporalHandoff handoff;
    assert(sauti_aac_sbr_temporal_history_push(&out,&history,&handoff));
    assert(history.frames==1u && !history.has_previous &&
           history.current_plan.deferred_slots==2u);
    assert(handoff.previous_tail_slots==0u);
    SautiAacSbrPayloadFrame following=saved;
    assert(sauti_aac_sbr_temporal_history_push(
        &following,&history,&handoff));
    assert(history.frames==2u && history.has_previous);
    assert(history.previous.grid.env_border[4]==17u);
    assert(history.previous.energy.env_count==4u);
    assert(handoff.previous_tail_slots==2u &&
           handoff.current_start_slots==0u &&
           handoff.overlapping_slots==2u);
    /* Exact, non-overlapping handoff: next FIXFIX-like payload starts
     * at QMF slot 2; first two slots retain the previous FIXVAR
     * envelope and noise indices. This maps metadata, not PCM. */
    SautiAacSbrTemporalHistory aligned={0};
    SautiAacSbrPayloadFrame next_aligned=saved;
    next_aligned.env_edges[0]=2u;
    next_aligned.noise_edges[0]=2u;
    assert(sauti_aac_sbr_temporal_history_push(&out,&aligned,&handoff));
    assert(sauti_aac_sbr_temporal_history_push(
        &next_aligned,&aligned,&handoff));
    assert(handoff.previous_tail_slots==2u &&
           handoff.current_start_slots==2u &&
           handoff.overlapping_slots==0u);
    SautiAacSbrResolvedFrame resolved;
    assert(sauti_aac_sbr_temporal_resolve(&aligned,&resolved));
    assert(resolved.previous_slots==2u);
    for(unsigned t=0;t<32u;t++){
        assert(resolved.slot[t].source==(t<2u?0u:1u));
        if(t<2u){
            assert(resolved.slot[t].envelope==3u);
            assert(resolved.slot[t].noise==1u);
        }else{
            assert(resolved.slot[t].envelope==(t<16u?0u:1u));
            assert(resolved.slot[t].noise==(t<16u?0u:1u));
        }
    }

    /* SBR-53a: actual energy/noise and signaled harmonic targets.
     * Deliberately distinguish the two original frame payloads. */
    aligned.previous.energy.env_energy[3][0]+=100.0;
    aligned.previous.energy.noise_factor[1][0]+=0.5;
    aligned.previous.harmonic[0]=1u;
    aligned.current.energy.env_energy[0][0]+=5.0;
    aligned.current.harmonic[0]=0u;
    SautiAacSbrTemporalHfTargets hf_targets;
    assert(sauti_aac_sbr_temporal_hf_targets(&aligned,&hf_targets));
    assert(hf_targets.previous_slots==2u);
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrTemporalHfSlot *slot=&hf_targets.slot[t];
        assert(slot->source==(t<2u?0u:1u));
        assert(slot->envelope==resolved.slot[t].envelope);
        assert(slot->noise==resolved.slot[t].noise);
        const SautiAacSbrPayloadFrame *f=t<2u?
                                    &aligned.previous:&aligned.current;
        const SautiAacSbrTables *tab=&f->tables;
        const unsigned fine=f->energy.freq_res[slot->envelope];
        const uint8_t *edges=fine?tab->high:tab->low;
        const unsigned bands=fine?tab->n_high:tab->n_low;
        for(unsigned b=0u;b<bands;b++)
            for(unsigned k=edges[b];k<edges[b+1u];k++)
                assert(slot->energy[k]==
                       f->energy.env_energy[slot->envelope][b]);
        for(unsigned b=0u;b<tab->n_noise;b++)
            for(unsigned k=tab->noise[b];k<tab->noise[b+1u];k++)
                assert(slot->noise_factor[k]==
                       f->energy.noise_factor[slot->noise][b]);
        const unsigned harmonic_bin=(tab->high[0]+tab->high[1])/2u;
        assert(slot->harmonic[harmonic_bin]==(t<2u?1u:0u));
    }
    SautiAacSbrTemporalHfTargets hf_saved;
    memcpy(&hf_saved,&hf_targets,sizeof(hf_targets));
    SautiAacSbrTemporalHistory hf_bad=aligned;
    hf_bad.previous.energy.env_energy[3][0]=NAN;
    assert(!sauti_aac_sbr_temporal_hf_targets(&hf_bad,&hf_targets));
    assert(memcmp(&hf_targets,&hf_saved,sizeof(hf_targets))==0);
    hf_bad=aligned;
    hf_bad.current.energy.noise_factor[0][0]=-1.0;
    assert(!sauti_aac_sbr_temporal_hf_targets(&hf_bad,&hf_targets));
    assert(memcmp(&hf_targets,&hf_saved,sizeof(hf_targets))==0);
    hf_bad=aligned;
    hf_bad.current.tables.noise[1]=hf_bad.current.tables.noise[0];
    assert(!sauti_aac_sbr_temporal_hf_targets(&hf_bad,&hf_targets));
    assert(memcmp(&hf_targets,&hf_saved,sizeof(hf_targets))==0);
    puts("SBR-53a: per-slot HF energy/noise/harmonic targets and rollback PASS");

    SautiAacSbrResolvedFrame prev_resolved=resolved;
    next_aligned.env_edges[0]=0u;
    next_aligned.noise_edges[0]=0u;
    aligned.current=next_aligned; /* overlap with previously retained tail */
    assert(!sauti_aac_sbr_temporal_resolve(&aligned,&resolved));
    assert(memcmp(&prev_resolved,&resolved,sizeof(resolved))==0);
    SautiAacSbrTemporalHistory snapshot_history=history;
    SautiAacSbrTemporalHandoff snapshot_handoff=handoff;
    following.env_edges[0]=4u;
    following.noise_edges[0]=4u;
    /* Force a new deferral then test forbidden temporal gap. */
    SautiAacSbrTemporalHistory pending={0};
    assert(sauti_aac_sbr_temporal_history_push(&out,&pending,&handoff));
    snapshot_handoff=handoff;
    snapshot_history=pending;
    assert(!sauti_aac_sbr_temporal_history_push(
        &following,&pending,&handoff));
    assert(memcmp(&snapshot_history,&pending,sizeof(pending))==0);
    assert(memcmp(&snapshot_handoff,&handoff,sizeof(handoff))==0);

    SautiAacSbrHfJointConfig hf;
    assert(!sauti_aac_sbr_payload_make_hf(&out,&hf));
    puts("SBR-52: FIXVAR 2-slot tail + following frame -> exact 32 QMF slot owners, rollback PASS");
}
int main(void){
    test_header_grid_entropy_and_harmonics();
    test_atomic_failure_modes();
    test_temporal_fixvar_17_side_data();
    puts("PASS HE-AAC SBR-29 entropy-coded FIL SCE payload to QMF-frame parameters");
    return 0;
}
