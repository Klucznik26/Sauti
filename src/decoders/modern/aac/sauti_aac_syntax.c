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
int sauti_aac_ps_header_state_init(SautiAacPsHeaderState *state){
    if(!state)return 0;
    *state=(SautiAacPsHeaderState){0};return 1;
}
int sauti_aac_ps_header_state_parse(const uint8_t *data,size_t size,
                                    size_t *bit_offset,
                                    SautiAacPsHeaderState *state,
                                    SautiAacPsHeader *out){
    if(!data || !bit_offset || !state || !out || state->initialized>1u ||
       (!state->initialized && state->frames!=0u) ||
       (state->initialized && !state->frames) || state->frames==UINT64_MAX)
        return 0;
    size_t pos=*bit_offset;SautiAacPsHeader parsed;
    if(!sauti_aac_ps_header_parse(data,size,&pos,&parsed))return 0;
    if(!parsed.has_header && !state->initialized)return 0;
    SautiAacPsHeaderState next=*state;
    if(parsed.has_header)next.header=parsed;
    next.initialized=1u;next.frames++;
    *state=next;*out=next.header;*bit_offset=pos;return 1;
}

/* ISO/IEC 14496-3 PS IID/ICC VLCs. Numeric codewords were cross-checked
 * against FFmpeg n6.1 libavcodec/aacpsdata.c (Alex Converse, LGPL-2.1+).
 * They are decoded by Sauti's bounded MSB-first bit reader; no FFmpeg code
 * or runtime dependency is used. Distribution of the numeric dataset needs
 * the same license review as the existing AAC/SBR Huffman tables. */
