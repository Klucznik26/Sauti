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

static bool feed_scaled(Atari8Pcm *p, const uint16_t levels[2],
                        unsigned scale, int16_t frame[2])
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
    uint64_t denominator = (uint64_t)p->clock_hz * scale;
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

/* Preserve the exact legacy integer integrator and scaling. */
bool atari8_pcm_feed(Atari8Pcm *p, const uint8_t levels[2], int16_t frame[2])
{
    uint16_t wide[2]={levels[0],p->channels==2 ? levels[1] : 0};
    return feed_scaled(p,wide,60u,frame);
}

bool atari8_pcm_feed_u16(Atari8Pcm *p, const uint16_t levels[2], int16_t frame[2])
{
    return feed_scaled(p,levels,65535u,frame);
}

/* Original quantized approximation to the hardware transfer curve
 * documented by Avery Lee, Altirra Hardware Reference Manual, E.2.
 * Uniform digital DAC weights for now. Normalize at total level 60;
 * apply before averaging and AC coupling, not to output PCM.
 * Curve: f(x)=x up to .14, else .14+(1-exp(-2.85*(x-.14)))/2.85.
 * LUT generated from f(i/60)/f(1), not from emulator source. */
const uint16_t atari8_pokey_curve[61]={
    0, 2371, 4742, 7114, 9485, 11856, 14227, 16598, 18970, 21321,
    23572, 25718, 27765, 29716, 31578, 33353, 35045, 36659, 38198, 39666,
    41066, 42401, 43673, 44887, 46045, 47148, 48201, 49205, 50162, 51075,
    51945, 52775, 53567, 54322, 55041, 55728, 56382, 57007, 57602, 58170,
    58711, 59227, 59719, 60189, 60636, 61063, 61470, 61859, 62229, 62582,
    62918, 63239, 63546, 63837, 64116, 64381, 64634, 64876, 65106, 65326,
    65535
};

