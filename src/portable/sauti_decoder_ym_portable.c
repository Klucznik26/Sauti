/* C odpowiednik sauti_decoder_ym.asm (patrz sauti_portable.h): wariant produkcyjny (tablica DAC emu2149, bez SAUTI_YM_REFERENCE_DAC / SAUTI_YM_ENV_SAMPLE_BEFORE_TICK). */
#include "sauti_portable.h"
typedef struct { uint32_t phase, index, attack, alternate, hold, holding; } PortYmEnv;
static const uint8_t ym_dac[32] = { 0, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 9, 11, 13, 15, 18, 22, 26, 31, 37, 45, 53, 63, 76, 90, 106, 127, 151, 180, 214, 255, 255 };
uint32_t sauti_ym_noise_step_asm(uint32_t *phase, uint32_t *lfsr, uint32_t step, uint32_t period) {
    uint64_t limit, acc; uint32_t s;
    if (!period) period = 1u;
    limit = (uint64_t)period << 16;
    acc = (uint64_t)*phase + step;
    s = *lfsr;
    if (!(s & 0x1ffffu)) s |= 1u;
    while (acc >= limit) {
        uint32_t fb, inv;
        acc -= limit;
        fb = ((s >> 2) ^ s) & 1u;
        inv = s & 0x20000u;
        s = ((s & 0x1ffffu) >> 1) | (fb << 16) | inv;
        if (!fb) s ^= 0x20000u;                                                            /* bez sprzezenia: odwrocenie wyjscia */
    }
    *phase = (uint32_t)acc; *lfsr = s;
    return (s >> 17) ^ 1u;
}
void sauti_ym_tone_step_asm(const uint8_t *regs, uint32_t *phase, uint32_t step, float *out, uint32_t noise_bit, uint32_t level) {
    float acc = 0.0f; unsigned ch;
    for (ch = 0; ch < 3u; ch++) {
        uint32_t period = (uint32_t)regs[2 * ch] | (((uint32_t)regs[2 * ch + 1] & 15u) << 8), inc, before, vol, dac; float amp; int tone_on;
        if (!period) period = 1u;
        inc = (uint32_t)(((uint64_t)step << 15) / period);
        before = phase[ch]; phase[ch] += inc;
        vol = regs[8 + ch];
        if (vol & 16u) vol = level & 31u;
        else { vol &= 15u; if (!vol) continue; vol = vol * 2u + 1u; }
        dac = ym_dac[vol];
        if (!dac) continue;
        amp = (float)dac * sp_u2f(0x3aab5601u);
        {   const uint32_t mixer = regs[7];
            tone_on = ((mixer >> ch) & 1u) || !(before & 0x80000000u);                      /* ton wylaczony w mikserze albo polfala dodatnia */
            if (!tone_on) { acc -= amp; continue; }
            if (((mixer >> (ch + 3u)) & 1u) || noise_bit) acc += amp; else acc -= amp; }
    }
    *out = acc;
}
void sauti_ym_env_init_asm(void *state, uint32_t shape) {
    PortYmEnv *e = (PortYmEnv *)state;
    shape &= 15u;
    e->phase = 0; e->index = 31u; e->attack = ((shape >> 2) & 1u) ? 31u : 0u;
    if (!(shape & 8u)) { e->alternate = (e->attack >> 4) & 1u; e->hold = 1u; }
    else { e->alternate = (shape >> 1) & 1u; e->hold = shape & 1u; }
    e->holding = 0;
}
uint32_t sauti_ym_env_step_asm(void *state, uint32_t step, uint32_t period) {
    PortYmEnv *e = (PortYmEnv *)state; uint64_t limit, acc;
    if (!period) period = 1u;
    limit = (uint64_t)period << 16;
    acc = (uint64_t)e->phase + step;
    while (acc >= limit) {
        acc -= limit;
        if (e->holding) continue;
        if (e->index) { e->index--; continue; }
        if (e->hold) { e->holding = 1u; if (e->alternate) e->attack ^= 31u; e->index = 0; }
        else { if (e->alternate) e->attack ^= 31u; e->index = 31u; }
    }
    e->phase = (uint32_t)acc;
    return e->index ^ e->attack;
}
float sauti_ym_drum_sample_asm(const uint8_t *p, uint32_t attr) {
    const uint32_t v = *p;
    if (attr & 4u) return (float)((int32_t)((v & 15u) * 2u) - 15) * 0.041666666666666664f;
    return (float)((attr & 2u) ? (int32_t)(int8_t)v : (int32_t)v - 128) * 0.002604166666666667f;
}
