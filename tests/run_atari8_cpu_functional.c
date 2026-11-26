/* Runs a user-supplied Klaus Dormann functional test image. No test
 * binary or third-party code is bundled with Sauti. */
#include "../src/decoders/retro/atari8/atari8_cpu.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

static uint8_t rd(void *ctx, uint16_t a) { return ((uint8_t *)ctx)[a]; }
static void wr(void *ctx, uint16_t a, uint8_t v) { ((uint8_t *)ctx)[a] = v; }

int main(int argc, char **argv)
{
    static uint8_t ram[65536];
    if (argc != 2) {
        fprintf(stderr,"Usage: %s 6502_functional_test.bin\n",argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1],"rb");
    if (!f) { perror(argv[1]); return 2; }
    size_t n = fread(ram,1,sizeof(ram),f);
    bool valid = n == sizeof(ram) && fgetc(f) == EOF && !ferror(f);
    if (fclose(f) != 0) valid = false;
    if (!valid) { fputs("Expected a 65536-byte test image\n",stderr); return 2; }
    Atari8Cpu c;
    atari8_cpu_init(&c,rd,wr,ram,0x0400);
    for (uint64_t steps=0; steps<UINT64_C(100000000); ++steps) {
        uint16_t pc = c.pc;
        if (!atari8_cpu_step(&c)) {
            fprintf(stderr,"Unsupported opcode %02X at %04X\n",(unsigned)c.bad_opcode,(unsigned)c.pc);
            return 1;
        }
        if (c.pc == pc) {
            if (pc == 0x3469 && ram[pc] == 0x4c && ram[pc+1] == 0x69 && ram[pc+2] == 0x34) {
                printf("PASS: Klaus Dormann 6502 functional test, %" PRIu64 " instructions, %" PRIu64 " cycles\n",steps+1,c.cycles);
                return 0;
            }
            fprintf(stderr,"FAIL: trap PC=%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X after %" PRIu64 " instructions\n",
                (unsigned)pc,(unsigned)c.a,(unsigned)c.x,(unsigned)c.y,(unsigned)c.p,(unsigned)c.sp,steps+1);
            return 1;
        }
    }
    fprintf(stderr,"FAIL: instruction budget exceeded at %04X\n",(unsigned)c.pc);
    return 1;
}
