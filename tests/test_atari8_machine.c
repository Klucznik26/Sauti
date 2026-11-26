#include "../src/decoders/retro/atari8/atari8_machine.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

typedef struct { uint64_t count; unsigned left, right; } Capture;
static void capture(void *ctx, const int16_t frame[2])
{
    Capture *c = ctx;
    ++c->count;
    c->left += frame[0] != 0;
    c->right += frame[1] != 0;
}

static void test_audio(bool stereo)
{
    Atari8Machine m;
    Capture out = {0};
    CHECK(atari8_machine_init(&m,false,stereo,48000,false,capture,&out));
    /* LDA #71; STA $D200; LDA #$AF; STA $D201;
       LDA #95; STA $D210; LDA #$A9; STA $D211;
       STA $D209; RTS. */
    static const uint8_t program[] = {
        0xa9,71,0x8d,0x00,0xd2,0xa9,0xaf,0x8d,0x01,0xd2,
        0xa9,95,0x8d,0x10,0xd2,0xa9,0xa9,0x8d,0x11,0xd2,
        0x8d,0x09,0xd2,0x60
    };
    CHECK(atari8_machine_load(&m,0x2000,program,sizeof(program)));
    CHECK(atari8_machine_call(&m,0x2000,100)==ATARI8_MACHINE_OK);
    CHECK(m.cpu.pc==0xffff && m.cpu.sp==0xfd);
    CHECK(m.cpu.cycles==34 && m.master_cycles==34);
    CHECK(m.core.pokey[0].audf[0]==(stereo?71:95));
    CHECK(m.core.pokey[0].audc[0]==(stereo?0xaf:0xa9));
    if (stereo) {
        CHECK(m.core.pokey[1].audf[0]==95);
        CHECK(m.core.pokey[1].audc[0]==0xa9);
    }
    CHECK(atari8_machine_idle(&m,1773447u-34u)==ATARI8_MACHINE_OK);
    CHECK(m.master_cycles==1773447u && m.cpu.cycles==34);
    CHECK(out.count==48000 && m.frames==out.count);
    CHECK(out.left>1000 && (stereo?out.right>1000:out.right==0));
    CHECK(atari8_machine_call(&m,0x2000,34)==ATARI8_MACHINE_OK);
    CHECK(m.cpu.cycles==68 && m.master_cycles==1773447u+34u);
}

static void test_bus_cycle(void)
{
    Atari8Machine m;
    CHECK(atari8_machine_init(&m,false,false,48000,false,NULL,NULL));
    static const uint8_t program[]={0xa9,0x1f,0x8d,0x01,0xd2,0xad,0x0e,0xd2,0x60};
    CHECK(atari8_machine_load(&m,0x2000,program,sizeof(program)));
    m.cpu.pc=0x2000;
    CHECK(atari8_machine_step(&m)==ATARI8_MACHINE_OK);
    CHECK(m.master_cycles==2 && m.core.pokey[0].audc[0]==0);
    CHECK(atari8_machine_step(&m)==ATARI8_MACHINE_OK);
    CHECK(m.master_cycles==6 && m.core.pokey[0].audc[0]==0x1f);
    CHECK(m.pcm.area[0]==0); /* new level begins on the next cycle */
    CHECK(atari8_machine_step(&m)==ATARI8_MACHINE_OK);
    CHECK(m.master_cycles==10 && m.cpu.a==0xff);
    CHECK(m.pcm.area[0]==4u*15u*48000u);
}

static void test_faults(void)
{
    Atari8Machine m;
    static const uint8_t loop[]={0x4c,0x00,0x20};
    CHECK(!atari8_machine_init(&m,false,false,0,false,NULL,NULL));
    CHECK(atari8_machine_init(&m,false,false,44100,true,NULL,NULL));
    CHECK(!atari8_machine_load(&m,0xffff,loop,sizeof(loop)));
    CHECK(!atari8_machine_load(&m,0xcfff,loop,sizeof(loop)));
    CHECK(atari8_machine_load(&m,0x2000,loop,sizeof(loop)));
    CHECK(atari8_machine_call(&m,0x2000,10)==ATARI8_MACHINE_BUDGET);
    CHECK(m.master_cycles==12);
    CHECK(atari8_machine_step(&m)==ATARI8_MACHINE_BUDGET && m.master_cycles==12);
    CHECK(atari8_machine_init(&m,false,false,44100,true,NULL,NULL));
    static const uint8_t io[]={0xad,0x00,0xd4,0x60};
    CHECK(atari8_machine_load(&m,0x2000,io,sizeof(io)));
    CHECK(atari8_machine_call(&m,0x2000,100)==ATARI8_MACHINE_IO);
    CHECK(m.fault_address==0xd400 && !m.fault_write && m.master_cycles==4);
    CHECK(atari8_machine_init(&m,false,false,44100,true,NULL,NULL));
    static const uint8_t jam[]={0x02};
    CHECK(atari8_machine_load(&m,0x2000,jam,sizeof(jam)));
    CHECK(atari8_machine_call(&m,0x2000,100)==ATARI8_MACHINE_CPU_STOP);
    CHECK(m.cpu.jammed && m.master_cycles==1);
}

