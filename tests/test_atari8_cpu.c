#include "../src/decoders/retro/atari8/atari8_cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
typedef struct {
    uint8_t ram[65536];
    uint16_t trace[16];
    unsigned count;
} Bus;
static uint8_t read_bus(void *ctx, uint16_t a)
{
    Bus *b = ctx;
    if (b->count < 16) b->trace[b->count++] = a;
    return b->ram[a];
}
static void write_bus(void *ctx, uint16_t a, uint8_t v)
{
    Bus *b = ctx;
    if (b->count < 16) b->trace[b->count++] = a;
    b->ram[a] = v;
}
static void step(Atari8Cpu *c, Bus *b, unsigned cycles)
{
    b->count = 0;
    uint64_t before = c->cycles;
    CHECK(atari8_cpu_step(c) == cycles);
    CHECK(c->cycles - before == cycles && b->count == cycles);
}
static void test_load_store(void)
{
    Bus b = {0};
    Atari8Cpu c;
    const uint8_t code[] = {
        0xa2, 0x01, 0xa0, 0x02, 0xa9, 0x80, 0x95, 0xff,
        0xbd, 0xff, 0x12, 0x99, 0xfe, 0x20, 0xa1, 0xfe,
        0xb1, 0xff, 0x9a, 0xba, 0xea, 0x02
    };
    memcpy(b.ram + 0x200, code, sizeof(code));
    b.ram[0x1300] = 0x42;
    b.ram[0xff] = 0xfe;
    b.ram[0x80fe] = 0x55;
    b.ram[0x8100] = 0x66;
    atari8_cpu_init(&c, read_bus, write_bus, &b, 0x200);
    step(&c,&b,2); CHECK(c.x == 1);
    step(&c,&b,2); CHECK(c.y == 2);
    step(&c,&b,2); CHECK(c.a == 0x80 && (c.p & ATARI8_CPU_N));
    step(&c,&b,4); CHECK(b.ram[0] == 0x80 && b.trace[2] == 0xff);
    step(&c,&b,5); CHECK(c.a == 0x42 && b.trace[3] == 0x1200);
    step(&c,&b,5); CHECK(b.ram[0x2100] == 0x42 && b.trace[3] == 0x2000);
    step(&c,&b,6); CHECK(c.a == 0x55 && b.trace[3] == 0xff && b.trace[4] == 0);
    step(&c,&b,6); CHECK(c.a == 0x66 && b.trace[4] == 0x8000);
    step(&c,&b,2); CHECK(c.sp == 1);
    step(&c,&b,2); CHECK(c.x == 1);
    step(&c,&b,2);
    CHECK(atari8_cpu_step(&c) == 0 && c.stopped && c.bad_opcode == 2);
    CHECK(c.pc == 0x215);
}
static void test_stack(void)
{
    Bus b = {0};
    Atari8Cpu c;
    const uint8_t code[] = {0xa9,0x80,0x20,0x00,0x40,0xea};
    const uint8_t sub[] = {0x48,0xa9,0,0x08,0x18,0x28,0x68,0x60};
    memcpy(b.ram + 0x300, code, sizeof(code));
    memcpy(b.ram + 0x4000, sub, sizeof(sub));
    atari8_cpu_init(&c,read_bus,write_bus,&b,0x300);
    step(&c,&b,2);
    step(&c,&b,6); CHECK(c.pc == 0x4000 && c.sp == 0xfb);
    CHECK(b.ram[0x1fd] == 3 && b.ram[0x1fc] == 4);
    step(&c,&b,3); step(&c,&b,2);
    step(&c,&b,3); CHECK(b.ram[0x1fa] & ATARI8_CPU_B);
    step(&c,&b,2); step(&c,&b,4);
    CHECK((c.p & ATARI8_CPU_Z) && !(c.p & ATARI8_CPU_B));
    step(&c,&b,4); CHECK(c.a == 0x80 && (c.p & ATARI8_CPU_N));
    step(&c,&b,6); CHECK(c.pc == 0x305 && c.sp == 0xfd);
}
static void test_branches(void)
{
    Bus b = {0};
    Atari8Cpu c;
    b.ram[0x20fd] = 0xd0; b.ram[0x20fe] = 2;
    b.ram[0x2101] = 0xd0; b.ram[0x2102] = 0xfc;
    atari8_cpu_init(&c,read_bus,write_bus,&b,0x20fd);
    step(&c,&b,4); CHECK(c.pc == 0x2101 && b.trace[3] == 0x2001);
    step(&c,&b,4); CHECK(c.pc == 0x20ff && b.trace[3] == 0x21ff);
    c.pc = 0x20fd; c.p |= ATARI8_CPU_Z;
    step(&c,&b,2); CHECK(c.pc == 0x20ff);
    b.ram[0x20ff] = 0x6c; b.ram[0x2100] = 0xff; b.ram[0x2101] = 0x30;
    b.ram[0x30ff] = 0x34; b.ram[0x3000] = 0x12; b.ram[0x3100] = 0x99;
    step(&c,&b,5); CHECK(c.pc == 0x1234 && b.trace[4] == 0x3000);
}
static void alu_case(Atari8Cpu *c, Bus *b, uint8_t op, uint8_t a,
                     uint8_t operand, uint8_t flags)
{
    c->pc = 0x200;
    c->a = a;
    c->p = ATARI8_CPU_U | ATARI8_CPU_I | flags;
    b->ram[0x200] = op;
    b->ram[0x201] = operand;
    step(c,b,2);
}

