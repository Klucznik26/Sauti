#include "atari8_pokey.h"
#include <string.h>

void atari8_pokey_reset(Atari8Pokey *p, bool ntsc)
{
    memset(p, 0, sizeof(*p));
    p->clock_hz = ntsc ? 1789773u : 1773447u;
    for (unsigned ch = 0; ch < 4; ++ch)
        p->counter[ch] = 1;
}

bool atari8_pokey_write(Atari8Pokey *p, uint8_t reg, uint8_t value)
{
    if (reg < 8) {
        unsigned ch = reg / 2;
        if (!(reg & 1)) {
            p->audf[ch] = value;
        } else {
            /* Only unconditional pure tone, volume-only and reset silence. */
            if (!(value & 0x10) && (value & 0xa0) != 0xa0 && value != 0)
                return false;
            p->audc[ch] = value;
        }
        return true;
    }
    if (reg == 8) {
        if (value & 0xfe)
            return false;
        p->audctl = value;
        return true;
    }
    if (reg == 9) {
        /* Approximation: actual STIMER latency is not modeled yet. */
        for (unsigned ch = 0; ch < 4; ++ch) {
            p->counter[ch] = (uint16_t)p->audf[ch] + 1;
            p->output[ch] = 0;
        }
        return true;
    }
    return false;
}

void atari8_pokey_step(Atari8Pokey *p, uint32_t cycles)
{
    while (cycles--) {
        bool tick28 = ++p->phase28 == 28;
        bool tick114 = ++p->phase114 == 114;
        if (tick28)
            p->phase28 = 0;
        if (tick114)
            p->phase114 = 0;
        if (!((p->audctl & 1) ? tick114 : tick28))
            continue;
        for (unsigned ch = 0; ch < 4; ++ch) {
            if (--p->counter[ch] == 0) {
                p->counter[ch] = (uint16_t)p->audf[ch] + 1;
                p->output[ch] ^= 1;
            }
        }
    }
}

uint8_t atari8_pokey_level(const Atari8Pokey *p)
{
    unsigned level = 0;
    for (unsigned ch = 0; ch < 4; ++ch) {
        if ((p->audc[ch] & 0x10) || p->output[ch])
            level += p->audc[ch] & 0x0f;
    }
    return (uint8_t)level;
}
