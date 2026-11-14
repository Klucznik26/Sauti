#include "sauti_aac_lc_sections.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_syntax.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t bytes[256]; size_t bits; } Bits;
static void put(Bits *b,unsigned width,uint32_t value){
    assert(width<=32u && b->bits+width<=sizeof(b->bytes)*8u);
    for(unsigned j=width;j>0;j--){
        b->bytes[b->bits/8]|=(uint8_t)(((value>>(j-1))&1u) << (7u-(b->bits%8u)));
        b->bits++;
    }
}
static void long_ics(Bits *b,unsigned max_sfb){
    put(b,1,0);put(b,2,0);put(b,1,1);
    put(b,6,max_sfb);put(b,1,0);
}
static void sce_header(Bits *b,unsigned max_sfb){
    put(b,3,0); /* ID_SCE */
    put(b,4,9); /* element_instance_tag */
    put(b,8,140); /* global_gain */
    long_ics(b,max_sfb);
}
static void long_sections(void){
    Bits b={0};SautiAacScePrefix result={0};size_t pos=0;
    sce_header(&b,5);
    put(&b,4,5);put(&b,5,2);
    put(&b,4,11);put(&b,5,3);
    assert(sauti_aac_lc_sce_prefix_parse(b.bytes,sizeof(b.bytes),&pos,&result));
    assert(pos==b.bits&&result.instance_tag==9&&result.global_gain==140);
    assert(result.ics.window_sequence==0 && result.ics.max_sfb==5);
    assert(result.sections.section_count==2 && result.sections.group_count==1);
    assert(result.sections.sections[0].group==0 &&
           result.sections.sections[0].codebook==5 &&
           result.sections.sections[0].first_sfb==0 &&
           result.sections.sections[0].end_sfb==2);
    assert(result.sections.sections[1].codebook==11 &&
           result.sections.sections[1].first_sfb==2 &&
           result.sections.sections[1].end_sfb==5);
    pos=0;SautiAacScePrefix sentinel=result;
    assert(!sauti_aac_lc_sce_prefix_parse(b.bytes,4,&pos,&result));
    assert(pos==0 && result.global_gain==sentinel.global_gain &&
           result.sections.section_count==sentinel.sections.section_count);
    b.bytes[0]|=0x20u; /* ID_CPE instead of SCE */
    pos=0;assert(!sauti_aac_lc_sce_prefix_parse(b.bytes,sizeof(b.bytes),
                                                &pos,&result)&&pos==0);
}
static void escape_sections(void){
    Bits b={0};size_t pos=0;SautiAacScePrefix s={0};
    sce_header(&b,35);
    put(&b,4,2);put(&b,5,31);put(&b,5,0);
    put(&b,4,0);put(&b,5,4);
    assert(sauti_aac_lc_sce_prefix_parse(b.bytes,sizeof(b.bytes),&pos,&s));
    assert(pos==b.bits && s.sections.section_count==2);
    assert(s.sections.sections[0].end_sfb==31 &&
           s.sections.sections[1].first_sfb==31 &&
           s.sections.sections[1].end_sfb==35);
}
static void short_ics_sections(void){
    Bits b={0};size_t pos=0;SautiAacScePrefix s={0};
    put(&b,3,0);put(&b,4,0);put(&b,8,192);
    put(&b,1,0);put(&b,2,2);put(&b,1,0);
    put(&b,4,5);
    put(&b,7,0x77u); /* grouping 1110111 => two groups [4,4] */
    put(&b,4,3);put(&b,3,5);
    put(&b,4,6);put(&b,3,5);
    assert(sauti_aac_lc_sce_prefix_parse(b.bytes,sizeof(b.bytes),&pos,&s));
    assert(pos==b.bits && s.ics.window_count==8 &&
           s.ics.group_count==2 && s.ics.group_length[0]==4 &&
           s.ics.group_length[1]==4 && s.sections.section_count==2 &&
           s.sections.sections[0].group==0 &&
           s.sections.sections[1].group==1);
}
static void invalid(void){
    Bits b={0};size_t pos=0;
    SautiAacScePrefix s={0};
    sce_header(&b,2);
    put(&b,4,12);put(&b,5,2); /* reserved section codebook 12 */
    assert(!sauti_aac_lc_sce_prefix_parse(b.bytes,sizeof(b.bytes),&pos,&s));
    assert(pos==0);
    b=(Bits){0};sce_header(&b,2);
    put(&b,4,1);put(&b,5,3); /* 3 bands, only 2 declared */
    assert(!sauti_aac_lc_sce_prefix_parse(b.bytes,sizeof(b.bytes),&pos,&s));
    b=(Bits){0};sce_header(&b,2);
    put(&b,4,1);put(&b,5,0); /* zero-band section */
    assert(!sauti_aac_lc_sce_prefix_parse(b.bytes,sizeof(b.bytes),&pos,&s));
    b=(Bits){0};sce_header(&b,33);
    put(&b,4,1);put(&b,5,31);put(&b,5,31);
    assert(!sauti_aac_lc_sce_prefix_parse(b.bytes,sizeof(b.bytes),&pos,&s));
    assert(pos==0);
}
static int real_mono_adts(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f){perror("AAC mono fixture");return 0;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 0;}
    long n=ftell(f);
    if(n<10||fseek(f,0,SEEK_SET)!=0){fclose(f);return 0;}
    uint8_t *bytes=malloc((size_t)n);
    if(!bytes){fclose(f);return 0;}
    int ok=fread(bytes,1,(size_t)n,f)==(size_t)n;
    fclose(f);
    SautiAacAdts header={0};
    if(ok)ok=sauti_aac_adts_parse_asm(bytes,(size_t)n,&header) &&
            header.core_channels==1u && header.core_aot==2u;
    SautiAacScePrefix sce={0};
    if(ok){
        const uint8_t *raw=bytes+header.header_bytes;
        size_t length=header.frame_bytes-header.header_bytes;
        size_t pos=0;
        SautiAacRawPrefix prefix={0};
        int kind=sauti_aac_raw_prefix_parse(raw,length,&pos,&prefix);
        ok=kind==2 && prefix.first_audio_id==0u &&
           sauti_aac_lc_sce_prefix_parse(raw,length,&pos,&sce) &&
           sce.sections.section_count>0u &&
           sce.ics.max_sfb>0u && pos<length*8u;
        if(ok)printf("PASS AAC-LC real mono: %u Hz, %u SFB, %u sections, gain=%u\n",
                     header.core_sample_rate,sce.ics.max_sfb,
                     sce.sections.section_count,sce.global_gain);
    }
    free(bytes);
    return ok;
}
int main(int argc,char **argv){
    long_sections();escape_sections();short_ics_sections();invalid();
    puts("PASS AAC-LC section_data: SCE, long/short grouping, Huffman codebook ranges, fail-closed");
    if(argc==2)return real_mono_adts(argv[1])?0:1;
    return argc==1?0:2;
}
