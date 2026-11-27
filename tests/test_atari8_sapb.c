#include "../src/decoders/retro/atari8/atari8_sapb.h"
#include "../src/decoders/retro/atari8/atari8_sapb_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)

typedef struct { uint64_t frames, left, right, different; } Capture;
static void capture(void *ctx,const int16_t frame[2])
{
    Capture *c=ctx;
    ++c->frames;
    c->left += frame[0]!=0;
    c->right += frame[1]!=0;
    c->different += frame[0]!=frame[1];
}
static size_t fixture(uint8_t *buf,bool stereo,bool ntsc)
{
    const char *header=stereo ?
        "SAP\r\nTYPE B\r\nINIT 2000\r\nPLAYER 2100\r\nSONGS 3\r\nDEFSONG 2\r\nSTEREO\r\nNTSC\r\nFASTPLAY 131\r\n" :
        "SAP\r\nTYPE B\r\nINIT 2000\r\nPLAYER 2100\r\nSONGS 3\r\nDEFSONG 2\r\n";
    (void)ntsc;
    size_t h=strlen(header);
    memcpy(buf,header,h);
    /* INIT stores A to $80, sets AUDF1 and AUDC1 on primary and
     * secondary (mirrored in mono), then RTS. */
    const uint8_t init[]={0x85,0x80,0xa9,71,0x8d,0x00,0xd2,
        0xa9,0xaf,0x8d,0x01,0xd2,0xa9,95,0x8d,0x10,0xd2,
        0xa9,0xa9,0x8d,0x11,0xd2,0x60};
    /* PLAYER increments RAM and writes AUDF1. */
    const uint8_t player[]={0xe6,0x80,0xa5,0x80,0x8d,0x00,0xd2,0x60};
    const uint8_t seg1[]={0xff,0xff,0x00,0x20,(uint8_t)(sizeof(init)-1),0x20};
    const uint8_t seg2[]={0x00,0x21,(uint8_t)(sizeof(player)-1),0x21};
    memcpy(buf+h,seg1,sizeof(seg1)); h+=sizeof(seg1);
    memcpy(buf+h,init,sizeof(init)); h+=sizeof(init);
    memcpy(buf+h,seg2,sizeof(seg2)); h+=sizeof(seg2);
    memcpy(buf+h,player,sizeof(player)); h+=sizeof(player);
    return h;
}
static void test_play(bool stereo)
{
    uint8_t data[512];
    size_t n=fixture(data,stereo,stereo);
    Atari8Sapb s;
    Atari8SapbInfo i;
    Capture c={0};
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_OK);
    CHECK(i.init==0x2000 && i.player==0x2100 && i.songs==3 && i.default_song==2);
    CHECK(i.stereo==stereo && i.ntsc==stereo);
    CHECK(i.interval==(stereo?131u:312u)*114u);
    CHECK(atari8_sapb_open(&s,data,n,-1,48000,capture,&c)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0x80]==2 && c.frames==0 && s.machine.frames==0);
    uint64_t start=s.machine.master_cycles;
    for(unsigned k=0;k<5;++k) CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0x80]==7 && s.machine.core.pokey[0].audf[0]==7);
    CHECK(s.intervals==5 && s.machine.master_cycles-start==5u*i.interval);
    CHECK(c.frames==(uint64_t)5*i.interval*48000u/s.machine.pcm.clock_hz);
    CHECK(s.machine.nonlinear_mix);
    CHECK(c.frames==s.machine.frames && c.left>0 && (stereo?c.right>0:c.right==0));
    if (stereo) CHECK(c.different>0);
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0x80]==0);
    CHECK(atari8_sapb_open(&s,data,n,3,48000,NULL,NULL)==ATARI8_SAPB_BAD_FORMAT);
    for(size_t k=0;k<n;++k) CHECK(atari8_sapb_parse(data,k,&i)!=ATARI8_SAPB_OK);
}
static void test_bad(void)
{
    uint8_t data[512];
    size_t n=fixture(data,false,false);
    Atari8SapbInfo i;
    Atari8Sapb s;
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_OK);
    size_t pos=i.binary_offset;
    data[pos+4]=0xff; data[pos+5]=0x1f;
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_BAD_FORMAT);
    n=fixture(data,false,false);
    data[pos+3]=0xd2; data[pos+5]=0xd2;
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_BAD_FORMAT);
    n=fixture(data,false,false);
    /* INIT immediately JAMs. */
    data[pos+6]=0x02;
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_RUNTIME);
    CHECK(s.machine.status==ATARI8_MACHINE_CPU_STOP);
    n=fixture(data,false,false);
    /* PLAYER loops indefinitely. */
    data[n-8]=0x4c; data[n-7]=0; data[n-6]=0x21;
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME);
    CHECK(s.machine.status==ATARI8_MACHINE_BUDGET);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME);
}
static void test_host_antic(void)
{
    Atari8Sapb s;
    uint8_t data[512];
    size_t n=fixture(data,false,false);
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    Atari8Machine *m=&s.machine;
    CHECK(m->sap_host_scheduled);
    /* Enable both interrupt sources, read NMIST, then RTS.
     * There must be no ANTIC NMI even when a VBI occurs. */
    static const uint8_t code[]={0xa9,0xc0,0x8d,0x0e,0xd4,0xad,0x0f,0xd4,0x60};
    CHECK(atari8_machine_load(m,0x2200,code,sizeof(code)));
    CHECK(atari8_machine_call(m,0x2200,100)==ATARI8_MACHINE_OK);
    CHECK(m->antic_nmien==0xc0 && m->cpu.a==0x1f);
    CHECK(m->master_cycles<248u*114u);
    CHECK(atari8_machine_idle(m,(uint32_t)(248u*114u-m->master_cycles))==ATARI8_MACHINE_OK);
    CHECK(m->antic_nmist==0x40 && !m->cpu.nmi_pending);
    CHECK(atari8_machine_call(m,0x2200,100)==ATARI8_MACHINE_OK);
    CHECK(m->cpu.a==0x5f);
    static const uint8_t clear[]={0x8d,0x0f,0xd4,0xad,0x0f,0xd4,0x60};
    CHECK(atari8_machine_load(m,0x2200,clear,sizeof(clear)));
    CHECK(atari8_machine_call(m,0x2200,100)==ATARI8_MACHINE_OK);
    CHECK(m->cpu.a==0x1f && !m->antic_nmist && m->antic_nmien==0xc0);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.intervals==1 && !m->cpu.nmi_pending);
}
static void test_shadow_load(void)
{
    uint8_t data[512];
    size_t n=fixture(data,false,false);
    /* An image segment straddles RAM and I/O. Loading it must not
     * perform writes to GTIA or change its output latch. */
    static const uint8_t block[]={0xff,0xcf,0x01,0xd0,0x11,0x22,0x33};
    memcpy(data+n,block,sizeof(block)); n+=sizeof(block);
    Atari8Sapb s;
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0xcfff]==0x11 && s.machine.ram[0xd000]==0x22);
    CHECK(s.machine.ram[0xd001]==0x33 && s.machine.console_latch==8);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    /* Entry points in MMIO remain explicitly unsupported. */
    CHECK(!memcmp(data+13,"INIT 2000",9));
    memcpy(data+18,"D000",4);
    Atari8SapbInfo i;
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_UNSUPPORTED);
}
static void test_overlong_player(void)
{
    uint8_t data[512];
    size_t n=fixture(data,false,false);
    Atari8Sapb s;
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    /* 256 x 256 nested countdowns: finite, but longer than one frame.
     * Use a smaller outer loop so execution fits within eight frames. */
    static const uint8_t loop[]={0xa2,40,0xa0,0,0x88,0xd0,0xfd,
                                 0xca,0xd0,0xf8,0x60};
    CHECK(atari8_machine_load(&s.machine,0x2100,loop,sizeof(loop)));
    uint64_t start=s.machine.master_cycles;
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.intervals==1 && s.missed_intervals==1);
    CHECK(s.machine.master_cycles-start==2u*s.info.interval);
    CHECK(s.machine.frames==(uint64_t)2*s.info.interval*48000u/s.machine.pcm.clock_hz);
}
static void test_refresh(void)
{
    Atari8Sapb s;
    uint8_t data[512], code[51];
    size_t n=fixture(data,false,false);
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    Atari8Machine *m=&s.machine;
    memset(code,0xea,50);
    code[50]=0x60;
    CHECK(atari8_machine_load(m,0x2200,code,sizeof(code)));
    unsigned align=(114u-(unsigned)(m->master_cycles%114u))%114u;
    CHECK(atari8_machine_idle(m,align)==ATARI8_MACHINE_OK);
    uint64_t start=m->master_cycles, cpu=m->cpu.cycles, stalls=m->refresh_stalls;
    CHECK(atari8_machine_call(m,0x2200,200)==ATARI8_MACHINE_OK);
    CHECK(m->cpu.cycles-cpu==106 && m->master_cycles-start==115);
    CHECK(m->refresh_stalls-stalls==9);
    stalls=m->refresh_stalls;
    CHECK(atari8_machine_idle(m,114)==ATARI8_MACHINE_OK);
    CHECK(m->refresh_stalls==stalls);
}
static void test_legacy_vbi_chain(void)
{
    uint8_t data[512];
    size_t n=fixture(data,false,false);
    Atari8Sapb s;
    /* Independent synthetic example. A player emits a POKEY write then
     * chains to the uninstalled previous VBI. A separate installer
     * saves VVBLKI into the JMP operand, but is not invoked by INIT. */
    static const uint8_t player[]={0xa9,0xaf,0x8d,0x01,0xd2,0x4c,0,0};
    memcpy(data+n-sizeof(player),player,sizeof(player));
    size_t original=n;
    static const uint8_t installer[]={0x00,0x23,0x0c,0x23,
        0xad,0x25,0x02,0x8d,0x07,0x21,
        0xad,0x24,0x02,0x8d,0x06,0x21,0x60};
    memcpy(data+n,installer,sizeof(installer)); n+=sizeof(installer);
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    uint8_t sp=s.machine.cpu.sp;
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.machine.sap_vbi_returns==1 && s.machine.cpu.sp==sp);
    CHECK(s.machine.ram[0]==0 && s.machine.ram[0x2106]==0 && s.machine.ram[0x2107]==0);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK && s.machine.sap_vbi_returns==2);
    /* The current VVBLKI can already point at PLAYER while the
     * saved previous vector (the JMP operand) remains uninstalled. */
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    s.machine.ram[0x224]=0x00;
    s.machine.ram[0x225]=0x21;
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.machine.sap_vbi_returns==1 && s.machine.cpu.sp==sp);
    CHECK(s.machine.ram[0x225]==0x21);
    /* Identical null jump without a VBI installer is still an error. */
    CHECK(atari8_sapb_open(&s,data,original,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME);
    CHECK(s.machine.status==ATARI8_MACHINE_BUDGET && !s.machine.sap_vbi_returns);
    /* An unrelated current VBI vector does not qualify. */
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    s.machine.ram[0x224]=1;
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME && !s.machine.sap_vbi_returns);
}
static void test_frame_restart(void)
{
    uint8_t data[512];
    size_t n=fixture(data,false,false);
    Atari8Sapb s;
    /* Finite work before an endless loop: each frame increments $80. */
    const uint8_t code[]={0xe6,0x80,0xa5,0x80,0x4c,0x04,0x21,0xea};
    memcpy(data+n-sizeof(code),code,sizeof(code));
    CHECK(atari8_sapb_open_mode(&s,data,n,0,48000,NULL,NULL,true)==ATARI8_SAPB_OK);
    uint8_t sp=s.machine.cpu.sp;
    uint64_t start=s.machine.master_cycles;
    for(unsigned i=0;i<5;++i) CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.intervals==5 && s.abandoned_calls==5 && s.machine.ram[0x80]==5);
    CHECK(s.machine.cpu.sp==(uint8_t)(sp-10));
    CHECK(s.machine.master_cycles>=start+5u*s.info.interval &&
          s.machine.master_cycles<start+5u*s.info.interval+10u);
    /* Returning code parks the CPU and does not leak stack entries. */
    n=fixture(data,false,false);
    CHECK(atari8_sapb_open_mode(&s,data,n,0,48000,NULL,NULL,true)==ATARI8_SAPB_OK);
    sp=s.machine.cpu.sp;
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.abandoned_calls==0 && s.machine.cpu.sp==sp);
    /* JAM is not swallowed in the alternate mode. */
    data[n-8]=0x02;
    CHECK(atari8_sapb_open_mode(&s,data,n,0,48000,NULL,NULL,true)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME);
    CHECK(s.machine.status==ATARI8_MACHINE_CPU_STOP);
}
static void test_sapc(void)
{
    uint8_t data[512];
    const char *header="SAP\r\nTYPE C\r\n\r\nMUSIC 3456\r\nPLAYER 2000\r\nSONGS 3\r\nDEFSONG 2\r\n";
    size_t n=strlen(header);
    memcpy(data,header,n);
    /* Independent synthetic C ABI fixture: entry table at $2000,
     * init at $2003, tick at $2006. Init records arguments and call
     * order. Tick increments a counter and drives primary POKEY. */
    static const uint8_t code[]={
        0xff,0xff,0x00,0x20,0x28,0x20,
        0xea,0xea,0xea, 0x4c,0x09,0x20, 0x4c,0x19,0x20,
        0x94,0x88,0xa4,0x90,0x99,0x80,0x00,0x8a,0x99,0x84,0x00,
        0xe6,0x90,0x60,0xea,0xea,
        0xe6,0x91,0xa5,0x91,0x8d,0x00,0xd2,
        0xa9,0xaf,0x8d,0x01,0xd2,0x60,0xea,0xea,0xea,
        0x56,0x34,0x56,0x34,0x42
    };
    memcpy(data+n,code,sizeof(code)); n+=sizeof(code);
    Atari8Sapb s;
    Atari8SapbInfo i;
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_OK);
    CHECK(i.type=='C' && i.music==0x3456 && i.player==0x2000);
    CHECK(atari8_sapb_open(&s,data,n,-1,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0x90]==2 && s.machine.ram[0x80]==0x70 &&
          s.machine.ram[0x81]==0 && s.machine.ram[0x84]==0x56 &&
          s.machine.ram[0x85]==2 && s.machine.ram[0xde]==0x34);
    CHECK(s.machine.frames==0);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0x91]==1 && s.machine.core.pokey[0].audf[0]==1);
    CHECK(atari8_sapb_open(&s,data,n,1,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0x85]==1);
    CHECK(atari8_sapb_open_mode(&s,data,n,0,48000,NULL,NULL,true)==ATARI8_SAPB_UNSUPPORTED);
    /* Missing music image, wrong header and out-of-range entry table. */
    CHECK(atari8_sapb_parse(data,n-5,&i)==ATARI8_SAPB_BAD_FORMAT);
    memcpy(data+15,"INIT ",5);
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_BAD_FORMAT);
    memcpy(data+15,"MUSIC",5);
    memcpy(data+34,"FFFD",4);
    CHECK(atari8_sapb_parse(data,n,&i)!=ATARI8_SAPB_OK);
}
static void test_sapd(void)
{
    uint8_t data[512];
    const char *header="SAP\r\nTYPE D\r\nINIT 2000\r\nSONGS 3\r\nDEFSONG 2\r\n";
    size_t n=strlen(header);
    memcpy(data,header,n);
    /* Save subsong, drive POKEY and repeatedly increment RAM without RTS. */
    static const uint8_t code[]={
        0xff,0xff,0x00,0x20,0x10,0x20,
        0x85,0x80,0xa9,71,0x8d,0x00,0xd2,
        0xa9,0xaf,0x8d,0x01,0xd2,
        0xe6,0x81,0x4c,0x0c,0x20
    };
    memcpy(data+n,code,sizeof(code)); n+=sizeof(code);
    Atari8Sapb s;
    Atari8SapbInfo i;
    Capture c={0};
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_OK);
    CHECK(i.type=='D' && i.init==0x2000 && i.default_song==2);
    CHECK(atari8_sapb_open(&s,data,n,-1,48000,capture,&c)==ATARI8_SAPB_OK);
    CHECK(s.machine.cpu.pc==0x2000 && s.machine.cpu.a==2);
    CHECK(!(s.machine.cpu.p & ATARI8_CPU_I) && s.machine.frames==0);
    for (unsigned j=1;j<=5;++j) {
        CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
        CHECK(s.machine.master_cycles>=j*i.interval &&
              s.machine.master_cycles<j*i.interval+114u);
    }
    CHECK(s.machine.ram[0x80]==2 && c.left>0 && s.intervals==5);
    CHECK(c.frames==s.machine.frames && s.machine.refresh_stalls>0);
    CHECK(atari8_sapb_open_mode(&s,data,n,0,48000,NULL,NULL,true)==ATARI8_SAPB_UNSUPPORTED);
    /* Headless setup and real one-bit speaker transitions. No POKEY
     * audio writes: the only sound source is CONSOL bit 3. */
    static const uint8_t speaker[]={
        0xff,0xff,0x00,0x20,0x1c,0x20,
        0xa9,0,0x8d,0x00,0xd4,          /* DMACTL=0 */
        0xa9,0x3c,0x8d,0x02,0xd3,      /* PACTL=3C */
        0xa9,0,0x8d,0x1f,0xd0,         /* speaker on */
        0x8d,0x0a,0xd4,                /* WSYNC */
        0xa9,8,0x8d,0x1f,0xd0,         /* speaker off */
        0x8d,0x0a,0xd4,                /* WSYNC */
        0x4c,0x0a,0x20
    };
    n=strlen(header);
    memcpy(data,header,n);
    memcpy(data+n,speaker,sizeof(speaker)); n+=sizeof(speaker);
    c=(Capture){0};
    CHECK(atari8_sapb_open(&s,data,n,-1,48000,capture,&c)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.machine.pia_pactl==0x3c && s.machine.antic_dmactl==0);
    CHECK(s.machine.sap_pokey_audio_writes==0 && s.machine.sap_consol_edges>0);
    CHECK(c.left>0 && c.right==0);
    /* PORTB: default flat RAM, data latch, mirrored address, DDR,
     * pulled-up inputs and no changes to the guest memory map. */
    static const uint8_t portb[]={
        0xad,0x01,0xd3,0x85,0x82,       /* initial FE */
        0xa9,0xfd,0x8d,0x01,0xd3,
        0xad,0x05,0xd3,0x85,0x83,       /* mirrored FD */
        0xa9,0x38,0x8d,0x03,0xd3,       /* select DDR */
        0xa9,0x0f,0x8d,0x01,0xd3,
        0xad,0x01,0xd3,0x85,0x84,
        0xa9,0x3c,0x8d,0x03,0xd3,       /* select data */
        0xa9,0,0x8d,0x01,0xd3,
        0xad,0x01,0xd3,0x85,0x85,       /* input pins high */
        0xad,0x00,0xe0,0x85,0x86,       /* same RAM */
        0x60
    };
    s.machine.ram[0xe000]=0x42;
    CHECK(atari8_machine_load(&s.machine,0x2200,portb,sizeof(portb)));
    CHECK(atari8_machine_call(&s.machine,0x2200,1000)==ATARI8_MACHINE_OK);
    CHECK(s.machine.ram[0x82]==0xfe && s.machine.ram[0x83]==0xfd);
    CHECK(s.machine.ram[0x84]==0x0f && s.machine.ram[0x85]==0xf0);
    CHECK(s.machine.ram[0x86]==0x42 && s.machine.ram[0xe000]==0x42);
    /* Headless video configuration and disconnected PIA port A. */
    static const uint8_t video_pia[]={
        0xa9,0x34,0x8d,0x02,0xd4,       /* DLISTL */
        0xa9,0x12,0x8d,0x03,0xd4,       /* DLISTH */
        0xa9,0x20,0x8d,0x00,0xd4,       /* request display DMA */
        0xad,0x02,0xd4,0x85,0x87,       /* write-only, open bus */
        0xad,0x06,0xd4,0x85,0x8a,       /* unused register */
        0xad,0x18,0xd4,0x85,0x8b,       /* mirrored unused register */
        0xa9,0x38,0x8d,0x02,0xd3,       /* DDRA */
        0xa9,0x0f,0x8d,0x00,0xd3,
        0xad,0x00,0xd3,0x85,0x88,
        0xa9,0x3c,0x8d,0x02,0xd3,       /* select data */
        0xa9,0x05,0x8d,0x00,0xd3,
        0xad,0x04,0xd3,0x85,0x89,       /* mirror, input pins high */
        0x60
    };
    CHECK(atari8_machine_load(&s.machine,0x2300,video_pia,sizeof(video_pia)));
    CHECK(atari8_machine_call(&s.machine,0x2300,1000)==ATARI8_MACHINE_OK);
    CHECK(s.machine.antic_video[2]==0x34 && s.machine.antic_video[3]==0x12);
    CHECK(s.machine.antic_dmactl==0x20 && s.machine.sap_video_dma_requests==1);
    CHECK(s.machine.ram[0x87]==0xff && s.machine.ram[0x88]==0x0f &&
          s.machine.ram[0x89]==0xf5);
    CHECK(s.machine.ram[0x8a]==0xff && s.machine.ram[0x8b]==0xff);
    /* Timer-only SAP profile: no external keyboard or SIO events. */
    static const uint8_t headless_irq[]={
        0xad,0x0c,0xd4,0x85,0x8c,
        0xad,0x0d,0xd4,0x85,0x8d,
        0xa9,0xfe,0x8d,0x0e,0xd2,
        0xa9,0x23,0x8d,0x0f,0xd2,
        0x60
    };
    s.machine.cpu.p |= ATARI8_CPU_I;
    CHECK(atari8_machine_load(&s.machine,0x2400,headless_irq,sizeof(headless_irq)));
    CHECK(atari8_machine_call(&s.machine,0x2400,1000)==ATARI8_MACHINE_OK);
    CHECK(s.machine.ram[0x8c]==0xff && s.machine.ram[0x8d]==0xff);
    CHECK(s.machine.core.pokey[0].irq_enable==6 &&
          s.machine.core.pokey[0].skctl==0x23);
    CHECK(s.machine.sap_non_timer_irq_requests==1 &&
          s.machine.sap_serial_clock_requests==1);

    /* Periodic D PLAYER runs before the first INIT instruction,
     * changes all registers/flags, writes RAM and returns via RTS.
     * INIT must resume with its original registers and stack. */
    const char *with_player="SAP\r\nTYPE D\r\nINIT 2000\r\nPLAYER 2100\r\nFASTPLAY 78\r\nSONGS 3\r\nDEFSONG 2\r\n";
    static const uint8_t periodic[]={
        0xff,0xff,0x00,0x20,0x04,0x20,
        0x85,0x80,0x4c,0x02,0x20, /* INIT saves A then loops */
        0x00,0x21,0x0b,0x21,
        0xe6,0x81,0xa9,0x99,0xa2,0x88,0xa0,0x77,
        0x38,0x78,0xf8,0x60
    };
    n=strlen(with_player);
    memcpy(data,with_player,n);
    memcpy(data+n,periodic,sizeof(periodic)); n+=sizeof(periodic);
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_OK && i.has_player);
    CHECK(atari8_sapb_open(&s,data,n,-1,48000,NULL,NULL)==ATARI8_SAPB_OK);
    s.machine.cpu.x=0x34; s.machine.cpu.y=0x56;
    s.machine.cpu.p=ATARI8_CPU_U|ATARI8_CPU_V;
    for(unsigned j=1;j<=5;++j) {
        CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
        CHECK(s.continuous_player_calls==j && s.machine.ram[0x81]==j);
        CHECK(s.machine.ram[0x80]==2 && s.machine.cpu.pc==0x2002);
        CHECK(s.machine.cpu.a==2 && s.machine.cpu.x==0x34 &&
              s.machine.cpu.y==0x56 && s.machine.cpu.sp==0xff);
        CHECK(s.machine.cpu.p==(ATARI8_CPU_U|ATARI8_CPU_V));
        CHECK(s.machine.master_cycles>=j*i.interval &&
              s.machine.master_cycles<j*i.interval+114u);
    }
    CHECK(!s.missed_intervals);
    CHECK(atari8_sapb_open_mode(&s,data,n,-1,48000,NULL,NULL,true)==ATARI8_SAPB_OK);
    for(unsigned j=1;j<=5;++j) {
        CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
        CHECK(s.continuous_player_calls==j && s.machine.ram[0x81]==j);
        CHECK(s.machine.ram[0x80]==2 && s.machine.cpu.pc==0x2002);
        CHECK(s.machine.cpu.a==2 && s.machine.cpu.sp==0xff);
    }
    CHECK(!s.abandoned_calls && !s.missed_intervals);
    /* Pending timer IRQ must not preempt periodic service entry.
     * The handler below acknowledges IRQEN, records its entry flags,
     * and returns. The original INIT flags must be restored. */
    static const uint8_t irq_player[]={
        0x08,0x68,0x85,0x82, /* PHP, PLA, STA $82 */
        0xa9,0,0x8d,0x0e,0xd2,
        0xe6,0x81,0x60
    };
    for(unsigned mode=0;mode<2;++mode) {
        CHECK(atari8_sapb_open_mode(&s,data,n,-1,48000,NULL,NULL,mode!=0)==ATARI8_SAPB_OK);
        CHECK(atari8_machine_load(&s.machine,0x2100,irq_player,sizeof(irq_player)));
        s.machine.ram[0x2200]=0x40; /* RTI without acknowledge */
        s.machine.ram[0xfffe]=0;
        s.machine.ram[0xffff]=0x22;
        s.machine.core.pokey[0].irq_enable=1;
        s.machine.core.pokey[0].irq_pending=1;
        CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
        CHECK(s.machine.ram[0x82]&ATARI8_CPU_I);
        CHECK(s.machine.ram[0x81]==1 && s.machine.ram[0x80]==2);
        CHECK(!s.machine.core.pokey[0].irq_pending &&
              !(s.machine.cpu.p&ATARI8_CPU_I));
        CHECK(s.machine.cpu.sp==0xff && !s.abandoned_calls);
    }
    CHECK(atari8_sapb_parse(data,n-1,&i)==ATARI8_SAPB_BAD_FORMAT);
    /* A non-returning PLAYER faults rather than hanging/reentering. */
    data[n-1]=0x4c;
    data[n++]=0x0b; data[n++]=0x21;
    data[strlen(with_player)+13]=0x0d; /* extend second segment */
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME);
    CHECK(s.machine.status==ATARI8_MACHINE_BUDGET);
    /* The explicit experimental scheduler permits unfinished calls,
     * but retains guest state and advances a bounded amount of time. */
    CHECK(atari8_sapb_open_mode(&s,data,n,0,48000,NULL,NULL,true)==ATARI8_SAPB_OK);
    for(unsigned j=1;j<=3;++j) {
        CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
        CHECK(s.abandoned_calls==j && s.machine.ram[0x81]==j);
        CHECK(s.machine.cpu.sp==(uint8_t)(0xffu-2u*j));
    }
    CHECK(s.machine.master_cycles>=3u*s.info.interval &&
          s.machine.master_cycles<3u*s.info.interval+114u);
    /* The alternate scheduler never swallows a CPU stop. */
    data[strlen(with_player)+15]=0x02;
    CHECK(atari8_sapb_open_mode(&s,data,n,0,48000,NULL,NULL,true)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME);
    CHECK(s.machine.status==ATARI8_MACHINE_CPU_STOP);
}
static void test_saps(void)
{
    uint8_t data[512];
    const char *header="SAP\r\nTYPE S\r\nINIT 2000\r\nPLAYER FFFF\r\nSONGS 3\r\nDEFSONG 2\r\n";
    static const uint8_t code[]={
        0xff,0xff,0x00,0x20,0x0e,0x20,
        0x85,0x80,0xa9,71,0x8d,0x00,0xd2,
        0xa9,0xaf,0x8d,0x01,0xd2,
        0x4c,0x0c,0x20
    };
    size_t n=strlen(header);
    memcpy(data,header,n);
    memcpy(data+n,code,sizeof(code)); n+=sizeof(code);
    Atari8Sapb s;
    Atari8SapbInfo i;
    Capture c={0};
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_OK);
    CHECK(i.type=='S' && i.has_player && i.player==0xffffu &&
          i.interval==78u*114u);
    CHECK(atari8_sapb_open(&s,data,n,-1,48000,capture,&c)==ATARI8_SAPB_OK);
    CHECK(s.machine.cpu.pc==0x2000 && s.machine.cpu.a==2 &&
          !s.machine.frames);
    s.machine.ram[0x45]=2;
    s.machine.ram[0xb07b]=0xff;
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0x45]==1 && s.machine.ram[0xb07b]==0xff);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0x45]==0 && s.machine.ram[0xb07b]==0);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    CHECK(s.machine.ram[0x45]==0xff && s.machine.ram[0xb07b]==0);
    CHECK(s.machine.ram[0x80]==2 && s.machine.cpu.sp==0xff &&
          s.machine.cpu.pc==0x200c && !s.continuous_player_calls);
    CHECK(c.left>0 && c.frames==s.machine.frames);
    CHECK(s.machine.master_cycles>=3u*i.interval &&
          s.machine.master_cycles<3u*i.interval+114u);
    CHECK(atari8_sapb_open_mode(&s,data,n,0,48000,NULL,NULL,true)==ATARI8_SAPB_UNSUPPORTED);
    CHECK(atari8_sapb_parse(data,n-1,&i)==ATARI8_SAPB_BAD_FORMAT);
    /* No PLAYER, explicit FASTPLAY and NTSC are accepted. */
    header="SAP\r\nTYPE S\r\nINIT 2000\r\nNTSC\r\nFASTPLAY 39\r\n";
    n=strlen(header);
    memcpy(data,header,n);
    memcpy(data+n,code,sizeof(code)); n+=sizeof(code);
    CHECK(atari8_sapb_parse(data,n,&i)==ATARI8_SAPB_OK);
    CHECK(!i.has_player && i.ntsc && i.interval==39u*114u);
    data[strlen(header)+6]=0x02;
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME);
    CHECK(s.machine.status==ATARI8_MACHINE_CPU_STOP);
}
static void test_time_metadata(void)
{
    uint8_t data[512];
    size_t n=fixture(data,false,false);
    Atari8SapbInfo info;
    CHECK(atari8_sapb_parse(data,n,&info)==ATARI8_SAPB_OK);
    CHECK(info.time[0].status==ATARI8_SAP_TIME_ABSENT);
    const char tags[]="TIME 1:02.3 LOOP\r\nTIME 00:00\r\nTIME 12:60\r\n";
    size_t pos=info.binary_offset, len=sizeof(tags)-1;
    memmove(data+pos+len,data+pos,n-pos);
    memcpy(data+pos,tags,len); n+=len;
    CHECK(atari8_sapb_parse(data,n,&info)==ATARI8_SAPB_OK);
    CHECK(info.time[0].status==ATARI8_SAP_TIME_VALID &&
          info.time[0].duration_ms==62300u && info.time[0].loop);
    CHECK(info.time[1].status==ATARI8_SAP_TIME_VALID &&
          info.time[1].duration_ms==0 && !info.time[1].loop);
    CHECK(info.time[2].status==ATARI8_SAP_TIME_INVALID);
    Atari8Sapb s;
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(s.info.time[0].duration_ms==62300u);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK);
    static const char *const bad[]={"","123:00","0:0","0:60","0:01.",
                                    "0:01.1234","0:01 LOOP ","-1:00"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)
        CHECK(atari8_sap_time_parse((const uint8_t*)bad[i],strlen(bad[i])).status==
              ATARI8_SAP_TIME_INVALID);
    static const char max[]="99:59.999";
    Atari8SapTime t=atari8_sap_time_parse((const uint8_t*)max,sizeof(max)-1);
    CHECK(t.status==ATARI8_SAP_TIME_VALID && t.duration_ms==5999999u);
}

