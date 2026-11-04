/* HE-AAC SBR-29: bit-accurate EXT_SBR_DATA (type 13) SCE
 * payload bridge into the independently verified SBR-28 QMF channel.
 *
 * This is a bounded FULL SBR SCE grammar for the supported subset:
 * extension nibble, bs_header_flag, header/reuse, bs_data_extra,
 * grid, dtdf/invf, entropy-coded envelope+noise, add_harmonic flags,
 * extended_data flag and byte padding. CPE/PS/CRC are deliberately
 * NOT accepted. No external codec, heap or PCM decode here.
 */
#include "sauti_aac_sbr_payload.h"
#include "sauti_aac_headers.h"
#include <string.h>

static int bits(const uint8_t *data,size_t n,size_t *pos,
                unsigned width,uint32_t *out) {
    return sauti_aac_bits_read_asm(data,n,pos,width,out);
}
static int build(const SautiAacSbrHeader *h,unsigned rate,
                 SautiAacSbrMaster *m,SautiAacSbrTables *t,
                 SautiAacSbrPatches *patches) {
    SautiAacSbrLimits lim;
    if(!sauti_aac_sbr_limits_compute(rate,h->start_freq,
        h->stop_freq,&lim))return 0;
    if(!(h->freq_scale==0u
            ? sauti_aac_sbr_master_linear(lim.k0,lim.k2,
                h->alter_scale,h->xover_band,m)
            : sauti_aac_sbr_master_log(lim.k0,lim.k2,
                h->freq_scale,h->alter_scale,h->xover_band,m)))
        return 0;
    return sauti_aac_sbr_tables_build(m,h->noise_bands,t) &&
           sauti_aac_sbr_patch_build(rate,m,t,patches);
}
static unsigned attack_env(const SautiAacSbrGrid *g) {
    if((g->frame_class==1u || g->frame_class==3u) &&
       g->pointer)
        return g->env_count+1u-g->pointer;
    if(g->frame_class==2u && g->pointer>1u)
        return g->pointer-1u;
    return 0u; /* no signaled onset: tones may start at first env */
}

static int payload_parse_impl(
    const uint8_t *bytes,size_t bytes_count,
    size_t bit_start,size_t payload_bits,unsigned output_rate,
    SautiAacSbrPayloadState *state,SautiAacSbrPayloadFrame *out,
    SautiAacSbrPayloadStage *stage,unsigned allow_temporal) {
    if(stage)*stage=SAUTI_SBR_PAYLOAD_INPUT;
    if(!bytes || !state || !out || state->initialized>1u ||
       (!state->initialized && state->frames!=0u) ||
       (state->initialized && !state->frames) ||
       state->frames==UINT64_MAX ||
       bytes_count>SIZE_MAX/8u || bit_start>bytes_count*8u ||
       payload_bits<8u || (payload_bits&7u)!=0u ||
       payload_bits>SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES*8u ||
       payload_bits>bytes_count*8u-bit_start)
        return 0;

    /* Normalize arbitrarily unaligned AAC FIL payload to a bounded
     * private array. Every downstream parser is now bounded to the
     * exact FIL extension, not the rest of ADTS raw_data_block. */
    uint8_t packed[SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES]={0};
    for(size_t i=0;i<payload_bits;i++){
        const size_t source=bit_start+i;
        const unsigned value=(bytes[source/8u] >>
                             (7u-(unsigned)(source&7u)))&1u;
        packed[i/8u]|=(uint8_t)(value<<(7u-(unsigned)(i&7u)));
    }
    const size_t len=payload_bits/8u;
    size_t pos=0u;
    uint32_t v=0u;
    if(stage)*stage=SAUTI_SBR_PAYLOAD_TYPE;
    if(!bits(packed,len,&pos,4u,&v) || v!=13u)return 0;
    if(stage)*stage=SAUTI_SBR_PAYLOAD_HEADER;
    if(!bits(packed,len,&pos,1u,&v))return 0;

    SautiAacSbrPayloadState next=*state;
    SautiAacSbrPayloadFrame result;
    memset(&result,0,sizeof(result));
    if(v) {
        if(!sauti_aac_sbr_header_parse(packed,len,&pos,
                                      &next.header))return 0;
    }else if(!state->initialized)
        return 0;
    result.header=next.header;
    if(stage)*stage=SAUTI_SBR_PAYLOAD_TABLES;
    if(!build(&result.header,output_rate,&result.master,
              &result.tables,&result.patches))
        return 0;

    if(stage)*stage=SAUTI_SBR_PAYLOAD_GRID;
    /* SCE syntax: bs_data_extra, optionally four reserved bits. */
    if(!bits(packed,len,&pos,1u,&v))return 0;
    if(v && !bits(packed,len,&pos,4u,&v))return 0;

    if(!sauti_aac_sbr_grid_parse(packed,len,&pos,16u,
                                &result.grid))
        return 0;
    /* For the first 1024-sample/32-slot bridge, require complete
     * within-frame time borders. Time borders that extend outside
     * [0,16] require lookahead across AAC frames and are deferred. */
    if(stage)*stage=SAUTI_SBR_PAYLOAD_BORDERS;
    if(!allow_temporal &&
       (result.grid.env_border[0]!=0u ||
        result.grid.env_border[result.grid.env_count]!=16u ||
        result.grid.noise_border[0]!=0u ||
        result.grid.noise_border[result.grid.noise_count]!=16u))
        return 0;

    if(stage)*stage=SAUTI_SBR_PAYLOAD_CONTROL;
    if(!sauti_aac_sbr_control_parse(packed,len,&pos,&result.grid,
                                   &result.tables,&result.control))
        return 0;
    if(stage)*stage=SAUTI_SBR_PAYLOAD_QUANT;
    if(!sauti_aac_sbr_quant_decode(packed,len,&pos,
                                  &result.grid,&result.tables,
                                  &result.control,result.header.amp_res,
                                  0u,&next.quant_state,&result.quant))
        return 0;
    if(stage)*stage=SAUTI_SBR_PAYLOAD_ENERGY;
    if(!sauti_aac_sbr_energy_dequant(&result.quant,&result.grid,
                                    &result.tables,result.header.amp_res,
                                    0u,&result.energy))
        return 0;

    if(stage)*stage=SAUTI_SBR_PAYLOAD_HARMONICS;
    /* bs_add_harmonic_flag, then n_high independent 1-bit flags. */
    if(!bits(packed,len,&pos,1u,&v))return 0;
    if(v)
        for(unsigned i=0;i<result.tables.n_high;i++){
            if(!bits(packed,len,&pos,1u,&v))return 0;
            result.harmonic[i]=(uint8_t)v;
        }

    /* The first supported subset has NO PS nor other extended data.
     * Reject nonzero bs_extended_data rather than silently dropping
     * stereo or treating its bytes as alignment filler. */
    if(stage)*stage=SAUTI_SBR_PAYLOAD_EXTENDED;
    if(!bits(packed,len,&pos,1u,&v) || v)return 0;

    if(stage)*stage=SAUTI_SBR_PAYLOAD_PADDING;
    if(pos>payload_bits || payload_bits-pos>7u)return 0;
    result.payload_data_bits=(uint32_t)pos;
    /* bs_fill_bits in this subset are required to be zero. */
    while(pos<payload_bits) {
        if(!bits(packed,len,&pos,1u,&v) || v)return 0;
    }

    for(unsigned i=0;i<=result.grid.env_count;i++)
        result.env_edges[i]=2u*result.grid.env_border[i];
    for(unsigned i=0;i<=result.grid.noise_count;i++)
        result.noise_edges[i]=2u*result.grid.noise_border[i];
    result.transient_env=attack_env(&result.grid);
    if(result.transient_env<result.grid.env_count)
        result.attack[result.transient_env]=
          result.transient_env!=0u?1u:0u;
    result.bs_smoothing_mode=result.header.smoothing_mode;
    result.limiter_mode=result.header.limiter_bands;
    result.gain_mode=result.header.limiter_gains;

    next.initialized=1u;
    next.frames++;
    *out=result;
    *state=next;
    if(stage)*stage=SAUTI_SBR_PAYLOAD_OK;
    return 1;
}

