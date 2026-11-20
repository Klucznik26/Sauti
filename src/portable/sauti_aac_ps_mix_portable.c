/* C odpowiednik sauti_aac_ps_mix.asm (patrz sauti_portable.h).  Kolejnosc dzialan jak w SSE: iloczyny, potem suma. */
#include "sauti_portable.h"
typedef struct { float re, im; } SpC;
static SpC ld(const uint8_t *p) { SpC c; memcpy(&c, p, sizeof c); return c; }
static void st(uint8_t *p, SpC c) { memcpy(p, &c, sizeof c); }
void sauti_aac_ps_mix_strided_asm(const void *direct, const void *decorrelated, void *left, void *right, const void *coefficient, size_t samples, size_t stride_bytes) {
    const uint8_t *d = (const uint8_t *)direct, *e = (const uint8_t *)decorrelated; uint8_t *l = (uint8_t *)left, *r = (uint8_t *)right; float h[4]; size_t i;
    if (!samples) return;
    memcpy(h, coefficient, sizeof h);                                                      /* h11 h12 h21 h22 */
    for (i = 0; i < samples; i++, d += stride_bytes, e += stride_bytes, l += stride_bytes, r += stride_bytes) {
        const SpC a = ld(d), b = ld(e); SpC x, y;
        x.re = a.re * h[0] + b.re * h[2]; x.im = a.im * h[0] + b.im * h[2];
        y.re = a.re * h[1] + b.re * h[3]; y.im = a.im * h[1] + b.im * h[3];
        st(l, x); st(r, y);
    }
}
static SpC cmul(SpC x, SpC c) { SpC o; o.re = x.re * c.re - x.im * c.im; o.im = x.im * c.re + x.re * c.im; return o; }
void sauti_aac_ps_mix_complex_strided_asm(const void *direct, const void *decorrelated, void *left, void *right, const void *coefficient, size_t samples, size_t stride_bytes) {
    const uint8_t *d = (const uint8_t *)direct, *e = (const uint8_t *)decorrelated; uint8_t *l = (uint8_t *)left, *r = (uint8_t *)right; SpC h[4]; size_t i;
    if (!samples) return;
    memcpy(h, coefficient, sizeof h);                                                      /* h11 h12 h21 h22 jako {re, im} */
    for (i = 0; i < samples; i++, d += stride_bytes, e += stride_bytes, l += stride_bytes, r += stride_bytes) {
        const SpC a = ld(d), b = ld(e); SpC p = cmul(a, h[0]), q = cmul(b, h[2]), x, y;
        x.re = p.re + q.re; x.im = p.im + q.im;
        p = cmul(a, h[1]); q = cmul(b, h[3]);
        y.re = p.re + q.re; y.im = p.im + q.im;
        st(l, x); st(r, y);
    }
}
