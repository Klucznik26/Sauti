/* USAC eSBR harmonic transposer (HBE): unit tests of sauti_usac_hbe.c (the streams themselves are in test_usac_xaac, scripts/test_usac_vs_xaac.sh and the frame by frame comparison with an instrumented libxaac of
 * docs SBR-115 check the numbers):
 *   1. the set-up from the SBR band tables: start / end band, synthesis size, first synthesis band, patch borders and the number of transposition orders for the tables of three libxaac streams (the values
 *      libxaac printed for them), the "distance above three bands" rule, a table that fits four orders, refusals (4:1, a synthesis that does not fit the 32 QMF bands, a wrong frame length);
 *   2. behaviour: silence in = silence out; the state is carried from frame to frame (the same frame twice does not give the same output) and cleared by a new set-up; two instances fed alike agree;
 *      a pitch above 127 is refused;
 *   3. what the transposer is for: a sinusoid in the low band (made by the real QMF analysis of this library) comes out at twice its frequency for the second order (plain products, pitch 0) and at three times for
 *      the third order, and with a pitch the cross products keep it there;
 *   4. the data tables: window prefixes, the table of the start bands, the phase vocoder rotation. */
#include "sauti_usac_hbe.h"
#include "sauti_usac_hbe_tables.h"
#include "sauti_aac_sbr_qmf_analysis.h"
#include <math.h>
#define M_PI_T 3.14159265358979323846
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t g_seed = 4242u;
static unsigned rnd(void) { g_seed = g_seed * 1664525u + 1013904223u; return g_seed >> 8; }
static double frand(void) { return (double)(rnd() & 0xffffu) / 32768.0 - 1.0; }

static SautiUsacHbe g_h, g_h2;
static SautiUsacHbeCx g_q[64 * 64], g_pv[64 * 64], g_pv2[64 * 64];

typedef struct { const char *name; unsigned n_low, low[8], n_high, high[14], start, end, ks, synth, stretch, xover[4]; } Tbl;
static const Tbl cases[] = {
    { "h1 (24 kHz)", 5, { 23, 27, 32, 38, 46, 61 }, 10, { 23, 25, 27, 29, 32, 35, 38, 42, 46, 53, 61 }, 23, 61, 10, 16, 3, { 23, 46, 61, 0 } },
    { "h2 (16 kHz, distance above three: the high table decides)", 4, { 30, 36, 44, 53, 64 }, 8, { 30, 33, 36, 40, 44, 48, 53, 58, 64 }, 30, 64, 12, 20, 3, { 30, 58, 64, 0 } },
    { "h3 (44.1 kHz)", 6, { 28, 32, 36, 40, 46, 52, 59 }, 12, { 28, 30, 32, 34, 36, 38, 40, 43, 46, 49, 52, 55, 59 }, 28, 59, 12, 20, 3, { 28, 55, 59, 0 } },
};

static void to8(const unsigned *src, unsigned n, uint8_t *dst) { unsigned i; for (i = 0; i < n; i++) dst[i] = (uint8_t)src[i]; }
static int setup(SautiUsacHbe *h, const Tbl *t) {
    uint8_t low[16], high[16];
    to8(t->low, t->n_low + 1u, low); to8(t->high, t->n_high + 1u, high);
    return sauti_usac_hbe_reinit(h, 32u, low, t->n_low, high, t->n_high, 0u, 0u);
}

