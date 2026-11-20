/* Rownowaznosc przenosnych jader w C (src/portable) z jadrami NASM: te same losowe i brzegowe wejscia, wyniki IDENTYCZNE bit w bit (kod zwrotu, bufory wyjsciowe, parametry wskaznikowe).
 * Wejscia sa deterministyczne (xorshift); kazdy przypadek wykonuje obie wersje na kopiach tych samych danych. */
#include "sauti_aac_dequant.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_huffman.h"
#include "sauti_aac_imdct.h"
#include "sauti_aac_spectral.h"
#include "sauti_aac_syntax.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAIR(ret, name, args) ret name args; ret name##_c args;
#define CONLY(ret, name, args) ret name##_c args;                    /* wersja asemblerowa jest zadeklarowana w naglowku projektu */
CONLY(int, sauti_aac_huffman_decode_asm, (const uint8_t *, size_t, size_t *, const SautiAacHuffNode *, size_t, int32_t *))
CONLY(int, sauti_aac_dequant_scale_asm, (const int32_t *, const float *, const float *, float *, size_t))
CONLY(int, sauti_aac_adts_parse_asm, (const uint8_t *, size_t, SautiAacAdts *))
CONLY(int, sauti_aac_bits_read_asm, (const uint8_t *, size_t, size_t *, unsigned, uint32_t *))
CONLY(double, sauti_aac_imdct_dot_asm, (const float *, const float *, size_t))
CONLY(int, sauti_aac_spectral_index_asm, (uint32_t, uint32_t, int16_t[4]))
CONLY(int, sauti_aac_window_overlap_asm, (const float *, const float *, float *, float *, size_t))
PAIR(void, sauti_aac_ps_mix_strided_asm, (const void *, const void *, void *, void *, const void *, size_t, size_t))
PAIR(void, sauti_aac_ps_mix_complex_strided_asm, (const void *, const void *, void *, void *, const void *, size_t, size_t))
PAIR(int, sauti_avr_parse_asm, (const uint8_t *, size_t, void *))
PAIR(void, sauti_avr_pcm_to_f32_asm, (const uint8_t *, float *, size_t, uint32_t, uint32_t))
PAIR(void, sauti_dvsm_pcm_f32_asm, (const uint8_t *, float *, size_t, uint32_t))
PAIR(void, sauti_dvsm_delta_mono_asm, (const uint8_t *, float *, size_t, const int16_t *))
PAIR(void, sauti_dvsm_delta_stereo_block_asm, (const uint8_t *, float *, size_t, const int16_t *))
PAIR(void, sauti_dvsm_voice_mono_asm, (const uint8_t *, float *, size_t, const int16_t *))
PAIR(void, sauti_dvsm_voice_stereo_block_asm, (const uint8_t *, float *, size_t, const int16_t *))
PAIR(void, sauti_ym_tone_step_asm, (const uint8_t *, uint32_t *, uint32_t, float *, uint32_t, uint32_t))
PAIR(uint32_t, sauti_ym_noise_step_asm, (uint32_t *, uint32_t *, uint32_t, uint32_t))
PAIR(void, sauti_ym_env_init_asm, (void *, uint32_t))
PAIR(uint32_t, sauti_ym_env_step_asm, (void *, uint32_t, uint32_t))
PAIR(float, sauti_ym_drum_sample_asm, (const uint8_t *, uint32_t))

static int g_fail;
static long g_cases;
#define CHECK(c, ...) do { g_cases++; if (!(c)) { if (g_fail < 40) { fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } g_fail++; } } while (0)

static uint64_t rs = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 16); }
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }
/* losowa liczba zmiennoprzecinkowa: skonczona, z rzadkimi zerami, denormalami i duzymi wartosciami (bez NaN: kolejnosc operandow nie jest okreslona dla dwoch NaN) */
static float rf(void) {
    const uint32_t k = rn(16); uint32_t u = rnd();
    if (k == 0) return 0.0f;
    if (k == 1) return -0.0f;
    if (k == 2) u &= 0x807fffffu;                                                    /* denormal */
    else if (k == 3) u = (u & 0x80000000u) | 0x7f000000u | (u & 0x7fffffu);       /* bardzo duza */
    else u = (u & 0x80000000u) | (0x30000000u + (u & 0x0fffffffu));               /* typowy zakres */
    { float f; memcpy(&f, &u, 4); return f; }
}
static int feq(float a, float b) { return memcmp(&a, &b, 4) == 0; }
static void fill(float *p, size_t n) { size_t i; for (i = 0; i < n; i++) p[i] = rf(); }
static void fillb(uint8_t *p, size_t n) { size_t i; for (i = 0; i < n; i++) p[i] = (uint8_t)rnd(); }

