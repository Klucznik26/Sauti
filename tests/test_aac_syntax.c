#include "sauti_aac_syntax.h"
#include "sauti_aac_headers.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {uint8_t bytes[512];size_t bit;} Bits;
static void put(Bits *b,unsigned n,uint32_t value){
    assert(n<=32 && b->bit+n<=sizeof(b->bytes)*8u);
    for(unsigned i=n;i>0;--i){
        unsigned one=(value>>(i-1))&1u;
        b->bytes[b->bit/8]|=(uint8_t)(one<<(7-(b->bit%8)));
        b->bit++;
    }
}
static void test_ics(void){
    Bits b={0};
    put(&b,5,31);size_t start=b.bit;
    put(&b,1,0);put(&b,2,0);put(&b,1,1);put(&b,6,40);put(&b,1,0);
    size_t pos=start;SautiAacIcsInfo info={0};
    assert(sauti_aac_ics_info_parse(b.bytes,sizeof(b.bytes),&pos,&info));
    assert(pos==b.bit&&info.window_sequence==0&&info.window_shape==1&&
           info.max_sfb==40&&info.window_count==1&&info.group_count==1&&
           info.group_length[0]==1);
    assert(!sauti_aac_ics_info_parse(b.bytes,1,&start,&info));
    assert(start==5);
    b=(Bits){0};
    put(&b,1,0);put(&b,2,2);put(&b,1,0);put(&b,4,12);
    const unsigned grouping[]={1,0,1,0,0,1,1};
    for(unsigned i=0;i<7;i++)put(&b,1,grouping[i]);
    pos=0;assert(sauti_aac_ics_info_parse(b.bytes,2,&pos,&info));
    assert(pos==15&&info.window_sequence==2&&info.window_count==8&&
           info.max_sfb==12&&info.group_count==4&&
           info.group_length[0]==2&&info.group_length[1]==2&&
           info.group_length[2]==1&&info.group_length[3]==3);
    b=(Bits){0};
    put(&b,1,0);put(&b,2,0);put(&b,1,0);put(&b,6,12);put(&b,1,1);
    pos=0;
    assert(!sauti_aac_ics_info_parse(b.bytes,2,&pos,&info)&&pos==0);
    b.bytes[0]|=0x80u;assert(!sauti_aac_ics_info_parse(b.bytes,2,&pos,&info));
    assert(!sauti_aac_ics_info_parse(NULL,2,&pos,&info));
}
static void test_fill(void){
    Bits b={0};SautiAacFillInfo f={0};
    put(&b,4,2);put(&b,4,13);put(&b,12,0xabc);
    size_t pos=0;
    assert(sauti_aac_fill_parse(b.bytes,3,&pos,&f));
    assert(pos==20&&f.payload_bytes==2&&f.extension_type==13&&
           f.sbr_candidate&&!f.sbr_crc_flag&&f.extension_start_bit==4&&
           f.extension_bits==16);
    pos=0;assert(!sauti_aac_fill_parse(b.bytes,2,&pos,&f)&&pos==0);
    b=(Bits){0};
    put(&b,4,0);pos=0;
    assert(sauti_aac_fill_parse(b.bytes,1,&pos,&f));
    assert(pos==4&&f.payload_bytes==0&&f.extension_type==UINT32_MAX&&
           !f.sbr_candidate);
    b=(Bits){0};put(&b,4,15);put(&b,8,2);put(&b,4,14);
    for(int i=0;i<124;i++)put(&b,1,0);
    pos=0;
    assert(sauti_aac_fill_parse(b.bytes,18,&pos,&f));
    assert(pos==12+128&&f.payload_bytes==16&&f.extension_type==14&&
           f.sbr_crc_flag);
    b=(Bits){0};put(&b,4,15);put(&b,8,255);put(&b,4,13);
    pos=0;assert(!sauti_aac_fill_parse(b.bytes,2,&pos,&f)&&pos==0);
}
static void test_raw(void){
    Bits b={0};SautiAacRawPrefix raw={0};
    put(&b,3,6);put(&b,4,2);put(&b,4,13);put(&b,12,0xabc);put(&b,3,7);
    size_t pos=0;
    assert(sauti_aac_raw_prefix_parse(b.bytes,4,&pos,&raw)==1);
    assert(pos==26&&raw.stop_bit==26&&raw.fill_count==1&&
           raw.has_sbr_candidate&&raw.first_audio_id==UINT32_MAX);
    b=(Bits){0};put(&b,3,1);put(&b,4,15);put(&b,3,7);
    pos=0;
    assert(sauti_aac_raw_prefix_parse(b.bytes,2,&pos,&raw)==2);
    assert(pos==0&&raw.stop_bit==0&&raw.first_audio_id==1&&raw.fill_count==0);
    b=(Bits){0};put(&b,3,7);pos=0;
    assert(sauti_aac_raw_prefix_parse(b.bytes,1,&pos,&raw)==1);
    assert(pos==3&&raw.fill_count==0);
    assert(!sauti_aac_raw_prefix_parse(b.bytes,0,&pos,&raw));
}
static void test_sbr(void){
    Bits b={0};size_t pos=0;SautiAacSbrHeader h={0};
    put(&b,1,1);put(&b,4,5);put(&b,4,10);put(&b,3,3);
    put(&b,2,0);put(&b,1,1);put(&b,1,1);
    put(&b,2,3);put(&b,1,0);put(&b,2,1);
    put(&b,2,1);put(&b,2,2);put(&b,1,0);put(&b,1,1);
    assert(b.bit==27);
    assert(sauti_aac_sbr_header_parse(b.bytes,4,&pos,&h)&&pos==27);
    assert(h.amp_res==1&&h.start_freq==5&&h.stop_freq==10&&
           h.xover_band==3&&h.extra_1&&h.extra_2&&h.freq_scale==3&&
           h.alter_scale==0&&h.noise_bands==1&&h.limiter_bands==1&&
           h.limiter_gains==2&&h.interpol_freq==0&&h.smoothing_mode==1);
    pos=0;assert(!sauti_aac_sbr_header_parse(b.bytes,3,&pos,&h)&&pos==0);
    b=(Bits){0};
    put(&b,1,0);put(&b,4,1);put(&b,4,4);put(&b,3,0);
    put(&b,2,0);put(&b,1,0);put(&b,1,0);
    pos=0;assert(sauti_aac_sbr_header_parse(b.bytes,2,&pos,&h));
    assert(pos==16&&h.freq_scale==2&&h.alter_scale==1&&
           h.noise_bands==2&&h.limiter_bands==2&&
           h.limiter_gains==2&&h.interpol_freq==1&&h.smoothing_mode==1);
    b.bytes[1]|=0x08u; /* reserved bit in position 12 */
    pos=0;assert(!sauti_aac_sbr_header_parse(b.bytes,2,&pos,&h)&&pos==0);
}
static void test_ps(void){
    Bits b={0};size_t pos=0;SautiAacPsHeader h={0};
    put(&b,1,1);put(&b,1,1);put(&b,3,5);
    put(&b,1,1);put(&b,3,4);put(&b,1,1);
    assert(sauti_aac_ps_header_parse(b.bytes,2,&pos,&h));
    assert(pos==10&&h.has_header&&h.enable_iid&&h.iid_mode==5&&
           h.enable_icc&&h.icc_mode==4&&h.enable_ext);
    pos=0;assert(!sauti_aac_ps_header_parse(b.bytes,1,&pos,&h)&&pos==0);
    b=(Bits){0};put(&b,1,1);put(&b,1,1);put(&b,3,6);
    put(&b,1,0);put(&b,1,0);
    pos=0;assert(!sauti_aac_ps_header_parse(b.bytes,1,&pos,&h)&&pos==0);
    b=(Bits){0};put(&b,1,0);pos=0;
    assert(sauti_aac_ps_header_parse(b.bytes,1,&pos,&h));
    assert(pos==1&&!h.has_header);
}
static void test_dsp(void){
    float in[2048],win[2048],overlap[1024],out[1024];
    for(size_t n=128;n<=1024;n*=8){
        for(size_t i=0;i<2*n;i++){
            in[i]=(float)(i%4)/4.f;
            win[i]=0.5f;
        }
        for(size_t i=0;i<n;i++)overlap[i]=0.25f;
        assert(sauti_aac_window_overlap_asm(in,win,overlap,out,n));
        for(size_t i=0;i<n;i++){
            assert(out[i]==0.25f+in[i]*0.5f);
            assert(overlap[i]==in[n+i]*0.5f);
        }
    }
    assert(!sauti_aac_window_overlap_asm(in,win,overlap,out,129));
    assert(!sauti_aac_window_overlap_asm(NULL,win,overlap,out,128));
    assert(!sauti_aac_window_overlap_asm(in,NULL,overlap,out,128));
    assert(!sauti_aac_window_overlap_asm(in,win,NULL,out,128));
    assert(!sauti_aac_window_overlap_asm(in,win,overlap,NULL,128));
}
static int real_aac(const char *path){
    FILE *f=fopen(path,"rb");
    if(!f){perror("AAC fixture");return 0;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 0;}
    long end=ftell(f);
    if(end<=0||fseek(f,0,SEEK_SET)!=0){fclose(f);return 0;}
    uint8_t *data=malloc((size_t)end);
    if(!data){fclose(f);return 0;}
    size_t length=fread(data,1,(size_t)end,f);
    fclose(f);
    SautiAacAdts adts={0};
    int ok=length==(size_t)end &&
        sauti_aac_adts_parse_asm(data,length,&adts) &&
        adts.core_sample_rate>=8000&&adts.core_channels<=2;
    if(ok){
        size_t pos=0;SautiAacRawPrefix prefix={0};
        uint8_t *payload=data+adts.header_bytes;
        size_t payload_len=adts.frame_bytes-adts.header_bytes;
        int parsed=sauti_aac_raw_prefix_parse(payload,payload_len,&pos,&prefix);
        ok=parsed==1 || parsed==2;
    }
    if(ok)printf("PASS real ADTS frame: %u Hz %u ch, %u bytes\n",
                 adts.core_sample_rate,adts.core_channels,adts.frame_bytes);
    free(data);
    return ok;
}
int main(int argc,char **argv){
    test_ics();test_fill();test_raw();test_sbr();test_ps();test_dsp();
    puts("PASS AAC syntax: ICS grouping, FIL SBR candidates, PS/SBR headers, ASM overlap-add");
    if(argc==2)return real_aac(argv[1])?0:1;
    return argc==1?0:2;
}