static void test_setup(void) {
    unsigned c, i;
    for (c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        const Tbl *t = &cases[c];
        memset(&g_h, 0, sizeof g_h);
        CHECK(setup(&g_h, t), "%s: reinit refused", t->name);
        CHECK(g_h.start_band == t->start && g_h.end_band == t->end && g_h.k_start == t->ks && g_h.synth_size == t->synth && g_h.max_stretch == t->stretch,
              "%s: start %u end %u ks %u synth %u stretch %u", t->name, g_h.start_band, g_h.end_band, g_h.k_start, g_h.synth_size, g_h.max_stretch);
        for (i = 0; i < 4u; i++) CHECK(g_h.x_over[i] == t->xover[i], "%s: x_over[%u] = %u, expected %u", t->name, i, g_h.x_over[i], t->xover[i]);
    }
    {   /* eSBR 4:1: the synthesis is limited to 16 low band QMF bands (the 16-band analysis), six patches are looked for.  Tables of a libxaac 4:1 stream: start 14, end 52, four orders */
        static const uint8_t low[] = { 14, 17, 21, 26, 32, 39, 52 }, high[] = { 14, 15, 17, 19, 21, 23, 26, 29, 32, 35, 39, 45, 52 };
        memset(&g_h, 0, sizeof g_h);
        CHECK(sauti_usac_hbe_reinit(&g_h, 64u, low, 6u, high, 12u, 1u, 0u), "4:1: refused");
        CHECK(g_h.synth_size == 12u && g_h.k_start + g_h.synth_size <= 16u && g_h.start_band == 14u && g_h.end_band == 52u, "4:1: synth %u k_start %u start %u end %u", g_h.synth_size, g_h.k_start, g_h.start_band, g_h.end_band);
    }
    {   /* a table that holds four orders: 4 * start <= end.  libxaac keeps a stale maximum here (0 after a start); a decoder that follows the clause uses all four */
        static const uint8_t low[] = { 8, 11, 15, 20, 26, 33 }, high[] = { 8, 9, 11, 13, 15, 17, 20, 23, 26, 29, 33 };
        memset(&g_h, 0, sizeof g_h);
        CHECK(sauti_usac_hbe_reinit(&g_h, 32u, low, 5u, high, 10u, 0u, 0u), "four orders: refused");
        CHECK(g_h.max_stretch == 4u && g_h.x_over[0] == 8u && g_h.x_over[1] == 15u && g_h.x_over[2] == 23u && g_h.x_over[3] == 29u, "four orders: stretch %u xover %u %u %u %u", g_h.max_stretch, g_h.x_over[0], g_h.x_over[1], g_h.x_over[2], g_h.x_over[3]);
    }
    {   /* refusals leave the instance unusable only by their return value: the caller must not process after a refusal */
        uint8_t low[16], high[16];
        to8(cases[0].low, 6u, low); to8(cases[0].high, 11u, high);
        memset(&g_h, 0, sizeof g_h);
        CHECK(!sauti_usac_hbe_reinit(&g_h, 32u, low, 5u, high, 10u, 1u, 0u), "eSBR 4:1 with a frame of 32 slots must be refused");
        CHECK(!sauti_usac_hbe_reinit(&g_h, 64u, low, 5u, high, 10u, 0u, 0u), "a frame of 64 slots without eSBR 4:1 must be refused");
        CHECK(!sauti_usac_hbe_reinit(&g_h, 24u, low, 5u, high, 10u, 0u, 0u), "a frame of 24 slots must be refused");
        CHECK(!sauti_usac_hbe_reinit(&g_h, 32u, NULL, 5u, high, 10u, 0u, 0u) && !sauti_usac_hbe_reinit(NULL, 32u, low, 5u, high, 10u, 0u, 0u), "null tables must be refused");
        {   /* a start band whose synthesis (4 * ((start + 4) / 8 + 1) bands from k_start) does not fit below band 32 */
            static const uint8_t l2[] = { 33, 40, 50, 64 }, h2[] = { 33, 36, 40, 45, 50, 57, 64 };
            CHECK(!sauti_usac_hbe_reinit(&g_h, 32u, l2, 3u, h2, 6u, 0u, 0u), "start band 33 must be refused");
        }
        {   /* a 768 line core limits the synthesis to the 24 bands of its QMF */
            static const uint8_t l3[] = { 28, 32, 36, 40, 46, 52, 59 }, h3[] = { 28, 30, 32, 34, 36, 38, 40, 43, 46, 49, 52, 55, 59 };
            memset(&g_h, 0, sizeof g_h);
            CHECK(sauti_usac_hbe_reinit(&g_h, 32u, l3, 6u, h3, 12u, 0u, 1u) && g_h.k_start + g_h.synth_size <= 24u, "768 core: k_start %u synth %u", g_h.k_start, g_h.synth_size);
        }
    }
}