static void test_text_metadata(void)
{
    uint8_t data[1024];
    size_t n=fixture(data,false,false);
    Atari8SapbInfo info;
    CHECK(atari8_sapb_parse(data,n,&info)==ATARI8_SAPB_OK);
    CHECK(!info.metadata.name.present && !info.metadata.author.present &&
          !info.metadata.date.present);
    static const char tags[]="NAME \"Old\"\r\nAUTHOR \"Test author\"\r\nDATE \"1992\"\r\nNAME \"New title\"\r\n";
    size_t pos=info.binary_offset,len=sizeof(tags)-1;
    memmove(data+pos+len,data+pos,n-pos);
    memcpy(data+pos,tags,len); n+=len;
    Atari8Sapb s;
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    memset(data,0,n);
    CHECK(s.info.metadata.name.present && !s.info.metadata.name.truncated &&
          !strcmp(s.info.metadata.name.value,"New title"));
    CHECK(!strcmp(s.info.metadata.author.value,"Test author") &&
          !strcmp(s.info.metadata.date.value,"1992"));
    uint8_t long_value[302];
    memset(long_value,'x',sizeof(long_value));
    long_value[0]=long_value[301]='"';
    Atari8SapText t;
    CHECK(atari8_sap_text_parse(&t,long_value,sizeof(long_value)));
    CHECK(t.present && t.truncated && strlen(t.value)==255u);
    CHECK(atari8_sap_text_parse(&t,long_value,257u)==false);
    long_value[256]='"';
    CHECK(atari8_sap_text_parse(&t,long_value,257u));
    CHECK(!t.truncated && strlen(t.value)==255u);
    CHECK(atari8_sap_text_parse(&t,(const uint8_t*)"\"\"",2));
    CHECK(t.present && !t.truncated && t.value[0]=='\0');
}