typedef struct {const uint32_t *code;const uint8_t *bits;uint8_t count,offset;} PsBook;
static const uint8_t ps_iid_df1_bits[61]={
18,18,18,18,18,18,18,18,18,17,18,17,17,16,16,15,14,14,13,12,12,11,10,10,8,7,6,5,4,3,1,3,4,5,6,7,8,9,10,11,11,12,13,14,14,15,16,16,17,17,18,17,18,18,18,18,18,18,18,18,18};
static const uint32_t ps_iid_df1_code[61]={
0x1FEB4,0x1FEB5,0x1FD76,0x1FD77,0x1FD74,0x1FD75,0x1FE8A,0x1FE8B,0x1FE88,0xFE80,0x1FEB6,0xFE82,0xFEB8,0x7F42,0x7FAE,0x3FAF,0x1FD1,0x1FE9,0xFE9,0x7EA,0x7FB,0x3FB,0x1FB,0x1FF,0x7C,0x3C,0x1C,0xC,0,1,1,2,1,0xD,0x1D,0x3D,0x7D,0xFC,0x1FC,0x3FC,0x3F4,0x7EB,0xFEA,0x1FEA,0x1FD6,0x3FD0,0x7FAF,0x7F43,0xFEB9,0xFE83,0x1FEB7,0xFE81,0x1FE89,0x1FE8E,0x1FE8F,0x1FE8C,0x1FE8D,0x1FEB2,0x1FEB3,0x1FEB0,0x1FEB1};
static const uint8_t ps_iid_dt1_bits[61]={
16,16,16,16,16,16,16,16,16,15,15,15,15,15,15,14,14,13,13,13,12,12,11,10,9,9,7,6,5,3,1,2,5,6,7,8,9,10,11,11,12,12,13,13,14,14,15,15,15,15,16,16,16,16,16,16,16,16,16,16,16};
static const uint32_t ps_iid_dt1_code[61]={
0x4ED4,0x4ED5,0x4ECE,0x4ECF,0x4ECC,0x4ED6,0x4ED8,0x4F46,0x4F60,0x2718,0x2719,0x2764,0x2765,0x276D,0x27B1,0x13B7,0x13D6,0x9C7,0x9E9,0x9ED,0x4EE,0x4F7,0x278,0x139,0x9A,0x9F,0x20,0x11,0xA,3,1,0,0xB,0x12,0x21,0x4C,0x9B,0x13A,0x279,0x270,0x4EF,0x4E2,0x9EA,0x9D8,0x13D7,0x13D0,0x27B2,0x27A2,0x271A,0x271B,0x4F66,0x4F67,0x4F61,0x4F47,0x4ED9,0x4ED7,0x4ECD,0x4ED2,0x4ED3,0x4ED0,0x4ED1};
static const uint8_t ps_iid_df0_bits[29]={17,17,17,17,16,15,13,10,9,7,6,5,4,3,1,3,4,5,6,6,8,11,13,14,14,15,17,18,18};
static const uint32_t ps_iid_df0_code[29]={0x1FFFB,0x1FFFC,0x1FFFD,0x1FFFA,0xFFFC,0x7FFC,0x1FFD,0x3FE,0x1FE,0x7E,0x3C,0x1D,0xD,5,0,4,0xC,0x1C,0x3D,0x3E,0xFE,0x7FE,0x1FFC,0x3FFC,0x3FFD,0x7FFD,0x1FFFE,0x3FFFE,0x3FFFF};
static const uint8_t ps_iid_dt0_bits[29]={19,19,19,20,20,20,17,15,12,10,8,6,4,2,1,3,5,7,9,11,13,14,17,19,20,20,20,20,20};
static const uint32_t ps_iid_dt0_code[29]={0x7FFF9,0x7FFFA,0x7FFFB,0xFFFF8,0xFFFF9,0xFFFFA,0x1FFFD,0x7FFE,0xFFE,0x3FE,0xFE,0x3E,0xE,2,0,6,0x1E,0x7E,0x1FE,0x7FE,0x1FFE,0x3FFE,0x1FFFC,0x7FFF8,0xFFFFB,0xFFFFC,0xFFFFD,0xFFFFE,0xFFFFF};
static const uint8_t ps_icc_df_bits[15]={14,14,12,10,7,5,3,1,2,4,6,8,9,11,13};
static const uint32_t ps_icc_df_code[15]={0x3FFF,0x3FFE,0xFFE,0x3FE,0x7E,0x1E,6,0,2,0xE,0x3E,0xFE,0x1FE,0x7FE,0x1FFE};
static const uint8_t ps_icc_dt_bits[15]={14,13,11,9,7,5,3,1,2,4,6,8,10,12,14};
static const uint32_t ps_icc_dt_code[15]={0x3FFE,0x1FFE,0x7FE,0x1FE,0x7E,0x1E,6,0,2,0xE,0x3E,0xFE,0x3FE,0xFFE,0x3FFF};
static const uint8_t ps_ipd_df_bits[8]={1,3,4,4,4,4,4,4};
static const uint32_t ps_ipd_df_code[8]={1,0,6,4,2,3,5,7};
static const uint8_t ps_ipd_dt_bits[8]={1,3,4,5,5,4,4,3};
static const uint32_t ps_ipd_dt_code[8]={1,2,2,3,2,0,3,3};
static const uint8_t ps_opd_df_bits[8]={1,3,4,4,5,5,4,3};
static const uint32_t ps_opd_df_code[8]={1,1,6,4,0xF,0xE,5,0};
static const uint8_t ps_opd_dt_bits[8]={1,3,4,5,5,4,4,3};
static const uint32_t ps_opd_dt_code[8]={1,2,1,7,6,0,2,3};
static const PsBook ps_iid_book[4]={
    {ps_iid_df0_code,ps_iid_df0_bits,29u,14u},
    {ps_iid_df1_code,ps_iid_df1_bits,61u,30u},
    {ps_iid_dt0_code,ps_iid_dt0_bits,29u,14u},
    {ps_iid_dt1_code,ps_iid_dt1_bits,61u,30u}};
static const PsBook ps_icc_book[2]={
    {ps_icc_df_code,ps_icc_df_bits,15u,7u},
    {ps_icc_dt_code,ps_icc_dt_bits,15u,7u}};
static const PsBook ps_ipd_book[2]={{ps_ipd_df_code,ps_ipd_df_bits,8u,0u},
                                    {ps_ipd_dt_code,ps_ipd_dt_bits,8u,0u}};