static void fill_random(SautiUsacHbeCx *q, double amp) {
    unsigned i, b;
    memset(q, 0, 32u * 64u * sizeof *q);
    for (i = 0; i < 32u; i++) for (b = 0; b < 32u; b++) { q[i * 64u + b].re = amp * frand(); q[i * 64u + b].im = amp * frand(); }
}

static void test_behaviour(void) {
    unsigned f, i, same = 0;
    memset(&g_h, 0, sizeof g_h); memset(&g_h2, 0, sizeof g_h2);
    CHECK(setup(&g_h, &cases[0]) && setup(&g_h2, &cases[0]), "setup");
    memset(g_q, 0, sizeof g_q);
    for (f = 0; f < 4u; f++) {
        double e = 0;
        CHECK(sauti_usac_hbe_process(&g_h, g_q, 24u, g_pv), "silence: refused");
        for (i = 0; i < 32u * 64u; i++) e += g_pv[i].re * g_pv[i].re + g_pv[i].im * g_pv[i].im;
        CHECK(e == 0.0, "silence in, energy %g out", e);
    }
    CHECK(!sauti_usac_hbe_process(&g_h, g_q, 128u, g_pv), "a pitch above 127 must be refused");
    memset(&g_h, 0, sizeof g_h); CHECK(setup(&g_h, &cases[0]), "setup");
    fill_random(g_q, 100.0);
    CHECK(sauti_usac_hbe_process(&g_h, g_q, 12u, g_pv) && sauti_usac_hbe_process(&g_h2, g_q, 12u, g_pv2), "process");
    CHECK(!memcmp(g_pv, g_pv2, sizeof g_pv), "two instances fed alike disagree");
    {   /* the same frame again: the history (the 13 column window, the filter memories) is no longer silence, so the output must change */
        static SautiUsacHbeCx first[32 * 64];
        unsigned nonzero = 0;
        memcpy(first, g_pv, sizeof first);
        CHECK(sauti_usac_hbe_process(&g_h, g_q, 12u, g_pv) && sauti_usac_hbe_process(&g_h2, g_q, 12u, g_pv2), "process 2");
        CHECK(!memcmp(g_pv, g_pv2, sizeof g_pv), "two instances fed alike disagree (second frame)");
        for (i = 0; i < 32u * 64u; i++) if (first[i].re != 0.0 || first[i].im != 0.0 || g_pv[i].re != 0.0 || g_pv[i].im != 0.0) { nonzero++; if (first[i].re == g_pv[i].re && first[i].im == g_pv[i].im) same++; }
        CHECK(nonzero > 0u && same * 10u < nonzero, "%u of %u output samples are the same in the first and the second frame of the same input: no state is carried", same, nonzero);
    }
    /* a new set-up clears the synthesis and analysis histories (the operating buffers are replayed by the caller) */
    memset(&g_h, 0, sizeof g_h); memset(&g_h2, 0, sizeof g_h2);
    CHECK(setup(&g_h, &cases[0]) && setup(&g_h2, &cases[0]), "setup");
    fill_random(g_q, 100.0);
    CHECK(sauti_usac_hbe_process(&g_h, g_q, 24u, g_pv), "process");
    CHECK(setup(&g_h, &cases[0]), "second set-up");
    CHECK(sauti_usac_hbe_process(&g_h, g_q, 24u, g_pv) && sauti_usac_hbe_process(&g_h2, g_q, 24u, g_pv2), "process after the second set-up");
    {   /* g_h2 saw the frame once, g_h saw it twice but its synthesis and analysis were cleared in between: the first output differs only through the operating buffers, which a set-up keeps */
        double e = 0, r = 0;
        for (i = 0; i < 32u * 64u; i++) { const double dr = g_pv[i].re - g_pv2[i].re, di = g_pv[i].im - g_pv2[i].im; e += dr * dr + di * di; r += g_pv2[i].re * g_pv2[i].re + g_pv2[i].im * g_pv2[i].im; }
        CHECK(e > 0.0 && r > 0.0, "the operating buffers must survive a set-up (e %g r %g)", e, r);
    }
}

