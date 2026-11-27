#include "atari8_pokey.h"
#include <string.h>

/* Each linked pair uses the low byte in the first channel. */
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
static void clock_noise(Atari8Pokey *p, bool initializing)
{
    static const uint8_t width[] = {4, 5, 9, 17};
    static const uint8_t tap[] = {1, 2, 5, 5};

    for (unsigned i = 0; i < 4; ++i) {
        uint32_t s = p->poly[i];
        uint32_t feedback = initializing ? 1u : ((s ^ (s >> tap[i])) & 1u);
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
    /* Deterministic playback reset, not physical power-up state. */
    p->skctl = 3;
    p->pot_count = 228;
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
            p->audc[ch] = value;
        }
        return true;
    }
    if (reg == 8) {
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
            if ((changed & 0x79) && !p->preserve_audctl_counters)
                restart_timers(p);
        }
        return true;
    }
    if (reg == 9) {
        /* Approximation: actual STIMER latency is not modeled yet. */
        restart_timers(p);
        return true;
    }
    if (reg == 10) {
        /* SKREST: no keyboard/serial errors exist in headless playback. */
        return true;
    }
    if (reg == 11) {
        /* POTGO: all disconnected inputs run to the timeout. */
        if (p->skctl & 3u)
            p->pot_count = 0;
        return true;
    }
    if (reg == 12)
        return true; /* unused write */
    if (reg == 13) {
        /* SEROUT holding register. Headless sink with no SIO device.
         * Serial transfer timing and serial IRQs are not modeled;
         * SKCTL serial/two-tone modes remain explicitly unsupported. */
        p->serout = value;
        return true;
    }
    if (reg == 14) {
        /* Only the three timer sources are implemented. */
        if (value & 0xf8)
            return false;
        p->irq_enable = value;
        /* Disabling a source acknowledges its pending interrupt. */
        p->irq_pending &= value;
        return true;
    }
    if (reg == 15) {
        /* Support the force-break two-tone mode with the serial clock
         * disabled. Other serial modes remain unsupported. */
        /* $20 selects a serial clock but also holds the keyboard/slow
         * clocks and noise generators in initialization. No serial
         * device or serial interrupt is implemented in this profile. */
        /* Asynchronous receive with the serial input held idle high:
         * no start bit, so timers 3/4 remain in reset. Only clock mode
         * 001 (external output clock) is supported for this case. */
        if ((value & 0x78u) != 0 && (value & 0xf8u) != 0x88u &&
            (value & 0x78u) != 0x10u && value != 0x20u)
            return false;
        p->skctl = value;
        if (value & 0x10u) {
            p->counter[2] = reload(p, 2);
            p->counter[3] = reload(p, 3);
        }
        if (!(value & 3u)) {
            /* Approximate initial prescaler phase; first pulses after
             * release at 19 and 78 cycles, excluding timer pipelines. */
            p->phase28 = 9;
            p->phase114 = 36;
        }
        return true;
    }
    return false;
}

bool atari8_pokey_read(const Atari8Pokey *p, uint8_t reg, uint8_t *value)
{
    if (!value)
        return false;
    if (reg < 8) {
        *value = p->pot_count;
        return true;
    }
    if (reg == 8) {
        *value = p->pot_count < 228 ? 0xff : 0;
        return true;
    }
    if (reg == 9 || reg == 11 || reg == 12 || reg == 13 || reg == 15) {
        /* Headless keyboard/serial idle and unused register reads.
         * Indexed NMOS stores may perform dummy reads here. */
        *value = 0xff;
        return true;
    }
    if (reg == 10) {
        /* The eight most significant bits of the selected LFSR. */
        *value = (uint8_t)((p->audctl & 0x80u) ?
                           (p->poly[2] >> 1) : (p->poly[3] >> 9));
        return true;
    }
    if (reg == 14) {
        /* IRQST is active low; reading does not acknowledge. */
        *value = (uint8_t)~p->irq_pending;
        return true;
    }
    return false;
}

bool atari8_pokey_irq(const Atari8Pokey *p)
{
    return p->irq_pending != 0;
}

void atari8_pokey_step(Atari8Pokey *p, uint32_t cycles)
{
    while (cycles--) {
        /* Approximate 1.5-cycle filter propagation by two full cycles. */
        for (unsigned ch = 0; ch < 2; ++ch) {
            if (p->filter_delay[ch] && --p->filter_delay[ch] == 0)
                p->filter_latch[ch] = p->filter_sample[ch];
        }
        bool initializing = !(p->skctl & 3u);
        clock_noise(p, initializing);
        bool tick28 = !initializing && ++p->phase28 == 28;
        bool tick114 = !initializing && ++p->phase114 == 114;
        if (tick28)
            p->phase28 = 0;
        if (tick114)
            p->phase114 = 0;
        if (!initializing && p->pot_count < 228 &&
            ((p->skctl & 4u) || tick114))
            ++p->pot_count;
        bool slow_tick = (p->audctl & 1) ? tick114 : tick28;
        bool timer2_pulse = false;
        for (unsigned ch = 0; ch < 4; ++ch) {
            if (ch >= 2 && (p->skctl & 0x10u)) {
                p->counter[ch] = reload(p, ch);
                continue; /* no asynchronous start bit on idle SIO */
            }
            if (!fast_timer(p->audctl, ch) && !slow_tick)
                continue;
            if (--p->counter[ch] == 0) {
                if (low_linked(p->audctl, ch)) {
                    /* No individual reload: the low byte wraps until the
                     * high timer expires and reloads the whole pair. */
                    p->counter[ch] = 256;
                } else {
                    p->counter[ch] = reload(p, ch);
                    if ((ch & 1u) && linked(p->audctl, ch))
                        p->counter[ch - 1] = reload(p, ch - 1);
                }
                /* Timer 1, 2 and 4 map to bits 0, 1 and 2.
                 * This milestone latches on the modeled timer event;
                 * actual IRQ propagation and STIMER latency are pending. */
                static const uint8_t timer_irq[4] = {1, 2, 0, 4};
                p->irq_pending |= p->irq_enable & timer_irq[ch];
                if (ch == 1) timer2_pulse = true;
                clock_waveform(p, ch);
                if (ch >= 2 && filter_enabled(p->audctl, ch - 2)) {
                    p->filter_sample[ch - 2] = p->output[ch - 2];
                    p->filter_delay[ch - 2] = 2;
                }
            }
        }
        /* Force-break selects the zero tone: timer 2 resynchronizes
         * timer 1 without an extra waveform or IRQ event. Reload timer
         * 1 after both timers have been evaluated for this cycle.
         * This is a steady-state approximation, not the silicon's
         * reset propagation pipeline. */
        if (timer2_pulse && (p->skctl & 0x88u) == 0x88u)
            p->counter[0] = reload(p, 0);
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