static void test_binary_alu(void)
{
    Bus b = {0};
    Atari8Cpu c;
    atari8_cpu_init(&c,read_bus,write_bus,&b,0x200);
    const unsigned mask = ATARI8_CPU_N | ATARI8_CPU_V | ATARI8_CPU_Z | ATARI8_CPU_C;
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned v = 0; v < 256; ++v)
            for (unsigned carry = 0; carry < 2; ++carry)
                for (unsigned sub = 0; sub < 2; ++sub) {
                    int value = sub ? (int)a - (int)v - (1 - (int)carry)
                                    : (int)a + (int)v + (int)carry;
                    int sa = a < 128 ? (int)a : (int)a - 256;
                    int sv = v < 128 ? (int)v : (int)v - 256;
                    int signed_value = sub ? sa - sv - (1 - (int)carry)
                                           : sa + sv + (int)carry;
                    unsigned raw = (uint8_t)value;
                    unsigned expected = (raw & 128u)
                        | (!raw ? ATARI8_CPU_Z : 0)
                        | (signed_value < -128 || signed_value > 127 ? ATARI8_CPU_V : 0)
                        | ((sub ? value >= 0 : value > 255) ? ATARI8_CPU_C : 0);
                    alu_case(&c,&b,sub ? 0xe9 : 0x69,(uint8_t)a,(uint8_t)v,
                             (uint8_t)(carry | ATARI8_CPU_N | ATARI8_CPU_Z | ATARI8_CPU_V));
                    CHECK(c.a == raw && (c.p & mask) == expected);
                    CHECK((c.p & (ATARI8_CPU_U | ATARI8_CPU_I)) == (ATARI8_CPU_U | ATARI8_CPU_I));
                }
}

static uint8_t bcd(unsigned n)
{
    return (uint8_t)((n / 10u) * 16u + n % 10u);
}

