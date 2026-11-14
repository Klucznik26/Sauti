#include "sauti_aac_dequant.h"
#include "sauti_aac_bands.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

int sauti_aac_lc_dequantize(const int32_t quant[1024],
                            unsigned sample_rate_index,
                            const SautiAacIcsInfo *ics,
                            const SautiAacSections *sections,
                            const SautiAacScaleFactors *factors,
                            float output[1024]){
    if(!quant || !ics || !sections || !factors || !output ||
       ics->window_sequence>3 || ics->group_count<1 ||
       ics->group_count>8 || ics->max_sfb!=sections->max_sfb ||
       ics->group_count!=sections->group_count ||
       sections->section_count>SAUTI_AAC_MAX_SECTIONS)return 0;
    const int short_win=ics->window_sequence==2;
    if(ics->window_count!=(short_win?8u:1u) ||
       (!short_win && ics->group_count!=1u))return 0;
    const uint16_t *offsets=NULL;unsigned count=0;
    if(!sauti_aac_band_offsets(sample_rate_index,(unsigned)short_win,
                               &offsets,&count) || ics->max_sfb>count)return 0;
    unsigned group_start[8]={0},sum=0;
    for(unsigned g=0;g<ics->group_count;g++){
        group_start[g]=sum;
        if(ics->group_length[g]<1u ||
           sum+ics->group_length[g]>ics->window_count)return 0;
        sum+=ics->group_length[g];
    }
    if(sum!=ics->window_count)return 0;
    unsigned current_g=0,current_sfb=0;
    for(unsigned k=0;k<sections->section_count;k++){
        const SautiAacSection *s=&sections->sections[k];
        if(s->group!=current_g || s->first_sfb!=current_sfb ||
           s->end_sfb<=s->first_sfb || s->end_sfb>ics->max_sfb ||
           s->codebook==12u || s->codebook>15u)return 0;
        for(unsigned band=s->first_sfb;band<s->end_sfb;band++){
            if(factors->codebook[s->group][band]!=s->codebook)return 0;
            if(s->codebook>=1u && s->codebook<=11u &&
               (factors->value[s->group][band]<0 ||
                factors->value[s->group][band]>255))return 0;
        }
        current_sfb=s->end_sfb;
        if(current_sfb==ics->max_sfb){current_g++;current_sfb=0;}
    }
    if(ics->max_sfb && current_g!=ics->group_count)return 0;
    if(!ics->max_sfb && sections->section_count)return 0;

    float next[1024]={0};
    /* Pure math reference for magnitude; NASM applies sign and SFB gain.
     * A faster pow43 LUT/SIMD implementation may replace this later. */
    float mag[1024]={0};
    for(unsigned k=0;k<sections->section_count;k++){
        const SautiAacSection *s=&sections->sections[k];
        if(s->codebook==0u || s->codebook>=13u)continue;
        for(unsigned band=s->first_sfb;band<s->end_sfb;band++){
            unsigned start=offsets[band],end=offsets[band+1];
            if(end<=start || end>(short_win?128u:1024u))return 0;
            float gain=exp2f(((float)factors->value[s->group][band]-100.0f)*0.25f);
            for(unsigned win=group_start[s->group];
                win<group_start[s->group]+ics->group_length[s->group];win++){
                unsigned base=win*(short_win?128u:1024u)+start;
                for(unsigned line=base;line<base+end-start;line++){
                    int32_t q=quant[line];
                    if(q < -8191 || q > 8191)return 0;
                    /* |q|^(4/3) = |q| * cbrt(|q|). libm powf sits
                     * systematically high of this power. */
                    int32_t aq=q<0?-q:q;
                    mag[line]=(float)((double)aq*cbrt((double)aq));
                }
                if(!sauti_aac_dequant_scale_asm(quant+base,mag+base,&gain,
                                                next+base,end-start))return 0;
            }
        }
    }
    memcpy(output,next,sizeof(next));
    return 1;
}