/* the real QMF analysis of a sinusoid, frame after frame */
static void tone_frames(double freq_in_bands, double amp, unsigned frames, SautiUsacHbe *h, unsigned pitch, double *band_energy) {
    SautiAacSbrQmfAnalysisState st;
    static float pcm[1024]; static SautiAacSbrComplex low[32 * 32];
    unsigned f, i, b; size_t n = 0;
    memset(&st, 0, sizeof st);
    memset(band_energy, 0, 64u * sizeof *band_energy);
    for (f = 0; f < frames; f++) {
        for (i = 0; i < 1024u; i++, n++) pcm[i] = (float)(amp * sin(M_PI_T * (freq_in_bands + 0.0) / 32.0 * (double)n));      /* band b is centred on (b + 0.5) * pi / 32 rad per sample: a tone at `freq_in_bands` + 0.5 bands */
        CHECK(sauti_aac_sbr_qmf_analyze_32(pcm, 1024u, 32u, 32u, low, 32u * 32u, &st), "qmf analysis");
        memset(g_q, 0, sizeof g_q);
        for (i = 0; i < 32u; i++) for (b = 0; b < 32u; b++) { g_q[i * 64u + b].re = low[i * 32u + b].re; g_q[i * 64u + b].im = low[i * 32u + b].im; }
        CHECK(sauti_usac_hbe_process(h, g_q, pitch, g_pv), "tone frame %u", f);
        if (f + 2u >= frames) for (i = 0; i < 32u; i++) for (b = 0; b < 64u; b++) band_energy[b] += g_pv[i * 64u + b].re * g_pv[i * 64u + b].re + g_pv[i * 64u + b].im * g_pv[i * 64u + b].im;
    }
}
static unsigned peak(const double *e, unsigned from, unsigned to) { unsigned b, best = from; for (b = from; b < to; b++) if (e[b] > e[best]) best = b; return best; }

static void test_tones(void) {
    double e[64]; unsigned c, pk, k;
    /* h1's tables: the second order fills 23..46 from 11.5..23, the third order 46..61 from 15.3..20.3 */
    for (k = 0; k < 2u; k++) {
        const unsigned pitch = k ? 24u : 0u;
        for (c = 0; c < 3u; c++) {
            static const unsigned src[3] = { 14u, 17u, 21u };            /* the tone sits at the lower edge of the source band: sin(pi * src / 32 * n) is centred between bands src - 1 and src */
            memset(&g_h, 0, sizeof g_h); CHECK(setup(&g_h, &cases[0]), "setup");
            tone_frames((double)src[c], 0.3, 6u, &g_h, pitch, e);
            pk = peak(e, 23u, 46u);                                       /* the second order's patch */
            CHECK(pk >= 2u * src[c] - 2u && pk <= 2u * src[c] + 1u && e[pk] > 0.0, "pitch %u, tone at %u: the peak of the transposed signal is band %u (expected near %u)", pitch, src[c], pk, 2u * src[c] - 1u);
        }
    }
    memset(&g_h, 0, sizeof g_h); CHECK(setup(&g_h, &cases[0]), "setup");
    tone_frames(17.0, 0.3, 6u, &g_h, 0u, e);                              /* 3 * 16.5 = 49.5: in the third order's patch (46..61) the transposer puts energy as well */
    pk = peak(e, 46u, 61u);
    CHECK(pk >= 49u && pk <= 52u && e[pk] > 0.0, "third order: tone at 17 gives its peak at band %u (expected 49..52)", pk);
}

