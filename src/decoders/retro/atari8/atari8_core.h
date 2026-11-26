#ifndef ATARI8_CORE_H
#define ATARI8_CORE_H

#include "atari8_pokey.h"

/* Internal sound subsystem; no CPU, memory or PCM renderer yet. */
typedef struct {
    Atari8Pokey pokey[2];
    bool stereo;
} Atari8Core;

void atari8_core_reset(Atari8Core *core, bool ntsc, bool stereo);
unsigned atari8_core_channels(const Atari8Core *core);
/* POKEY bus window $D200-$D2FF. False: unsupported address/register. */
bool atari8_core_write(Atari8Core *core, uint16_t address, uint8_t value);
bool atari8_core_read(const Atari8Core *core, uint16_t address, uint8_t *value);
void atari8_core_step(Atari8Core *core, uint32_t cycles);
/* Standard stereo modification connects only primary POKEY IRQ to CPU. */
bool atari8_core_irq(const Atari8Core *core);
/* Digital levels 0..60; right is zero in mono mode. */
void atari8_core_levels(const Atari8Core *core, uint8_t levels[2]);

#endif
