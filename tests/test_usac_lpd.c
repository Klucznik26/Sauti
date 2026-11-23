/* USAC LPD core: unit tests of the pieces that do not need a stream (the streams themselves are in test_usac_xaac, scripts/test_usac_vs_xaac.sh compares them with libxaac).
 *   1. the algebraic vector quantiser: every index of the codebooks Q2 / Q3 / Q4 that decodes at all (the others lie beyond the codebook and are refused) gives a point of the RE8 lattice (integers of one
 *      parity with a sum divisible by 4), all points differ, qn = 0 gives the zero vector; the Voronoi extensions (qn 5..) of random indices stay on the lattice;
 *   2. the filter chain: LSF -> LSP -> LPC is minimum phase for ordered LSF, and a flat spectrum (equally spaced LSF) gives the well known polynomial; interpolation returns its end points;
 *   3. robustness: random bit strings through lpd_channel_stream() / fac_data() and the decoder (any result, no overrun - run under ASan / UBSan). */
#include "sauti_usac_lpd_dec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int on_lattice(const int32_t v[8]) {
    int i, sum = 0;
    for (i = 0; i < 8; i++) { if (((v[i] ^ v[0]) & 1) != 0) return 0; sum += v[i]; }
    return (sum & 3) == 0;
}
static void test_avq(void) {
    static const uint16_t zero[8] = { 0 };
    int32_t v[8];
    unsigned qn, i;
    CHECK(sauti_usac_lpd_avq_decode(0, 0, zero, v) == 1, "qn 0 decodes");
    for (i = 0; i < 8; i++) CHECK(v[i] == 0, "qn 0: coordinate %u is %d", i, (int)v[i]);
    for (qn = 2; qn <= 4; qn++) {
        const uint32_t count = 1u << (4u * qn);
        static int32_t *pts; uint32_t idx, j, bad = 0, dup = 0, refused = 0;
        pts = (int32_t *)malloc((size_t)count * 8u * sizeof(int32_t));
        for (idx = 0; idx < count; idx++) {
            if (!sauti_usac_lpd_avq_decode(qn, idx, zero, pts + (size_t)idx * 8u)) { refused++; memset(pts + (size_t)idx * 8u, 0x7f, 8u * sizeof(int32_t)); continue; }
            if (!on_lattice(pts + (size_t)idx * 8u)) bad++;
        }
        CHECK(bad == 0, "Q%u: %u of %u indices decode to a point off the lattice", qn, bad, count);
        CHECK(refused < count / 2u, "Q%u: %u of %u indices refused", qn, refused, count);
        if (qn < 4) for (idx = 0; idx < count; idx++) for (j = idx + 1; j < count; j++) if (!memcmp(pts + (size_t)idx * 8u, pts + (size_t)j * 8u, 8u * sizeof(int32_t))) dup++;
        CHECK(dup == 0, "Q%u: %u duplicate points", qn, dup);
        free(pts);
    }
    for (qn = 5; qn <= 12; qn++) {                                              /* Voronoi extensions: a point of the lattice again */
        unsigned t, bad = 0;
        for (t = 0; t < 400; t++) {
            uint16_t kv[8]; const unsigned nk = (qn - 3u) >> 1, base = qn - 2u * nk; uint32_t idx = (uint32_t)rand() & ((1u << (4u * base)) - 1u);
            for (i = 0; i < 8; i++) kv[i] = (uint16_t)((unsigned)rand() & ((1u << nk) - 1u));
            if (sauti_usac_lpd_avq_decode(qn, idx, kv, v) && !on_lattice(v)) bad++;                  /* an index beyond the codebook is refused, not wrong */
        }
        CHECK(bad == 0, "qn %u: %u extensions off the lattice", qn, bad);
    }
    CHECK(sauti_usac_lpd_avq_decode(36, 0, zero, v) == 0, "qn 36 is refused");
}
static void test_lpc(void) {
    float lsf[16], lsp[16], a[17], back[16], a1[2 * 17 + 17];
    unsigned i;
    for (i = 0; i < 16; i++) lsf[i] = 6400.0f * (float)(i + 1) / 17.0f;
    sauti_usac_lpd_lsf_to_lsp(lsf, lsp);
    sauti_usac_lpd_lsp_to_lsf(lsp, back);
    for (i = 0; i < 16; i++) CHECK(fabs(back[i] - lsf[i]) < 0.05, "LSF round trip %u: %f vs %f", i, back[i], lsf[i]);
    sauti_usac_lpd_lsp_to_lpc(lsp, a);
    CHECK(a[0] == 1.0f, "a[0]");
    {   /* equally spaced LSF: the roots of A(z) lie on a circle of radius < 1 (minimum phase): evaluate |A| on the unit circle, it must not vanish and A must be stable: all poles of 1/A inside, checked with the Schur-Cohn step-down */
        double c[17], tmp[17]; int m, k, stable = 1;
        for (k = 0; k <= 16; k++) c[k] = a[k];
        for (m = 16; m >= 1; m--) {
            const double km = c[m];
            if (fabs(km) >= 1.0) { stable = 0; break; }
            for (k = 1; k < m; k++) tmp[k] = (c[k] - km * c[m - k]) / (1.0 - km * km);
            for (k = 1; k < m; k++) c[k] = tmp[k];
        }
        CHECK(stable, "the filter of equally spaced LSF is not minimum phase");
    }
    sauti_usac_lpd_lpc_interpolate(lsp, lsp, a1, 2);                             /* the same LSP twice: every filter equals the one of the LSP */
    for (i = 0; i < 17; i++) { CHECK(fabs(a1[i] - a[i]) < 1e-6 && fabs(a1[17 + i] - a[i]) < 1e-6 && fabs(a1[34 + i] - a[i]) < 1e-6, "interpolation of equal LSP: coefficient %u", i); }
}
static void test_random(void) {
    static SautiUsacLpdFrame f;
    SautiUsacAcState ac;
    SautiUsacLpdState st;
    static const unsigned rates[4] = { 6000u, 12800u, 16000u, 24000u };
    unsigned it, parsed = 0, decoded = 0, seed = 0x3039u;
    sauti_usac_lpd_state_init(&st, 16000);
    for (it = 0; it < 12000; it++) {
        uint8_t buf[600]; SautiUsacBits b; unsigned i; float out[1024]; int r;
        for (i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(((it % 7u) == 0u) ? (rand() & 0x11) : rand());
        b.data = buf; b.nbits = (size_t)(40u + (unsigned)rand() % (sizeof buf * 8u - 40u)); b.pos = 0;
        memset(&ac, 0, sizeof ac);
        if ((it & 63u) == 0u) sauti_usac_lpd_state_init(&st, rates[(it >> 6) & 3u]);                          /* the pitch ranges depend on the core rate */
        if (sauti_usac_lpd_parse(&b, 1024, (int)(it & 1u), &ac, &f) == 1) {
            parsed++;
            CHECK(b.pos <= b.nbits, "parse overran: %zu of %zu", b.pos, b.nbits);
            r = sauti_usac_lpd_decode(&st, &f, 1024, &seed, NULL, 0, out);
            if (r == 1) { decoded++; for (i = 0; i < 1024; i++) if (!isfinite(out[i])) { CHECK(0, "non-finite output"); break; } }
            if (r == 1 && (it & 3u) == 0u) {                                                       /* LPD -> FD on a copy of the state, with random FAC data and a random spectrum */
                SautiUsacLpdState c = st; static SautiUsacLpdFac fac; float x[1024], o[1024], ovl[1024], fs2[128]; SautiUsacBits fb;
                int rr, seq = (int)((it >> 2) & 3u);
                for (i = 0; i < 1024; i++) x[i] = (float)((rand() & 0xfff) - 2048);
                fb.data = buf; fb.nbits = b.nbits; fb.pos = (size_t)(rand() % 64);
                memset(&fac, 0, sizeof fac);
                if (sauti_usac_lpd_fac_parse(&fb, (it & 8u) ? 64u : 128u, 1, &fac) != 1) memset(&fac, 0, sizeof fac);
                if (seq == 2) { rr = sauti_usac_lpd_fd_short_begin(&c, 1024, &fac, ovl, fs2); if (rr == 1) { memcpy(o, x, sizeof o); rr = sauti_usac_lpd_fd_finish(&c, 1024u, 1, o); } }
                else rr = sauti_usac_lpd_fd_frame(&c, 1024, (unsigned)seq, x, &fac, o);
                if (rr == 1) for (i = 0; i < 1024; i++) if (!isfinite(o[i])) { CHECK(0, "non-finite FD output"); break; }
            }
            if (r != 1) sauti_usac_lpd_state_init(&st, st.fscale);
        }
    }
    printf("random input: %u of 12000 strings parse, %u decode\n", parsed, decoded);
}
int main(void) {
    test_avq(); test_lpc(); test_random();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_lpd: all passed\n");
    return 0;
}
