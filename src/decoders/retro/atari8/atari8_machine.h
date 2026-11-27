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

/* Diagnostic snapshots at instruction boundaries, before CPU execution. */
#define ATARI8_TRACE_SIZE 16u
typedef struct {
    uint16_t pc, irq_vector;
    uint8_t opcode, operand1, operand2, a, x, y, sp, p;
    bool irq, nmi;
    uint64_t cycle;
} Atari8Trace;

/* Optional write histogram; caller owns storage, zero before use. */
typedef struct {
    uint64_t audctl[2][256], audc[2][4][16], skctl[2][256], stimer[2];
} Atari8PokeyProfile;

/* Internal music-player machine. Do not move or copy after init:
 * CPU bus callbacks refer to this object. */
typedef struct {
    uint8_t ram[65536];
    Atari8Cpu cpu;
    Atari8Core core;
    Atari8Pcm pcm;
    uint64_t master_cycles, frames;
    Atari8PokeyProfile *pokey_profile;
    Atari8MachineSink sink;
    void *sink_context;
    Atari8MachineResult status;
    uint16_t fault_address;
    bool fault_write, ntsc;
    uint8_t console_latch, fault_value;
    /* Set only by SAP host-scheduled playback before guest execution. */
    bool sap_host_scheduled;
    bool nonlinear_mix; /* experimental, selected before output begins */
    /* Continuous SAP-D headless hardware profile. */
    bool sap_continuous;
    uint8_t pia_pactl, pia_pbctl, pia_portb, pia_ddrb;
    uint8_t pia_porta, pia_ddra, antic_dmactl;
    uint8_t antic_video[16]; /* write-only headless video register latches */
    uint64_t sap_video_dma_requests;
    uint64_t sap_non_timer_irq_requests, sap_serial_clock_requests;
    uint64_t sap_pokey_audio_writes, sap_consol_writes, sap_consol_edges;
    /* SAP-B compatibility: recognized legacy VBI chain tails only. */
    uint8_t sap_vbi_tails[8192];
    uint64_t sap_vbi_returns;
    uint64_t refresh_stalls; /* CPU-only DRAM refresh delays in SAP profile */
    uint8_t antic_nmien, antic_nmist;
    Atari8Trace trace[ATARI8_TRACE_SIZE], first_zero;
    Atari8Trace before_zero[ATARI8_TRACE_SIZE];
    unsigned before_zero_count;
    unsigned trace_next, trace_count;
    bool entered_zero;
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
