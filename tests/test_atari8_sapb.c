#include "../src/decoders/retro/atari8/atari8_sapb.h"
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
    /* Do not silently accept active graphics DMA. */
    data[strlen(header)+7]=0x20;
    CHECK(atari8_sapb_open(&s,data,n,-1,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME);
    CHECK(s.machine.fault_address==0xd400);
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
    CHECK(atari8_sapb_parse(data,n-1,&i)==ATARI8_SAPB_BAD_FORMAT);
    /* A non-returning PLAYER faults rather than hanging/reentering. */
    data[n-1]=0x4c;
    data[n++]=0x0b; data[n++]=0x21;
    data[strlen(with_player)+13]=0x0d; /* extend second segment */
    CHECK(atari8_sapb_open(&s,data,n,0,48000,NULL,NULL)==ATARI8_SAPB_OK);
    CHECK(atari8_sapb_interval(&s)==ATARI8_SAPB_RUNTIME);
    CHECK(s.machine.status==ATARI8_MACHINE_BUDGET);
}
int main(void)
{
    test_play(false); test_play(true); test_bad(); test_host_antic(); test_shadow_load(); test_overlong_player(); test_refresh(); test_legacy_vbi_chain(); test_frame_restart(); test_sapc(); test_sapd();
    puts("PASS: experimental Atari8 SAP-B/C/D");
    return 0;
}