static void test_tables(void) {
    unsigned i; double e = 0;
    CHECK(sauti_usac_hbe_start_band_to_kl[0] == 0 && sauti_usac_hbe_start_band_to_kl[7] == 2 && sauti_usac_hbe_start_band_to_kl[23] == 10 && sauti_usac_hbe_start_band_to_kl[30] == 12, "start band table spot values");
    CHECK(sauti_usac_hbe_window[20] != 0.0f && sauti_usac_hbe_window[1200] != 0.0f, "window values");
    for (i = 0; i < 64u; i++) e += (double)sauti_usac_hbe_pv_cos[i] * sauti_usac_hbe_pv_cos[i] + (double)sauti_usac_hbe_pv_sin[i] * sauti_usac_hbe_pv_sin[i];
    CHECK(fabs(e - 64.0) < 1e-3, "the phase vocoder rotation is not unit: %.6f", e);
    for (i = 0; i < 256u; i++) {
        e = (double)sauti_usac_hbe_xprod_cos_2[2u * i] * sauti_usac_hbe_xprod_cos_2[2u * i] + (double)sauti_usac_hbe_xprod_cos_2[2u * i + 1u] * sauti_usac_hbe_xprod_cos_2[2u * i + 1u];
        CHECK(fabs(e - 1.0) < 1e-3, "cross product rotation %u of the second order is not unit: %.6f", i, e);
    }
    CHECK(fabs((double)sauti_usac_hbe_interp_coeff[0][0] - 0.3984033437) < 1e-6 && sauti_usac_hbe_interp_coeff[1][1] < 0.0f, "interpolation coefficients");
}


/* spectral replication of eSBR 4:1: the source band (+ 1) every band of 64 ends up with, from libxaac's function (hbe_repl_spec) on a buffer that holds band + 1 in every band, for eleven sets of patch borders */
static void test_replicate(void) {
    static const struct { int x[6], stretch, src[64]; } rc[] = {
    { {10,20,30,40,50,60}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,31,32,33,34,35,36,37,38,39,40,31,32,33,34,35,36,37,38,39,40,61,62,63,64} },
    { {10,20,30,40,50,0}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,31,32,33,34,35,36,37,38,39,40,51,52,53,54,55,56,57,58,59,60,61,62,63,64} },
    { {8,17,26,35,44,53}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,28,29,30,31,32,33,34,35,34,27,28,29,30,31,32,33,34,35,54,55,56,57,58,59,60,61,62,63,64} },
    { {12,24,36,48,60,64}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,37,38,39,40,41,42,43,44,45,46,47,48,45,46,47,48} },
    { {9,19,29,39,49,61}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,30,31,32,33,34,35,36,37,38,39,30,31,32,33,34,35,36,37,38,39,38,39,62,63,64} },
    { {11,23,35,47,59,64}, 3, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,24,25,26,27,28,29,30,31,32,33,34,35,24,25,26,27,28,29,30,31,32,33,34,35,30,31,32,33,34} },
    { {10,21,32,43,55,64}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,34,35,36,37,38,39,40,41,42,43,42,43,34,35,36,37,38,39,40,41,42} },
    { {14,28,42,52,0,0}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64} },
    { {6,13,19,26,33,40}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,21,22,23,24,25,26,25,20,21,22,23,24,25,26,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64} },
    { {10,20,30,41,51,64}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,32,33,34,35,36,37,38,39,40,41,32,33,34,35,36,37,38,39,40,41,38,39,40} },
    { {13,27,41,55,62,64}, 4, {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,48,49,50,51,52,53,54,53,54} },
    };
    unsigned c, b, col;
    for (c = 0; c < sizeof rc / sizeof rc[0]; c++) {
        memset(&g_h, 0, sizeof g_h);
        g_h.ready = 1u; g_h.no_bins = 32u; g_h.upsamp4 = 1u; g_h.max_stretch = (unsigned)rc[c].stretch;
        for (b = 0; b < 6u; b++) g_h.x_over[b] = (unsigned)rc[c].x[b];
        memset(g_pv, 0, sizeof g_pv);
        for (col = 0; col < 32u; col++) for (b = 0; b < 64u; b++) { g_pv[col * 64u + b].re = (double)(b + 1u); g_pv[col * 64u + b].im = -(double)(b + 1u); }
        sauti_usac_hbe_replicate(&g_h, g_pv);
        for (col = 0; col < 32u; col += 31u) for (b = 0; b < 64u; b++)
            CHECK((int)g_pv[col * 64u + b].re == rc[c].src[b] && (int)g_pv[col * 64u + b].im == -rc[c].src[b], "replication case %u column %u band %u: %d / %d, expected %d", c, col, b, (int)g_pv[col * 64u + b].re, (int)g_pv[col * 64u + b].im, rc[c].src[b]);
    }
}

