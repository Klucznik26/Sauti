/* AAC-LC mono bitstream integration, fail-closed on unimplemented tools.
 * No speculative TNS/pulse parsing and no FFmpeg runtime linkage. */
#include "sauti_aac_lc_frame.h"
#include "sauti_aac_lc_sections.h"
#include "sauti_aac_spectral_data.h"
#include "sauti_aac_syntax.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_bands.h"
#include <string.h>
#include <math.h>

static int read(const uint8_t *p,size_t n,size_t *pos,
                unsigned bits,uint32_t *v){
    return sauti_aac_bits_read_asm(p,n,pos,bits,v);
}
static int mono_parse_ex(const uint8_t *raw,size_t length,
                           size_t *bit_offset,unsigned rate_index,
                           SautiAacLcMonoData *out,SautiAacReason *reason){
    if(!raw||!bit_offset||!out||length>SIZE_MAX/8u||
       *bit_offset>length*8u||rate_index>=13u)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_SCE;
    size_t pos=*bit_offset;
    /* The encoder can put EXT_FILL (type 0) before the SCE, often
     * carrying an encoder identification string. Inspect each FIL
     * extension before skipping it; never discard SBR/PS or arbitrary
     * unknown extensions, which may alter output sample rate/content. */
    for(;;){
        size_t probe=pos;
        uint32_t id=0;
        if(!read(raw,length,&probe,3,&id))return 0;
        if(id!=6u)break;
        pos=probe;
        SautiAacFillInfo fill;
        if(!sauti_aac_fill_parse(raw,length,&pos,&fill))return 0;
        if(fill.payload_bytes!=0u && fill.extension_type!=0u){
            *reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;
            return -1;
        }
    }
    size_t probe=pos;
    uint32_t first_id=0;
    if(!read(raw,length,&probe,3,&first_id))return 0;
    if(first_id!=0u){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_PREFIX;
        return -1;
    }
    SautiAacScePrefix sce;
    if(!sauti_aac_lc_sce_prefix_parse(raw,length,&pos,&sce)){*reason=SAUTI_AAC_REASON_INVALID_SCE;return 0;}
    const uint16_t *offsets=NULL;unsigned count=0;
    if(!sauti_aac_band_offsets(rate_index,
                               sce.ics.window_sequence==2u,
                               &offsets,&count))return 0;
    (void)offsets;
    if(sce.ics.max_sfb>count){*reason=SAUTI_AAC_REASON_INVALID_SCE;return 0;}
    SautiAacLcMonoData next;
    memset(&next,0,sizeof(next));
    next.ics=sce.ics;
    next.sections=sce.sections;
    next.global_gain=sce.global_gain;
    if(!sauti_aac_lc_scalefactors_parse(raw,length,&pos,sce.global_gain,
                                      &next.sections,&next.factors)){*reason=SAUTI_AAC_REASON_INVALID_SCALEFACTORS;return 0;}
    /* The three presence flags immediately follow scale_factor_data.
     * Pulse, TNS and gain control payloads have variable lengths.
     * Never interpret their bits as spectral_data. */
    uint32_t flag=0;
    if(!read(raw,length,&pos,1,&flag))return 0;
    if(flag){*reason=SAUTI_AAC_REASON_PULSE;return -1;} /* pulse_data_present */
    if(!read(raw,length,&pos,1,&flag))return 0;
    if(flag){*reason=SAUTI_AAC_REASON_TNS;return -1;} /* tns_data_present */
    if(!read(raw,length,&pos,1,&flag))return 0;
    if(flag){*reason=SAUTI_AAC_REASON_GAIN_CONTROL;return -1;} /* gain_control_data_present */
    for(unsigned i=0;i<next.sections.section_count;i++)
        if(next.sections.sections[i].codebook>=14u){*reason=SAUTI_AAC_REASON_INTENSITY;return -1;}
    if(!sauti_aac_lc_spectral_data_parse(raw,length,&pos,rate_index,
                        &next.ics,&next.sections,next.quant)){*reason=SAUTI_AAC_REASON_INVALID_SPECTRAL;return 0;}
    *out=next;
    *bit_offset=pos;
    return 1;
}
int sauti_aac_lc_mono_parse(const uint8_t *raw,size_t length,
                           size_t *bit_offset,unsigned rate_index,
                           SautiAacLcMonoData *out){
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    return mono_parse_ex(raw,length,bit_offset,rate_index,out,&reason);
}
int sauti_aac_lc_adts_mono_decode_ex(const uint8_t *frame,size_t length,
                                 SautiAacLcSynthesis *state,
                                 float pcm[1024],SautiAacReason *reason){
    if(!reason)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_ADTS;
    if(!frame || !state || !pcm)return 0;
    SautiAacAdts adts={0};
    if(!sauti_aac_adts_parse_asm(frame,length,&adts))return 0;
    if(adts.frame_bytes>length || adts.header_bytes>adts.frame_bytes)
        return 0;
    if(adts.core_channels!=1u || adts.core_aot!=2u ||
       adts.raw_blocks!=1u || adts.crc_present){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ADTS;return -1;}
    /* ADTS sample-rate index occupies bits 2..5 in header byte 2.
     * 0..12 only; explicit 15 is not valid ADTS sample frequency. */
    unsigned rate=(frame[2]>>2u)&15u;
    if(rate>=13u)return 0;
    const uint8_t *raw=frame+adts.header_bytes;
    size_t raw_length=adts.frame_bytes-adts.header_bytes;
    size_t pos=0;
    SautiAacLcMonoData parsed;
    int status=mono_parse_ex(raw,raw_length,&pos,rate,&parsed,reason);
    if(status!=1)return status;
    /* The one SCE should be followed by zero or more filler elements,
     * then ID_END and zero byte alignment. Refuse all FIL conservatively
     * until extension contents can be inspected for implicit SBR. */
    for(;;){
        uint32_t id=0;
        if(!read(raw,raw_length,&pos,3,&id)){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}
        if(id==7u)break;
        if(id==6u){*reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;return -1;}
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;return -1; /* extra element */
    }
    /* ADTS bitstream is padded to a whole byte after ID_END. */
    while((pos&7u)!=0u){
        uint32_t pad=1;
        if(!read(raw,raw_length,&pos,1,&pad) || pad){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}
    }
    if(pos!=raw_length*8u){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}
    /* Once earlier frames were skipped, the overlap history cannot be
     * reconstructed. This is a capability gap, not malformed AAC. */
    if(state->initialized!=1u){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_HISTORY;
        return -1;
    }
    SautiAacLcSynthesis next=*state;
    float result[1024];
    if(!sauti_aac_lc_synthesis_frame_pns(&next,rate,&parsed.ics,
          &parsed.sections,&parsed.factors,parsed.quant,result)){*reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;}
    /* AAC-22: the internal synthesis uses an unscaled spectral
     * amplitude convention. Real 440 Hz ADTS comparison established
     * native/reference RMS = 65536.4 (2^16), with correlation 1.0.
     * Apply the provisional 2^-16 conversion ONLY at the public ADTS
     * PCM boundary, leaving IMDCT and overlap in internal units.
     * Reference validation on other signals is still mandatory. */
    for(unsigned i=0;i<1024;i++){
        const float value=ldexpf(result[i],-16);
        if(!isfinite(value)){
            *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
            return 0;
        }
        result[i]=value;
    }
    *state=next;
    memcpy(pcm,result,sizeof(result));
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}

int sauti_aac_lc_adts_mono_decode(const uint8_t *frame,size_t length,
                                 SautiAacLcSynthesis *state,
                                 float pcm[1024]){
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    return sauti_aac_lc_adts_mono_decode_ex(frame,length,state,pcm,&reason);
}
const char *sauti_aac_reason_name(SautiAacReason reason){
    static const char *const names[]={
        "ok","invalid_adts","unsupported_adts","unsupported_prefix",
        "unsupported_fil","invalid_sce","invalid_scalefactors",
        "pulse","tns","gain_control","pns","intensity",
        "invalid_spectral","unsupported_element","invalid_trailing",
        "invalid_synthesis","unsupported_history"
    };
    return (unsigned)reason<sizeof(names)/sizeof(names[0])?
        names[reason]:"unknown";
}
