/* Original AAC-LC scale_factor_data parser using Sauti's NASM
 * Huffman bitstream primitive and independently stored standard tables. */
#include "sauti_aac_scalefactors.h"
#include "sauti_aac_tables.h"
#include "sauti_aac_headers.h"
#include <string.h>

int sauti_aac_lc_scalefactors_parse(const uint8_t *data,size_t length,
                                   size_t *bit_offset,uint32_t global_gain,
                                   const SautiAacSections *sections,
                                   SautiAacScaleFactors *out){
    if(!data || !bit_offset || !sections || !out || global_gain>255u ||
       length>SIZE_MAX/8u || *bit_offset>length*8u ||
       sections->group_count<1u || sections->group_count>8u ||
       sections->max_sfb>63u ||
       sections->section_count>SAUTI_AAC_MAX_SECTIONS)return 0;
    /* Enforce exact, ordered, gap-free coverage for each window group.
     * Do not trust a caller-supplied section map. */
    unsigned expected_group=0,expected_sfb=0;
    for(uint32_t k=0;k<sections->section_count;k++){
        const SautiAacSection *s=&sections->sections[k];
        if(s->group!=expected_group || s->first_sfb!=expected_sfb ||
           s->end_sfb<=s->first_sfb || s->end_sfb>sections->max_sfb ||
           s->codebook==12u || s->codebook>15u)return 0;
        expected_sfb=s->end_sfb;
        if(expected_sfb==sections->max_sfb){
            expected_group++;
            expected_sfb=0;
        }
    }
    if(sections->max_sfb!=0u && expected_group!=sections->group_count)return 0;
    if(sections->max_sfb==0u && sections->section_count!=0u)return 0;

    SautiAacHuffNode nodes[SAUTI_AAC_HUFF_MAX_NODES];
    const size_t node_count=sauti_aac_standard_tree_build(0,nodes,
                                           SAUTI_AAC_HUFF_MAX_NODES);
    if(!node_count)return 0;
    SautiAacScaleFactors next;
    memset(&next,0,sizeof(next));
    size_t pos=*bit_offset;
    int32_t spectral=(int32_t)global_gain;
    int32_t intensity=0;
    int32_t noise=(int32_t)global_gain-90;
    int first_noise=1;
    for(uint32_t k=0;k<sections->section_count;k++){
        const SautiAacSection *s=&sections->sections[k];
        for(unsigned band=s->first_sfb;band<s->end_sfb;band++){
            const unsigned cb=s->codebook;
            next.codebook[s->group][band]=(uint8_t)cb;
            if(cb==0)continue;
            int32_t delta=0;
            if(cb==13u && first_noise){
                uint32_t raw=0;
                if(!sauti_aac_bits_read_asm(data,length,&pos,9,&raw))
                    return 0;
                delta=(int32_t)raw-256;
                first_noise=0;
            }else{
                if(!sauti_aac_scalefactor_symbol_decode(data,length,&pos,
                                      nodes,node_count,&delta))return 0;
            }
            if(cb==13u){
                noise+=delta;
                next.value[s->group][band]=noise;
            }else if(cb==14u || cb==15u){
                intensity+=delta;
                next.value[s->group][band]=intensity;
            }else{
                spectral+=delta;
                if(spectral<0 || spectral>255)return 0;
                next.value[s->group][band]=spectral;
            }
        }
    }
    *out=next;
    *bit_offset=pos;
    return 1;
}
