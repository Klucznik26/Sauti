#ifndef ATARI8_MACHINE_H
#define ATARI8_MACHINE_H
#include "atari8_cpu.h"
#include "atari8_core.h"
#include "atari8_pcm.h"
#include <stddef.h>

typedef enum {
    ATARI8_MACHINE_OK = 0,
    ATARI8_MACHINE_BAD_ARGUMENT,
    ATARI8_MACHINE_IO,
    ATARI8_MACHINE_CPU_STOP,
    ATARI8_MACHINE_BUDGET
} Atari8MachineResult;

/* Synchronous output callback; copy the frame before returning.
 * Do not reenter or modify the machine from the callback. */
typedef void (*Atari8MachineSink)(void *, const int16_t frame[2]);

/* Internal music-player machine. Do not move or copy after init:
 * CPU bus callbacks refer to this object. */
typedef struct {
    uint8_t ram[65536];
    Atari8Cpu cpu;
    Atari8Core core;
    Atari8Pcm pcm;
    uint64_t master_cycles, frames;
    Atari8MachineSink sink;
    void *sink_context;
    Atari8MachineResult status;
    uint16_t fault_address;
    bool fault_write, ntsc;
    uint8_t console_latch;
} Atari8Machine;

bool atari8_machine_init(Atari8Machine *m, bool ntsc, bool stereo,
                         uint32_t rate, bool dc_block,
                         Atari8MachineSink sink, void *context);
/* Load bytes directly into RAM, excluding the I/O window. */
bool atari8_machine_load(Atari8Machine *m, uint16_t address,
                         const uint8_t *data, size_t size);
/* Execute one instruction or interrupt, with cycle-synchronous sound. */
Atari8MachineResult atari8_machine_step(Atari8Machine *m);
/* Advance sound with CPU parked (host-scheduled player). */
Atari8MachineResult atari8_machine_idle(Atari8Machine *m, uint32_t cycles);
/* Host subroutine call with a synthetic RTS return address ($FFFF).
 * Preserves CPU registers except PC and stack during call. Stack setup
 * is host bookkeeping, not emulated JSR cycles. Budget is checked at
 * instruction boundaries; WSYNC can add up to 114 master cycles per
 * write, so a final instruction can overshoot by more than CPU cycles.
 * Faults and budget exhaustion latch status until reinitialization. */
Atari8MachineResult atari8_machine_call(Atari8Machine *m, uint16_t entry,
                                        uint64_t cycle_budget);
#endif