static void test_indexed_pokey_store(void)
{
    Atari8Machine m;
    CHECK(atari8_machine_init(&m,false,true,48000,false,NULL,NULL));
    /* Indexed STA performs a read of ALLPOT before writing AUDCTL,
     * and a POT read before writing AUDC. Neither must fault. */
    static const uint8_t code[] = {
        0xa2,8,0xa9,0x40,0x9d,0x00,0xd2,
        0xa2,1,0xa9,0xaf,0x9d,0x10,0xd2,0x60
    };
    CHECK(atari8_machine_load(&m,0x2000,code,sizeof(code)));
    CHECK(atari8_machine_call(&m,0x2000,24)==ATARI8_MACHINE_OK);
    CHECK(m.master_cycles==24 && m.core.pokey[0].audctl==0x40);
    CHECK(m.core.pokey[1].audc[0]==0xaf);
}
static void test_headless_video(void)
{
    Atari8Machine m;
    static const uint8_t code[] = {
        0xad,0x1f,0xd0,0x85,0x80, /* CONSOL */
        0xad,0x14,0xd0,0x85,0x81, /* PAL */
        0xad,0x10,0xd0,0x85,0x82, /* TRIG0 */
        0xa9,0x0a,0x8d,0x1f,0xd0,
        0xad,0x3f,0xd0,0x85,0x83, /* mirror */
        0xa9,0,0x8d,0x0e,0xd4,0x60
    };
    for (unsigned ntsc=0;ntsc<2;++ntsc) {
        CHECK(atari8_machine_init(&m,ntsc,false,48000,false,NULL,NULL));
        CHECK(atari8_machine_load(&m,0x2000,code,sizeof(code)));
        CHECK(atari8_machine_call(&m,0x2000,100)==ATARI8_MACHINE_OK);
        CHECK(m.ram[0x80]==7 && m.ram[0x81]==(ntsc?15:1));
        CHECK(m.ram[0x82]==1 && m.ram[0x83]==5);
    }
    static const uint8_t sync[]={0x8d,0x0a,0xd4,0xad,0x0b,0xd4,0x60};
    CHECK(atari8_machine_init(&m,false,false,48000,false,NULL,NULL));
    CHECK(atari8_machine_load(&m,0x2000,sync,sizeof(sync)));
    CHECK(atari8_machine_idle(&m,114)==ATARI8_MACHINE_OK);
    CHECK(atari8_machine_call(&m,0x2000,200)==ATARI8_MACHINE_OK);
    CHECK(m.cpu.cycles==14 && m.master_cycles==238 && m.cpu.a==1);
    CHECK(m.frames==238u*48000u/1773447u);
    CHECK(atari8_machine_init(&m,true,false,48000,false,NULL,NULL));
    CHECK(atari8_machine_load(&m,0x2000,sync,sizeof(sync)));
    CHECK(atari8_machine_idle(&m,261u*114u)==ATARI8_MACHINE_OK);
    CHECK(atari8_machine_call(&m,0x2000,200)==ATARI8_MACHINE_OK);
    CHECK(m.cpu.a==0);
    static const uint8_t nmi[]={0xa9,0x40,0x8d,0x0e,0xd4,0x60};
    CHECK(atari8_machine_init(&m,false,false,48000,false,NULL,NULL));
    CHECK(atari8_machine_load(&m,0x2000,nmi,sizeof(nmi)));
    CHECK(atari8_machine_call(&m,0x2000,100)==ATARI8_MACHINE_IO);
    CHECK(m.fault_address==0xd40e && m.fault_write && m.fault_value==0x40);
}
static void test_absent_expansion(void)
{
    Atari8Machine m;
    static const uint8_t code[]={
        0xad,0x06,0xd1,0x85,0x80, /* PBI read */
        0xa9,0x42,0x8d,0xff,0xd1, /* no device selected */
        0xad,0xff,0xd1,0x85,0x81,
        0xad,0x00,0xd5,0x85,0x82, /* no cartridge */
        0xad,0xff,0xd7,0x85,0x83,
        0xa9,0x55,0x8d,0x0d,0xd2, /* SEROUT */
        0x8d,0x1d,0xd2,0x60
    };
    CHECK(atari8_machine_init(&m,false,true,48000,false,NULL,NULL));
    CHECK(atari8_machine_load(&m,0x2000,code,sizeof(code)));
    CHECK(atari8_machine_call(&m,0x2000,100)==ATARI8_MACHINE_OK);
    for(unsigned i=0;i<4;++i) CHECK(m.ram[0x80+i]==0xff);
    CHECK(m.core.pokey[0].serout==0x55 && m.core.pokey[1].serout==0x55);
    CHECK(m.ram[0xd1ff]==0);
    static const uint8_t unsupported[]={0xa9,0x0b,0x8d,0x0f,0xd2,0x60};
    CHECK(atari8_machine_load(&m,0x2000,unsupported,sizeof(unsupported)));
    CHECK(atari8_machine_call(&m,0x2000,100)==ATARI8_MACHINE_IO);
    CHECK(m.fault_address==0xd20f && m.fault_write && m.fault_value==0x0b);
}
static void test_trace(void)
{
    Atari8Machine m;
    static const uint8_t code[]={0x4c,0,0};
    CHECK(atari8_machine_init(&m,false,false,48000,false,NULL,NULL));
    CHECK(atari8_machine_load(&m,0x2000,code,sizeof(code)));
    CHECK(atari8_machine_call(&m,0x2000,200)==ATARI8_MACHINE_BUDGET);
    CHECK(m.entered_zero && m.first_zero.pc==0x2000);
    CHECK(m.first_zero.opcode==0x4c && m.first_zero.cycle==0);
    CHECK(m.first_zero.irq_vector==0 && !m.first_zero.irq);
    CHECK(m.trace_count==ATARI8_TRACE_SIZE);
    CHECK(m.trace[(m.trace_next+ATARI8_TRACE_SIZE-1)%ATARI8_TRACE_SIZE].pc==0);
}
int main(void)
{
    test_audio(false); test_audio(true); test_bus_cycle(); test_faults();
    test_indexed_pokey_store(); test_headless_video(); test_absent_expansion();
    test_trace();
    puts("PASS: experimental Atari8 CPU/RAM/POKEY machine");
    return 0;
}
