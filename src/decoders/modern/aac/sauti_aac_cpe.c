/* AAC-26: native AAC-LC CPE grammar and two-channel synthesis.
 * This stage supports non-MS common-window and independent-window CPE.
 * Never silently ignore mid/side, intensity, SBR, pulse or gain tools. */
#include "sauti_aac_cpe.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_spectral_data.h"
#include "sauti_aac_syntax.h"
#include "sauti_aac_pns.h"
#include <math.h>
#include <string.h>

static int get(const uint8_t *data,size_t length,size_t *pos,
               unsigned width,uint32_t *value){
    return sauti_aac_bits_read_asm(data,length,pos,width,value);
}
static int channel_parse(const uint8_t *data,size_t length,size_t *pos,
                         unsigned rate,const SautiAacIcsInfo *common,
                         SautiAacLcMonoData *ch,SautiAacReason *reason,
                         unsigned allow_intensity){
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
        if(ch->sections.sections[i].codebook>=14u && !allow_intensity){
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
        if(mask==1u || mask==2u){
            for(unsigned g=0;g<shared.group_count;g++)
                for(unsigned sfb=0;sfb<shared.max_sfb;sfb++){
                    uint32_t used=mask==2u?1u:0u;
                    if(mask==1u && !get(raw,length,&pos,1,&used))
                        return 0;
                    parsed.ms_used[g][sfb]=(uint8_t)used;
                }
        }
    }
    for(unsigned ch=0;ch<2;ch++){
        int status=channel_parse(raw,length,&pos,rate,
                                common?&shared:NULL,&parsed.channel[ch],
                                reason,common && ch==1u);
        if(status!=1)return status;
    }
    /* Intensity is permitted only in the right channel of a CPE
     * with common_window. For independent ICS, channel_parse fails. */
    *out=parsed;
    *bit_offset=pos;
    *reason=SAUTI_AAC_REASON_OK;
    return 1;
}
/* AAC Mid/Side: dequantized M,S -> L=M+S, R=M-S.
 * Reconstruct per SFB/window before applying TNS or inverse MDCT.
 * Reject MS over PNS until correlated stereo noise is implemented. */
static int apply_ms(const SautiAacCpeData *cpe,unsigned rate,
                    float left[1024],float right[1024],
                    SautiAacReason *reason){
    if(!cpe->common_window || !cpe->ms_mask_present)return 1;
    const SautiAacIcsInfo *ics=&cpe->channel[0].ics;
    const unsigned shortwin=ics->window_sequence==2u;
    const uint16_t *offsets=NULL;unsigned bands=0;
    if(!sauti_aac_band_offsets(rate,shortwin,&offsets,&bands) ||
       ics->max_sfb>bands)return 0;
    unsigned first_window=0;
    for(unsigned g=0;g<ics->group_count;g++){
        for(unsigned band=0;band<ics->max_sfb;band++){
            if(!cpe->ms_used[g][band])continue;
            const unsigned a=cpe->channel[0].factors.codebook[g][band];
            const unsigned b=cpe->channel[1].factors.codebook[g][band];
            if(b==14u || b==15u)continue; /* intensity handles MS sign */
            if(a==13u && b==13u)continue; /* correlated PNS follows */
            if(a>11u || b>11u){
                *reason=SAUTI_AAC_REASON_MID_SIDE;return -1;
            }
            const unsigned start=offsets[band],end=offsets[band+1u];
            if(end<=start || end>(shortwin?128u:1024u))return 0;
            for(unsigned w=first_window;
                 w<first_window+ics->group_length[g];w++){
                const unsigned base=w*(shortwin?128u:1024u);
                for(unsigned i=base+start;i<base+end;i++){
                    const float mid=left[i],side=right[i];
                    const float l=mid+side,r=mid-side;
                    if(!isfinite(l)||!isfinite(r))return 0;
                    left[i]=l;right[i]=r;
                }
            }
        }
        first_window+=ics->group_length[g];
    }
    return first_window==ics->window_count;
}
/* AAC-29C: when both channels encode PNS in a common-window SFB,
 * the MS mask signals correlated noise (not a spectral M/S butterfly).
 * Preserve the left normalized noise shape and adapt its amplitude to
 * the right transmitted noise gain. Both channels' RNG streams have
 * already been consumed; modification remains within the atomic CPE
 * transaction and never changes the stored state on failure. */
