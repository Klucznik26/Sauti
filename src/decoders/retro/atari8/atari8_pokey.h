#ifndef ATARI8_POKEY_H
#define ATARI8_POKEY_H

#include <stdbool.h>
#include <stdint.h>

/* Experimental internal interface, not the public MaktabaSauti API. */
typedef struct {
    uint8_t audf[4], audc[4], audctl, skctl;
    uint32_t counter[4];
    bool preserve_audctl_counters; /* opt-in transition experiment */
    uint8_t irq_enable, irq_pending; /* active-high internal masks */
    uint8_t output[4]; /* raw waveform flip-flops */
    uint8_t filter_latch[2], filter_sample[2], filter_delay[2];
    uint32_t clock_hz;
    uint32_t phase28, phase114;
    uint8_t serout; /* headless transmit holding register */
    uint8_t pot_count; /* headless disconnected-input scan */
    uint32_t poly[4]; /* 4, 5, 9, 17 bits */
    uint8_t noise_history[4]; /* one cycle of delay per channel */
} Atari8Pokey;

void atari8_pokey_reset(Atari8Pokey *p, bool ntsc);
/* Unsupported writes return false without changing state. */
bool atari8_pokey_write(Atari8Pokey *p, uint8_t reg, uint8_t value);
/* Read register map; non-audio inputs use a headless idle model. */
bool atari8_pokey_read(const Atari8Pokey *p, uint8_t reg, uint8_t *value);
bool atari8_pokey_irq(const Atari8Pokey *p);
void atari8_pokey_step(Atari8Pokey *p, uint32_t cycles);
/* Linear digital level 0..60, not PCM. */
uint8_t atari8_pokey_level(const Atari8Pokey *p);
#endif
