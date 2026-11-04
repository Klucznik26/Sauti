/* HE-AAC SBR-30: genuine ADTS + AAC-LC SCE + ID_FIL EXT_SBR_DATA.
 * The single-channel SBR grammar is decoded via SBR-29, not guessed
 * from a byte signature or the ADTS profile. Only the resulting AAC-LC
 * core PCM (1024 samples) is exposed; HE-AAC 2x QMF PCM is a next step.
 *
 * Strict atomicity across two persistent states, PCM and parsed SBR.
 * This deliberately does not modify the existing AAC-LC-only public
 * decoder, whose conservative rejection of implicit SBR is preserved.
 */
#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_syntax.h"
#include <math.h>
#include <string.h>

static int read(const uint8_t *data,size_t length,size_t *pos,
                unsigned width,uint32_t *value){
    return sauti_aac_bits_read_asm(data,length,pos,width,value);
}

/* EXT_FILL_DATA (type 1): four zero bits followed by 0xA5
 * bytes. Validate, never silently skip arbitrary opaque fill data.
 * FIL payload positions may be unaligned to bytes. */
static int valid_fill_data(const uint8_t *raw,size_t raw_length,
                           const SautiAacFillInfo *fill){
    if(!fill || fill->extension_type!=1u ||
       fill->extension_bits<8u)return 0;
    size_t pos=fill->extension_start_bit+4u;
    const size_t end=fill->extension_start_bit+fill->extension_bits;
    uint32_t value=0u;
    if(!read(raw,raw_length,&pos,4u,&value) || value!=0u)return 0;
    while(pos<end){
        if(end-pos<8u || !read(raw,raw_length,&pos,8u,&value) ||
           value!=0xa5u)return 0;
    }
    return pos==end;
}

int sauti_aac_he_adts_mono_init(SautiAacHeAdtsMonoState *state){
    if(!state)return 0;
    SautiAacHeAdtsMonoState next={0};
    if(!sauti_aac_lc_synthesis_init(&next.core,0u))return 0;
    *state=next;
    return 1;
}

int sauti_aac_he_adts_mono_decode_ex(
    const uint8_t *frame,size_t frame_size,
    SautiAacHeAdtsMonoState *state,
    float core_pcm[1024],
    SautiAacSbrPayloadFrame *params,
    SautiAacReason *reason){
    if(!reason)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_ADTS;
    if(!frame || !state || !core_pcm || !params ||
       frame_size<7u || frame_size>SIZE_MAX/8u)return 0;

    SautiAacAdts adts;
    if(!sauti_aac_adts_parse_asm(frame,frame_size,&adts) ||
       adts.header_bytes>adts.frame_bytes ||
       adts.frame_bytes>frame_size)return 0;
    if(adts.core_aot!=2u || adts.core_channels!=1u ||
       adts.raw_blocks!=1u || adts.crc_present ||
       state->core.initialized!=1u ||
       state->frames==UINT64_MAX){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_ADTS;
        return -1;
    }
    const unsigned rate=(frame[2]>>2u)&15u;
    if(rate>=13u || adts.core_sample_rate>UINT32_MAX/2u)return 0;
    const unsigned output_rate=adts.core_sample_rate*2u;
    const uint8_t *raw=frame+adts.header_bytes;
    const size_t raw_length=adts.frame_bytes-adts.header_bytes;

    SautiAacHeAdtsMonoState next=*state;
    SautiAacLcMonoData sce;
    SautiAacSbrPayloadFrame sbr={0};
    float pcm[1024];
    size_t pos=0u;
    /* Existing SCE parser already handles initial harmless EXT_FILL
     * elements and stops exactly at the end of spectral_data(). */
    const int status=sauti_aac_lc_mono_parse(
        raw,raw_length,&pos,rate,&sce);
    if(status!=1){
        *reason=status==-1?SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT:
                           SAUTI_AAC_REASON_INVALID_SCE;
        return status;
    }

    unsigned got_sbr=0u;
    for(;;){
        uint32_t id=0;
        if(!read(raw,raw_length,&pos,3u,&id)){
            *reason=SAUTI_AAC_REASON_INVALID_TRAILING;
            return 0;
        }
        if(id==7u)break; /* ID_END */
        if(id!=6u){
            *reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;
            return -1;
        }
        SautiAacFillInfo fill;
        if(!sauti_aac_fill_parse(raw,raw_length,&pos,&fill)){
            *reason=SAUTI_AAC_REASON_INVALID_TRAILING;
            return 0;
        }
        if(!fill.payload_bytes)continue;
        if(fill.extension_type==0u)continue; /* EXT_FILL */
        if(fill.extension_type==1u){ /* EXT_FILL_DATA */
            if(!valid_fill_data(raw,raw_length,&fill)){
                *reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;
                return -1;
            }
            continue;
        }
        if(fill.extension_type!=13u || got_sbr){
            *reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;
            return -1;
        }
        if(!sauti_aac_sbr_payload_sce_parse(
               raw,raw_length,
               fill.extension_start_bit,fill.extension_bits,
               output_rate,&next.sbr,&sbr)){
            *reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;
            return -1;
        }
        got_sbr=1u;
    }
    if(!got_sbr){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;
        return -1;
    }
    while(pos&7u){
        uint32_t padding=1u;
        if(!read(raw,raw_length,&pos,1u,&padding) || padding){
            *reason=SAUTI_AAC_REASON_INVALID_TRAILING;
            return 0;
        }
    }
    if(pos!=raw_length*8u){
        *reason=SAUTI_AAC_REASON_INVALID_TRAILING;
        return 0;
    }

    if(!sauti_aac_lc_synthesis_frame_tns(
           &next.core,rate,&sce.ics,&sce.sections,&sce.factors,
           sce.quant,sce.tns_present?&sce.tns:NULL,pcm)){
        *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
        return 0;
    }
    /* Same provisional AAC-22 public float scale as the standard
     * native AAC-LC ADTS decoder. */
    for(unsigned i=0;i<1024u;i++){
        const float value=ldexpf(pcm[i],-16);
        if(!isfinite(value)){
            *reason=SAUTI_AAC_REASON_INVALID_SYNTH;
            return 0;
        }
        pcm[i]=value;
    }
    next.frames++;
    memcpy(core_pcm,pcm,sizeof(pcm));
    *params=sbr;
    *state=next;
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
