#include "atari8_pcm.h"
#include <string.h>

bool atari8_pcm_init(Atari8Pcm *p, uint32_t clock_hz, uint32_t rate,
                     unsigned channels, bool dc_block)
{
    if (!p || (clock_hz != 1773447u && clock_hz != 1789773u)
        || rate < 8000u || rate > 192000u
        || (channels != 1 && channels != 2))
        return false;

    memset(p, 0, sizeof(*p));
    p->clock_hz = clock_hz;
    p->rate = rate;
    p->channels = channels;
    p->dc_block = dc_block;
    /* Backward-Euler first-order DC blocker, nominal 20 Hz cutoff. */
    p->alpha = (double)rate / ((double)rate + 125.66370614359173);
    return true;
}

static int16_t signed_sample(double x)
{
    if (x >= 32767.0)
        return 32767;
    if (x <= -32768.0)
        return -32768;
    return (int16_t)(x >= 0.0 ? x + 0.5 : x - 0.5);
}

bool atari8_pcm_feed(Atari8Pcm *p, const uint8_t levels[2], int16_t frame[2])
{
    /* Use integer time units: one cycle is rate units, one frame is
     * clock_hz units. Split the cycle exactly at a frame boundary. */
    uint32_t remaining = p->clock_hz - p->phase;
    if (p->rate < remaining) {
        for (unsigned ch = 0; ch < p->channels; ++ch)
            p->area[ch] += (uint64_t)levels[ch] * p->rate;
        p->phase += p->rate;
        return false;
    }

    uint32_t tail = p->rate - remaining;
    uint64_t denominator = (uint64_t)p->clock_hz * 60u;
    for (unsigned ch = 0; ch < p->channels; ++ch) {
        uint64_t integral = p->area[ch] + (uint64_t)levels[ch] * remaining;
        uint64_t scaled = integral * 32767u;
        if (p->dc_block) {
            double x = (double)scaled / (double)denominator;
            double y = p->alpha * (p->dc_state[ch] + x - p->previous[ch]);
            p->previous[ch] = x;
            p->dc_state[ch] = y;
            frame[ch] = signed_sample(y);
        } else {
            frame[ch] = (int16_t)((scaled + denominator / 2u) / denominator);
        }
        p->area[ch] = (uint64_t)levels[ch] * tail;
    }
    if (p->channels == 1)
        frame[1] = 0;
    p->phase = tail;
    return true;
}
