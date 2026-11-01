/* AAC-26: native AAC-LC CPE grammar and two-channel synthesis.
 * This stage supports non-MS common-window and independent-window CPE.
 * Never silently ignore mid/side, intensity, SBR, pulse or gain tools. */
#include "sauti_aac_cpe.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_spectral_data.h"
#include "sauti_aac_syntax.h"
#include <math.h>
#include <string.h>

static int get(const uint8_t *data,size_t length,size_t *pos,
               unsigned width,uint32_t *value){
    return sauti_aac_bits_read_asm(data,length,pos,width,value);
}
static int channel_parse(const uint8_t *data,size_t length,size_t *pos,
                         unsigned rate,const SautiAacIcsInfo *common,
                         SautiAacLcMonoData *ch,SautiAacReason *reason){
    uint32_t v=0;
    if(!get(data,length,pos,8,&ch->global_gain))return 0;
    if(common)ch->ics=*common;
    else if(!sauti_aac_ics_info_parse(data,length,pos,&ch->ics))return 0;
    const uint16_t *offsets=NULL;unsigned bands=0;
    if(!sauti_aac_band_offsets(rate,ch->ics.window_sequence==2u,
                               &offsets,&bands) || ch->ics.max_sfb>bands)
        return 0;
    (void)offsets;
    if(!sauti_aac_lc_sections_parse(data,length,pos,&ch->ics,
                                    &ch->sections))return 0;
    if(!sauti_aac_lc_scalefactors_parse(data,length,pos,ch->global_gain,
                                       &ch->sections,&ch->factors)){
        *reason=SAUTI_AAC_REASON_INVALID_SCALEFACTORS;
        return 0;
    }
    if(!get(data,length,pos,1,&v))return 0;
    if(v){*reason=SAUTI_AAC_REASON_PULSE;return -1;}
    if(!get(data,length,pos,1,&v))return 0;
    if(v){
        ch->tns_present=1u;
        if(!sauti_aac_tns_parse(data,length,pos,&ch->ics,&ch->tns))
            return 0;
    }
    if(!get(data,length,pos,1,&v))return 0;
    if(v){*reason=SAUTI_AAC_REASON_GAIN_CONTROL;return -1;}
    for(unsigned i=0;i<ch->sections.section_count;i++)
        if(ch->sections.sections[i].codebook>=14u){
            *reason=SAUTI_AAC_REASON_INTENSITY;return -1;
        }
    if(!sauti_aac_lc_spectral_data_parse(data,length,pos,rate,&ch->ics,
                                          &ch->sections,ch->quant)){
        *reason=SAUTI_AAC_REASON_INVALID_SPECTRAL;return 0;
    }
    return 1;
}
int sauti_aac_lc_cpe_parse(const uint8_t *raw,size_t length,
                           size_t *bit_offset,unsigned rate,
                           SautiAacCpeData *out,SautiAacReason *reason){
    if(!raw||!bit_offset||!out||!reason||rate>=13u||
       length>SIZE_MAX/8u||*bit_offset>length*8u)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_SCE;
    size_t pos=*bit_offset;
    uint32_t id=0,tag=0,common=0;
    if(!get(raw,length,&pos,3,&id)||id!=1u ||
       !get(raw,length,&pos,4,&tag) ||
       !get(raw,length,&pos,1,&common))return 0;
    (void)tag;
    SautiAacCpeData parsed={0};
    parsed.common_window=common;
    SautiAacIcsInfo shared={0};
    if(common){
        if(!sauti_aac_ics_info_parse(raw,length,&pos,&shared))return 0;
        uint32_t mask=0;
        if(!get(raw,length,&pos,2,&mask)||mask==3u)return 0;
        parsed.ms_mask_present=mask;
        if(mask==2u){
            /* MS applies to all bands; do not interpret without stereo DSP. */
            *reason=SAUTI_AAC_REASON_INTENSITY;return -1;
        }
        if(mask==1u){
            for(unsigned g=0;g<shared.group_count;g++)
                for(unsigned sfb=0;sfb<shared.max_sfb;sfb++){
                    uint32_t used=0;
                    if(!get(raw,length,&pos,1,&used))return 0;
                    if(used){
                        *reason=SAUTI_AAC_REASON_INTENSITY;return -1;
                    }
                }
        }
    }
    for(unsigned ch=0;ch<2;ch++){
        int status=channel_parse(raw,length,&pos,rate,
                                common?&shared:NULL,&parsed.channel[ch],
                                reason);
        if(status!=1)return status;
    }
    *out=parsed;
    *bit_offset=pos;
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
int sauti_aac_lc_adts_stereo_decode_ex(const uint8_t *frame,size_t length,
                                      SautiAacLcSynthesis state[2],
                                      float interleaved[2048],
                                      SautiAacReason *reason){
    if(!reason)return 0;
    *reason=SAUTI_AAC_REASON_INVALID_ADTS;
    if(!frame||!state||!interleaved)return 0;
    SautiAacAdts adts={0};
    if(!sauti_aac_adts_parse_asm(frame,length,&adts) ||
       adts.frame_bytes>length||adts.header_bytes>adts.frame_bytes)return 0;
    if(adts.core_channels!=2u||adts.core_aot!=2u||
       adts.raw_blocks!=1u||adts.crc_present){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_ADTS;return -1;
    }
    unsigned rate=(frame[2]>>2u)&15u;
    if(rate>=13u)return 0;
    const uint8_t *raw=frame+adts.header_bytes;
    size_t raw_length=adts.frame_bytes-adts.header_bytes,pos=0;
    for(;;){
        size_t probe=pos;
        uint32_t id=0;
        if(!get(raw,raw_length,&probe,3,&id))return 0;
        if(id!=6u)break;
        pos=probe;
        SautiAacFillInfo fill;
        if(!sauti_aac_fill_parse(raw,raw_length,&pos,&fill))return 0;
        if(fill.payload_bytes && fill.extension_type!=0u){
            *reason=SAUTI_AAC_REASON_UNSUPPORTED_FIL;return -1;
        }
    }
    SautiAacCpeData parsed;
    int status=sauti_aac_lc_cpe_parse(raw,raw_length,&pos,rate,
                                       &parsed,reason);
    if(status!=1)return status;
    for(;;){
        uint32_t id=0;
        if(!get(raw,raw_length,&pos,3,&id)){
            *reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;
        }
        if(id==7u)break;
        *reason=id==6u?SAUTI_AAC_REASON_UNSUPPORTED_FIL:
                        SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT;
        return -1;
    }
    while(pos&7u){
        uint32_t pad=1;
        if(!get(raw,raw_length,&pos,1,&pad)||pad){
            *reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;
        }
    }
    if(pos!=raw_length*8u){
        *reason=SAUTI_AAC_REASON_INVALID_TRAILING;return 0;
    }
    if(state[0].initialized!=1u||state[1].initialized!=1u){
        *reason=SAUTI_AAC_REASON_UNSUPPORTED_HISTORY;return -1;
    }
    SautiAacLcSynthesis next[2]={state[0],state[1]};
    float left[1024],right[1024],pcm[2048];
    for(unsigned ch=0;ch<2;ch++){
        SautiAacLcMonoData *data=&parsed.channel[ch];
        if(!sauti_aac_lc_synthesis_frame_tns(&next[ch],rate,
                   &data->ics,&data->sections,&data->factors,data->quant,
                   data->tns_present?&data->tns:NULL,
                   ch==0?left:right)){
            *reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;
        }
    }
    for(unsigned i=0;i<1024;i++){
        float l=ldexpf(left[i],-16),r=ldexpf(right[i],-16);
        if(!isfinite(l)||!isfinite(r)){
            *reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;
        }
        pcm[2*i]=l;pcm[2*i+1]=r;
    }
    state[0]=next[0];state[1]=next[1];
    memcpy(interleaved,pcm,sizeof(pcm));
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