#ifndef SAUTI_AAC_DIAG_INDEPENDENT_PNS
static int apply_correlated_pns(const SautiAacCpeData *cpe,unsigned rate,
                                const float left[1024],float right[1024],
                                SautiAacReason *reason){
    if(!cpe->common_window || !cpe->ms_mask_present)return 1;
    const SautiAacIcsInfo *ics=&cpe->channel[0].ics;
    const unsigned shortwin=ics->window_sequence==2u;
    const uint16_t *offsets=NULL;unsigned bands=0;
    if(!sauti_aac_band_offsets(rate,shortwin,&offsets,&bands) ||
       ics->max_sfb>bands)return 0;
    unsigned start_window=0;
    for(unsigned g=0;g<ics->group_count;g++){
        if(!ics->group_length[g] ||
           start_window+ics->group_length[g]>ics->window_count)return 0;
        for(unsigned band=0;band<ics->max_sfb;band++){
            if(!cpe->ms_used[g][band])continue;
            const unsigned a=cpe->channel[0].factors.codebook[g][band];
            const unsigned b=cpe->channel[1].factors.codebook[g][band];
            if(a!=13u && b!=13u)continue;
            if(a!=13u || b!=13u){
                *reason=SAUTI_AAC_REASON_MID_SIDE;
                return -1;
            }
            const int32_t ls=cpe->channel[0].factors.value[g][band];
            const int32_t rs=cpe->channel[1].factors.value[g][band];
            if(ls< -512 || ls>512 || rs< -512 || rs>512)return 0;
            const double scale=exp2(((double)rs-(double)ls)/4.0);
            if(!isfinite(scale))return 0;
            const unsigned first=offsets[band],last=offsets[band+1u];
            if(first>=last || last>(shortwin?128u:1024u))return 0;
            for(unsigned w=start_window;
                 w<start_window+ics->group_length[g];w++){
                const unsigned base=w*(shortwin?128u:1024u);
                for(unsigned k=first;k<last;k++){
                    const double v=(double)left[base+k]*scale;
                    if(!isfinite(v) || fabs(v)>3.402823466e38)return 0;
                    right[base+k]=(float)v;
                }
            }
        }
        start_window+=ics->group_length[g];
    }
    return start_window==ics->window_count?1:0;
}
#endif /* SAUTI_AAC_DIAG_INDEPENDENT_PNS */
/* Intensity Stereo (AAC-LC): only right-channel pseudo-codebooks
 * 14 (out-of-phase) and 15 (in-phase), with common_window.
 * Right spectrum = left spectrum * sign * 2^(-is_pos/4).
 * For MS-marked intensity bands the sign is inverted, but MS itself
 * must not be applied to these bands. Apply after MS, before TNS.
 */
static int apply_intensity(const SautiAacCpeData *cpe,unsigned rate,
                           const float left[1024],float right[1024]){
    const SautiAacIcsInfo *ics=&cpe->channel[1].ics;
    const unsigned shortwin=ics->window_sequence==2u;
    const uint16_t *offsets=NULL;unsigned count=0;
    if(!sauti_aac_band_offsets(rate,shortwin,&offsets,&count) ||
       ics->max_sfb>count)return 0;
    unsigned group_start=0;
    for(unsigned g=0;g<ics->group_count;g++){
        if(ics->group_length[g]==0 ||
           group_start+ics->group_length[g]>ics->window_count)return 0;
        for(unsigned band=0;band<ics->max_sfb;band++){
            unsigned cb=cpe->channel[1].factors.codebook[g][band];
            if(cb!=14u && cb!=15u)continue;
            if(!cpe->common_window)return 0;
            unsigned left_cb=cpe->channel[0].factors.codebook[g][band];
            if(left_cb>=13u)return 0; /* PNS stereo correlation deferred */
            const int32_t position=cpe->channel[1].factors.value[g][band];
            if(position < -512 || position > 512)return 0;
            double sign=cb==15u?1.0:-1.0;
            if(cpe->ms_used[g][band])sign=-sign;
            double scale=sign*exp2(-(double)position/4.0);
            if(!isfinite(scale))return 0;
            const unsigned first=offsets[band],last=offsets[band+1u];
            if(first>=last || last>(shortwin?128u:1024u))return 0;
            for(unsigned w=group_start;
                 w<group_start+ics->group_length[g];w++){
                unsigned base=w*(shortwin?128u:1024u);
                for(unsigned k=first;k<last;k++){
                    double value=(double)left[base+k]*scale;
                    if(!isfinite(value) || fabs(value)>3.402823466e38)
                        return 0;
                    right[base+k]=(float)value;
                }
            }
        }
        group_start+=ics->group_length[g];
    }
    return group_start==ics->window_count;
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
    float spectral[2][1024],left[1024],right[1024],pcm[2048];
    /* AAC PNS uses one decoder-wide RNG stream. Consume left bands
     * before right bands; never independently reseed the channels.
     * Keep the public two-state API synchronized after success. */
    uint32_t pns_stream=next[0].pns_seed;
    for(unsigned ch=0;ch<2;ch++){
        SautiAacLcMonoData *data=&parsed.channel[ch];
        if(!sauti_aac_lc_dequantize(data->quant,rate,
                    &data->ics,&data->sections,&data->factors,
                    spectral[ch]) ||
           !sauti_aac_pns_apply(&data->ics,rate,&data->sections,
                               &data->factors,&pns_stream,
                               spectral[ch])){
            *reason=SAUTI_AAC_REASON_INVALID_SYNTH;return 0;
        }
    }
    next[0].pns_seed=pns_stream;
    next[1].pns_seed=pns_stream;
    int ms_status=apply_ms(&parsed,rate,spectral[0],spectral[1],reason);
    if(ms_status!=1){
        if(ms_status==0)*reason=SAUTI_AAC_REASON_INVALID_SYNTH;
        return ms_status;
    }
    /* Diagnostic build only: FFmpeg's float AAC decoder does not
     * correlate NOISE_BT bands in its MS butterfly. Keep the normal
     * Sauti behavior untouched until the PCM matrix settles this. */
#ifndef SAUTI_AAC_DIAG_INDEPENDENT_PNS
    int pns_status=apply_correlated_pns(&parsed,rate,
                                        spectral[0],spectral[1],reason);
    if(pns_status!=1){
        if(pns_status==0)*reason=SAUTI_AAC_REASON_INVALID_SYNTH;
        return pns_status;
    }
#endif
    if(!apply_intensity(&parsed,rate,spectral[0],spectral[1])){
        *reason=SAUTI_AAC_REASON_INTENSITY;return 0;
    }
    for(unsigned ch=0;ch<2;ch++){
        const SautiAacLcMonoData *data=&parsed.channel[ch];
        if(!sauti_aac_lc_synthesis_spectral(&next[ch],&data->ics,
                   data->tns_present?&data->tns:NULL,rate,
                   spectral[ch],ch==0?left:right)){
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
