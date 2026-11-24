/* C odpowiednik maktabasauti_mod_mix.asm (patrz sauti_portable.h).  Ta sama suma calkowita. */
#include "sauti_portable.h"

void maktabasauti_mod_voice_add(int32_t acc[2], const uint8_t *sample, const uint8_t *flip,
                                uint32_t idx, uint32_t length, uint32_t clear_n, int32_t vol, uint32_t pan) {
    uint8_t b;
    int32_t s;
    if (!sample || idx >= length) return;
    b = idx < clear_n ? 0u : sample[idx];
    if (flip && ((flip[idx >> 3] >> (idx & 7u)) & 1u)) b ^= 0xFFu;
    s = (b < 128u ? (int32_t)b : (int32_t)b - 256) * vol;
    acc[0] += s * (255 - (int32_t)pan) / 255;
    acc[1] += s * (int32_t)pan / 255;
}
