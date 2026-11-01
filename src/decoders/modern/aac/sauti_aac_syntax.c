/* AAC-family structural bitstream grammars over the native NASM reader.
 * No FFmpeg, no codec tables, no allocations, no OS dependencies.
 * These parsers do NOT claim to decode entropy-coded audio/SBR/PS PCM.
 * Commit output state/bit position only after full successful parse. */
#include "sauti_aac_syntax.h"
#include "sauti_aac_headers.h"
#include <stdint.h>
#include <string.h>

static int read(const uint8_t *data,size_t size,size_t *pos,
                unsigned bits,uint32_t *out){
    return sauti_aac_bits_read_asm(data,size,pos,bits,out);
}
int sauti_aac_ics_info_parse(const uint8_t *data,size_t size,
                             size_t *bit_offset,SautiAacIcsInfo *info){
    if(!data||!bit_offset||!info)return 0;
    size_t pos=*bit_offset;uint32_t r=0,v=0;
    SautiAacIcsInfo next;memset(&next,0,sizeof(next));
    if(!read(data,size,&pos,1,&r) || r!=0 ||
       !read(data,size,&pos,2,&next.window_sequence) ||
       !read(data,size,&pos,1,&next.window_shape))return 0;
    if(next.window_sequence==2u){
        next.window_count=8;
        if(!read(data,size,&pos,4,&next.max_sfb))return 0;
        next.group_count=1;next.group_length[0]=1;
        for(unsigned i=1;i<8;i++){
            if(!read(data,size,&pos,1,&v))return 0;
            if(v)next.group_length[next.group_count-1u]++;
            else next.group_length[next.group_count++]=1;
        }
    }else{
        next.window_count=1;
        next.group_count=1;
        next.group_length[0]=1;
        if(!read(data,size,&pos,6,&next.max_sfb) ||
           !read(data,size,&pos,1,&r) || r)return 0;
        /* AAC-Main predictor data / other profiles are not decoded. */
    }
    *info=next;*bit_offset=pos;return 1;
}
int sauti_aac_fill_parse(const uint8_t *data,size_t size,
                         size_t *bit_offset,SautiAacFillInfo *info){
    if(!data||!bit_offset||!info)return 0;
    size_t pos=*bit_offset;
    uint32_t count=0,esc=0,ext=UINT32_MAX;
    if(!read(data,size,&pos,4,&count))return 0;
    if(count==15u){
        if(!read(data,size,&pos,8,&esc))return 0;
        count+=esc-1u;  /* ISO: count = 15 + esc_count - 1. */
    }
    if(size>SIZE_MAX/8u)return 0;
    size_t total=size*8u;
    if(pos>total || (size_t)count>(total-pos)/8u)return 0;
    size_t ext_start=pos;
    if(count && !read(data,size,&pos,4,&ext))return 0;
    SautiAacFillInfo next;
    memset(&next,0,sizeof(next));
    next.payload_bytes=count;
    next.extension_start_bit=ext_start;
    next.extension_bits=(size_t)count*8u;
    next.extension_type=ext;
    next.sbr_candidate=(ext==13u || ext==14u);
    next.sbr_crc_flag=(ext==14u);
    /* This is a header-only classification; we skip the opaque payload.
     * Never identify PS here: PS is signaled inside the SBR extension. */
    *info=next;*bit_offset=ext_start+next.extension_bits;return 1;
}
int sauti_aac_raw_prefix_parse(const uint8_t *data,size_t size,
                               size_t *bit_offset,SautiAacRawPrefix *info){
    if(!data||!bit_offset||!info)return 0;
    size_t pos=*bit_offset;
    SautiAacRawPrefix next;memset(&next,0,sizeof(next));
    next.first_audio_id=UINT32_MAX;
    for(;;){
        size_t id_pos=pos;
        uint32_t id=0;
        if(!read(data,size,&pos,3,&id))return 0;
        if(id==7u){ /* ID_END */
            next.stop_bit=pos;*info=next;*bit_offset=pos;return 1;
        }
        if(id==6u){ /* ID_FIL */
            SautiAacFillInfo fill;
            if(!sauti_aac_fill_parse(data,size,&pos,&fill))return 0;
            next.fill_count++;
            next.has_sbr_candidate|=fill.sbr_candidate;
            continue;
        }
        /* Channel elements have variable length: do not search forward
         * for fake FIL or END bit patterns inside Huffman payload. */
        next.first_audio_id=id;next.stop_bit=id_pos;
        *info=next;*bit_offset=id_pos;return 2;
    }
}
int sauti_aac_sbr_header_parse(const uint8_t *data,size_t size,
                               size_t *bit_offset,SautiAacSbrHeader *header){
    if(!data||!bit_offset||!header)return 0;
    size_t pos=*bit_offset;uint32_t reserved=0;
    SautiAacSbrHeader next;memset(&next,0,sizeof(next));
    if(!read(data,size,&pos,1,&next.amp_res) ||
       !read(data,size,&pos,4,&next.start_freq) ||
       !read(data,size,&pos,4,&next.stop_freq) ||
       !read(data,size,&pos,3,&next.xover_band) ||
       !read(data,size,&pos,2,&reserved) ||
       !read(data,size,&pos,1,&next.extra_1) ||
       !read(data,size,&pos,1,&next.extra_2) || reserved)return 0;
    next.freq_scale=2;next.alter_scale=1;next.noise_bands=2;
    next.limiter_bands=2;next.limiter_gains=2;
    next.interpol_freq=1;next.smoothing_mode=1;
    if(next.extra_1){
        if(!read(data,size,&pos,2,&next.freq_scale) ||
           !read(data,size,&pos,1,&next.alter_scale) ||
           !read(data,size,&pos,2,&next.noise_bands))return 0;
    }
    if(next.extra_2){
        if(!read(data,size,&pos,2,&next.limiter_bands) ||
           !read(data,size,&pos,2,&next.limiter_gains) ||
           !read(data,size,&pos,1,&next.interpol_freq) ||
           !read(data,size,&pos,1,&next.smoothing_mode))return 0;
    }
    *header=next;*bit_offset=pos;return 1;
}
int sauti_aac_ps_header_parse(const uint8_t *data,size_t size,
                              size_t *bit_offset,SautiAacPsHeader *header){
    if(!data||!bit_offset||!header)return 0;
    size_t pos=*bit_offset;
    SautiAacPsHeader next;memset(&next,0,sizeof(next));
    if(!read(data,size,&pos,1,&next.has_header))return 0;
    if(next.has_header){
        if(!read(data,size,&pos,1,&next.enable_iid))return 0;
        if(next.enable_iid &&
           (!read(data,size,&pos,3,&next.iid_mode) ||
            next.iid_mode>5u))return 0;
        if(!read(data,size,&pos,1,&next.enable_icc))return 0;
        if(next.enable_icc &&
           (!read(data,size,&pos,3,&next.icc_mode) ||
            next.icc_mode>5u))return 0;
        if(!read(data,size,&pos,1,&next.enable_ext))return 0;
    }
    *header=next;*bit_offset=pos;return 1;
}
