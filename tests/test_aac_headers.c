#include "sauti_aac_headers.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void adts_make(uint8_t *p,size_t n,uint8_t freq,uint8_t channels,
                      int crc,unsigned blocks){
    assert(n>=9 && n<=8191 && freq<=12 && channels>=1 && channels<=2 &&
           blocks>=1 && blocks<=4);
    memset(p,0,n);
    p[0]=0xff;
    p[1]=(uint8_t)(crc?0xf0:0xf1);
    p[2]=(uint8_t)(0x40u | (freq<<2) | (channels>>2));
    p[3]=(uint8_t)(((channels&3u)<<6) | ((n>>11)&3u));
    p[4]=(uint8_t)(n>>3);
    p[5]=(uint8_t)(((n&7u)<<5) | 0x1fu);
    p[6]=(uint8_t)(0xfcu | (blocks-1u));
}
static void test_adts(void){
    uint8_t p[100];SautiAacAdts out={0};
    adts_make(p,sizeof(p),4,2,0,1);
    assert(sauti_aac_adts_parse_asm(p,sizeof(p),&out));
    assert(out.core_sample_rate==44100 && out.core_channels==2 &&
           out.core_aot==2 && out.frame_bytes==100 &&
           out.header_bytes==7 && out.raw_blocks==1 && !out.crc_present);
    adts_make(p,sizeof(p),6,1,1,4);
    assert(sauti_aac_adts_parse_asm(p,sizeof(p),&out));
    assert(out.core_sample_rate==24000 && out.core_channels==1 &&
           out.header_bytes==9 && out.crc_present && out.raw_blocks==4);
    assert(!sauti_aac_adts_parse_asm(p,8,&out));
    { int ok=sauti_aac_adts_parse_asm(p,99,&out);
      if(ok)fprintf(stderr,"ADTS short frame accepted: declared=%u core_rate=%u header=%u bytes[3..5]=%02x %02x %02x\n",out.frame_bytes,out.core_sample_rate,out.header_bytes,p[3],p[4],p[5]);
      assert(!ok);
    }
    p[2]|=0xc0;assert(!sauti_aac_adts_parse_asm(p,sizeof(p),&out));
    adts_make(p,sizeof(p),4,2,0,1);
    p[1]|=0x02;assert(!sauti_aac_adts_parse_asm(p,sizeof(p),&out));
    adts_make(p,sizeof(p),4,2,0,1);
    p[2]=(uint8_t)((p[2]&0xc3)| (15u<<2));
    assert(!sauti_aac_adts_parse_asm(p,sizeof(p),&out));
    adts_make(p,sizeof(p),4,2,0,1);
    p[2]&=(uint8_t)~1u;p[3]&=0x3f;
    assert(!sauti_aac_adts_parse_asm(p,sizeof(p),&out));
    assert(!sauti_aac_adts_parse_asm(NULL,sizeof(p),&out));
    assert(!sauti_aac_adts_parse_asm(p,sizeof(p),NULL));
}
static void test_bits(void){
    uint8_t data[]={0xba,0x9d,0xef,0x42};
    size_t pos=0;uint32_t v=77;
    assert(sauti_aac_bits_read_asm(data,4,&pos,3,&v)&&v==5&&pos==3);
    assert(sauti_aac_bits_read_asm(data,4,&pos,9,&v)&&v==0x1a9&&pos==12);
    assert(sauti_aac_bits_read_asm(data,4,&pos,20,&v)&&v==0xdef42&&pos==32);
    assert(!sauti_aac_bits_read_asm(data,4,&pos,1,&v)&&pos==32&&v==0xdef42);
    assert(!sauti_aac_bits_read_asm(data,4,&pos,33,&v));
    assert(!sauti_aac_bits_read_asm(data,4,&pos,0,&v));
    assert(!sauti_aac_bits_read_asm(data,4,NULL,1,&v));
    assert(!sauti_aac_bits_read_asm(NULL,4,&pos,1,&v));
}
static void putbits(uint8_t *p,size_t *offset,unsigned count,uint32_t bits){
    for(unsigned i=count;i>0;i--){
        unsigned bit=(bits>>(i-1))&1u;
        p[*offset/8]|=(uint8_t)(bit<<(7-(*offset%8)));
        ++*offset;
    }
}
static void asc_make(uint8_t *p,size_t *bytes,unsigned aot,
                     unsigned freq,unsigned channels,unsigned extfreq){
    memset(p,0,8);size_t pos=0;
    putbits(p,&pos,5,aot);
    putbits(p,&pos,4,freq);
    putbits(p,&pos,4,channels);
    if(aot==5 || aot==29){
        putbits(p,&pos,4,extfreq);
        putbits(p,&pos,5,2); /* Underlying AAC-LC AOT */
    }
    *bytes=(pos+7)/8;
}
static void test_asc(void){
    uint8_t b[8];size_t n;SautiAacConfig cfg={0};
    asc_make(b,&n,2,4,2,0);
    assert(sauti_aac_asc_parse(b,n,&cfg));
    assert(cfg.core_aot==2&&cfg.core_sample_rate==44100&&
           cfg.output_sample_rate==44100&&cfg.core_channels==2&&
           cfg.output_channels==2&&!cfg.sbr_present&&!cfg.ps_present&&
           !cfg.explicit_extension);
    asc_make(b,&n,5,6,2,3);
    assert(sauti_aac_asc_parse(b,n,&cfg));
    assert(cfg.core_aot==2&&cfg.core_sample_rate==24000&&
           cfg.output_sample_rate==48000&&cfg.core_channels==2&&
           cfg.output_channels==2&&cfg.sbr_present&&!cfg.ps_present&&
           cfg.explicit_extension);
    asc_make(b,&n,29,6,1,3);
    assert(sauti_aac_asc_parse(b,n,&cfg));
    assert(cfg.core_aot==2&&cfg.core_sample_rate==24000&&
           cfg.output_sample_rate==48000&&cfg.core_channels==1&&
           cfg.output_channels==2&&cfg.sbr_present&&cfg.ps_present);
    assert(!sauti_aac_asc_parse(b,2,&cfg));
    asc_make(b,&n,29,6,2,3);
    assert(!sauti_aac_asc_parse(b,n,&cfg));
    asc_make(b,&n,29,15,1,3);
    assert(!sauti_aac_asc_parse(b,n,&cfg));
    asc_make(b,&n,2,13,2,0);
    assert(!sauti_aac_asc_parse(b,n,&cfg));
    asc_make(b,&n,3,4,2,0);
    assert(!sauti_aac_asc_parse(b,n,&cfg));
}
int main(void){test_adts();test_bits();test_asc();
    puts("PASS AAC ASM: ADTS bounds, bitstream reader, LC/HE-AAC/HE-AACv2 ASC");
    return 0;
}
