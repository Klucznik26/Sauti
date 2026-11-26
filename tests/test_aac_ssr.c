/* AAC SSR (object type 3): gain_control_data() syntax and the synthesis (band IMDCT, gain control function, overlap-add, IPQF) of src/decoders/modern/aac/sauti_aac_ssr.c.
 *  1. the syntax: the widths of the fields for the four window sequences (5 bits long, 2 short, 4 + 2 start, 4 + 5 stop), max_band 0, truncation, refusal of a wrong sequence;
 *  2. the synthesis against fingerprints of the ISO/IEC 14496-5 reference gain control tool on 40 frames of random spectra and random gain data (all window sequences, both window shapes);
 *  3. algebra: no data == all-zero data, silence in = silence out, linearity, a state that is initialised again repeats the output. */
#include "sauti_aac_ssr.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "test_aac_ssr_fingerprints.h"

static int g_failures;
#define CHECK(c, ...) do { if (!(c)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { uint8_t b[256]; size_t n; } Bits;
static void put(Bits *x, unsigned v, unsigned w) { for (unsigned i = 0; i < w; i++, x->n++) { if (!(x->n & 7u)) x->b[x->n >> 3] = 0; if ((v >> (w - 1u - i)) & 1u) x->b[x->n >> 3] |= (uint8_t)(0x80u >> (x->n & 7u)); } }

static void test_syntax(void) {
    SautiAacSsrGain g; size_t pos; Bits x;
    /* long window: max_band 2 (bands 1 and 2), band 1: one change (level 6, location 17), band 2: none */
    memset(&x, 0, sizeof x); put(&x, 2, 2); put(&x, 1, 3); put(&x, 6, 4); put(&x, 17, 5); put(&x, 0, 3);
    pos = 0; CHECK(sauti_aac_ssr_gain_parse(x.b, (x.n + 7) / 8, &pos, 0, &g) == 1 && pos == x.n, "long: parse, %zu / %zu bits", pos, x.n);
    CHECK(g.max_band == 2 && g.band[1][0].num == 1 && g.band[1][0].lev[0] == 6 && g.band[1][0].loc[0] == 17 && g.band[2][0].num == 0 && g.band[3][0].num == 0, "long: values");
    /* eight short windows: 2-bit locations; max_band 1, window 0: 2 changes, window 5: 1 change, the others none */
    memset(&x, 0, sizeof x); put(&x, 1, 2);
    for (unsigned w = 0; w < 8; w++) {
        if (w == 0) { put(&x, 2, 3); put(&x, 3, 4); put(&x, 1, 2); put(&x, 15, 4); put(&x, 3, 2); }
        else if (w == 5) { put(&x, 1, 3); put(&x, 0, 4); put(&x, 2, 2); }
        else put(&x, 0, 3);
    }
    pos = 0; CHECK(sauti_aac_ssr_gain_parse(x.b, (x.n + 7) / 8, &pos, 2, &g) == 1 && pos == x.n, "short: parse");
    CHECK(g.band[1][0].num == 2 && g.band[1][0].lev[1] == 15 && g.band[1][0].loc[1] == 3 && g.band[1][5].num == 1 && g.band[1][5].loc[0] == 2 && g.band[1][7].num == 0, "short: values");
    /* long start: block 0 has 4-bit locations, block 1 has 2-bit locations */
    memset(&x, 0, sizeof x); put(&x, 1, 2); put(&x, 1, 3); put(&x, 4, 4); put(&x, 9, 4); put(&x, 1, 3); put(&x, 5, 4); put(&x, 3, 2);
    pos = 0; CHECK(sauti_aac_ssr_gain_parse(x.b, (x.n + 7) / 8, &pos, 1, &g) == 1 && pos == x.n && g.band[1][0].loc[0] == 9 && g.band[1][1].loc[0] == 3 && g.band[1][1].lev[0] == 5, "start: 4 + 2 bits");
    /* long stop: block 0 has 4-bit locations, block 1 has 5-bit locations */
    memset(&x, 0, sizeof x); put(&x, 1, 2); put(&x, 1, 3); put(&x, 4, 4); put(&x, 9, 4); put(&x, 1, 3); put(&x, 5, 4); put(&x, 30, 5);
    pos = 0; CHECK(sauti_aac_ssr_gain_parse(x.b, (x.n + 7) / 8, &pos, 3, &g) == 1 && pos == x.n && g.band[1][0].loc[0] == 9 && g.band[1][1].loc[0] == 30, "stop: 4 + 5 bits");
    /* max_band 0: two bits and nothing else */
    memset(&x, 0, sizeof x); put(&x, 0, 2); pos = 0; CHECK(sauti_aac_ssr_gain_parse(x.b, 1, &pos, 0, &g) == 1 && pos == 2 && g.max_band == 0, "max_band 0");
    /* truncation and a wrong window sequence leave the output alone */
    memset(&g, 0x55, sizeof g); memset(&x, 0, sizeof x); put(&x, 3, 2); put(&x, 1, 3); put(&x, 6, 4); pos = 0;
    CHECK(sauti_aac_ssr_gain_parse(x.b, 1, &pos, 0, &g) == 0 && g.max_band == 0x55, "truncated: refused, output unchanged");
    pos = 0; CHECK(sauti_aac_ssr_gain_parse(x.b, 2, &pos, 4, &g) == 0, "window sequence 4");
}

