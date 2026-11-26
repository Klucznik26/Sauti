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
int main(void)
{
    test_play(false); test_play(true); test_bad(); test_host_antic(); test_shadow_load();
    puts("PASS: experimental Atari8 SAP-B");
    return 0;
}
