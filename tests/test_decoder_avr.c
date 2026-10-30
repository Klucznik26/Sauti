#include "sauti_decoder.h"
#include "sauti_decoder_avr.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void be16(unsigned char *p, unsigned v){p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;}
static void be32(unsigned char *p, unsigned v){p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}
static void header(unsigned char *p,size_t n,unsigned ch,unsigned bits,unsigned sign,unsigned units){
    memset(p,0,n); memcpy(p,"2BIT",4);
    be16(p+12,ch==2?0xffff:0);
    be16(p+14,bits);
    be16(p+16,sign?0xffff:0);
    be32(p+22,0xff00bb80); /* 48 kHz and legacy replay byte FF */
    be32(p+26,units);
}
static void check(unsigned char *p,size_t n,unsigned ch,uint64_t frames,float expected0,float expected1){
    const SautiDecoderOps *registry[]={&sauti_decoder_avr_ops};
    SautiDecoder d={0};float out[8]={0};
    assert(sauti_decoder_open(&d,registry,1,p,n));
    assert(d.info.sample_rate==48000&&d.info.channels==ch&&d.info.total_frames==frames);
    assert(sauti_decoder_read(&d,out,frames)==frames);
    assert(out[0]==expected0 && out[1]==expected1);
    assert(sauti_decoder_read(&d,out,1)==0);
    assert(sauti_decoder_seek(&d,1));
    assert(sauti_decoder_read(&d,out,1)==1);
    assert(!sauti_decoder_seek(&d,frames+1));
    sauti_decoder_close(&d);
}
int main(void){
    unsigned char p[144];
    header(p,sizeof(p),1,8,0,2); p[128]=0;p[129]=255;
    check(p,130,1,2,-1.0f,127.0f/128.0f);
    header(p,sizeof(p),1,8,1,2);p[128]=0x80;p[129]=0x7f;
    check(p,130,1,2,-1.0f,127.0f/128.0f);
    header(p,sizeof(p),2,16,1,4);
    p[128]=0x80;p[129]=0;p[130]=0x7f;p[131]=0xff;
    p[132]=0;p[133]=0;p[134]=0x40;p[135]=0;
    check(p,136,2,2,-1.0f,32767.0f/32768.0f);
    header(p,sizeof(p),2,16,0,4);
    p[128]=0;p[129]=0;p[130]=0xff;p[131]=0xff;
    p[132]=0x80;p[133]=0;p[134]=0xc0;p[135]=0;
    check(p,136,2,2,-1.0f,32767.0f/32768.0f);
    header(p,sizeof(p),1,8,0,2);
    const SautiDecoderOps *reg[]={&sauti_decoder_avr_ops};
    SautiDecoder d={0};
    assert(!sauti_decoder_open(&d,reg,1,p,129)); /* truncated */
    p[0]='X';assert(!sauti_decoder_open(&d,reg,1,p,130));
    p[0]='2';be16(p+14,12);assert(!sauti_decoder_open(&d,reg,1,p,130));
    be16(p+14,8);be16(p+12,2);be32(p+26,3);assert(!sauti_decoder_open(&d,reg,1,p,130));
    be16(p+12,0);be32(p+26,2);be16(p+40,1);assert(!sauti_decoder_open(&d,reg,1,p,130));
    be16(p+40,0);be32(p+22,0xff000000);assert(!sauti_decoder_open(&d,reg,1,p,130));
    /* Partial reads, exact seek-to-EOF, and valid AVR loop metadata. */
    header(p,sizeof(p),1,8,0,4);
    p[128]=0;p[129]=64;p[130]=128;p[131]=255;
    be16(p+18,0xffff); /* file is marked loopable; PCM stream is still finite */
    be32(p+30,1);be32(p+34,4);
    {
        SautiDecoder part={0};
        float one[4]={0};
        assert(sauti_decoder_open(&part,reg,1,p,132));
        assert(part.info.total_frames==4);
        assert(sauti_decoder_read(&part,one,1)==1 && one[0]==-1.0f);
        assert(sauti_decoder_read(&part,one,2)==2);
        assert(one[0]==-0.5f && one[1]==0.0f);
        assert(sauti_decoder_seek(&part,3));
        assert(sauti_decoder_read(&part,one,2)==1);
        assert(one[0]==127.0f/128.0f);
        assert(sauti_decoder_seek(&part,4));
        assert(sauti_decoder_read(&part,one,1)==0);
        assert(sauti_decoder_seek(&part,0));
        assert(sauti_decoder_read(&part,one,4)==4);
        sauti_decoder_close(&part);
    }
    /* Explicitly reject unsupported bit depths, compression and bad stereo
       sample-unit counts instead of interpreting them as PCM noise. */
    header(p,sizeof(p),1,12,0,2);
    assert(!sauti_decoder_open(&d,reg,1,p,132));
    header(p,sizeof(p),1,14,0,2);
    assert(!sauti_decoder_open(&d,reg,1,p,132));
    header(p,sizeof(p),1,8,0,2);
    be16(p+40,1);
    assert(!sauti_decoder_open(&d,reg,1,p,132));
    header(p,sizeof(p),2,8,0,3);
    assert(!sauti_decoder_open(&d,reg,1,p,132));
    puts("PASS: Atari AVR ASM PCM 8/16-bit mono/stereo, seek/EOF, loop metadata, malformed headers");
    return 0;
}