static unsigned long long rs = 88172645463325252ULL;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs >> 11); }
static int make_frame(int prev_seq, int *ws, int *shape, float spec[1024], SautiAacSsrGain *gain) {      /* the generator of the fingerprints: keep in step with the reference harness */
    *shape = (int)(rnd() & 1u);
    if (prev_seq == 0) *ws = (rnd() % 3 == 0) ? 1 : 0; else if (prev_seq == 1) *ws = 2; else if (prev_seq == 2) *ws = (rnd() % 2) ? 2 : 3; else *ws = (rnd() % 3 == 0) ? 1 : 0;
    for (int i = 0; i < 1024; i++) spec[i] = (float)(((int)(rnd() % 2001) - 1000) / (1.0 + i / 40.0));
    memset(gain, 0, sizeof *gain);
    {
        const int blocks = *ws == 0 ? 1 : *ws == 2 ? 8 : 2;
        gain->max_band = 3;
        for (int b = 1; b < 4; b++)
            for (int k = 0; k < blocks; k++) {
                int n = (int)(rnd() % 4), loc = 0;
                gain->band[b][k].num = (uint8_t)n;
                for (int j = 0; j < n; j++) {
                    const int lim = *ws == 2 ? 4 : (*ws == 0 ? 32 : (k == 0 ? 16 : (*ws == 1 ? 4 : 32)));
                    loc += 1 + (int)(rnd() % 3); if (loc >= lim) loc = lim - 1;
                    gain->band[b][k].lev[j] = (uint8_t)(rnd() % 16); gain->band[b][k].loc[j] = (uint8_t)loc;
                }
            }
    }
    return 1;
}
static int near(double a, double b) { return fabs(a - b) <= 1e-4 * (fabs(b) + 1.0); }
static void test_reference(void) {
    SautiAacSsrState st; int prev_seq = 0, prev_shape = 0;
    sauti_aac_ssr_state_init(&st);
    for (int f = 0; f < 40; f++) {
        float spec[1024], out[1024]; SautiAacSsrGain gain; int ws, shape; double a = 0, b = 0, c = 0, d = 0;
        make_frame(prev_seq, &ws, &shape, spec, &gain);
        CHECK(ws == ssr_fp[f].ws && shape == ssr_fp[f].shape, "frame %d: generator out of step (ws %d / %d)", f, ws, ssr_fp[f].ws);
        CHECK(sauti_aac_ssr_synthesis(&st, (unsigned)ws, (unsigned)prev_shape, (unsigned)shape, spec, &gain, out) == 1, "frame %d: synthesis", f);
        for (int i = 0; i < 1024; i++) { const double v = out[i]; a += v; b += v * v; c += v * (i + 1); if (i > 1) { const double dd = (double)out[i] - 2.0 * out[i - 1] + out[i - 2]; d += dd * dd; } }
        CHECK(near(a, ssr_fp[f].sum) && near(b, ssr_fp[f].sq) && near(c, ssr_fp[f].wsum) && near(d, ssr_fp[f].d2), "frame %d (ws %d shape %d): fingerprint %.6e %.6e %.6e %.6e vs %.6e %.6e %.6e %.6e", f, ws, shape, a, b, c, d,
              ssr_fp[f].sum, ssr_fp[f].sq, ssr_fp[f].wsum, ssr_fp[f].d2);
        prev_seq = ws; prev_shape = shape;
    }
}
static void test_algebra(void) {
    SautiAacSsrState s1, s2; SautiAacSsrGain zero; float spec[1024], a[1024], b[1024], z[1024] = { 0 }; int i;
    memset(&zero, 0, sizeof zero);
    for (i = 0; i < 1024; i++) spec[i] = (float)(sin(0.07 * i) * 100.0 / (1.0 + i / 64.0));
    sauti_aac_ssr_state_init(&s1); sauti_aac_ssr_state_init(&s2);
    for (int f = 0; f < 3; f++) {
        CHECK(sauti_aac_ssr_synthesis(&s1, 0, (unsigned)(f > 0 ? 1 : 0), 1, spec, NULL, a) == 1 && sauti_aac_ssr_synthesis(&s2, 0, (unsigned)(f > 0 ? 1 : 0), 1, spec, &zero, b) == 1, "synthesis");
        CHECK(memcmp(a, b, sizeof a) == 0, "frame %d: no gain data == all-zero gain data", f);
    }
    sauti_aac_ssr_state_init(&s1);
    CHECK(sauti_aac_ssr_synthesis(&s1, 0, 0, 0, z, NULL, a) == 1, "silence");
    for (i = 0; i < 1024; i++) if (a[i] != 0.0f) { CHECK(0, "silence in, sample %d = %g", i, a[i]); break; }
    {   /* linearity: twice the spectrum, twice the output */
        float spec2[1024], c1[1024], c2[1024];
        for (i = 0; i < 1024; i++) spec2[i] = 2.0f * spec[i];
        sauti_aac_ssr_state_init(&s1); sauti_aac_ssr_state_init(&s2);
        CHECK(sauti_aac_ssr_synthesis(&s1, 0, 0, 0, spec, NULL, c1) == 1 && sauti_aac_ssr_synthesis(&s2, 0, 0, 0, spec2, NULL, c2) == 1, "linearity: synthesis");
        for (i = 0; i < 1024; i++) if (fabs(c2[i] - 2.0 * c1[i]) > 1e-4 * (fabs(c1[i]) + 1e-3)) { CHECK(0, "linearity at %d: %g vs %g", i, c2[i], c1[i]); break; }
    }
    CHECK(sauti_aac_ssr_synthesis(&s1, 4, 0, 0, spec, NULL, a) == 0 && sauti_aac_ssr_synthesis(&s1, 0, 2, 0, spec, NULL, a) == 0 && sauti_aac_ssr_synthesis(&s1, 0, 0, 2, spec, NULL, a) == 0, "wrong sequence / shape refused");
}
int main(void) {
    test_syntax(); test_reference(); test_algebra();
    printf(g_failures ? "test_aac_ssr: %d FAILURES\n" : "test_aac_ssr: all passed\n", g_failures);
    return g_failures != 0;
}
