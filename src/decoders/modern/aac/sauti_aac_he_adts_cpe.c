/* HE-AAC stereo CPE bridge: one ADTS CPE plus one SBR CPE FIL.
 * No external codec and no invented high-band PCM. */
#include "sauti_aac_he_adts_cpe.h"
#include "sauti_aac_adts_crc.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_syntax.h"
#include <string.h>

static int read_bits(const uint8_t *data,size_t size,size_t *pos,
                     unsigned width,uint32_t *value){
    return sauti_aac_bits_read_asm(data,size,pos,width,value);
}
int sauti_aac_he_adts_cpe_init(SautiAacHeAdtsCpeState *state){
    if(!state)return 0;
    SautiAacHeAdtsCpeState next={0};
    if(!sauti_aac_lc_synthesis_init(&next.core[0],0u) ||
       !sauti_aac_lc_synthesis_init(&next.core[1],0u))return 0;
    *state=next;
    return 1;
}
int sauti_aac_he_adts_cpe_decode_ex(
    const uint8_t *frame,size_t frame_bytes,
    SautiAacHeAdtsCpeState *state,float pcm[2048],
    SautiAacSbrPayloadCpeFrame *payload,SautiAacReason *reason){
    if(!reason)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_ADTS;
    if(!frame || !state || !pcm || !payload || frame_bytes<7u ||
       frame_bytes>SIZE_MAX/8u)return 0;
    SautiAacAdts adts={0};
    if(!sauti_aac_adts_parse_asm(frame,frame_bytes,&adts) ||
       adts.header_bytes>adts.frame_bytes || adts.frame_bytes>frame_bytes)
        return 0;
    if(adts.core_aot!=2u || adts.core_channels!=2u ||
       adts.raw_blocks!=1u || state->frames==UINT64_MAX ||
       state->frames!=state->sbr.frames ||
       state->core[0].initialized!=1u || state->core[1].initialized!=1u){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_ADTS;
        return -1;
    }
    const unsigned rate=(frame[2]>>2u)&15u;
    if(rate>=13u || adts.core_sample_rate>UINT32_MAX/2u)return 0;
    const uint8_t *raw=frame+adts.header_bytes;
    const size_t raw_length=adts.frame_bytes-adts.header_bytes;
    SautiAacHeAdtsCpeState next=*state;
    SautiAacCpeData cpe;
    size_t pos=0u;
    const size_t cpe_start=pos;
    const int parsed=sauti_aac_lc_cpe_parse(raw,raw_length,&pos,rate,
                                             &cpe,reason);
    if(parsed!=1)return parsed;
    if(adts.crc_present){
        if(pos<cpe_start+3u || cpe.crc_second_ics_bit>pos){
            *reason=SAUTI_AAC_REASON_INVALID_SCE;return 0;
        }
        uint16_t crc=0xffffu;
        size_t count=pos-(cpe_start+3u);
        if(count>192u)count=192u;
        size_t second=pos-cpe.crc_second_ics_bit;
        if(second>128u)second=128u;
        if(!sauti_aac_adts_crc16_update_bits(frame,7u,0u,56u,&crc) ||
           !sauti_aac_adts_crc16_update_bits(raw,raw_length,cpe_start+3u,
                                              count,&crc) ||
           !sauti_aac_adts_crc16_update_zero_bits(192u-count,&crc) ||
           !sauti_aac_adts_crc16_update_bits(raw,raw_length,
                                              cpe.crc_second_ics_bit,second,&crc) ||
           !sauti_aac_adts_crc16_update_zero_bits(128u-second,&crc) ||
           crc!=(uint16_t)((uint16_t)frame[7u]<<8u|frame[8u])){
            *reason=SAUTI_AAC_REASON_INVALID_ADTS;return 0;
        }
    }
    unsigned got_sbr=0u;
    SautiAacSbrPayloadCpeFrame side={0};
    for(;;){
        uint32_t id=0u;
        if(!read_bits(raw,raw_length,&pos,3u,&id)){
            *reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;
        }
        if(id==7u)break;
        if(id!=6u){*reason=SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;return -1;}
        SautiAacFillInfo fill;
        if(!sauti_aac_fill_parse(raw,raw_length,&pos,&fill)){
            *reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;
        }
        if(!fill.payload_bytes || fill.extension_type==0u)continue;
        if((fill.extension_type!=13u && fill.extension_type!=14u) || got_sbr){
            *reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;return -1;
        }
        if(!sauti_aac_sbr_payload_cpe_parse(
               raw,raw_length,fill.extension_start_bit,fill.extension_bits,
               adts.core_sample_rate*2u,&next.sbr,&side)){
            *reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;return -1;
        }
        got_sbr=1u;
    }
    if(!got_sbr){*reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;return -1;}
    while(pos&7u){
        uint32_t padding=1u;
        if(!read_bits(raw,raw_length,&pos,1u,&padding) || padding){
            *reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;
        }
    }
    if(pos!=raw_length*8u){*reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;}
    float rendered[2048];
    const int synthesized=sauti_aac_lc_cpe_synthesize(
        &cpe,rate,next.core,rendered,reason);
    if(synthesized!=1)return synthesized;
    next.frames++;
    *state=next;memcpy(pcm,rendered,sizeof(rendered));*payload=side;
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
