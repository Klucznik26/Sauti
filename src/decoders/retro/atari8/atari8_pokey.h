#ifndef ATARI8_POKEY_H
#define ATARI8_POKEY_H

#include <stdbool.h>
#include <stdint.h>

/* Experimental internal interface, not the public MaktabaSauti API. */
typedef struct {
    uint8_t audf[4], audc[4], audctl;
    uint32_t counter[4];
    uint8_t output[4];
    uint32_t clock_hz;
    uint32_t phase28, phase114;
} Atari8Pokey;

void atari8_pokey_reset(Atari8Pokey *p, bool ntsc);
/* Unsupported writes return false without changing state. */
bool atari8_pokey_write(Atari8Pokey *p, uint8_t reg, uint8_t value);
void atari8_pokey_step(Atari8Pokey *p, uint32_t cycles);
/* Linear digital level 0..60, not PCM. */
uint8_t atari8_pokey_level(const Atari8Pokey *p);
#endif