static void test_declared_eof(void)
{
    uint8_t data[512];
    Atari8Sapb s;
    Atari8SapbInfo info;
    for (unsigned stereo=0;stereo<2;++stereo) {
        size_t n=fixture(data,stereo!=0,stereo!=0);
        CHECK(atari8_sapb_open(&s,data,n,-1,48000,NULL,NULL)==ATARI8_SAPB_OK);
        CHECK(!atari8_sapb_enable_declared_eof(&s));
        CHECK(atari8_sapb_parse(data,n,&info)==ATARI8_SAPB_OK);
        static const char tags[]="TIME 00:00\r\nTIME 00:60\r\nTIME 00:00.025 LOOP\r\n";
        size_t pos=info.binary_offset,len=sizeof(tags)-1;
        memmove(data+pos+len,data+pos,n-pos);
        memcpy(data+pos,tags,len); n+=len;
        for (unsigned mode=0;mode<2;++mode) {
            Capture c={0};
            CHECK(atari8_sapb_open_mode(&s,data,n,-1,44100,capture,&c,mode!=0)==ATARI8_SAPB_OK);
            CHECK(s.selected_song==2 && atari8_sapb_enable_declared_eof(&s));
            Atari8SapbResult r=ATARI8_SAPB_OK;
            for(unsigned i=0;i<10 && r==ATARI8_SAPB_OK;++i)
                r=atari8_sapb_interval(&s);
            CHECK(r==ATARI8_SAPB_EOF && c.frames==1102u);
            CHECK(atari8_sapb_output_frames(&s)==1102u);
            uint64_t cycle=s.machine.master_cycles;
            CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_EOF &&
                  c.frames==1102u && s.machine.master_cycles==cycle);
        }
        Capture c={0};
        CHECK(atari8_sapb_open(&s,data,n,0,48000,capture,&c)==ATARI8_SAPB_OK);
        CHECK(atari8_sapb_enable_declared_eof(&s));
        CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_EOF && !c.frames && !s.intervals);
        CHECK(atari8_sapb_open(&s,data,n,1,48000,NULL,NULL)==ATARI8_SAPB_OK);
        CHECK(!atari8_sapb_enable_declared_eof(&s));
        CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_OK && s.machine.frames>0);
        CHECK(!atari8_sapb_enable_declared_eof(&s));
        CHECK(atari8_sapb_output_frames(&s)==s.machine.frames);
        CHECK(atari8_sapb_open(&s,data,n,-1,48000,NULL,NULL)==ATARI8_SAPB_OK);
        CHECK(atari8_sapb_enable_declared_eof(&s));
        Atari8SapbResult r=ATARI8_SAPB_OK;
        for(unsigned i=0;i<10 && r==ATARI8_SAPB_OK;++i)
            r=atari8_sapb_interval(&s);
        CHECK(r==ATARI8_SAPB_EOF && atari8_sapb_output_frames(&s)==1200u);
    }
    const char *headers[]={
        "SAP\r\nTYPE C\r\nMUSIC 2000\r\nPLAYER 2000\r\nTIME 00:00.023\r\n",
        "SAP\r\nTYPE D\r\nINIT 2000\r\nTIME 00:00.023\r\n",
        "SAP\r\nTYPE S\r\nINIT 2000\r\nTIME 00:00.023\r\n"
    };
    const uint8_t ccode[]={0xff,0xff,0,0x20,6,0x20,0xea,0xea,0xea,0x60,0xea,0xea,0x60};
    const uint8_t loop[]={0xff,0xff,0,0x20,2,0x20,0x4c,0,0x20};
    for(unsigned k=0;k<3;++k) {
        size_t n=strlen(headers[k]), size=k?sizeof(loop):sizeof(ccode);
        memcpy(data,headers[k],n);
        memcpy(data+n,k?loop:ccode,size); n+=size;
        Capture c={0};
        CHECK(atari8_sapb_open(&s,data,n,0,48000,capture,&c)==ATARI8_SAPB_OK);
        CHECK(atari8_sapb_enable_declared_eof(&s));
        Atari8SapbResult r=ATARI8_SAPB_OK;
        for(unsigned i=0;i<10 && r==ATARI8_SAPB_OK;++i)
            r=atari8_sapb_interval(&s);
        CHECK(r==ATARI8_SAPB_EOF && c.frames==1104u);
        CHECK(atari8_sapb_output_frames(&s)==1104u);
        CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_EOF && c.frames==1104u);
    }
}

