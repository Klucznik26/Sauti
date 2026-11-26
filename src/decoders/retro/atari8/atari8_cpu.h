#ifndef ATARI8_CPU_H
#define ATARI8_CPU_H
#include <stdbool.h>
#include <stdint.h>

enum {
    ATARI8_CPU_C = 1, ATARI8_CPU_Z = 2, ATARI8_CPU_I = 4,
    ATARI8_CPU_D = 8, ATARI8_CPU_B = 16, ATARI8_CPU_U = 32,
    ATARI8_CPU_V = 64, ATARI8_CPU_N = 128
};
typedef uint8_t (*Atari8CpuRead)(void *, uint16_t);
typedef void (*Atari8CpuWrite)(void *, uint16_t, uint8_t);
typedef struct {
    uint16_t pc;
    uint8_t a, x, y, sp, p;
    uint64_t cycles;
    Atari8CpuRead read;
    Atari8CpuWrite write;
    void *bus;
    bool stopped;
    uint8_t bad_opcode;
} Atari8Cpu;

/* Deterministic subroutine-player entry, not a hardware reset sequence.
 * Callbacks must be non-null and remain valid. */
void atari8_cpu_init(Atari8Cpu *c, Atari8CpuRead read, Atari8CpuWrite write,
                     void *bus, uint16_t entry);
/* Execute one instruction. Returns cycles, or zero on unsupported opcode.
 * An unsupported opcode latches stopped; its fetch still consumes a cycle. */
unsigned atari8_cpu_step(Atari8Cpu *c);
#endif
