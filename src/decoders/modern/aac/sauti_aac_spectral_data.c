#include "sauti_aac_spectral_data.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_tables.h"
#include "sauti_aac_spectral.h"
#include <string.h>
#include <stdint.h>

int sauti_aac_lc_spectral_data_parse(const uint8_t *data,size_t length,
                                     size_t *bit_offset,
                                     unsigned sample_rate_index,
                                     const SautiAacIcsInfo *ics,
                                     const SautiAacSections *sections,
                                     int32_t output[1024]){
    if(!data || !bit_offset || !ics || !sections || !output ||
       length>SIZE_MAX/8u || *bit_offset>length*8u ||
       ics->window_sequence>3u ||
       sections->group_count!=ics->group_count ||
       sections->max_sfb!=ics->max_sfb ||
       sections->section_count>SAUTI_AAC_MAX_SECTIONS)return 0;
    const int short_window=ics->window_sequence==2u;
    if(ics->group_count<1u || ics->group_count>8u ||
       ics->window_count!=(short_window?8u:1u) ||
       (!short_window && ics->group_count!=1u))return 0;
    const uint16_t *offsets=NULL;unsigned count=0;
    if(!sauti_aac_band_offsets(sample_rate_index,(unsigned)short_window,
                               &offsets,&count) || ics->max_sfb>count)return 0;
    unsigned total=0;
    for(unsigned g=0;g<ics->group_count;g++){
        if(!ics->group_length[g] || total+ics->group_length[g]>ics->window_count)
            return 0;
        total+=ics->group_length[g];
    }
    if(total!=ics->window_count)return 0;
    /* Validate every group map before consuming any bits. */
    unsigned expected_group=0,expected_sfb=0;
    for(unsigned k=0;k<sections->section_count;k++){
        const SautiAacSection *sec=&sections->sections[k];
        if(sec->group!=expected_group || sec->first_sfb!=expected_sfb ||
           sec->end_sfb<=sec->first_sfb || sec->end_sfb>ics->max_sfb ||
           sec->codebook==12u || sec->codebook>15u)return 0;
        expected_sfb=sec->end_sfb;
        if(expected_sfb==ics->max_sfb){expected_sfb=0;expected_group++;}
    }
    if(ics->max_sfb && expected_group!=ics->group_count)return 0;
    if(!ics->max_sfb && sections->section_count)return 0;
    int32_t next[1024]={0};
    size_t pos=*bit_offset;
    SautiAacHuffNode tree[SAUTI_AAC_HUFF_MAX_NODES];
    unsigned group_start[8]={0},sum=0;
    for(unsigned g=0;g<ics->group_count;g++){
        group_start[g]=sum;
        sum+=ics->group_length[g];
    }
    for(unsigned k=0;k<sections->section_count;k++){
        const SautiAacSection *sec=&sections->sections[k];
        unsigned cb=sec->codebook;
        if(cb==0u || cb>=13u)continue; /* zero/PNS/intensity: no spectral bits */
        size_t tree_count=sauti_aac_standard_tree_build(cb,tree,
                                                 SAUTI_AAC_HUFF_MAX_NODES);
        if(!tree_count)return 0;
        const unsigned tuple_width=cb<=4u?4u:2u;
        for(unsigned band=sec->first_sfb;band<sec->end_sfb;band++){
            unsigned from=offsets[band],to=offsets[band+1];
            if(to<=from || to>(short_window?128u:1024u) ||
               (to-from)%tuple_width)return 0;
            /* AAC groups are coded by band, then window, then coefficient.
             * Reassemble into natural per-window ordering in the output. */
            for(unsigned w=group_start[sec->group];
                w<group_start[sec->group]+ics->group_length[sec->group];w++){
                for(unsigned line=from;line<to;line+=tuple_width){
                    int32_t values[4];
                    if(!sauti_aac_spectral_decode(cb,data,length,&pos,
                                                  tree,tree_count,values))return 0;
                    for(unsigned j=0;j<tuple_width;j++)
                        next[w*(short_window?128u:1024u)+line+j]=values[j];
                }
            }
        }
    }
    memcpy(output,next,sizeof(next));
    *bit_offset=pos;
    return 1;
}
