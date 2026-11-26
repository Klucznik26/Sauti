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

static bool filter_enabled(uint8_t ctl, unsigned ch)
{
    return (ctl & (ch == 0 ? 4u : 2u)) != 0;
}

static void restart_timers(Atari8Pokey *p)
{
    for (unsigned ch = 0; ch < 4; ++ch) {
        p->counter[ch] = reload(p, ch);
        p->output[ch] = 1;
    }
}

/* Right-shifting representations of the documented polynomial
 * sequences. The short audio sequences have inverted polarity.
 * The 9/17-bit generator is temporarily represented by two free-running
 * registers; switching between them is not yet hardware-exact. */
static void clock_noise(Atari8Pokey *p)
{
    static const uint8_t width[] = {4, 5, 9, 17};
    static const uint8_t tap[] = {1, 2, 5, 5};

    for (unsigned i = 0; i < 4; ++i) {
        uint32_t s = p->poly[i];
        uint32_t feedback = (s ^ (s >> tap[i])) & 1u;
        s = (s >> 1) | (feedback << (width[i] - 1));
        p->poly[i] = s;
        unsigned bit = (s & 1u) ^ (i < 2 ? 1u : 0u);
        p->noise_history[i] =
            (uint8_t)(((unsigned)p->noise_history[i] << 1 | bit) & 15u);
    }
}

static void clock_waveform(Atari8Pokey *p, unsigned ch)
{
    uint8_t mode = p->audc[ch];
    if (!(mode & 0x80) && !((p->noise_history[1] >> ch) & 1u))
        return;
    if (mode & 0x20) {
        p->output[ch] ^= 1;
    } else {
        unsigned poly = (mode & 0x40) ? 0 : ((p->audctl & 0x80) ? 2 : 3);
        p->output[ch] = (uint8_t)((p->noise_history[poly] >> ch) & 1u);
    }
}

void atari8_pokey_reset(Atari8Pokey *p, bool ntsc)
{
    memset(p, 0, sizeof(*p));
    p->clock_hz = ntsc ? 1789773u : 1773447u;
    p->poly[0] = 15u;
    p->poly[1] = 31u;
    p->poly[2] = 511u;
    p->poly[3] = 131071u;
    p->noise_history[2] = p->noise_history[3] = 15u;
    p->filter_latch[0] = p->filter_latch[1] = 1;
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
            if (low_linked(p->audctl, ch) && oscillator_audible(value))
                return false;
            p->audc[ch] = value;
        }
        return true;
    }
    if (reg == 8) {
        /* The clock pulses of linked channel 3 are not modeled yet. */
        if ((value & 0x0c) == 0x0c)
            return false;
        for (unsigned ch = 0; ch < 4; ch += 2) {
            if (low_linked(value, ch) && oscillator_audible(p->audc[ch]))
                return false;
        }
        if (p->audctl != value) {
            uint8_t changed = p->audctl ^ value;
            p->audctl = value;
            for (unsigned ch = 0; ch < 2; ++ch) {
                if (!filter_enabled(value, ch)) {
                    p->filter_latch[ch] = 1;
                    p->filter_delay[ch] = 0;
                }
            }
            /* Simplified timer mode transition; noise selection alone
             * must not restart the frequency timers. */
            if (changed & 0x79)
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
        /* Approximate 1.5-cycle filter propagation by two full cycles. */
        for (unsigned ch = 0; ch < 2; ++ch) {
            if (p->filter_delay[ch] && --p->filter_delay[ch] == 0)
                p->filter_latch[ch] = p->filter_sample[ch];
        }
        clock_noise(p);
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
                clock_waveform(p, ch);
                if (ch >= 2 && filter_enabled(p->audctl, ch - 2)) {
                    p->filter_sample[ch - 2] = p->output[ch - 2];
                    p->filter_delay[ch - 2] = 2;
                }
            }
        }
    }
}

uint8_t atari8_pokey_level(const Atari8Pokey *p)
{
    unsigned level = 0;
    for (unsigned ch = 0; ch < 4; ++ch) {
        uint8_t bit = p->output[ch];
        if (ch < 2)
            bit ^= p->filter_latch[ch];
        if ((p->audc[ch] & 0x10) || bit)
            level += p->audc[ch] & 0x0f;
    }
    return (uint8_t)level;
}
