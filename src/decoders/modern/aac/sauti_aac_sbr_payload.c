/* HE-AAC SBR-29/62: bit-accurate EXT_SBR_DATA (type 13) and
 * EXT_SBR_DATA_CRC (type 14) SCE
 * payload bridge into the independently verified SBR-28 QMF channel.
 *
 * This is a bounded FULL SBR SCE grammar for the supported subset:
 * extension nibble, bs_header_flag, header/reuse, bs_data_extra,
 * grid, dtdf/invf, entropy-coded envelope+noise, add_harmonic flags,
 * byte-counted extended_data with standardized PS side data, and byte padding.
 * CPE remains a separate grammar; type-14 CRC is verified before state commits. No external
 * codec, heap or PCM decode here.
 */
#include "sauti_aac_sbr_payload.h"
#include "sauti_aac_sbr_crc.h"
#include "sauti_aac_headers.h"
#include <string.h>
#include <math.h>

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

/* Parse the byte-counted SBR extension range without allowing PS to see the
 * following SBR fill bits. The PS parser supports arbitrary bit offsets, but
 * compacting this range makes its byte-size boundary exact. */
static int ps_extension_parse(const uint8_t *data,size_t size,size_t *pos,
                              size_t available,SautiAacPsHeaderState *state,
                              SautiAacPsFrame *frame,size_t *used){
    if(!data||!pos||!state||!frame||!used||available>SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES*8u ||
       size>SIZE_MAX/8u || *pos>size*8u || available>size*8u-*pos)return 0;
    uint8_t compact[SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES]={0};
    for(size_t i=0u;i<available;i++){
        const size_t source=*pos+i;
        const unsigned bit=(data[source/8u]>>(7u-(unsigned)(source&7u)))&1u;
        compact[i/8u]|=(uint8_t)(bit<<(7u-(unsigned)(i&7u)));
    }
    size_t ps_pos=0u;
    if(!sauti_aac_ps_frame_parse(compact,(available+7u)/8u,&ps_pos,state,frame) ||
       ps_pos>available)return 0;
    *used=ps_pos;
    return 1;
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
       state->ps.initialized>1u ||
       (!state->ps.initialized && state->ps.frames!=0u) ||
       (state->ps.initialized && !state->ps.frames) ||
       state->ps.frames>state->frames ||
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
    uint32_t v=0u,extension=0u,received_crc=0u;
    if(stage)*stage=SAUTI_SBR_PAYLOAD_TYPE;
    if(!bits(packed,len,&pos,4u,&extension) ||
       (extension!=13u && extension!=14u))return 0;
    if(extension==14u && !bits(packed,len,&pos,10u,&received_crc))return 0;
    const size_t crc_start=pos;
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

    /* SBR extended_data has its own byte-counted scope. Decode the one
     * standardized PS extension (id 2) into side data and reject all other
     * extension ids rather than silently dropping future stereo information. */
    if(stage)*stage=SAUTI_SBR_PAYLOAD_EXTENDED;
    if(!bits(packed,len,&pos,1u,&v))return 0;
    if(v){
        uint32_t extension_bytes=0u,esc=0u;
        if(!bits(packed,len,&pos,4u,&extension_bytes))return 0;
        if(extension_bytes==15u){
            if(!bits(packed,len,&pos,8u,&esc) || esc>UINT32_MAX-15u)return 0;
            extension_bytes+=esc;
        }
        if(pos>payload_bits || (size_t)extension_bytes>(payload_bits-pos)/8u)
            return 0;
        size_t remain=(size_t)extension_bytes*8u;
        while(remain>7u){
            uint32_t id=0u;size_t used=0u;
            if(!bits(packed,len,&pos,2u,&id))return 0;
            remain-=2u;
            if(id!=2u || result.has_ps ||
               !ps_extension_parse(packed,len,&pos,remain,&next.ps,
                                   &result.ps,&used))return 0;
            pos+=used;remain-=used;result.has_ps=1u;
        }
        while(remain){
            if(!bits(packed,len,&pos,1u,&v) || v)return 0;
            remain--;
        }
    }

    if(stage)*stage=SAUTI_SBR_PAYLOAD_PADDING;
    if(pos>payload_bits || payload_bits-pos>7u)return 0;
    if(extension==14u){
        uint16_t calculated=0u;
        /* bs_sbr_crc_bits protects sbr_extension_data after itself, up to
         * its syntactic end. The extension type and byte-alignment fill are
         * outside that region. Check before committing differential state. */
        if(!sauti_aac_sbr_crc10_bits(packed,len,crc_start,pos-crc_start,
                                     &calculated) ||
           calculated!=received_crc)
            return 0;
    }
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

/* CPE needs separate sbr_dtdf() for each channel but, when coupled, only
 * one sbr_invf() follows both flag sets. The existing public control parser
 * intentionally models the common single-channel order, so keep this small
 * bounded variant local to the CPE grammar. */
static int cpe_control_parse(const uint8_t *data,size_t size,size_t *pos,
                             const SautiAacSbrGrid *grid,
                             const SautiAacSbrTables *tables,
                             unsigned with_invf,SautiAacSbrControl *out){
    if(!data || !pos || !grid || !tables || !out ||
       grid->env_count<1u || grid->env_count>SAUTI_AAC_SBR_MAX_ENV ||
       grid->noise_count!=(grid->env_count==1u?1u:2u) ||
       tables->n_noise<1u ||
       tables->n_noise>SAUTI_AAC_SBR_MAX_NOISE_BANDS)
        return 0;
    SautiAacSbrControl next={0};
    size_t at=*pos;uint32_t v=0u;
    next.env_count=grid->env_count;
    next.noise_count=grid->noise_count;
    next.noise_band_count=tables->n_noise;
    for(unsigned i=0;i<grid->env_count;i++){
        if(!bits(data,size,&at,1u,&v))return 0;
        next.df_env[i]=(uint8_t)v;
    }
    for(unsigned i=0;i<grid->noise_count;i++){
        if(!bits(data,size,&at,1u,&v))return 0;
        next.df_noise[i]=(uint8_t)v;
    }
    if(with_invf)
        for(unsigned i=0;i<tables->n_noise;i++){
            if(!bits(data,size,&at,2u,&v))return 0;
            next.invf_mode[i]=(uint8_t)v;
        }
    *out=next;*pos=at;
    return 1;
}
static int payload_frame_finish(SautiAacSbrPayloadFrame *frame,
                                const SautiAacSbrHeader *header,
                                const SautiAacSbrGrid *grid,
                                const SautiAacSbrControl *control,
                                const SautiAacSbrQuantFrame *quant,
                                const SautiAacSbrEnergyFrame *energy,
                                const SautiAacSbrMaster *master,
                                const SautiAacSbrTables *tables,
                                const SautiAacSbrPatches *patches,
                                const uint8_t harmonic[],
                                uint32_t payload_data_bits){
    if(!frame || !header || !grid || !control || !quant || !energy ||
       !master || !tables || !patches || !harmonic ||
       grid->env_count<1u || grid->env_count>SAUTI_AAC_SBR_MAX_ENV ||
       grid->noise_count!=(grid->env_count==1u?1u:2u))return 0;
    SautiAacSbrPayloadFrame result={0};
    result.header=*header;result.grid=*grid;result.control=*control;
    result.quant=*quant;result.energy=*energy;result.master=*master;
    result.tables=*tables;result.patches=*patches;
    memcpy(result.harmonic,harmonic,tables->n_high);
    for(unsigned i=0;i<=grid->env_count;i++)
        result.env_edges[i]=2u*grid->env_border[i];
    for(unsigned i=0;i<=grid->noise_count;i++)
        result.noise_edges[i]=2u*grid->noise_border[i];
    result.transient_env=attack_env(grid);
    if(result.transient_env<grid->env_count)
        result.attack[result.transient_env]=result.transient_env!=0u?1u:0u;
    result.bs_smoothing_mode=header->smoothing_mode;
    result.limiter_mode=header->limiter_bands;
    result.gain_mode=header->limiter_gains;
    result.payload_data_bits=payload_data_bits;
    *frame=result;
    return 1;
}

int sauti_aac_sbr_payload_cpe_parse(
    const uint8_t *bytes,size_t bytes_count,
    size_t bit_start,size_t payload_bits,unsigned output_rate,
    SautiAacSbrPayloadCpeState *state,SautiAacSbrPayloadCpeFrame *out){
    if(!bytes || !state || !out || state->initialized>1u ||
       (!state->initialized && state->frames!=0u) ||
       (state->initialized && !state->frames) ||
       state->frames==UINT64_MAX || bytes_count>SIZE_MAX/8u ||
       bit_start>bytes_count*8u || payload_bits<8u ||
       (payload_bits&7u)!=0u ||
       payload_bits>SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES*8u ||
       payload_bits>bytes_count*8u-bit_start)
        return 0;
    uint8_t packed[SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES]={0};
    for(size_t i=0;i<payload_bits;i++){
        const size_t source=bit_start+i;
        packed[i/8u]|=(uint8_t)(((bytes[source/8u] >>
            (7u-(unsigned)(source&7u)))&1u) <<
            (7u-(unsigned)(i&7u)));
    }
    const size_t len=payload_bits/8u;
    size_t pos=0u;uint32_t extension=0u,v=0u,received_crc=0u;
    if(!bits(packed,len,&pos,4u,&extension) ||
       (extension!=13u && extension!=14u) ||
       (extension==14u && !bits(packed,len,&pos,10u,&received_crc)))
        return 0;
    const size_t crc_start=pos;
    if(!bits(packed,len,&pos,1u,&v))return 0;
    SautiAacSbrPayloadCpeState next=*state;
    if(v){
        if(!sauti_aac_sbr_header_parse(packed,len,&pos,&next.header))return 0;
    }else if(!state->initialized)return 0;
    SautiAacSbrMaster master;
    SautiAacSbrTables tables;
    SautiAacSbrPatches patches;
    if(!build(&next.header,output_rate,&master,&tables,&patches))return 0;
    if(!bits(packed,len,&pos,1u,&v))return 0;
    if(v){
        uint32_t reserved=0u;
        if(!bits(packed,len,&pos,4u,&reserved) || reserved ||
           !bits(packed,len,&pos,4u,&reserved) || reserved)return 0;
    }
    uint32_t coupling=0u;
    if(!bits(packed,len,&pos,1u,&coupling))return 0;
    SautiAacSbrGrid grid[2];
    SautiAacSbrControl control[2];
    if(!sauti_aac_sbr_grid_parse(packed,len,&pos,16u,&grid[0]))return 0;
    if(coupling)grid[1]=grid[0];
    else if(!sauti_aac_sbr_grid_parse(packed,len,&pos,16u,&grid[1]))return 0;
    if(coupling){
        if(!cpe_control_parse(packed,len,&pos,&grid[0],&tables,0u,
                              &control[0]) ||
           !cpe_control_parse(packed,len,&pos,&grid[1],&tables,1u,
                              &control[1]))return 0;
        /* In coupled syntax inverse filtering belongs to channel zero. */
        memcpy(control[0].invf_mode,control[1].invf_mode,
               sizeof(control[0].invf_mode));
    }else{
        if(!sauti_aac_sbr_control_parse(packed,len,&pos,&grid[0],&tables,
                                        &control[0]) ||
           !sauti_aac_sbr_control_parse(packed,len,&pos,&grid[1],&tables,
                                        &control[1]))return 0;
    }
    SautiAacSbrQuantFrame quant[2];
    if(coupling){
        if(!sauti_aac_sbr_quant_decode(packed,len,&pos,&grid[0],&tables,
                                       &control[0],next.header.amp_res,0u,
                                       &next.quant_state[0],&quant[0]) ||
           !sauti_aac_sbr_quant_decode(packed,len,&pos,&grid[1],&tables,
                                       &control[1],next.header.amp_res,1u,
                                       &next.quant_state[1],&quant[1]))return 0;
    }else if(!sauti_aac_sbr_quant_decode_cpe_uncoupled(
                  packed,len,&pos,grid,&tables,control,next.header.amp_res,
                  next.quant_state,quant))return 0;
    SautiAacSbrEnergyFrame energy[2];
    if(coupling){
        if(!sauti_aac_sbr_coupled_dequant(&quant[0],&quant[1],&grid[0],
                                           &tables,next.header.amp_res,
                                           &energy[0],&energy[1]))return 0;
    }else if(!sauti_aac_sbr_energy_dequant(&quant[0],&grid[0],&tables,
                                            next.header.amp_res,0u,&energy[0]) ||
              !sauti_aac_sbr_energy_dequant(&quant[1],&grid[1],&tables,
                                            next.header.amp_res,0u,&energy[1]))
        return 0;
    uint8_t harmonic[2][SAUTI_AAC_SBR_MAX_MASTER_BANDS]={{0}};
    for(unsigned ch=0;ch<2u;ch++){
        if(!bits(packed,len,&pos,1u,&v))return 0;
        if(v)for(unsigned i=0;i<tables.n_high;i++){
            if(!bits(packed,len,&pos,1u,&v))return 0;
            harmonic[ch][i]=(uint8_t)v;
        }
    }
    if(!bits(packed,len,&pos,1u,&v) || v || pos>payload_bits ||
       payload_bits-pos>7u)return 0;
    if(extension==14u){
        uint16_t calculated=0u;
        if(!sauti_aac_sbr_crc10_bits(packed,len,crc_start,pos-crc_start,
                                     &calculated) || calculated!=received_crc)
            return 0;
    }
    const uint32_t data_bits=(uint32_t)pos;
    while(pos<payload_bits)
        if(!bits(packed,len,&pos,1u,&v) || v)return 0;
    SautiAacSbrPayloadCpeFrame result={0};
    result.coupling=coupling;
    for(unsigned ch=0;ch<2u;ch++)
        if(!payload_frame_finish(&result.channel[ch],&next.header,&grid[ch],
                                 &control[ch],&quant[ch],&energy[ch],&master,
                                 &tables,&patches,harmonic[ch],data_bits))
            return 0;
    next.initialized=1u;next.frames++;
    *out=result;*state=next;
    return 1;
}

int sauti_aac_sbr_payload_cpe_make_hf(
    const SautiAacSbrPayloadCpeFrame *frame,unsigned channel,
    SautiAacSbrHfJointConfig *out){
    return frame && channel<2u
        ? sauti_aac_sbr_payload_make_hf(&frame->channel[channel],out) : 0;
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

/* SBR-47: split decoded temporal side data without altering its
 * envelope identity. This makes delayed QMF windows explicit and
 * keeps the PCM-producing entry point strictly unchanged. */
static int temporal_split(const uint8_t *edges,unsigned count,
                          SautiAacSbrTemporalSegment *current,
                          uint8_t *current_count,
                          SautiAacSbrTemporalSegment *deferred,
                          uint8_t *deferred_count) {
    unsigned nc=0u,nd=0u;
    if(count<1u || count>SAUTI_AAC_SBR_MAX_ENV)return 0;
    for(unsigned e=0;e<count;e++){
        const unsigned a=edges[e],b=edges[e+1u];
        if(b<=a || b>38u)return 0;
        if(a<32u) {
            const unsigned z=b<32u?b:32u;
            if(a<z)
                current[nc++]=(SautiAacSbrTemporalSegment){
                    (uint8_t)a,(uint8_t)z,(uint8_t)e};
        }
        if(b>32u){
            const unsigned x=a>32u?a:32u;
            if(x<b)
                deferred[nd++]=(SautiAacSbrTemporalSegment){
                    (uint8_t)(x-32u),(uint8_t)(b-32u),(uint8_t)e};
        }
    }
    *current_count=(uint8_t)nc;
    *deferred_count=(uint8_t)nd;
    return 1;
}

int sauti_aac_sbr_temporal_plan(
    const SautiAacSbrPayloadFrame *frame,
    SautiAacSbrTemporalPlan *out){
    if(!frame || !out ||
       frame->grid.env_count<1u ||
       frame->grid.env_count>SAUTI_AAC_SBR_MAX_ENV ||
       frame->grid.noise_count!=(frame->grid.env_count==1u?1u:2u))
        return 0;
    SautiAacSbrTemporalPlan next={0};
    if(!temporal_split(frame->env_edges,frame->grid.env_count,
                       next.env_current,&next.env_current_count,
                       next.env_deferred,&next.env_deferred_count) ||
       !temporal_split(frame->noise_edges,frame->grid.noise_count,
                       next.noise_current,&next.noise_current_count,
                       next.noise_deferred,&next.noise_deferred_count))
        return 0;
    const unsigned end=frame->env_edges[frame->grid.env_count];
    if(frame->noise_edges[frame->grid.noise_count]!=end)return 0;
    /* Future SBR QMF history is bounded to at most 3 SBR time slots
     * (6 QMF slots) by the classic 1024-sample sbr_grid syntax. */
    next.deferred_slots=(uint8_t)(end>32u?end-32u:0u);
    *out=next;
    return 1;
}

/* SBR-49: keep decoded temporal parameters intact until a future
 * two-frame QMF stage can use them. No synthesis, clipping, or gain
 * arbitration is performed by this side-data container. */
int sauti_aac_sbr_temporal_history_push(
    const SautiAacSbrPayloadFrame *frame,
    SautiAacSbrTemporalHistory *history,
    SautiAacSbrTemporalHandoff *handoff){
    if(!frame || !history || !handoff ||
       history->ready>1u || history->has_previous>1u ||
       history->frames==UINT64_MAX ||
       (!history->ready && (history->frames!=0u ||
                            history->has_previous)) ||
       (history->ready && history->frames==0u))
        return 0;

    SautiAacSbrTemporalPlan plan;
    if(!sauti_aac_sbr_temporal_plan(frame,&plan))return 0;
    if(frame->env_edges[0]!=frame->noise_edges[0])return 0;
    const unsigned beginning=frame->env_edges[0];
    /* The current SBR grid cannot start beyond the current 32-slot
     * window; a previous tail cannot be abandoned across a gap. */
    if(beginning>32u)return 0;

    SautiAacSbrTemporalHandoff result={0};
    if(history->ready){
        const unsigned pending=history->current_plan.deferred_slots;
        result.previous_tail_slots=(uint8_t)pending;
        result.current_start_slots=(uint8_t)beginning;
        if(pending && beginning>pending)return 0;
        result.overlapping_slots=(uint8_t)(
            pending>beginning?pending-beginning:0u);
    }

    SautiAacSbrTemporalHistory next=*history;
    if(next.ready){
        next.previous=next.current;
        next.previous_plan=next.current_plan;
        next.has_previous=1u;
    }
    next.current=*frame;
    next.current_plan=plan;
    next.ready=1u;
    next.frames++;
    *history=next;
    *handoff=result;
    return 1;
}

/* SBR-52: a strict ownership map for each output-frame QMF slot.
 * This resolves TIME and original side-data ownership only, never
 * converts the metadata into QMF gains or publishes audio samples. */
static int assign_temporal_segments(
    const SautiAacSbrTemporalSegment *segments,unsigned count,
    unsigned source,uint8_t owner[32],uint8_t index[32]){
    if(count>SAUTI_AAC_SBR_MAX_ENV)return 0;
    for(unsigned e=0;e<count;e++){
        const unsigned a=segments[e].start,b=segments[e].end;
        if(a>=b || b>32u)return 0;
        for(unsigned t=a;t<b;t++){
            if(owner[t]!=255u)return 0; /* overlapping ownership */
            owner[t]=(uint8_t)source;
            index[t]=segments[e].envelope;
        }
    }
    return 1;
}

int sauti_aac_sbr_temporal_resolve(
    const SautiAacSbrTemporalHistory *history,
    SautiAacSbrResolvedFrame *out){
    if(!history || !out || history->ready!=1u ||
       history->frames==0u || history->has_previous>1u ||
       (history->has_previous && history->frames<2u))
        return 0;
    const SautiAacSbrTemporalPlan *now=&history->current_plan;
    const unsigned prev=history->has_previous?
        history->previous_plan.deferred_slots:0u;
    const unsigned start=history->current.env_edges[0];
    if(start!=prev || start>6u ||
       history->current.noise_edges[0]!=start)return 0;

    uint8_t env_src[32],env_idx[32],noise_src[32],noise_idx[32];
    memset(env_src,255,sizeof(env_src));
    memset(noise_src,255,sizeof(noise_src));
    memset(env_idx,0,sizeof(env_idx));
    memset(noise_idx,0,sizeof(noise_idx));
    if(prev){
        const SautiAacSbrTemporalPlan *old=&history->previous_plan;
        if(!assign_temporal_segments(
                old->env_deferred,old->env_deferred_count,0u,
                env_src,env_idx) ||
           !assign_temporal_segments(
                old->noise_deferred,old->noise_deferred_count,0u,
                noise_src,noise_idx))return 0;
    }
    if(!assign_temporal_segments(
            now->env_current,now->env_current_count,1u,
            env_src,env_idx) ||
       !assign_temporal_segments(
            now->noise_current,now->noise_current_count,1u,
            noise_src,noise_idx))return 0;

    SautiAacSbrResolvedFrame result={0};
    result.previous_slots=(uint8_t)prev;
    for(unsigned t=0;t<32u;t++){
        if(env_src[t]==255u || noise_src[t]==255u ||
           env_src[t]!=noise_src[t])return 0;
        const SautiAacSbrPayloadFrame *source=
            env_src[t]==0u?&history->previous:&history->current;
        if(env_idx[t]>=source->grid.env_count ||
           noise_idx[t]>=source->grid.noise_count)return 0;
        result.slot[t]=(SautiAacSbrSlotOwner){
            env_src[t],env_idx[t],noise_idx[t]};
    }
    *out=result;
    return 1;
}


/* SBR-53a: materialize true values from the original SBR owner.
 * Pure side-data bridge; the existing PCM decoder is unchanged. */
static int temporal_freq_valid(const uint8_t *edges,unsigned count,
                               unsigned cap,unsigned kx,unsigned k2){
    if(!edges || count==0u || count>cap ||
       edges[0]!=kx || edges[count]!=k2)return 0;
    for(unsigned b=0u;b<count;b++)
        if(edges[b]>=edges[b+1u] || edges[b+1u]>k2)return 0;
    return 1;
}
static int temporal_hf_frame_valid(
    const SautiAacSbrPayloadFrame *frame){
    const SautiAacSbrTables *tab=&frame->tables;
    const unsigned kx=tab->kx,k2=kx+tab->m;
    if(tab->kx>63u || tab->m==0u || k2>64u ||
       frame->grid.env_count==0u ||
       frame->grid.env_count>SAUTI_AAC_SBR_MAX_ENV ||
       frame->grid.noise_count==0u ||
       frame->grid.noise_count>2u ||
       frame->energy.env_count!=frame->grid.env_count ||
       frame->energy.noise_count!=frame->grid.noise_count ||
       frame->limiter_mode>3u || frame->gain_mode>3u ||
       frame->bs_smoothing_mode>1u ||
       frame->transient_env>frame->grid.env_count ||
       !temporal_freq_valid(tab->high,tab->n_high,
                            SAUTI_AAC_SBR_MAX_MASTER_BANDS,kx,k2) ||
       !temporal_freq_valid(tab->low,tab->n_low,
                            SAUTI_AAC_SBR_MAX_MASTER_BANDS/2u,kx,k2) ||
       !temporal_freq_valid(tab->noise,tab->n_noise,
                            SAUTI_AAC_SBR_MAX_NOISE_BANDS,kx,k2))
        return 0;
    for(unsigned b=0u;b<tab->n_high;b++)
        if(frame->harmonic[b]>1u)return 0;
    for(unsigned e=0u;e<frame->energy.env_count;e++){
        if(frame->energy.freq_res[e]>1u || frame->attack[e]>1u)return 0;
        const unsigned n=frame->energy.freq_res[e]?
                         tab->n_high:tab->n_low;
        for(unsigned b=0u;b<n;b++){
            const double v=frame->energy.env_energy[e][b];
            if(!isfinite(v) || v<0.0)return 0;
        }
    }
    for(unsigned n=0u;n<frame->energy.noise_count;n++)
        for(unsigned b=0u;b<tab->n_noise;b++){
            const double v=frame->energy.noise_factor[n][b];
            if(!isfinite(v) || v<0.0)return 0;
        }
    return 1;
}

int sauti_aac_sbr_temporal_hf_targets(
    const SautiAacSbrTemporalHistory *history,
    SautiAacSbrTemporalHfTargets *out){
    if(!history || !out)return 0;
    SautiAacSbrResolvedFrame owners;
    if(!sauti_aac_sbr_temporal_resolve(history,&owners) ||
       !temporal_hf_frame_valid(&history->current) ||
       (owners.previous_slots &&
        (!history->has_previous ||
         !temporal_hf_frame_valid(&history->previous))))
        return 0;

    SautiAacSbrTemporalHfTargets next={0};
    next.previous_slots=owners.previous_slots;
    for(unsigned t=0u;t<32u;t++){
        const SautiAacSbrSlotOwner owner=owners.slot[t];
        const SautiAacSbrPayloadFrame *f=
            owner.source==0u?&history->previous:&history->current;
        if(owner.source>1u ||
           (owner.source==0u && !owners.previous_slots) ||
           owner.envelope>=f->energy.env_count ||
           owner.noise>=f->energy.noise_count)return 0;
        const SautiAacSbrTables *tab=&f->tables;
        SautiAacSbrTemporalHfSlot *slot=&next.slot[t];
        slot->source=owner.source;
        slot->envelope=owner.envelope;
        slot->noise=owner.noise;
        slot->smooth_bypass=(uint8_t)(f->bs_smoothing_mode || f->attack[owner.envelope]);
        slot->kx=(uint8_t)tab->kx;
        slot->k2=(uint8_t)(tab->kx+tab->m);
        const unsigned fine=f->energy.freq_res[owner.envelope];
        const uint8_t *edges=fine?tab->high:tab->low;
        const unsigned bands=fine?tab->n_high:tab->n_low;
        for(unsigned b=0u;b<bands;b++){
            const double v=f->energy.env_energy[owner.envelope][b];
            for(unsigned k=edges[b];k<edges[b+1u];k++)
                slot->energy[k]=v;
        }
        for(unsigned b=0u;b<tab->n_noise;b++){
            const double q=f->energy.noise_factor[owner.noise][b];
            for(unsigned k=tab->noise[b];k<tab->noise[b+1u];k++)
                slot->noise_factor[k]=q;
        }
        for(unsigned b=0u;b<tab->n_high;b++)
            if(f->harmonic[b]){
                const unsigned k=(tab->high[b]+tab->high[b+1u])/2u;
                slot->harmonic[k]=1u;
            }
    }
    *out=next;
    return 1;
}
