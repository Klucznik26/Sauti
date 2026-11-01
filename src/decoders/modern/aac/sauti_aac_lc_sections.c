/* Original AAC-LC section data parser (non-error-resilient AAC-LC).
 * ISO/IEC 13818-7 section_data uses a 4-bit codebook index and a sequence of
 * 3-bit (short) or 5-bit (long) increments, with repeated all-ones escapes.
 * Uses Sauti's dependency-free NASM bit reader, not FFmpeg.
 */
#include "sauti_aac_lc_sections.h"
#include "sauti_aac_headers.h"
#include <stdint.h>
#include <string.h>

static int bits(const uint8_t *p,size_t n,size_t *pos,unsigned width,
                uint32_t *v) {
    return sauti_aac_bits_read_asm(p,n,pos,width,v);
}
int sauti_aac_lc_sections_parse(const uint8_t *data,size_t length,
                                size_t *bit_offset,
                                const SautiAacIcsInfo *ics,
                                SautiAacSections *out){
    if(!data||!bit_offset||!ics||!out)return 0;
    if(ics->window_sequence>3u || ics->group_count<1u ||
       ics->group_count>8u || ics->max_sfb>63u)return 0;
    if(ics->window_sequence==2u) {
        if(ics->window_count!=8u || ics->max_sfb>15u)return 0;
    } else if(ics->window_count!=1u || ics->group_count!=1u)return 0;
    unsigned total_windows=0;
    for(uint32_t g=0;g<ics->group_count;g++){
        if(ics->group_length[g]==0)return 0;
        total_windows+=ics->group_length[g];
    }
    if(total_windows!=ics->window_count)return 0;

    SautiAacSections next;
    memset(&next,0,sizeof(next));
    next.group_count=ics->group_count;
    next.max_sfb=ics->max_sfb;
    size_t pos=*bit_offset;
    unsigned section_bits=ics->window_sequence==2u?3u:5u;
    uint32_t escape=(UINT32_C(1)<<section_bits)-1u;
    for(uint32_t g=0;g<ics->group_count;g++){
        uint32_t sfb=0;
        while(sfb<ics->max_sfb){
            uint32_t cb,step,run=0;
            if(!bits(data,length,&pos,4,&cb) || cb==12u)return 0;
            /* Each length increment is bounded by remaining band count.
             * A malicious stream cannot make this loop unbounded. */
            do{
                if(!bits(data,length,&pos,section_bits,&step))return 0;
                if(step>ics->max_sfb-sfb-run)return 0;
                run+=step;
            }while(step==escape);
            if(!run || next.section_count>=SAUTI_AAC_MAX_SECTIONS)
                return 0;
            SautiAacSection *s=&next.sections[next.section_count++];
            s->group=(uint8_t)g;
            s->codebook=(uint8_t)cb;
            s->first_sfb=(uint8_t)sfb;
            s->end_sfb=(uint8_t)(sfb+run);
            sfb+=run;
        }
    }
    *out=next;
    *bit_offset=pos;
    return 1;
}
int sauti_aac_lc_sce_prefix_parse(const uint8_t *data,size_t length,
                                  size_t *bit_offset,SautiAacScePrefix *out){
    if(!data||!bit_offset||!out)return 0;
    SautiAacScePrefix next;
    memset(&next,0,sizeof(next));
    size_t pos=*bit_offset;
    uint32_t id;
    if(!bits(data,length,&pos,3,&id) || id!=0u ||
       !bits(data,length,&pos,4,&next.instance_tag) ||
       !bits(data,length,&pos,8,&next.global_gain) ||
       !sauti_aac_ics_info_parse(data,length,&pos,&next.ics) ||
       !sauti_aac_lc_sections_parse(data,length,&pos,&next.ics,
                                   &next.sections))return 0;
    *out=next;
    *bit_offset=pos;
    return 1;
}