typedef struct {
    int16_t samples[2048][2];
    size_t n;
} StreamReference;

static void capture_reference(void *ctx,const int16_t frame[2])
{
    StreamReference *r=ctx;
    CHECK(r->n<2048u);
    r->samples[r->n][0]=frame[0];
    r->samples[r->n][1]=frame[1];
    ++r->n;
}

static void check_stream(const uint8_t *data,size_t n,int song,bool restart)
{
    Atari8Sapb low;
    StreamReference ref={0};
    CHECK(atari8_sapb_open_mode(&low,data,n,song,44100,capture_reference,&ref,restart)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_enable_declared_eof(&low));
    Atari8SapbResult r=ATARI8_SAPB_OK;
    for(unsigned i=0;i<20 && r==ATARI8_SAPB_OK;++i)
        r=atari8_sapb_interval(&low);
    CHECK(r==ATARI8_SAPB_EOF && ref.n==1102u);
    Atari8SapbStream *s=NULL;
    uint8_t owned_test[512];
    CHECK(n<=sizeof(owned_test));
    memcpy(owned_test,data,n);
    CHECK(atari8_sapb_stream_open(&s,owned_test,n,song,44100,true,restart)==ATARI8_SAPB_OK);
    memset(owned_test,0,sizeof(owned_test));
    uint64_t length;
    CHECK(atari8_sapb_stream_length(s,&length) && length==ref.n);
    CHECK(atari8_sapb_stream_info(s)->type==low.info.type);
    CHECK(atari8_sapb_stream_tell(s)==0);
    int16_t pcm[97][2];
    size_t got;
    CHECK(atari8_sapb_stream_read(s,NULL,0,&got)==ATARI8_SAPB_OK && !got);
    size_t pos=0;
    do {
        r=atari8_sapb_stream_read(s,pcm,97,&got);
        CHECK(got<=ref.n-pos);
        CHECK(!memcmp(pcm,ref.samples+pos,got*sizeof(*pcm)));
        pos+=got;
        CHECK(atari8_sapb_stream_tell(s)==pos);
    } while(r==ATARI8_SAPB_OK);
    CHECK(r==ATARI8_SAPB_EOF && pos==ref.n);
    const uint64_t targets[]={0,17,1000,31,1102,1101,0};
    for(unsigned k=0;k<sizeof(targets)/sizeof(targets[0]);++k) {
        uint64_t target=targets[k];
        CHECK(atari8_sapb_stream_seek(s,target)==ATARI8_SAPB_OK);
        CHECK(atari8_sapb_stream_tell(s)==target);
        CHECK(atari8_sapb_stream_seek(s,length+1)==ATARI8_SAPB_BAD_FORMAT);
        CHECK(atari8_sapb_stream_tell(s)==target);
        size_t expected=length-target<97u ? (size_t)(length-target) : 97u;
        r=atari8_sapb_stream_read(s,pcm,97,&got);
        CHECK(got==expected && r==(expected<97u?ATARI8_SAPB_EOF:ATARI8_SAPB_OK));
        CHECK(!memcmp(pcm,ref.samples+target,got*sizeof(*pcm)));
    }
    atari8_sapb_stream_close(s);
}

