#include "atari8_pokey.h"
#include <string.h>

/* Each linked pair uses the low byte in the first channel. Only
 * the high channel's oscillator is implemented for linked timers. */
static bool linked(uint8_t ctl, unsigned ch)
{
    return (ctl & (ch < 2 ? 0x10 : 0x08)) != 0;
}

static bool fast(uint8_t ctl, unsigned ch)
{
    return (ctl & (ch < 2 ? 0x40 : 0x20)) != 0;
}

static bool low_linked(uint8_t ctl, unsigned ch)
{
    return !(ch & 1) && linked(ctl, ch);
}

static bool oscillator_audible(uint8_t audc)
{
    return !(audc & 0x10) && (audc & 0x0f);
}

static bool fast_timer(uint8_t ctl, unsigned ch)
{
    return fast(ctl, ch) && (!(ch & 1) || linked(ctl, ch));
}

/* Counter units are selected clock ticks, or master cycles in fast mode.
 * The fast path includes the hardware's steady-state reload delay. */
static uint32_t reload(const Atari8Pokey *p, unsigned ch)
{
    if ((ch & 1) && linked(p->audctl, ch)) {
        uint32_t n = ((uint32_t)p->audf[ch] << 8) | p->audf[ch - 1];
        return n + (fast_timer(p->audctl, ch) ? 7u : 1u);
    }
    return (uint32_t)p->audf[ch] + (fast_timer(p->audctl, ch) ? 4u : 1u);
}

static void restart_timers(Atari8Pokey *p)
{
    for (unsigned ch = 0; ch < 4; ++ch) {
        p->counter[ch] = reload(p, ch);
        p->output[ch] = 0;
    }
}

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
            if (low_linked(p->audctl, ch) && oscillator_audible(value))
                return false;
            p->audc[ch] = value;
        }
        return true;
    }
    if (reg == 8) {
        /* Noise selection and high-pass filters remain unsupported. */
        if (value & 0x86)
            return false;
        for (unsigned ch = 0; ch < 4; ch += 2) {
            if (low_linked(value, ch) && oscillator_audible(p->audc[ch]))
                return false;
        }
        if (p->audctl != value) {
            p->audctl = value;
            /* Simplified mode transition; not hardware-exact phase. */
            restart_timers(p);
        }
        return true;
    }
    if (reg == 9) {
        /* Approximation: actual STIMER latency is not modeled yet. */
        restart_timers(p);
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
        bool slow_tick = (p->audctl & 1) ? tick114 : tick28;
        for (unsigned ch = 0; ch < 4; ++ch) {
            if (low_linked(p->audctl, ch))
                continue;
            if (!fast_timer(p->audctl, ch) && !slow_tick)
                continue;
            if (--p->counter[ch] == 0) {
                p->counter[ch] = reload(p, ch);
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