/* golden vectors from libxaac (tests/data/make_hbe_golden.py): the first frames (from the reset state) of four streams go through the transposer, the sums of the output of every frame are compared.  They cover the plain
 * products (pitch below one band) and the cross products (pitch 12 is p = 1.0 exactly, 24, 36, 64), eSBR 2:1 and 4:1.  Tolerance 3e-5 of the energy scale: libxaac computes in float, this decoder in double. */
static void test_golden(void) {
    FILE *f = fopen("tests/data/hbe_golden.bin", "rb");
    int32_t ncases, c;
    static float inb[64 * 20 * 2];
    if (!f) { CHECK(0, "cannot read tests/data/hbe_golden.bin (run from the repository root)"); return; }
    {   char magic[4]; if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "HBEG", 4) || fread(&ncases, 4, 1, f) != 1) { CHECK(0, "golden file header"); fclose(f); return; } }
    for (c = 0; c < ncases; c++) {
        int32_t hd[14], fr, n;
        if (fread(hd, 4, 14, f) != 14) { CHECK(0, "golden case %d header", (int)c); break; }
        {
            const int nb = hd[0], up4 = hd[1], S = hd[2], ks = hd[3], start = hd[4], end = hd[5], ms = hd[6], nf = hd[13];
            memset(&g_h, 0, sizeof g_h);
            g_h.ready = 1u; g_h.no_bins = (unsigned)nb; g_h.upsamp4 = (unsigned)up4; g_h.synth_size = (unsigned)S; g_h.k_start = (unsigned)ks; g_h.start_band = (unsigned)start; g_h.end_band = (unsigned)end; g_h.max_stretch = (unsigned)ms;
            for (n = 0; n < 6; n++) g_h.x_over[n] = (unsigned)hd[7 + n];
            for (fr = 0; fr < nf; fr++) {
                int32_t pitch; double exp4[4], got[4] = { 0, 0, 0, 0 }, scale; int s, b, i;
                if (fread(&pitch, 4, 1, f) != 1 || fread(inb, 4, (size_t)nb * (size_t)S * 2u, f) != (size_t)nb * (size_t)S * 2u || fread(exp4, 8, 4, f) != 4) { CHECK(0, "golden case %d frame %d truncated", (int)c, (int)fr); break; }
                memset(g_q, 0, sizeof g_q);
                for (s = 0; s < nb; s++) for (b = 0; b < S; b++) { g_q[s * 64 + ks + b].re = (double)inb[((size_t)s * (size_t)S + (size_t)b) * 2u]; g_q[s * 64 + ks + b].im = (double)inb[((size_t)s * (size_t)S + (size_t)b) * 2u + 1u]; }
                CHECK(sauti_usac_hbe_process(&g_h, g_q, (unsigned)pitch, g_pv), "golden case %d frame %d refused", (int)c, (int)fr);
                for (s = 0; s < nb; s++) for (b = start; b < end; b++) {
                    const double re = g_pv[s * 64 + b].re, im = g_pv[s * 64 + b].im, w = ((double)s * 131.0 + (double)b * 7.0 + 1.0) / ((double)nb * 64.0);
                    got[0] += re; got[1] += im; got[2] += re * re + im * im; got[3] += (re + 2.0 * im) * w;
                }
                scale = sqrt(exp4[2] * (double)(nb * (end - start))) + 1e-9;
                for (i = 0; i < 4; i++) CHECK(fabs(got[i] - exp4[i]) <= (i == 2 ? 3e-5 * exp4[2] : 3e-5 * scale) + 1e-12, "golden case %d (S %d pitch %d) frame %d sum %d: %.9g, libxaac %.9g", (int)c, S, (int)pitch, (int)fr, i, got[i], exp4[i]);
            }
        }
    }
    fclose(f);
}

int main(void) {
    test_tables(); test_setup(); test_behaviour(); test_tones(); test_replicate(); test_golden();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_hbe: all passed\n"); return 0;
}