static const PsBook ps_opd_book[2]={{ps_opd_df_code,ps_opd_df_bits,8u,0u},
                                    {ps_opd_dt_code,ps_opd_dt_bits,8u,0u}};

static int ps_symbol(const uint8_t *data,size_t size,size_t *pos,
                     const PsBook *book,int *value){
    if(!data||!pos||!book||!value)return 0;
    uint32_t code=0u,bit=0u;
    for(unsigned width=1u;width<=20u;width++){
        if(!read(data,size,pos,1u,&bit))return 0;
        code=(code<<1u)|bit;
        for(unsigned s=0u;s<book->count;s++)
            if(book->bits[s]==width && book->code[s]==code){
                *value=(int)s-(int)book->offset;
                return 1;
            }
    }
    return 0;
}
static const PsBook *ps_book_for_table(SautiAacPsHuffmanTable table){
    static const PsBook ordered[SAUTI_AAC_PS_HUFFMAN_TABLES]={
        {ps_iid_df0_code,ps_iid_df0_bits,29u,14u},
        {ps_iid_df1_code,ps_iid_df1_bits,61u,30u},
        {ps_iid_dt0_code,ps_iid_dt0_bits,29u,14u},
        {ps_iid_dt1_code,ps_iid_dt1_bits,61u,30u},
        {ps_icc_df_code,ps_icc_df_bits,15u,7u},
        {ps_icc_dt_code,ps_icc_dt_bits,15u,7u},
        {ps_ipd_df_code,ps_ipd_df_bits,8u,0u},
        {ps_ipd_dt_code,ps_ipd_dt_bits,8u,0u},
        {ps_opd_df_code,ps_opd_df_bits,8u,0u},
        {ps_opd_dt_code,ps_opd_dt_bits,8u,0u}};
    return table<SAUTI_AAC_PS_HUFFMAN_TABLES?&ordered[table]:NULL;
}
int sauti_aac_ps_huffman_codeword(SautiAacPsHuffmanTable table,
                                  unsigned symbol,uint32_t *code,
                                  uint8_t *width){
    const PsBook *book=ps_book_for_table(table);
    if(!book||!code||!width||symbol>=book->count)return 0;
    *code=book->code[symbol];*width=book->bits[symbol];return 1;
}
int sauti_aac_ps_huffman_symbol_decode(
    SautiAacPsHuffmanTable table,const uint8_t *bytes,size_t size,
    size_t *bit_offset,int *delta){
    const PsBook *book=ps_book_for_table(table);
    if(!book||!bytes||!bit_offset||!delta)return 0;
    size_t pos=*bit_offset;int value=0;
    if(!ps_symbol(bytes,size,&pos,book,&value))return 0;
    *delta=value;*bit_offset=pos;return 1;
}
static unsigned ps_parameter_count(unsigned mode){
    static const uint8_t count[6]={10u,20u,34u,10u,20u,34u};
    return mode<6u?count[mode]:0u;
}
static unsigned ps_phase_parameter_count(unsigned mode){
    static const uint8_t count[6]={5u,11u,17u,5u,11u,17u};
    return mode<6u?count[mode]:0u;
}
static int ps_decode_parameters(const uint8_t *data,size_t size,size_t *pos,
                                const PsBook *books,unsigned quant,
                                unsigned count,unsigned num_env,
                                const int8_t previous[],int8_t output[][34],
                                unsigned is_iid){
    if(!data||!pos||!books||!previous||!output||count>34u||num_env>4u)return 0;
    for(unsigned e=0u;e<num_env;e++){
        uint32_t dt=0u;int delta=0,value=0;
        if(!read(data,size,pos,1u,&dt))return 0;
        const PsBook *book=is_iid ? &books[(dt?2u:0u)+quant] : &books[dt];
        if(!ps_symbol(data,size,pos,book,&delta))return 0;
        const int8_t *base=dt?(e?output[e-1u]:previous):NULL;
        for(unsigned b=0u;b<count;b++){
            if(b!=0u && !ps_symbol(data,size,pos,book,&delta))return 0;
            value=(dt?base[b]:value)+delta;
            if((is_iid && (value<-(7+8*(int)quant) || value>7+8*(int)quant)) ||
               (!is_iid && (value<0 || value>7)))return 0;
            output[e][b]=(int8_t)value;
        }
    }
    return 1;
}
static int ps_decode_phase(const uint8_t *data,size_t size,size_t *pos,
                           const PsBook books[2],unsigned count,unsigned env,
                           const int8_t previous[],
                           int8_t output[][SAUTI_AAC_PS_MAX_PHASE_PARAMETERS]){
    if(!data||!pos||!books||!previous||!output||
       count>SAUTI_AAC_PS_MAX_PHASE_PARAMETERS||env>4u)return 0;
    for(unsigned e=0;e<env;e++){
        uint32_t dt=0;int delta=0;
        if(!read(data,size,pos,1u,&dt)||!ps_symbol(data,size,pos,&books[dt],&delta))return 0;
        const int8_t *base=dt?(e?output[e-1u]:previous):NULL;int value=0;
        for(unsigned b=0;b<count;b++){
            if(b&& !ps_symbol(data,size,pos,&books[dt],&delta))return 0;
            value=((dt?base[b]:value)+delta)&7;
            output[e][b]=(int8_t)value;
        }
    }
    return 1;
}
int sauti_aac_ps_frame_parse(const uint8_t *data,size_t size,
                             size_t *bit_offset,
                             SautiAacPsHeaderState *state,
                             SautiAacPsFrame *frame){
    static const uint8_t num_env_tab[2][4]={{0,1,2,4},{1,2,3,4}};
    if(!data||!bit_offset||!state||!frame)return 0;
    size_t pos=*bit_offset,header_pos=pos;
    uint32_t header_present=0,esc=0;
    SautiAacPsHeaderState next_state=*state;
    SautiAacPsFrame next;memset(&next,0,sizeof(next));
    if(!read(data,size,&header_pos,1,&header_present) ||
       !sauti_aac_ps_header_state_parse(data,size,&pos,&next_state,
                                        &next.header) ||
       !read(data,size,&pos,1,&next.frame_class) ||
       !read(data,size,&pos,2,&next.num_env_idx))return 0;
    next.header_present=header_present;
    next.num_env=num_env_tab[next.frame_class][next.num_env_idx];
    if(next.frame_class){
        for(uint32_t e=0;e<next.num_env;e++){
            uint32_t border=0;
            if(!read(data,size,&pos,5,&border) ||
               (e && border<=next.border_position[e-1u]))return 0;
            next.border_position[e]=(uint8_t)border;
        }
    }else if(next.num_env){
        /* Fixed-frame borders are the final QMF-slot index of each envelope
         * in the standard 32-slot PS frame: 31, 15/31 or 7/15/23/31. */
        for(uint32_t e=0u;e<next.num_env;e++)
            next.border_position[e]=(uint8_t)(((e+1u)*32u)/next.num_env-1u);
    }
    if(next.header.enable_iid){
        next.iid_count=(uint8_t)ps_parameter_count(next.header.iid_mode);
        if(!next.iid_count || !ps_decode_parameters(
               data,size,&pos,ps_iid_book,next.header.iid_mode>2u,
               next.iid_count,next.num_env,next_state.iid_last,next.iid,1u))
            return 0;
    }
    if(next.header.enable_icc){
        next.icc_count=(uint8_t)ps_parameter_count(next.header.icc_mode);
        if(!next.icc_count || !ps_decode_parameters(
               data,size,&pos,ps_icc_book,0u,next.icc_count,next.num_env,
               next_state.icc_last,next.icc,0u))return 0;
    }
    if(next.header.enable_ext){
        if(!read(data,size,&pos,4,&next.extension_bytes))return 0;
        if(next.extension_bytes==15u){
            if(!read(data,size,&pos,8,&esc) || esc>UINT32_MAX-15u)return 0;
            next.extension_bytes+=esc;
        }
        if(size>SIZE_MAX/8u)return 0;
        size_t total=size*8u;
        if(pos>total || (size_t)next.extension_bytes>(total-pos)/8u)return 0;
        size_t remain=(size_t)next.extension_bytes*8u;
        while(remain>7u){
            uint32_t id=0u,reserved=0u,ipdopd=0u;size_t before=pos;
            if(!read(data,size,&pos,2u,&id) || id!=0u ||
               !read(data,size,&pos,1u,&ipdopd))return 0;
            next.enable_ipdopd=(uint8_t)ipdopd;
            if(next.enable_ipdopd){
                /* PS phase resolution follows the IID mode as 5/11/17
                 * bands, rather than the 10/20/34 IID band count. */
                next.ipdopd_count=(uint8_t)ps_phase_parameter_count(
                    next.header.iid_mode);
                if(!next.header.enable_iid || !next.ipdopd_count ||
                   !ps_decode_phase(data,size,&pos,ps_ipd_book,next.ipdopd_count,
                                    next.num_env,next_state.ipd_last,next.ipd) ||
                   !ps_decode_phase(data,size,&pos,ps_opd_book,next.ipdopd_count,
                                    next.num_env,next_state.opd_last,next.opd))return 0;
            }
            if(!read(data,size,&pos,1u,&reserved)||reserved)return 0;
            const size_t used=pos-before;
            if(used>remain)return 0;
            remain-=used;
        }
        for(size_t fill_count=0u;fill_count<remain;fill_count++){
            uint32_t fill=0u;if(!read(data,size,&pos,1u,&fill)||fill)return 0;
        }
    }
    /* The PS mixer always needs a parameter set through QMF slot 31. A frame
     * may legally signal no envelopes, or a variable final border before that
     * slot; materialize a tail from the current final envelope (or streaming
     * history) without altering the signaled syntax count. */
    next.resolved_num_env=next.num_env;
    if(!next.num_env || next.border_position[next.num_env-1u]<31u){
        const unsigned target=next.resolved_num_env++;
        if(target>=SAUTI_AAC_PS_MAX_ENVELOPES)return 0;
        if(next.num_env){
            memcpy(next.iid[target],next.iid[next.num_env-1u],sizeof(next.iid[target]));
            memcpy(next.icc[target],next.icc[next.num_env-1u],sizeof(next.icc[target]));
            memcpy(next.ipd[target],next.ipd[next.num_env-1u],sizeof(next.ipd[target]));
            memcpy(next.opd[target],next.opd[next.num_env-1u],sizeof(next.opd[target]));
        }else{
            memcpy(next.iid[target],next_state.iid_last,sizeof(next.iid[target]));
            memcpy(next.icc[target],next_state.icc_last,sizeof(next.icc[target]));
            memcpy(next.ipd[target],next_state.ipd_last,sizeof(next.ipd[target]));
            memcpy(next.opd[target],next_state.opd_last,sizeof(next.opd[target]));
        }
        next.border_position[target]=31u;
    }
    if(next.header.enable_iid){
        if(next.num_env)memcpy(next_state.iid_last,next.iid[next.num_env-1u],
                               sizeof(next_state.iid_last));
    }else memset(next_state.iid_last,0,sizeof(next_state.iid_last));
    if(next.header.enable_icc){
        if(next.num_env)memcpy(next_state.icc_last,next.icc[next.num_env-1u],
                               sizeof(next_state.icc_last));
    }else memset(next_state.icc_last,0,sizeof(next_state.icc_last));
    if(next.enable_ipdopd && next.num_env){
        memcpy(next_state.ipd_last,next.ipd[next.num_env-1u],sizeof(next_state.ipd_last));
        memcpy(next_state.opd_last,next.opd[next.num_env-1u],sizeof(next_state.opd_last));
    }else if(!next.enable_ipdopd){
        memset(next_state.ipd_last,0,sizeof(next_state.ipd_last));
        memset(next_state.opd_last,0,sizeof(next_state.opd_last));
    }
    *state=next_state;*frame=next;*bit_offset=pos;return 1;
}