int sauti_aac_sbr_payload_sce_parse_ex(
    const uint8_t *bytes,size_t bytes_count,
    size_t bit_start,size_t payload_bits,unsigned output_rate,
    SautiAacSbrPayloadState *state,SautiAacSbrPayloadFrame *out,
    SautiAacSbrPayloadStage *stage) {
    return payload_parse_impl(bytes,bytes_count,bit_start,payload_bits,
                              output_rate,state,out,stage,0u);
}

int sauti_aac_sbr_payload_temporal_parse_ex(
    const uint8_t *bytes,size_t bytes_count,
    size_t bit_start,size_t payload_bits,unsigned output_rate,
    SautiAacSbrPayloadState *state,SautiAacSbrPayloadFrame *out,
    SautiAacSbrPayloadStage *stage) {
    return payload_parse_impl(bytes,bytes_count,bit_start,payload_bits,
                              output_rate,state,out,stage,1u);
}

int sauti_aac_sbr_payload_sce_parse(
    const uint8_t *bytes,size_t bytes_count,
    size_t bit_start,size_t payload_bits,unsigned output_rate,
    SautiAacSbrPayloadState *state,SautiAacSbrPayloadFrame *out) {
    return sauti_aac_sbr_payload_sce_parse_ex(
        bytes,bytes_count,bit_start,payload_bits,output_rate,
        state,out,NULL);
}

int sauti_aac_sbr_payload_make_hf(
    const SautiAacSbrPayloadFrame *frame,
    SautiAacSbrHfJointConfig *out) {
    if(!frame || !out ||
       frame->grid.env_count<1u ||
       frame->grid.env_count>SAUTI_AAC_SBR_MAX_ENV ||
       frame->grid.noise_count!=(frame->grid.env_count==1u?1u:2u) ||
       frame->env_edges[0]!=0u ||
       frame->env_edges[frame->grid.env_count]!=32u ||
       frame->noise_edges[0]!=0u ||
       frame->noise_edges[frame->grid.noise_count]!=32u ||
       frame->tables.n_high<1u ||
       frame->tables.n_high>SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
       frame->limiter_mode>3u || frame->gain_mode>3u ||
       frame->transient_env>frame->grid.env_count)
        return 0;
    SautiAacSbrHfJointConfig cfg={
        .tables=&frame->tables,
        .patches=&frame->patches,
        .energy=&frame->energy,
        .harmonic_flags=frame->harmonic,
        .flag_count=frame->tables.n_high,
        .transient_env=frame->transient_env,
        .env_edges=frame->env_edges,
        .noise_edges=frame->noise_edges,
        .slots=32u,
        .limiter_mode=frame->limiter_mode,
        .gain_mode=frame->gain_mode
    };
    *out=cfg;
    return 1;
}