static void t_huffman(void) {
    int it;
    for (it = 0; it < 20000; it++) {
        SautiAacHuffNode nodes[48]; uint8_t data[12]; size_t cnt = rn(10) == 0 ? (rn(2) ? 4097u + rn(10) : rn(2)) : 2u + rn(40), len = rn(10), o1, o2; size_t i; int32_t s1 = 0x55555, s2 = 0x55555; int r1, r2;
        for (i = 0; i < 48; i++) { nodes[i].child_zero = rn(8) == 0 ? 0xffffu : (uint16_t)rn((uint32_t)cnt + 3u); nodes[i].child_one = rn(8) == 0 ? 0xffffu : (uint16_t)rn((uint32_t)cnt + 3u); nodes[i].symbol = (int16_t)rnd(); nodes[i].is_terminal = rn(4) == 0 ? (uint16_t)rn(4) : 0; }
        fillb(data, sizeof data);
        o1 = o2 = rn(10) == 0 ? (len * 8u + rn(3)) : rn((uint32_t)len * 8u + 1u);
        r1 = sauti_aac_huffman_decode_asm(data, len, &o1, nodes, cnt > 48 ? cnt : cnt, &s1);
        r2 = sauti_aac_huffman_decode_asm_c(data, len, &o2, nodes, cnt, &s2);
        CHECK(r1 == r2 && o1 == o2 && s1 == s2, "huffman cnt=%zu len=%zu: asm %d/%zu/%d c %d/%zu/%d", cnt, len, r1, o1, s1, r2, o2, s2);
    }
    {   size_t o = 0; int32_t s = 0; uint8_t d[1] = { 0 };
        CHECK(sauti_aac_huffman_decode_asm(NULL, 1, &o, NULL, 2, &s) == sauti_aac_huffman_decode_asm_c(NULL, 1, &o, NULL, 2, &s), "huffman null");
        CHECK(sauti_aac_huffman_decode_asm(d, ~(size_t)0 / 8u, &o, (const SautiAacHuffNode *)d, 0, &s) == sauti_aac_huffman_decode_asm_c(d, ~(size_t)0 / 8u, &o, (const SautiAacHuffNode *)d, 0, &s), "huffman huge length"); }
}
static void t_dequant(void) {
    int it;
    for (it = 0; it < 3000; it++) {
        static int32_t q[1100]; static float p[1100], d1[1100], d2[1100]; float sc; size_t n = rn(8) == 0 ? rn(1090) : 1u + rn(1024), i; int r1, r2;
        for (i = 0; i < n + 4; i++) { q[i] = rn(3) == 0 ? (int32_t)rn(5) - 2 : (int32_t)rnd(); p[i] = rf(); if (p[i] < 0) p[i] = -p[i]; d1[i] = d2[i] = -1.0f; }
        sc = rf();
        r1 = sauti_aac_dequant_scale_asm(q, p, &sc, d1, n); r2 = sauti_aac_dequant_scale_asm_c(q, p, &sc, d2, n);
        CHECK(r1 == r2 && memcmp(d1, d2, (n + 4) * 4) == 0, "dequant n=%zu ret %d/%d", n, r1, r2);
    }
    {   float a[4] = { 1, 2, 3, 4 }, b[4] = { 0 }; int32_t q[4] = { 1, -1, 2, -2 }; float sc = 2.0f;
        CHECK(sauti_aac_dequant_scale_asm(NULL, a, &sc, b, 4) == sauti_aac_dequant_scale_asm_c(NULL, a, &sc, b, 4), "dequant null q");
        CHECK(sauti_aac_dequant_scale_asm(q, a, &sc, b, 0) == sauti_aac_dequant_scale_asm_c(q, a, &sc, b, 0), "dequant n=0");
        CHECK(sauti_aac_dequant_scale_asm(q, a, &sc, b, 1025) == sauti_aac_dequant_scale_asm_c(q, a, &sc, b, 1025), "dequant n=1025"); }
}
static void t_window(void) {
    static const size_t ns[] = { 128, 1024, 0, 1, 127, 129, 256, 2048 }; int it;
    for (it = 0; it < 400; it++) {
        static float im[2048], w[2048], h1[1024], h2[1024], o1[1024], o2[1024]; size_t n = ns[rn(8)], i; int r1, r2;
        fill(im, 2048); fill(w, 2048); fill(h1, 1024); memcpy(h2, h1, sizeof h1); for (i = 0; i < 1024; i++) o1[i] = o2[i] = 7.0f;
        r1 = sauti_aac_window_overlap_asm(im, w, h1, o1, n); r2 = sauti_aac_window_overlap_asm_c(im, w, h2, o2, n);
        CHECK(r1 == r2 && memcmp(h1, h2, sizeof h1) == 0 && memcmp(o1, o2, sizeof o1) == 0, "window n=%zu ret %d/%d", n, r1, r2);
    }
}
static void t_imdct(void) {
    int it;
    for (it = 0; it < 3000; it++) {
        static float a[2100], b[2100]; size_t n = rn(10) == 0 ? rn(3) : 1u + rn(2048); double r1, r2;
        fill(a, n); fill(b, n);
        r1 = sauti_aac_imdct_dot_asm(a, b, n); r2 = sauti_aac_imdct_dot_asm_c(a, b, n);
        CHECK(memcmp(&r1, &r2, 8) == 0, "imdct dot n=%zu %.17g vs %.17g", n, r1, r2);
    }
    CHECK(sauti_aac_imdct_dot_asm(NULL, NULL, 5) == sauti_aac_imdct_dot_asm_c(NULL, NULL, 5), "imdct null");
}
static void t_spectral(void) {
    uint32_t cb, idx;
    for (cb = 0; cb < 15; cb++) for (idx = 0; idx < 400; idx++) {
        int16_t a[4] = { 111, 222, 333, 444 }, b[4] = { 111, 222, 333, 444 }; int r1 = sauti_aac_spectral_index_asm(cb, idx, a), r2 = sauti_aac_spectral_index_asm_c(cb, idx, b);
        CHECK(r1 == r2 && memcmp(a, b, sizeof a) == 0, "spectral cb=%u idx=%u: %d/%d", cb, idx, r1, r2);
    }
    CHECK(sauti_aac_spectral_index_asm(1, 0, NULL) == sauti_aac_spectral_index_asm_c(1, 0, NULL), "spectral null");
    CHECK(sauti_aac_spectral_index_asm(11, 0xffffffffu, (int16_t[4]){ 0 }) == sauti_aac_spectral_index_asm_c(11, 0xffffffffu, (int16_t[4]){ 0 }), "spectral huge index");
}
static void t_headers(void) {
    int it;
    for (it = 0; it < 40000; it++) {
        uint8_t d[16]; size_t n = rn(14); SautiAacAdts a, b; int r1, r2;
        fillb(d, sizeof d);
        if (rn(3)) { d[0] = 0xff; d[1] = (uint8_t)(0xf0u | (rnd() & 9u)); }
        if (rn(3)) d[2] = (uint8_t)((1u << 6) | ((rnd() % 13u) << 2) | (rnd() & 1u));
        if (rn(3)) { const unsigned len = 7u + rn(12); d[3] = (uint8_t)((d[3] & 0xfcu) | ((len >> 11) & 3u)); d[4] = (uint8_t)(len >> 3); d[5] = (uint8_t)((len & 7u) << 5 | (d[5] & 0x1fu)); }
        memset(&a, 0xaa, sizeof a); memset(&b, 0xaa, sizeof b);
        r1 = sauti_aac_adts_parse_asm(d, n, &a); r2 = sauti_aac_adts_parse_asm_c(d, n, &b);
        CHECK(r1 == r2 && memcmp(&a, &b, sizeof a) == 0, "adts n=%zu: %d/%d", n, r1, r2);
    }
    for (it = 0; it < 40000; it++) {
        uint8_t d[10]; size_t bytes = rn(11), o1, o2; unsigned cnt = rn(10) == 0 ? rn(40) : 1u + rn(32); uint32_t v1 = 0x1234, v2 = 0x1234; int r1, r2;
        fillb(d, sizeof d); o1 = o2 = rn(bytes * 8u + 3u);
        r1 = sauti_aac_bits_read_asm(d, bytes, &o1, cnt, &v1); r2 = sauti_aac_bits_read_asm_c(d, bytes, &o2, cnt, &v2);
        CHECK(r1 == r2 && o1 == o2 && v1 == v2, "bits n=%zu cnt=%u: %d/%d", bytes, cnt, r1, r2);
    }
}
static void t_psmix(void) {
    int it;
    for (it = 0; it < 4000; it++) {
        static uint8_t d[4096], e[4096], l1[4096], r1[4096], l2[4096], r2[4096]; float h[8]; size_t samples = rn(24), stride = 8u * (1u + rn(4)), i; const int cplx = (int)rn(2);
        fillb(d, sizeof d); fillb(e, sizeof e);
        for (i = 0; i < 4096; i += 4) { const float f = rf(); memcpy(d + i, &f, 4); const float g = rf(); memcpy(e + i, &g, 4); }
        fill(h, 8); memset(l1, 0x5a, sizeof l1); memcpy(l2, l1, sizeof l1); memcpy(r1, l1, sizeof l1); memcpy(r2, l1, sizeof l1);
        if (cplx) { sauti_aac_ps_mix_complex_strided_asm(d, e, l1, r1, h, samples, stride); sauti_aac_ps_mix_complex_strided_asm_c(d, e, l2, r2, h, samples, stride); }
        else { sauti_aac_ps_mix_strided_asm(d, e, l1, r1, h, samples, stride); sauti_aac_ps_mix_strided_asm_c(d, e, l2, r2, h, samples, stride); }
        CHECK(memcmp(l1, l2, sizeof l1) == 0 && memcmp(r1, r2, sizeof r1) == 0, "ps_mix %s samples=%zu stride=%zu", cplx ? "complex" : "real", samples, stride);
    }
}
static void t_avr(void) {
    int it;
    for (it = 0; it < 40000; it++) {
        uint8_t buf[200]; size_t len = rn(10) == 0 ? rn(200) : 128u + rn(72); struct { uint32_t a, b, c, d; uint64_t f; const uint8_t *p; } m1, m2; int r1, r2; uint32_t units;
        fillb(buf, sizeof buf); memcpy(buf, "2BIT", 4);
        if (rn(8)) { buf[12] = rn(2) ? 0xff : 0; buf[13] = buf[12]; if (rn(10) == 0) buf[13] = 1; }
        if (rn(8)) { buf[14] = 0; buf[15] = rn(2) ? 8 : 16; if (rn(12) == 0) buf[15] = 12; }
        if (rn(8)) { buf[16] = rn(2) ? 0xff : 0; buf[17] = buf[16]; }
        if (rn(8)) buf[40] = buf[41] = 0;
        units = rn(4) == 0 ? rnd() : (rn(6) * 2u);
        buf[26] = (uint8_t)(units >> 24); buf[27] = (uint8_t)(units >> 16); buf[28] = (uint8_t)(units >> 8); buf[29] = (uint8_t)units;
        if (rn(10) == 0) buf[0] = 'X';
        memset(&m1, 0, sizeof m1); memset(&m2, 0, sizeof m2);
        r1 = sauti_avr_parse_asm(buf, len, &m1); r2 = sauti_avr_parse_asm_c(buf, len, &m2);
        CHECK(r1 == r2 && (!r1 || (m1.a == m2.a && m1.b == m2.b && m1.c == m2.c && m1.d == m2.d && m1.f == m2.f && m1.p == m2.p)), "avr parse len=%zu: %d/%d", len, r1, r2);
    }
    for (it = 0; it < 2000; it++) {
        uint8_t src[600]; float d1[300], d2[300]; size_t n = rn(300); const uint32_t bits = rn(4) == 0 ? rnd() % 33u : (rn(2) ? 8u : 16u), sg = rn(3) ? rn(2) : rnd();
        fillb(src, sizeof src); memset(d1, 0x33, sizeof d1); memset(d2, 0x33, sizeof d2);
        sauti_avr_pcm_to_f32_asm(src, d1, n, bits, sg); sauti_avr_pcm_to_f32_asm_c(src, d2, n, bits, sg);
        CHECK(memcmp(d1, d2, sizeof d1) == 0, "avr convert n=%zu bits=%u sg=%u", n, bits, sg);
    }
}
static void t_dvsm(void) {
    int it;
    for (it = 0; it < 6000; it++) {
        uint8_t in[2200]; int16_t steps[256]; float o1[1100], o2[1100]; size_t i, frames = rn(8) == 0 ? rn(2) : 1u + rn(500); const unsigned kind = rn(6);
        fillb(in, sizeof in); for (i = 0; i < 256; i++) steps[i] = (int16_t)(rn(3) ? (int)rn(2000) - 1000 : (int)rnd());
        memset(o1, 0x44, sizeof o1); memset(o2, 0x44, sizeof o2);
        switch (kind) {
        case 0: { const uint32_t depth = rn(4) == 0 ? rnd() % 33u : (rn(2) ? 8u : 16u); sauti_dvsm_pcm_f32_asm(in, o1, frames, depth); sauti_dvsm_pcm_f32_asm_c(in, o2, frames, depth); break; }
        case 1: sauti_dvsm_delta_mono_asm(in, o1, frames, steps); sauti_dvsm_delta_mono_asm_c(in, o2, frames, steps); break;
        case 2: sauti_dvsm_delta_stereo_block_asm(in, o1, frames, steps); sauti_dvsm_delta_stereo_block_asm_c(in, o2, frames, steps); break;
        case 3: sauti_dvsm_voice_mono_asm(in, o1, frames, steps); sauti_dvsm_voice_mono_asm_c(in, o2, frames, steps); break;
        default: sauti_dvsm_voice_stereo_block_asm(in, o1, frames, steps); sauti_dvsm_voice_stereo_block_asm_c(in, o2, frames, steps); break;
        }
        CHECK(memcmp(o1, o2, sizeof o1) == 0, "dvsm kind=%u frames=%zu", kind, frames);
    }
}
typedef struct { uint32_t phase, index, attack, alternate, hold, holding; } Env;
static void t_ym(void) {
    int it; uint32_t shape, attr, b;
    for (it = 0; it < 40000; it++) {
        uint32_t p1 = rnd(), p2, l1 = rn(4) == 0 ? rnd() : (rnd() & 0x3ffffu), l2, step = rn(4) == 0 ? rnd() : rn(1u << 22), period = rn(40), r1, r2;
        p2 = p1; l2 = l1;
        r1 = sauti_ym_noise_step_asm(&p1, &l1, step, period); r2 = sauti_ym_noise_step_asm_c(&p2, &l2, step, period);
        CHECK(r1 == r2 && p1 == p2 && l1 == l2, "noise step=%u period=%u: %u/%u %u/%u %u/%u", step, period, r1, r2, p1, p2, l1, l2);
    }
    for (it = 0; it < 60000; it++) {
        uint8_t regs[16]; uint32_t ph1[3], ph2[3], step = rn(4) == 0 ? rnd() : rn(1u << 24), nb = rn(3) == 0 ? rnd() : rn(2), lv = rnd() & 63u; float o1 = 9.0f, o2 = 9.0f; size_t i;
        fillb(regs, sizeof regs); if (rn(4) == 0) regs[0] = regs[1] = 0; if (rn(3)) { regs[8] &= 31; regs[9] &= 31; regs[10] &= 31; }
        for (i = 0; i < 3; i++) ph1[i] = ph2[i] = rnd();
        sauti_ym_tone_step_asm(regs, ph1, step, &o1, nb, lv); sauti_ym_tone_step_asm_c(regs, ph2, step, &o2, nb, lv);
        CHECK(feq(o1, o2) && memcmp(ph1, ph2, sizeof ph1) == 0, "tone step=%u: %.9g/%.9g", step, (double)o1, (double)o2);
    }
    for (shape = 0; shape < 32; shape++) for (it = 0; it < 300; it++) {
        Env a, b2; uint32_t s, k; memset(&a, 0xcc, sizeof a); memset(&b2, 0xcc, sizeof b2);
        sauti_ym_env_init_asm(&a, shape); sauti_ym_env_init_asm_c(&b2, shape);
        CHECK(memcmp(&a, &b2, sizeof a) == 0, "env init shape=%u", shape);
        for (k = 0; k < 40; k++) {
            const uint32_t st = rn(3) == 0 ? rn(1u << 20) : rn(1u << 24), pe = rn(40); s = sauti_ym_env_step_asm(&a, st, pe); b = sauti_ym_env_step_asm_c(&b2, st, pe);
            CHECK(s == b && memcmp(&a, &b2, sizeof a) == 0, "env step shape=%u st=%u pe=%u: %u/%u", shape, st, pe, s, b);
        }
    }
    for (b = 0; b < 256; b++) for (attr = 0; attr < 8; attr++) { uint8_t v = (uint8_t)b; const float x = sauti_ym_drum_sample_asm(&v, attr), y = sauti_ym_drum_sample_asm_c(&v, attr); CHECK(feq(x, y), "drum byte=%u attr=%u: %.9g/%.9g", b, attr, (double)x, (double)y); }
}
int main(void) {
    t_huffman(); t_dequant(); t_window(); t_imdct(); t_spectral(); t_headers(); t_psmix(); t_avr(); t_dvsm(); t_ym();
    printf("portable kernels vs NASM: %s (%ld cases, %d differences)\n", g_fail ? "FAIL" : "PASS", g_cases, g_fail);
    return g_fail ? 1 : 0;
}