static void test_decimal_alu(void)
{
    Bus b = {0};
    Atari8Cpu c;
    atari8_cpu_init(&c,read_bus,write_bus,&b,0x200);
    for (unsigned a = 0; a < 100; ++a)
        for (unsigned v = 0; v < 100; ++v)
            for (unsigned carry = 0; carry < 2; ++carry) {
                unsigned sum = a + v + carry;
                alu_case(&c,&b,0x69,bcd(a),bcd(v),(uint8_t)(ATARI8_CPU_D | carry));
                CHECK(c.a == bcd(sum % 100) && !!(c.p & ATARI8_CPU_C) == (sum >= 100));
                int diff = (int)a - (int)v - (1 - (int)carry);
                alu_case(&c,&b,0xe9,bcd(a),bcd(v),(uint8_t)(ATARI8_CPU_D | carry));
                CHECK(c.a == bcd((unsigned)(diff + 100) % 100) && !!(c.p & ATARI8_CPU_C) == (diff >= 0));
            }
    alu_case(&c,&b,0x69,0x79,0,ATARI8_CPU_D | ATARI8_CPU_C);
    CHECK(c.a == 0x80 && (c.p & ATARI8_CPU_N) && (c.p & ATARI8_CPU_V));
    alu_case(&c,&b,0x69,0x50,0x50,ATARI8_CPU_D);
    CHECK(c.a == 0 && (c.p & ATARI8_CPU_C) && !(c.p & ATARI8_CPU_Z)
          && (c.p & ATARI8_CPU_N) && (c.p & ATARI8_CPU_V));
    alu_case(&c,&b,0xe9,0,1,ATARI8_CPU_D | ATARI8_CPU_C);
    CHECK(c.a == 0x99 && !(c.p & ATARI8_CPU_C) && (c.p & ATARI8_CPU_N));
    alu_case(&c,&b,0xe9,0x80,1,ATARI8_CPU_D | ATARI8_CPU_C);
    CHECK(c.a == 0x79 && (c.p & ATARI8_CPU_C) && (c.p & ATARI8_CPU_V));
    alu_case(&c,&b,0x69,0x0f,0x0f,ATARI8_CPU_D | ATARI8_CPU_C);
    CHECK(c.a == 0x15 && !(c.p & ATARI8_CPU_C));
}

static void test_logic_compare(void)
{
    Bus b = {0};
    Atari8Cpu c;
    atari8_cpu_init(&c,read_bus,write_bus,&b,0x200);
    alu_case(&c,&b,0x09,0x01,0x80,ATARI8_CPU_C);
    CHECK(c.a == 0x81 && (c.p & ATARI8_CPU_N) && (c.p & ATARI8_CPU_C));
    alu_case(&c,&b,0x29,0x80,0x7f,ATARI8_CPU_N);
    CHECK(c.a == 0 && (c.p & ATARI8_CPU_Z) && !(c.p & ATARI8_CPU_N));
    alu_case(&c,&b,0x49,0xff,0xff,ATARI8_CPU_V);
    CHECK(c.a == 0 && (c.p & ATARI8_CPU_Z) && (c.p & ATARI8_CPU_V));
    alu_case(&c,&b,0xc9,0x80,0x80,ATARI8_CPU_V);
    CHECK(c.a == 0x80 && (c.p & ATARI8_CPU_Z) && (c.p & ATARI8_CPU_C) && (c.p & ATARI8_CPU_V));
    c.x = 0;
    alu_case(&c,&b,0xe0,0,1,ATARI8_CPU_C);
    CHECK((c.p & ATARI8_CPU_N) && !(c.p & ATARI8_CPU_C));
    c.y = 0xff;
    alu_case(&c,&b,0xc0,0,0x7f,0);
    CHECK((c.p & ATARI8_CPU_N) && (c.p & ATARI8_CPU_C));
    c.pc = 0x200; c.a = 1; b.ram[0x200] = 0x24; b.ram[0x201] = 0x80;
    b.ram[0x80] = 0xc0;
    step(&c,&b,3);
    CHECK(c.a == 1 && (c.p & ATARI8_CPU_N) && (c.p & ATARI8_CPU_V) && (c.p & ATARI8_CPU_Z));
    c.pc = 0x200; c.x = 1; c.a = 1; c.p &= (uint8_t)~(ATARI8_CPU_D | ATARI8_CPU_C);
    b.ram[0x200] = 0x7d; b.ram[0x201] = 0xff; b.ram[0x202] = 0x30;
    b.ram[0x3100] = 2;
    step(&c,&b,5);
    CHECK(c.a == 3 && b.trace[3] == 0x3000 && b.trace[4] == 0x3100);
}

int main(void)
{
    test_load_store(); test_stack(); test_branches();
    test_binary_alu(); test_decimal_alu(); test_logic_compare();
    puts("PASS: experimental Atari8 NMOS 6502 data/control/ALU subset");
    return 0;
}