static void test_stream(void)
{
    uint8_t data[512];
    for(unsigned stereo=0;stereo<2;++stereo) {
        size_t n=fixture(data,stereo!=0,stereo!=0);
        Atari8SapbInfo info;
        CHECK(atari8_sapb_parse(data,n,&info)==ATARI8_SAPB_OK);
        static const char tags[]="TIME 00:00\r\nTIME 00:60\r\nTIME 00:00.025 LOOP\r\n";
        size_t pos=info.binary_offset,len=sizeof(tags)-1;
        memmove(data+pos+len,data+pos,n-pos);
        memcpy(data+pos,tags,len); n+=len;
        check_stream(data,n,-1,false);
        check_stream(data,n,-1,true);
        Atari8SapbStream *s=NULL;
        size_t got; int16_t pcm[1][2]; uint64_t length;
        CHECK(atari8_sapb_stream_open(&s,data,n,0,48000,true,false)==ATARI8_SAPB_OK);
        CHECK(atari8_sapb_stream_length(s,&length) && !length);
        CHECK(atari8_sapb_stream_read(s,pcm,1,&got)==ATARI8_SAPB_EOF && !got);
        atari8_sapb_stream_close(s);
        CHECK(atari8_sapb_stream_open(&s,data,n,1,48000,true,false)==ATARI8_SAPB_OK);
        CHECK(!atari8_sapb_stream_length(s,&length));
        CHECK(atari8_sapb_stream_read(s,pcm,1,&got)==ATARI8_SAPB_OK && got==1);
        atari8_sapb_stream_close(s);
    }
    const char *headers[]={
        "SAP\r\nTYPE C\r\nMUSIC 2000\r\nPLAYER 2000\r\nTIME 00:00.025\r\n",
        "SAP\r\nTYPE D\r\nINIT 2000\r\nTIME 00:00.025\r\n",
        "SAP\r\nTYPE S\r\nINIT 2000\r\nTIME 00:00.025\r\n",
        "SAP\r\nTYPE D\r\nINIT 2000\r\nPLAYER 2100\r\nTIME 00:00.025\r\n"
    };
    const uint8_t ccode[]={0xff,0xff,0,0x20,11,0x20,
        0xea,0xea,0xea,0x60,0xea,0xea,0xa9,0xaf,0x8d,1,0xd2,0x60};
    const uint8_t loop[]={0xff,0xff,0,0x20,7,0x20,
        0xa9,0xaf,0x8d,1,0xd2,0x4c,5,0x20};
    const uint8_t player[]={0,0x21,0,0x21,0x60};
    for(unsigned k=0;k<4;++k) {
        size_t n=strlen(headers[k]), size=k?sizeof(loop):sizeof(ccode);
        memcpy(data,headers[k],n);
        memcpy(data+n,k?loop:ccode,size); n+=size;
        if(k==3) { memcpy(data+n,player,sizeof(player)); n+=sizeof(player); }
        check_stream(data,n,0,false);
        if(k==3) check_stream(data,n,0,true);
    }
    /* Invalid source and a JAMming PLAYER: errors cannot be concealed
     * by buffering or transactional seek. */
    Atari8SapbStream *s=NULL;
    CHECK(atari8_sapb_stream_open(&s,data,1,0,48000,true,false)==ATARI8_SAPB_BAD_FORMAT && !s);
    size_t n=fixture(data,false,false);
    data[n-8]=0x02;
    CHECK(atari8_sapb_stream_open(&s,data,n,0,48000,false,false)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_stream_seek(s,1000)==ATARI8_SAPB_RUNTIME);
    CHECK(atari8_sapb_stream_tell(s)==0);
    int16_t pcm[1000][2]; size_t got;
    CHECK(atari8_sapb_stream_read(s,pcm,1000,&got)==ATARI8_SAPB_RUNTIME);
    CHECK(atari8_sapb_stream_tell(s)==got);
    CHECK(atari8_sapb_stream_read(s,pcm,1000,&got)==ATARI8_SAPB_RUNTIME && !got);
    atari8_sapb_stream_close(s);
}

int main(void)
{
    test_stream();
    test_declared_eof();
    test_text_metadata();
    test_time_metadata();
    test_play(false); test_play(true); test_bad(); test_host_antic(); test_shadow_load(); test_overlong_player(); test_refresh(); test_legacy_vbi_chain(); test_frame_restart(); test_sapc(); test_sapd(); test_saps();
    puts("PASS: experimental Atari8 SAP-B/C/D/S");
    return 0;
}
