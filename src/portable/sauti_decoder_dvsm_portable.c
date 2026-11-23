/* C odpowiednik sauti_decoder_dvsm.asm (patrz sauti_portable.h). */
#include "sauti_portable.h"
#define S16 sp_u2f(0x38000000u)
#define S8 sp_u2f(0x3c000000u)
/* Pack 2 wraps around like the add.w of the 68000 player (Wax D2D Pro); the Voice pack (4) keeps saturating (no reference known) */
static int32_t wrap16(int32_t v) { return (int32_t)(((uint32_t)v & 0xffffu) ^ 0x8000u) - 0x8000; }
static int32_t sat(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
static int32_t seed(const uint8_t *p) { return (int32_t)(int16_t)sp_be16(p); }
void sauti_dvsm_pcm_f32_asm(const uint8_t *src, float *dst, size_t count, uint32_t depth) {
    size_t i;
    for (i = 0; i < count; i++) dst[i] = depth == 16u ? (float)seed(src + 2 * i) * S16 : (float)(int32_t)(int8_t)src[i] * S8;
}
void sauti_dvsm_delta_mono_asm(const uint8_t *in, float *out, size_t frames, const int16_t *steps) {
    int32_t a; size_t i;
    if (!frames) return;
    a = seed(in); in += 2; out[0] = (float)a * S16;
    for (i = 1; i < frames; i++) { a = wrap16(a + steps[in[i - 1]]); out[i] = (float)a * S16; }
}
void sauti_dvsm_delta_stereo_block_asm(const uint8_t *in, float *out, size_t frames, const int16_t *steps) {
    int32_t l, r; size_t i;
    if (!frames) return;
    l = seed(in); r = seed(in + 2); in += 4; out[0] = (float)l * S16; out[1] = (float)r * S16;
    for (i = 1; i < frames; i++, in += 2) { l = wrap16(l + steps[in[0]]); r = wrap16(r + steps[in[1]]); out[2 * i] = (float)l * S16; out[2 * i + 1] = (float)r * S16; }
}
void sauti_dvsm_voice_mono_asm(const uint8_t *in, float *out, size_t frames, const int16_t *steps) {
    int32_t a; size_t i;
    if (!frames) return;
    a = seed(in); in += 2; out[0] = (float)a * S16;
    for (i = 1; i < frames; i++) {                                                         /* dwa kody 4-bitowe w bajcie, starszy pierwszy */
        const uint8_t b = in[(i - 1) / 2]; const unsigned code = (i & 1u) ? (unsigned)(b >> 4) : (unsigned)(b & 15u);
        a = sat(a + steps[code]); out[i] = (float)a * S16;
    }
}
void sauti_dvsm_voice_stereo_block_asm(const uint8_t *in, float *out, size_t frames, const int16_t *steps) {
    int32_t l, r; size_t i;
    if (!frames) return;
    l = seed(in); r = seed(in + 2); in += 4; out[0] = (float)l * S16; out[1] = (float)r * S16;
    for (i = 1; i < frames; i++, in++) { l = sat(l + steps[*in >> 4]); r = sat(r + steps[*in & 15u]); out[2 * i] = (float)l * S16; out[2 * i + 1] = (float)r * S16; }
}
