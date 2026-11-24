/* AAC LTP (object type 4: long-term prediction): ics_info ltp_data, the LTP module (src/decoders/modern/aac/sauti_aac_ltp.c: forward MDCT of the predicted signal, window weights, analysis TNS,
 * the three-block time buffer) and the LTP decode path of sauti_aac_lcn.c.  The access units (tests/test_aac_ltp_vectors.h) and the fixtures tests/data/ltp_*.{m4a,aac,loas} come from the
 * repository's own stream generator (tests/data/make_ltp_fixtures.sh), NOT from an encoder; their decoded PCM agrees with FFmpeg to ~136 dB and (coarsely) with FAAD2 to ~60-80 dB
 * (scripts/test_aac_ltp_vs_ffmpeg.sh), so the fingerprints pin a verified result.
 *   1. ics_info: ltp_data (lag, gain index, used flags, band limit 40), ltp_data_present = 0, short windows, refusals (LC index), truncation at every length;
 *   2. the forward MDCT against its definition, the window weights against the synthesis, the analysis TNS filter as the inverse of the synthesis one;
 *   3. the prediction against an independent double-precision model (lags 0..2047, every gain, used flags, long / start / stop frames, both window shapes), no-ops (no ltp_data, EIGHT_SHORT),
 *      the buffer update;
 *   4. fingerprints of the first access units (stereo with M/S, TNS, short windows, KBD; mono), atomic on failure: every truncation and random bit flips leave the state (incl. the time
 *      buffers) and the output untouched;
 *   5. the public API: profile LTP, length, MP4 == ADTS (profile 3) == LOAS bit for bit, 1-sample reads, exact seek over the checkpoints of a 150-frame stream (the buffers), 13 sampling rates;
 *   6. refusals: SBR in the access units, frameLengthFlag 1, channel configuration 3 / 0. */
#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES
#include "maktabasauti/maktabasauti.h"
#include "sauti_aac_lcn.h"
#include "sauti_aac_ltp.h"
#include "sauti_aac_tns.h"
#include "sauti_aac_window.h"
#include "sauti_aac_syntax.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_layout.h"
#include "sauti_aac_lcm.h"
#include "test_aac_ltp_vectors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_failures, g_dump;
static const char *g_eol = ",\n";
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int near(double a, double b) { return fabs(a - b) <= 1e-5 * (1.0 + fabs(b)); }
static float frand(float scale) { return ((float)((int)(rnd() % 20001u) - 10000) / 10000.0f) * scale; }

typedef struct { uint8_t b[4096]; unsigned n; } Bits;
static void put(Bits *x, unsigned value, unsigned width) { while (width--) { const unsigned bit = (value >> width) & 1u; if ((x->n & 7u) == 0) x->b[x->n >> 3] = 0; if (bit) x->b[x->n >> 3] |= (uint8_t)(0x80u >> (x->n & 7u)); x->n++; } }

/* ---- 1. ics_info ------------------------------------------------------------------------------------------------------------------------------------------------ */
static void test_ics_info(void) {
    const unsigned ltp48 = SAUTI_AAC_IDX(3, SAUTI_AAC_LAYOUT_1024) | SAUTI_AAC_IDX_LTP_FLAG; SautiAacIcsInfo ics; Bits x; size_t pos; unsigned i;
    memset(&x, 0, sizeof x);                                                                                 /* ONLY_LONG, KBD, max_sfb 12, predictor_data_present, ltp_data_present, lag 1234, coef 5, flags */
    put(&x, 0, 1); put(&x, 0, 2); put(&x, 1, 1); put(&x, 12, 6); put(&x, 1, 1); put(&x, 1, 1); put(&x, 1234, 11); put(&x, 5, 3); for (i = 0; i < 12; i++) put(&x, (i % 3u) == 0, 1);
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, (x.n + 7) / 8, &pos, ltp48, &ics) == 1 && pos == x.n, "ltp_data parses to the end (pos %zu of %u)", pos, x.n);
    CHECK(ics.predictor_present == 1 && ics.ltp_present == 1 && ics.ltp_lag == 1234 && ics.ltp_coef == 5 && ics.window_shape == 1 && ics.max_sfb == 12, "fields: present %u ltp %u lag %u coef %u", ics.predictor_present, ics.ltp_present, ics.ltp_lag, ics.ltp_coef);
    for (i = 0; i < 12; i++) CHECK(ics.ltp_used[i] == ((i % 3u) == 0), "ltp_used[%u]", i);
    CHECK(ics.ltp_used[12] == 0, "no flag above max_sfb");
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, (x.n + 7) / 8, &pos, SAUTI_AAC_IDX(3, SAUTI_AAC_LAYOUT_1024), &ics) == 0, "the same bits in an LC stream are refused");
    for (unsigned len = 0; len * 8 < x.n; len++) { pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, len, &pos, ltp48, &ics) == 0 && pos == 0, "truncated to %u bytes must fail and leave pos", len); }
    memset(&x, 0, sizeof x); put(&x, 0, 1); put(&x, 0, 2); put(&x, 0, 1); put(&x, 49, 6); put(&x, 1, 1); put(&x, 1, 1); put(&x, 7, 11); put(&x, 0, 3); for (i = 0; i < 40; i++) put(&x, 1, 1);    /* flags stop at 40 */
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, (x.n + 7) / 8, &pos, ltp48, &ics) == 1 && pos == x.n && ics.ltp_used[39] == 1, "flags stop at 40: pos %zu of %u", pos, x.n);
    memset(&x, 0, sizeof x); put(&x, 0, 1); put(&x, 0, 2); put(&x, 0, 1); put(&x, 12, 6); put(&x, 1, 1); put(&x, 0, 1);     /* predictor_data_present, ltp_data_present = 0 */
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, (x.n + 7) / 8, &pos, ltp48, &ics) == 1 && pos == x.n && ics.predictor_present == 1 && ics.ltp_present == 0, "ltp_data_present = 0");
    memset(&x, 0, sizeof x); put(&x, 0, 1); put(&x, 0, 2); put(&x, 0, 1); put(&x, 12, 6); put(&x, 0, 1);                      /* no predictor_data */
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, (x.n + 7) / 8, &pos, ltp48, &ics) == 1 && pos == x.n && ics.predictor_present == 0 && ics.ltp_present == 0, "no predictor_data");
    memset(&x, 0, sizeof x); put(&x, 0, 1); put(&x, 2, 2); put(&x, 0, 1); put(&x, 5, 4); put(&x, 0x2a, 7);                  /* EIGHT_SHORT: no ltp_data */
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, 8, &pos, ltp48, &ics) == 1 && ics.ltp_present == 0 && pos == 1 + 2 + 1 + 4 + 7, "short windows carry no ltp_data");
    memset(&x, 0, sizeof x); put(&x, 1234, 11); put(&x, 6, 3); put(&x, 0x5, 3);                                              /* ltp_data alone (the second channel of a common window) */
    memset(&ics, 0, sizeof ics); ics.max_sfb = 3; pos = 0; CHECK(sauti_aac_ltp_data_parse(x.b, (x.n + 7) / 8, &pos, &ics) == 1 && pos == x.n && ics.ltp_present == 1 && ics.ltp_lag == 1234 && ics.ltp_coef == 6 && ics.ltp_used[0] == 1 && ics.ltp_used[1] == 0 && ics.ltp_used[2] == 1, "ltp_data_parse");
    pos = 0; ics.max_sfb = 3; CHECK(sauti_aac_ltp_data_parse(x.b, 2, &pos, &ics) == 0 && pos == 0, "ltp_data_parse truncated");
    CHECK(sauti_aac_ltp_data_parse(NULL, 4, &pos, &ics) == 0, "ltp_data_parse NULL");
}

/* ---- 2. transform pieces ---------------------------------------------------------------------------------------------------------------------------------------- */
static void direct_mdct(const double *z, double *X) {                                                         /* X[k] = sum_i z[i] cos(pi/N (i + 1/2 + N/2)(k + 1/2)), N = 1024 */
    for (unsigned k = 0; k < 1024; k++) { double s = 0; for (unsigned i = 0; i < 2048; i++) s += z[i] * cos(M_PI / 1024.0 * ((double)i + 0.5 + 512.0) * ((double)k + 0.5)); X[k] = s; }
}
static void test_transforms(void) {
    static float in[2048], out[1024]; static double z[2048], X[1024]; unsigned i, k; double worst = 0, peak = 0;
    for (i = 0; i < 2048; i++) { in[i] = frand(3000.0f); z[i] = (double)in[i]; }
    CHECK(sauti_aac_ltp_mdct(in, out) == 1, "forward MDCT");
    direct_mdct(z, X);
    for (k = 0; k < 1024; k++) { if (fabs(X[k]) > peak) peak = fabs(X[k]); const double d = fabs(X[k] - (double)out[k]); if (d > worst) worst = d; }
    CHECK(worst < 1e-4 * peak, "forward MDCT deviates %.3e from the definition (peak %.3e)", worst, peak);
    memset(in, 0, sizeof in); in[700] = 1.0f; sauti_aac_ltp_mdct(in, out); for (i = 0; i < 2048; i++) z[i] = 0; z[700] = 1.0; direct_mdct(z, X);
    for (k = 0, worst = 0; k < 1024; k++) { const double d = fabs(X[k] - (double)out[k]); if (d > worst) worst = d; }
    CHECK(worst < 1e-4, "an impulse: deviation %.3e", worst);
    /* window weights == the synthesis: with a zero previous overlap, pcm = imdct * w (first half) and next = imdct * w (second half) */
    for (unsigned seq = 0; seq < 4; seq++) for (unsigned ps = 0; ps < 2; ps++) for (unsigned cs = 0; cs < 2; cs++) {
        static float w[2048], x[2048], zero[1024], pcm[1024], next[1024];
        const int ok = sauti_aac_window_long_weights(1024, seq, ps, cs, w);
        if (seq == 2) { CHECK(!ok, "weights for EIGHT_SHORT are not defined"); continue; }
        CHECK(ok, "weights seq %u", seq);
        for (i = 0; i < 2048; i++) x[i] = frand(100.0f);
        memset(zero, 0, sizeof zero);
        CHECK(sauti_aac_window_synth_n(1024, seq, ps, cs, x, zero, pcm, next) == 1, "synth");
        for (i = 0; i < 1024; i++) { CHECK(pcm[i] == x[i] * w[i], "seq %u: pcm[%u] != x * w", seq, i); CHECK(next[i] == x[1024 + i] * w[1024 + i], "seq %u: next[%u] != x * w", seq, i); }
        if (seq == 1) {
            for (i = 1024; i < 1024 + 448; i++) CHECK(w[i] == 1.0f, "start window flat part at %u", i);
            for (i = 1024 + 448 + 128; i < 2048; i++) CHECK(w[i] == 0.0f, "start window zero tail at %u", i);
        }
        if (seq == 3) {
            for (i = 0; i < 448; i++) CHECK(w[i] == 0.0f, "stop window zero head at %u", i);
            for (i = 576; i < 1024; i++) CHECK(w[i] == 1.0f, "stop window flat part at %u", i);
        }
    }
    CHECK(sauti_aac_window_long_weights(1000, 0, 0, 0, (float *)in) == 0 && sauti_aac_window_long_weights(1024, 5, 0, 0, (float *)in) == 0 && sauti_aac_window_long_weights(1024, 0, 2, 0, (float *)in) == 0 && sauti_aac_window_long_weights(1024, 0, 0, 0, NULL) == 0, "weights: bad arguments");
    /* analysis TNS is the inverse of the synthesis filter */
    { SautiAacTnsData tns; SautiAacIcsInfo ics; static float a[1024], b[1024]; const unsigned idx = SAUTI_AAC_IDX(3, SAUTI_AAC_LAYOUT_1024); unsigned dir;
      memset(&ics, 0, sizeof ics); ics.window_count = 1; ics.group_count = 1; ics.group_length[0] = 1; ics.max_sfb = 49;
      for (dir = 0; dir < 2; dir++) {
          memset(&tns, 0, sizeof tns); tns.filter_count[0] = 2;
          tns.filter[0][0].length = 20; tns.filter[0][0].order = 6; tns.filter[0][0].direction = (uint8_t)dir; tns.filter[0][0].resolution = 4; { const int8_t c[6] = { 3, -2, 5, 1, -4, 2 }; memcpy(tns.filter[0][0].coefficients, c, 6); }
          tns.filter[0][1].length = 12; tns.filter[0][1].order = 3; tns.filter[0][1].direction = 0; tns.filter[0][1].resolution = 3; { const int8_t c[3] = { 2, -1, 1 }; memcpy(tns.filter[0][1].coefficients, c, 3); }
          for (i = 0; i < 1024; i++) a[i] = b[i] = frand(1000.0f);
          CHECK(sauti_aac_tns_analysis(&tns, &ics, idx, a) == 1, "analysis");
          { double d = 0; for (i = 0; i < 1024; i++) d += fabs((double)a[i] - (double)b[i]); CHECK(d > 1.0, "analysis must change the spectrum (%.3g)", d); }
          CHECK(sauti_aac_tns_apply(&tns, &ics, idx, a) == 1, "synthesis");
          { double worst = 0; for (i = 0; i < 1024; i++) { const double d = fabs((double)a[i] - (double)b[i]); if (d > worst) worst = d; } CHECK(worst < 2e-3, "direction %u: synthesis(analysis(x)) deviates %.3e", dir, worst); }
      } }
}

/* ---- 3. the prediction against an independent model ------------------------------------------------------------------------------------------------------------ */
static double kbd_or_sine(unsigned m, unsigned shape, double alpha, unsigned i);
static double ref_weight(unsigned seq, unsigned ps, unsigned cs, unsigned i) {                              /* long / start / stop window of 2048 samples from the definition (sine, KBD alpha 4 / 6) */
    const double alpha_l = 4.0, alpha_s = 6.0; (void)alpha_l;
    const unsigned n = 1024, ns = 128, lead = 448;
    double w;
    #define KB(m, shape, a, j) kbd_or_sine((m), (shape), (a), (j))
    if (seq == 0) w = i < n ? KB(n, ps, 4.0, i) : KB(n, cs, 4.0, 2 * n - 1 - i);
    else if (seq == 1) w = i < n ? KB(n, ps, 4.0, i) : i < n + lead ? 1.0 : i < n + lead + ns ? KB(ns, cs, alpha_s, n + lead + ns - 1 - i) : 0.0;
    else w = i < lead ? 0.0 : i < lead + ns ? KB(ns, ps, alpha_s, i - lead) : i < n ? 1.0 : KB(n, cs, 4.0, 2 * n - 1 - i);
    return w;
}
static double bessel_i0(double x) { double term = 1, sum = 1; int k; for (k = 1; k < 60; k++) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; } return sum; }
static double kbd_or_sine(unsigned m, unsigned shape, double alpha, unsigned i) {
    if (shape == 0) return sin(M_PI / (2.0 * (double)m) * ((double)i + 0.5));
    { double num = 0, den = 0; unsigned j; for (j = 0; j <= m; j++) { const double r = 2.0 * (double)j / (double)m - 1.0; const double kv = bessel_i0(M_PI * alpha * sqrt(1.0 - r * r)); if (j <= i) num += kv; den += kv; } return sqrt(num / den); }
}
static const double gain_table[8] = { 0.570829, 0.696616, 0.813004, 0.911304, 0.984900, 1.067894, 1.194601, 1.369533 };
static void test_prediction(void) {
    const unsigned idx = SAUTI_AAC_IDX(3, SAUTI_AAC_LAYOUT_1024); const uint16_t *off = NULL; unsigned bands = 0, cases = 0;
    static SautiAacLtpState st; static float spec[1024], keep[1024]; static double z[2048], X[1024]; SautiAacIcsInfo ics; unsigned i, k;
    CHECK(sauti_aac_band_offsets(idx, 0, &off, &bands) == 1, "bands");
    for (i = 0; i < 3072; i++) st.buf[i] = 1.0f + (float)i;
    sauti_aac_ltp_reset(&st); for (i = 0; i < 3072; i++) CHECK(st.buf[i] == 0.0f, "reset buffer %u", i);
    for (i = 0; i < 3072; i++) st.buf[i] = frand(2000.0f);
    static const unsigned lags[] = { 0, 1, 500, 1023, 1024, 1500, 2047 };
    for (unsigned li = 0; li < sizeof lags / sizeof lags[0]; li++) for (unsigned seq = 0; seq < 4; seq += (li & 1u) ? 1u : 3u) {                 /* seq 0 and 3 for even lags, all for odd */
        const unsigned ps = li & 1u, cs = (li >> 1) & 1u, coef = (li + seq) & 7u;
        if (seq == 2) continue;
        memset(&ics, 0, sizeof ics); ics.window_count = 1; ics.group_count = 1; ics.group_length[0] = 1; ics.max_sfb = 40; ics.window_sequence = (uint8_t)seq; ics.window_shape = cs; ics.predictor_present = 1;
        ics.ltp_present = 1; ics.ltp_lag = (uint16_t)lags[li]; ics.ltp_coef = (uint8_t)coef; for (i = 0; i < 40; i++) ics.ltp_used[i] = (uint8_t)((i + li) % 3u != 0);
        for (i = 0; i < 1024; i++) spec[i] = keep[i] = frand(50.0f);
        CHECK(sauti_aac_ltp_apply(&st, &ics, idx, ps, NULL, spec) == 1, "apply lag %u seq %u", lags[li], seq);
        { const unsigned count = lags[li] < 1024 ? lags[li] + 1024 : 2048;
          for (i = 0; i < 2048; i++) z[i] = (i < count ? (double)st.buf[i + 2048 - lags[li]] * gain_table[coef] : 0.0) * ref_weight(seq, ps, cs, i); }
        direct_mdct(z, X);
        { double peak = 0, worst = 0; for (k = 0; k < 1024; k++) { if (fabs(X[k]) > peak) peak = fabs(X[k]); }
          for (unsigned sfb = 0; sfb < 49; sfb++) for (k = off[sfb]; k < off[sfb + 1]; k++) {
              const double want = (double)keep[k] + (sfb < 40 && ics.ltp_used[sfb] ? X[k] : 0.0), d = fabs(want - (double)spec[k]); if (d > worst) worst = d; }
          CHECK(worst < 2e-4 * peak + 1e-3, "lag %u seq %u shapes %u/%u coef %u: deviation %.3e (peak %.3e)", lags[li], seq, ps, cs, coef, worst, peak); }
        cases++;
    }
    CHECK(cases >= 7, "%u prediction cases", cases);
    /* no-ops and refusals */
    memset(&ics, 0, sizeof ics); ics.window_count = 1; ics.group_count = 1; ics.group_length[0] = 1; ics.max_sfb = 40; ics.predictor_present = 1; ics.ltp_lag = 100; ics.ltp_coef = 3; memset(ics.ltp_used, 1, sizeof ics.ltp_used);
    for (i = 0; i < 1024; i++) spec[i] = keep[i] = frand(50.0f);
    ics.ltp_present = 0; CHECK(sauti_aac_ltp_apply(&st, &ics, idx, 0, NULL, spec) == 1 && memcmp(spec, keep, sizeof spec) == 0, "ltp_data_present = 0: nothing added");
    ics.ltp_present = 1; ics.window_sequence = 2; CHECK(sauti_aac_ltp_apply(&st, &ics, idx, 0, NULL, spec) == 1 && memcmp(spec, keep, sizeof spec) == 0, "EIGHT_SHORT: nothing added");
    ics.window_sequence = 0; memset(ics.ltp_used, 0, sizeof ics.ltp_used); CHECK(sauti_aac_ltp_apply(&st, &ics, idx, 0, NULL, spec) == 1 && memcmp(spec, keep, sizeof spec) == 0, "no band used: nothing added");
    memset(ics.ltp_used, 1, sizeof ics.ltp_used); ics.max_sfb = 5; sauti_aac_ltp_apply(&st, &ics, idx, 0, NULL, spec);
    for (k = off[5]; k < 1024; k++) if (spec[k] != keep[k]) { CHECK(0, "line %u above max_sfb changed", k); break; }
    ics.max_sfb = 40; memcpy(spec, keep, sizeof spec); ics.ltp_lag = 2048; CHECK(sauti_aac_ltp_apply(&st, &ics, idx, 0, NULL, spec) == 0 && memcmp(spec, keep, sizeof spec) == 0, "lag 2048 refused, spectrum untouched");
    ics.ltp_lag = 10; ics.ltp_coef = 8; CHECK(sauti_aac_ltp_apply(&st, &ics, idx, 0, NULL, spec) == 0, "gain index 8 refused");
    ics.ltp_coef = 0; CHECK(sauti_aac_ltp_apply(&st, &ics, idx, 2, NULL, spec) == 0 && sauti_aac_ltp_apply(NULL, &ics, idx, 0, NULL, spec) == 0 && sauti_aac_ltp_apply(&st, NULL, idx, 0, NULL, spec) == 0 && sauti_aac_ltp_apply(&st, &ics, idx, 0, NULL, NULL) == 0, "bad arguments");
    CHECK(sauti_aac_ltp_apply(&st, &ics, SAUTI_AAC_IDX(3, SAUTI_AAC_LAYOUT_960), 0, NULL, spec) == 0, "LTP needs the 1024 layout");
    /* with TNS: the prediction goes through the analysis filter */
    { SautiAacTnsData tns; static float a[1024], b[1024]; memset(&tns, 0, sizeof tns); tns.filter_count[0] = 1; tns.filter[0][0].length = 20; tns.filter[0][0].order = 4; tns.filter[0][0].resolution = 4; { const int8_t c[4] = { 3, -2, 5, 1 }; memcpy(tns.filter[0][0].coefficients, c, 4); }
      ics.ltp_lag = 300; ics.ltp_coef = 2; ics.max_sfb = 40; ics.window_sequence = 0; memset(spec, 0, sizeof spec); memcpy(a, spec, sizeof a); memcpy(b, spec, sizeof b);
      sauti_aac_ltp_apply(&st, &ics, idx, 0, NULL, a); sauti_aac_ltp_apply(&st, &ics, idx, 0, &tns, b);
      { double d = 0; for (k = 0; k < 1024; k++) d += fabs((double)a[k] - (double)b[k]); CHECK(d > 1.0, "TNS changes the prediction (%.3g)", d); }
      sauti_aac_tns_apply(&tns, &ics, idx, b); { double worst = 0; for (k = 0; k < off[40]; k++) { const double dd = fabs((double)a[k] - (double)b[k]); if (dd > worst) worst = dd; } CHECK(worst < 5e-2, "synthesis(analysis(prediction)) == prediction: %.3e", worst); } }
    /* the buffer update: [0,1024) <- [1024,2048), [1024,2048) <- pcm, [2048,3072) <- overlap */
    { static float pcm[1024], ov[1024], old[3072]; for (i = 0; i < 1024; i++) { pcm[i] = (float)i + 0.25f; ov[i] = -(float)i - 0.5f; } memcpy(old, st.buf, sizeof old); sauti_aac_ltp_update(&st, pcm, ov);
      CHECK(memcmp(st.buf, old + 1024, 1024 * sizeof(float)) == 0 && memcmp(st.buf + 1024, pcm, sizeof pcm) == 0 && memcmp(st.buf + 2048, ov, sizeof ov) == 0, "buffer update"); }
}

/* ---- 4. access units through sauti_aac_lcn_decode ------------------------------------------------------------------------------------------------------------- */
typedef struct { const char *name; unsigned rate_index, channels; const uint8_t *const *au; const size_t *au_n; size_t frames; const double (*fp)[2][4]; } Stream;
static void fingerprint(const float *pcm, unsigned n, unsigned ch, double out[2][4]) {                    /* channel 0 and (for more than two channels) the last channel */
    unsigned c, i;
    for (c = 0; c < (ch < 2 ? ch : 2u); c++) {
        const unsigned chan = c == 0 ? 0 : ch - 1;
        double s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        for (i = 0; i < n; i++) { const double v = (double)pcm[i * ch + chan]; s1 += v; s2 += v * v; s3 += v * (double)(i + 1) / (double)n; if (i >= 2) { const double d = v - 2.0 * (double)pcm[(i - 1) * ch + chan] + (double)pcm[(i - 2) * ch + chan]; s4 += d * d; } }
        out[c][0] = s1; out[c][1] = s2; out[c][2] = s3; out[c][3] = s4;
    }
}
#include "test_aac_ltp_fingerprints.h"
static void cfg_ltp(SautiAacLcnConfig *c, unsigned rate, unsigned channels) { memset(c, 0, sizeof *c); c->rate_index = rate; c->channel_config = channels; c->ltp = 1; }
static int ltp_state_is_reset(const SautiAacLcnState *s) { unsigned c, i; for (c = 0; c < 2; c++) for (i = 0; i < 3072; i++) if (s->ltp[c].buf[i] != 0.0f) return 0; return 1; }
static void test_vectors(const Stream *s) {
    SautiAacLcnConfig cfg; static SautiAacLcnState st; float pcm[2 * 1024]; size_t f; SautiAacReason why;
    cfg_ltp(&cfg, s->rate_index, s->channels);
    CHECK(sauti_aac_lcn_frame_samples(&cfg) == 1024u, "%s: frame length", s->name);
    sauti_aac_lcn_state_init(&st); CHECK(ltp_state_is_reset(&st), "%s: LTP buffers not zero after init", s->name);
    for (f = 0; f < s->frames; f++) {
        double fp[2][4] = { { 0 } }; unsigned c, k;
        CHECK(sauti_aac_lcn_decode(&cfg, &st, s->au[f], s->au_n[f], pcm, &why) == 1, "%s frame %zu: decode", s->name, f);
        fingerprint(pcm, 1024, s->channels, fp);
        if (g_dump) { printf("    { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} }%s", fp[0][0], fp[0][1], fp[0][2], fp[0][3], fp[1][0], fp[1][1], fp[1][2], fp[1][3], g_eol); continue; }
        for (c = 0; c < s->channels; c++) for (k = 0; k < 4; k++) CHECK(near(fp[c][k], s->fp[f][c][k]), "%s frame %zu ch %u fingerprint %u: %.12e vs %.12e", s->name, f, c, k, fp[c][k], s->fp[f][c][k]);
    }
    CHECK(!ltp_state_is_reset(&st), "%s: the LTP buffers carry the output", s->name);
}
static void test_atomic(const Stream *s) {
    SautiAacLcnConfig cfg; static SautiAacLcnState st, before; float pcm[2 * 1024], keep[2 * 1024]; SautiAacReason why; size_t f, len, i; int round; long accepted = 0, rejected = 0;
    cfg_ltp(&cfg, s->rate_index, s->channels);
    sauti_aac_lcn_state_init(&st);
    for (i = 0; i < 2 * 1024; i++) keep[i] = pcm[i] = 12345.0f;
    for (f = 0; f < s->frames; f++) {
        for (len = 0; len <= s->au_n[f]; len++) {                                                           /* every truncation */
            int r; before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcn_decode(&cfg, &st, s->au[f], len, pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu len %zu: failure (%d) must leave state and output alone", s->name, f, len, r); rejected++; }
            else { for (i = 0; i < 1024u * s->channels; i++) CHECK(isfinite(pcm[i]), "%s: non-finite output", s->name); st = before; accepted++; }
        }
        for (round = 0; round < 150; round++) {                                                             /* random bit flips */
            uint8_t au[2048]; int r, nflip = 1 + (int)(rnd() % 4u);
            memcpy(au, s->au[f], s->au_n[f]);
            while (nflip--) { const size_t at = rnd() % s->au_n[f]; au[at] ^= (uint8_t)(1u << (rnd() & 7u)); }
            before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcn_decode(&cfg, &st, au, s->au_n[f], pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu flip: failure (%d) must leave state and output alone", s->name, f, r); rejected++; }
            else { for (i = 0; i < 1024u * s->channels; i++) CHECK(isfinite(pcm[i]), "%s: non-finite output after flips", s->name); st = before; accepted++; }
        }
        { SautiAacLcnState adv = st; sauti_aac_lcn_decode(&cfg, &adv, s->au[f], s->au_n[f], pcm, &why); st = adv; }
    }
    printf("%s: %ld accepted, %ld rejected mutated or truncated access units\n", s->name, accepted, rejected);
    CHECK(rejected > 100, "%s: mutations never fail?", s->name);
}

/* ---- 5. the public API on the fixtures -------------------------------------------------------------------------------------------------------------------------- */
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *b; long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET); b = (uint8_t *)malloc((size_t)len + 1u);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)len; return b;
}
static maktabasauti_decoder_t *open_mem(const uint8_t *data, size_t n, maktabasauti_stream_info_t *info, int *result) {
    maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
    if (maktabasauti_io_from_memory(data, n, NULL, &p.io) != MAKTABASAUTI_OK) { if (result) *result = 1; return NULL; }
    r = maktabasauti_decoder_open(&p, &d); if (result) *result = (int)r;
    if (r != MAKTABASAUTI_OK) { p.io.close(p.io.user); return NULL; }
    info->struct_size = sizeof *info; (void)maktabasauti_decoder_get_info(d, info);
    return d;
}
static float *decode_all(const uint8_t *data, size_t n, unsigned want_channels, unsigned want_rate, size_t chunk, size_t *frames_out, const char *name) {
    maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, NULL); float *pcm = NULL, *buf; size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    *frames_out = 0;
    if (!d) { CHECK(0, "%s: open", name); return NULL; }
    CHECK(info.channels == want_channels && info.sample_rate == want_rate, "%s: %u ch %u Hz, expected %u ch %u Hz", name, (unsigned)info.channels, (unsigned)info.sample_rate, want_channels, want_rate);
    CHECK(strcmp(info.codec_profile, "LTP") == 0, "%s: profile '%s'", name, info.codec_profile);
    buf = (float *)malloc(chunk * want_channels * sizeof(float));
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, chunk, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * want_channels * sizeof(float)); } memcpy(pcm + total * want_channels, buf, got * want_channels * sizeof(float)); total += got; }
        if (r == MAKTABASAUTI_EOF) break;
        if (r != MAKTABASAUTI_OK) { CHECK(0, "%s: read -> %d", name, (int)r); break; }
    }
    free(buf); maktabasauti_decoder_close(d); *frames_out = total; return pcm;
}
static void test_files(void) {
    static const struct { const char *path; unsigned channels, rate, frames; double fp[2][4]; } files[] = LTP_FILE_FINGERPRINTS;
    float *first[3] = { NULL, NULL, NULL }; size_t first_n[3] = { 0, 0, 0 }; unsigned k; const unsigned nfiles = (unsigned)(sizeof files / sizeof files[0]);
    for (k = 0; k < nfiles; k++) {
        size_t n, frames, one_frames, at; uint8_t *data = slurp(files[k].path, &n); float *pcm, *one; double fp[2][4] = { { 0 } }; unsigned c, j; int finite = 1; float peak = 0;
        if (!data) { CHECK(0, "cannot read %s (run from the repository root)", files[k].path); continue; }
        pcm = decode_all(data, n, files[k].channels, files[k].rate, 4096, &frames, files[k].path);
        CHECK(frames == files[k].frames * 1024u, "%s: %zu frames, expected %u", files[k].path, frames, files[k].frames * 1024u);
        for (at = 0; at < frames * files[k].channels; at++) { if (!isfinite(pcm[at])) finite = 0; if (fabsf(pcm[at]) > peak) peak = fabsf(pcm[at]); }
        CHECK(finite && peak > 0.0005f && peak < 20.0f, "%s: peak %.4f (not silent, finite)", files[k].path, (double)peak);
        fingerprint(pcm, (unsigned)frames, files[k].channels, fp);
        if (g_dump) printf("    { \"%s\", %u, %u, %u, { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} } }%s", files[k].path, files[k].channels, files[k].rate, files[k].frames, fp[0][0], fp[0][1], fp[0][2], fp[0][3], fp[1][0], fp[1][1], fp[1][2], fp[1][3], g_eol);
        else for (c = 0; c < (files[k].channels < 2 ? files[k].channels : 2u); c++) for (j = 0; j < 4; j++) CHECK(near(fp[c][j], files[k].fp[c][j]), "%s ch %u whole-file fingerprint %u: %.12e vs %.12e", files[k].path, c, j, fp[c][j], files[k].fp[c][j]);
        if (k < 3) {                                                                                         /* the three containers of ltp_st carry the same access units */
            one = decode_all(data, n, files[k].channels, files[k].rate, 1, &one_frames, "1-sample reads");
            CHECK(one && one_frames == frames && memcmp(one, pcm, frames * files[k].channels * sizeof(float)) == 0, "%s: 1-sample reads differ", files[k].path);
            free(one); first[k] = pcm; first_n[k] = frames * files[k].channels;
        } else free(pcm);
        free(data);
    }
    if (first[0] && first[1]) CHECK(first_n[0] == first_n[1] && memcmp(first[0], first[1], first_n[0] * sizeof(float)) == 0, "ADTS (profile 3) and MP4 of the same access units must decode identically");
    if (first[0] && first[2]) CHECK(first_n[0] == first_n[2] && memcmp(first[0], first[2], first_n[0] * sizeof(float)) == 0, "LOAS and MP4 of the same access units must decode identically");
    for (k = 0; k < 3; k++) free(first[k]);
}
static void test_seek(void) {                                                                                /* 150 frames: checkpoints at 64 and 128 carry the LTP buffers */
    size_t n, frames, got, i; uint8_t *data = slurp("tests/data/ltp_long.m4a", &n); maktabasauti_stream_info_t info; maktabasauti_decoder_t *d; float *ref, a[500];
    static const uint64_t targets[] = { 100000, 131072, 131000, 70000, 64 * 1024, 64 * 1024 - 3, 1000, 0, 153000, 140000 };
    if (!data) { CHECK(0, "cannot read tests/data/ltp_long.m4a"); return; }
    ref = decode_all(data, n, 1, 48000, 4096, &frames, "seek reference");
    d = open_mem(data, n, &info, NULL);
    if (d && ref) {
        for (i = 0; i < sizeof targets / sizeof targets[0]; i++) {
            const size_t want = targets[i] + 500u <= frames ? 500u : (size_t)(frames - targets[i]);
            maktabasauti_result_t r = maktabasauti_decoder_seek(d, targets[i], MAKTABASAUTI_SEEK_EXACT, NULL);
            CHECK(r == MAKTABASAUTI_OK, "seek %llu -> %d", (unsigned long long)targets[i], (int)r);
            r = maktabasauti_decoder_read(d, a, want, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == want && memcmp(a, ref + targets[i], want * sizeof(float)) == 0, "seek to %llu differs from sequential decoding (LTP buffers)", (unsigned long long)targets[i]);
        }
    } else CHECK(0, "seek test setup");
    if (d) maktabasauti_decoder_close(d);
    free(ref); free(data);
}
static int open_result(const uint8_t *data, size_t n) { int r = 0; maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, &r); if (d) maktabasauti_decoder_close(d); return r; }
static void test_refusals_files(void) {
    size_t n, i; uint8_t *data; unsigned patched = 0;
    data = slurp("tests/data/ltp_sbr.m4a", &n);
    if (!data) CHECK(0, "cannot read tests/data/ltp_sbr.m4a"); else { const int r = open_result(data, n); CHECK(r == (int)MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "an SBR fill element in an LTP stream: expected unsupported feature, got %d", r); free(data); }
    data = slurp("tests/data/ltp_st.m4a", &n);
    if (!data) { CHECK(0, "cannot read tests/data/ltp_st.m4a"); return; }
    CHECK(open_result(data, n) == (int)MAKTABASAUTI_OK, "the unpatched stereo fixture opens");
    /* ASC: AOT 4 (00100), rate 3 (0011), channels 2 (0010), frameLengthFlag 0, dependsOnCoreCoder 0, extensionFlag 0 = 0x21 0x90 (DecoderSpecificInfo, length 2) */
    for (i = 0; i + 3 < n; i++) if (data[i] == 0x02 && data[i + 1] == 0x21 && data[i + 2] == 0x90) {
        patched++;
        data[i + 2] = 0x94;                                                                                  /* frameLengthFlag 1 */
        CHECK(open_result(data, n) != (int)MAKTABASAUTI_OK, "LTP with frameLengthFlag 1 is refused");
        data[i + 2] = 0x98;                                                                                  /* channel_configuration 3 */
        CHECK(open_result(data, n) != (int)MAKTABASAUTI_OK, "LTP with channel_configuration 3 is refused");
        data[i + 2] = 0x80;                                                                                  /* channel_configuration 0 (PCE) */
        CHECK(open_result(data, n) != (int)MAKTABASAUTI_OK, "LTP with channel_configuration 0 is refused");
        data[i + 2] = 0x90;
    }
    CHECK(patched == 1u, "the AudioSpecificConfig of the fixture was found %u times", patched);
    free(data);
}

static void test_config(void) {
    SautiAacLcnConfig c; unsigned r, cc;
    for (r = 0; r <= 13; r++) for (cc = 0; cc <= 8; cc++) {
        cfg_ltp(&c, r, cc); CHECK(sauti_aac_lcn_frame_samples(&c) == ((r <= 12 && (cc == 1 || cc == 2)) ? 1024u : 0u), "LTP rate %u config %u: %u", r, cc, sauti_aac_lcn_frame_samples(&c));
        c.main = 1; CHECK(sauti_aac_lcn_frame_samples(&c) == 0u, "Main and LTP together are refused");
    }
}

/* ---- multichannel (sauti_aac_lcm): channel_configuration 3..7 ------------------------------------------------------------------------------------------------- */
static void test_lcm_config(void) {
    SautiAacLcnConfig c; unsigned r, cc;
    for (r = 0; r <= 13; r++) for (cc = 0; cc <= 8; cc++) {
        static const unsigned want[8] = { 0, 0, 0, 3, 4, 5, 6, 8 };
        memset(&c, 0, sizeof c); c.rate_index = r; c.channel_config = cc; c.ltp = 1;
        CHECK(sauti_aac_lcm_channels(&c) == ((cc <= 7 && r <= 12) ? want[cc] : 0u), "multichannel rate %u config %u: %u", r, cc, sauti_aac_lcm_channels(&c));
        c.main = 1; c.ltp = 1; CHECK(sauti_aac_lcm_channels(&c) == 0u, "Main and LTP together");
        memset(&c, 0, sizeof c); c.rate_index = r; c.channel_config = cc; CHECK(sauti_aac_lcm_channels(&c) == 0u, "neither Main nor LTP");
    }
}
static void test_lcm_atomic(void) {
    static SautiAacLcmState st, before; SautiAacLcnConfig cfg; float pcm[8 * 1024], keep[8 * 1024]; SautiAacReason why; size_t f, len, i; int round; long accepted = 0, rejected = 0;
    static const uint8_t *const au[] = { ltp_c6_au0, ltp_c6_au1, ltp_c6_au2 }; static const size_t au_n[] = { sizeof ltp_c6_au0, sizeof ltp_c6_au1, sizeof ltp_c6_au2 };
    memset(&cfg, 0, sizeof cfg); cfg.rate_index = 3; cfg.channel_config = 6; cfg.ltp = 1;
    CHECK(sauti_aac_lcm_channels(&cfg) == 6u, "5.1");
    sauti_aac_lcm_state_init(&st, &cfg);
    for (i = 0; i < 8 * 1024; i++) keep[i] = pcm[i] = 12345.0f;
    for (f = 0; f < 3; f++) {
        for (len = 0; len <= au_n[f]; len++) {                                                               /* every truncation */
            int r; before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcm_decode(&cfg, &st, au[f], len, pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "5.1 frame %zu len %zu: failure (%d) must leave state and output alone", f, len, r); rejected++; }
            else { for (i = 0; i < 6u * 1024u; i++) CHECK(isfinite(pcm[i]), "non-finite output"); st = before; accepted++; }
        }
        for (round = 0; round < 60; round++) {                                                               /* random bit flips */
            uint8_t b[4096]; int r, nflip = 1 + (int)(rnd() % 4u);
            memcpy(b, au[f], au_n[f]);
            while (nflip--) { const size_t at = rnd() % au_n[f]; b[at] ^= (uint8_t)(1u << (rnd() & 7u)); }
            before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcm_decode(&cfg, &st, b, au_n[f], pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "5.1 frame %zu flip: failure (%d) must leave state and output alone", f, r); rejected++; }
            else { st = before; accepted++; }
        }
        { SautiAacLcmState adv = st; sauti_aac_lcm_decode(&cfg, &adv, au[f], au_n[f], pcm, &why); st = adv; }
    }
    printf("5.1 ltp: %ld accepted, %ld rejected mutated or truncated access units\n", accepted, rejected);
    CHECK(rejected > 50, "5.1: mutations never fail?");
    { SautiAacLcnConfig bad = cfg; static const uint8_t none[1] = { 0 }; bad.channel_config = 2; CHECK(sauti_aac_lcm_decode(&bad, &st, none, 1, pcm, &why) == 0, "a stereo configuration is not for the multichannel decoder"); }
}
static void test_lcm_seek(void) {                                                                           /* 70 frames of 3 channels: the checkpoint at 64 carries the heap-allocated per-channel state */
    size_t n, frames, got, i; uint8_t *data = slurp("tests/data/ltp_c3_long.m4a", &n); maktabasauti_stream_info_t info; maktabasauti_decoder_t *d; float *ref, a[3 * 500];
    static const uint64_t targets[] = { 66000, 65536, 65535, 30000, 1000, 0, 71000, 66000, 40000 };
    if (!data) { CHECK(0, "cannot read tests/data/ltp_c3_long.m4a"); return; }
    ref = decode_all(data, n, 3, 48000, 4096, &frames, "multichannel seek reference");
    d = open_mem(data, n, &info, NULL);
    if (d && ref) {
        for (i = 0; i < sizeof targets / sizeof targets[0]; i++) {
            const size_t want = targets[i] + 500u <= frames ? 500u : (size_t)(frames - targets[i]);
            maktabasauti_result_t r = maktabasauti_decoder_seek(d, targets[i], MAKTABASAUTI_SEEK_EXACT, NULL);
            CHECK(r == MAKTABASAUTI_OK, "seek %llu -> %d", (unsigned long long)targets[i], (int)r);
            r = maktabasauti_decoder_read(d, a, want, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == want && memcmp(a, ref + targets[i] * 3u, want * 3u * sizeof(float)) == 0, "seek to %llu differs from sequential decoding (multichannel state)", (unsigned long long)targets[i]);
        }
    } else CHECK(0, "seek test setup");
    if (d) maktabasauti_decoder_close(d);
    free(ref); free(data);
}
/* ---- ADTS error check and ADIF ---------------------------------------------------------------------------------------------------------------------------------- */
static size_t read_until_error(const uint8_t *data, size_t n, unsigned channels, float *out, size_t cap, int *result) {
    maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, NULL); size_t total = 0, got; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *result = 0;
    if (!d) { *result = -999; return 0; }
    CHECK(info.channels == channels, "channels %u", (unsigned)info.channels);
    while (total < cap) {
        r = maktabasauti_decoder_read(d, out + total * channels, 1024 < cap - total ? 1024 : cap - total, MAKTABASAUTI_FORMAT_F32, &got);
        total += got;
        if (r != MAKTABASAUTI_OK) break;
    }
    *result = (int)r; maktabasauti_decoder_close(d); return total;
}
static void test_crc_and_adif(void) {
    size_t n_ref, n_crc, n_bad, n_adif; uint8_t *ref = slurp("tests/data/ltp_st.aac", &n_ref), *crc = slurp("tests/data/ltp_st_crc.aac", &n_crc), *bad = slurp("tests/data/ltp_st_crcbad.aac", &n_bad), *adif = slurp("tests/data/ltp_st.adif", &n_adif);
    static float a[2 * 41 * 1024], b[2 * 41 * 1024]; size_t fa, fb; int ra, rb;
    if (!ref || !crc || !bad || !adif) { CHECK(0, "cannot read the CRC / ADIF fixtures"); free(ref); free(crc); free(bad); free(adif); return; }
    fa = read_until_error(ref, n_ref, 2, a, 41u * 1024u, &ra);
    CHECK(fa == 40u * 1024u && ra == (int)MAKTABASAUTI_EOF, "plain ADTS: %zu frames, result %d", fa, ra);
    fb = read_until_error(crc, n_crc, 2, b, 41u * 1024u, &rb);                                               /* protection_absent = 0: the CRC of every frame is checked and matches */
    CHECK(fb == fa && rb == (int)MAKTABASAUTI_EOF && memcmp(a, b, fa * 2u * sizeof(float)) == 0, "ADTS with the error check decodes like the plain stream (%zu frames, result %d)", fb, rb);
    fb = read_until_error(bad, n_bad, 2, b, 41u * 1024u, &rb);                                               /* a wrong CRC in access unit 7: the seven frames before it, then corrupt data */
    CHECK(fb == 7u * 1024u && rb == (int)MAKTABASAUTI_E_CORRUPT_DATA && memcmp(a, b, fb * 2u * sizeof(float)) == 0, "a wrong CRC stops the stream after 7 frames: %zu frames, result %d", fb, rb);
    fb = read_until_error(adif, n_adif, 2, b, 41u * 1024u, &rb);                                             /* ADIF carries the same raw_data_blocks */
    CHECK(fb == fa && rb == (int)MAKTABASAUTI_EOF && memcmp(a, b, fa * 2u * sizeof(float)) == 0, "ADIF decodes like ADTS (%zu frames, result %d)", fb, rb);
    { size_t off_c = 0, off_p = 0, i; unsigned rejected = 0, plain_ok = 0;                                    /* damage one protected byte of frame 5 in the CRC stream and in the plain stream */
      for (i = 0; i < 5; i++) { off_c += (((size_t)crc[off_c + 3] & 3u) << 11) | ((size_t)crc[off_c + 4] << 3) | ((size_t)crc[off_c + 5] >> 5); off_p += (((size_t)ref[off_p + 3] & 3u) << 11) | ((size_t)ref[off_p + 4] << 3) | ((size_t)ref[off_p + 5] >> 5); }
      for (i = 0; i < 12; i++) {
          uint8_t *d2 = (uint8_t *)malloc(n_crc), *d3 = (uint8_t *)malloc(n_ref);
          memcpy(d2, crc, n_crc); memcpy(d3, ref, n_ref); d2[off_c + 9 + 2 + i] ^= 0x10u; d3[off_p + 7 + 2 + i] ^= 0x10u;
          fb = read_until_error(d2, n_crc, 2, b, 41u * 1024u, &rb); if ((rb == (int)MAKTABASAUTI_E_CORRUPT_DATA || rb == (int)MAKTABASAUTI_E_UNSUPPORTED_FEATURE) && fb == 5u * 1024u) rejected++;   /* parse-level refusals come before the CRC comparison */
          fb = read_until_error(d3, n_ref, 2, b, 41u * 1024u, &rb); if (rb == (int)MAKTABASAUTI_EOF) plain_ok++;
          free(d2); free(d3);
      }
      CHECK(rejected == 12u, "a flipped protected bit of frame 5 is caught by the CRC (%u of 12)", rejected);
      CHECK(plain_ok >= 3u, "the same damage without the CRC mostly decodes (%u of 12): the CRC, not the parser, is what rejects it", plain_ok); }
    free(ref); free(crc); free(bad); free(adif);
}

/* ---- every channel of the multichannel files: decoded PCM fingerprints (the file-level tests above pin only two channels) ------------------------------------------------------ */
static const char *const ltp_mc_path[] = { "tests/data/ltp_c3.m4a", "tests/data/ltp_c4.m4a", "tests/data/ltp_c5.m4a", "tests/data/ltp_c6.m4a", "tests/data/ltp_c7.m4a", "tests/data/ltp_c3_long.m4a", "tests/data/ltp_c6_pns.m4a" };
static const unsigned ltp_mc_channels[] = { 3, 4, 5, 6, 8, 3, 6 };
#define ltp_MC_FILES (sizeof ltp_mc_path / sizeof ltp_mc_path[0])
#include "test_aac_ltp_mc_fingerprints.h"
static void fingerprint_channel(const float *pcm, size_t n, unsigned ch, unsigned chan, double out[4]) {
    double s1 = 0, s2 = 0, s3 = 0, s4 = 0; size_t i;
    for (i = 0; i < n; i++) { const double v = (double)pcm[i * ch + chan]; s1 += v; s2 += v * v; s3 += v * (double)(i + 1) / (double)n; if (i >= 2) { const double d = v - 2.0 * (double)pcm[(i - 1) * ch + chan] + (double)pcm[(i - 2) * ch + chan]; s4 += d * d; } }
    out[0] = s1; out[1] = s2; out[2] = s3; out[3] = s4;
}
static void test_lcm_files(int dump) {
    unsigned k, c, j;
    if (dump) printf("static const size_t ltp_mc_frames[%u] = {", (unsigned)ltp_MC_FILES);
    for (k = 0; k < ltp_MC_FILES; k++) {
        size_t n, frames; uint8_t *data = slurp(ltp_mc_path[k], &n); float *pcm;
        if (!data) { CHECK(0, "cannot read %s", ltp_mc_path[k]); continue; }
        pcm = decode_all(data, n, ltp_mc_channels[k], 48000, 4096, &frames, ltp_mc_path[k]);
        if (dump) { printf("%s%zu", k ? ", " : " ", frames); free(pcm); free(data); continue; }
        CHECK(frames == ltp_mc_frames[k], "%s: %zu frames, expected %zu", ltp_mc_path[k], frames, ltp_mc_frames[k]);
        for (c = 0; pcm && c < ltp_mc_channels[k]; c++) { double fp[4]; fingerprint_channel(pcm, frames, ltp_mc_channels[k], c, fp); for (j = 0; j < 4; j++) CHECK(near(fp[j], ltp_mc_fp[k][c][j]), "%s ch %u fingerprint %u: %.12e vs %.12e", ltp_mc_path[k], c, j, fp[j], ltp_mc_fp[k][c][j]); }
        free(pcm); free(data);
    }
    if (dump) { printf(" };\nstatic const double ltp_mc_fp[%u][8][4] = {\n", (unsigned)ltp_MC_FILES);
        for (k = 0; k < ltp_MC_FILES; k++) { size_t n, frames; uint8_t *data = slurp(ltp_mc_path[k], &n); float *pcm = data ? decode_all(data, n, ltp_mc_channels[k], 48000, 4096, &frames, ltp_mc_path[k]) : NULL;
            printf("    {\n"); for (c = 0; pcm && c < ltp_mc_channels[k]; c++) { double fp[4]; fingerprint_channel(pcm, frames, ltp_mc_channels[k], c, fp); printf("        {%.12e, %.12e, %.12e, %.12e},\n", fp[0], fp[1], fp[2], fp[3]); } printf("    },\n"); free(pcm); free(data); }
        printf("};\n"); }
}

/* ---- edges of sauti_aac_lcm_decode: a byte after the end of the block, more elements than the configuration has channels  ------------------------------------------------------ */
static void test_lcm_edges(void) {
    SautiAacLcnConfig cfg, cfg7; static SautiAacLcmState st, before; SautiAacReason why; float pcm[8 * 1024], keep[8 * 1024]; const size_t n = sizeof ltp_c6_au0; size_t i, last = 0, nbits; uint8_t buf[4096], over[8192]; int r;
    memset(&cfg, 0, sizeof cfg); cfg.rate_index = 3; cfg.channel_config = 6; cfg.ltp = 1; cfg7 = cfg; cfg7.channel_config = 7;
    for (i = 0; i < 8 * 1024; i++) keep[i] = pcm[i] = 12345.0f;
    sauti_aac_lcm_state_init(&st, &cfg);
    memcpy(buf, ltp_c6_au0, n); buf[n] = 0;                                                                   /* one whole byte after the padding of the block: not a valid block */
    before = st; r = sauti_aac_lcm_decode(&cfg, &st, buf, n + 1u, pcm, &why);
    CHECK(r != 1 && memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "a byte after the block must be refused (%d) and change nothing", r);
    r = sauti_aac_lcm_decode(&cfg, &st, ltp_c6_au0, n, pcm, &why); CHECK(r == 1, "the block itself decodes (%d)", r); st = before;
    for (i = 0; i < n * 8u; i++) if (ltp_c6_au0[i >> 3] & (0x80u >> (i & 7u))) last = i;                       /* the last 1 bit is the last bit of ID_END (111), the padding is zero */
    CHECK(last >= 2u && ((ltp_c6_au0[(last - 2u) >> 3] >> (7u - ((last - 2u) & 7u))) & 1u) && ((ltp_c6_au0[(last - 1u) >> 3] >> (7u - ((last - 1u) & 7u))) & 1u), "ID_END found");
    memset(over, 0, sizeof over); nbits = 0;                                                                  /* the elements of the 5.1 block twice (12 channels) in a 7.1 (8 channels) configuration */
    for (i = 0; i < last - 2u; i++) { if (ltp_c6_au0[i >> 3] & (0x80u >> (i & 7u))) over[nbits >> 3] |= (uint8_t)(0x80u >> (nbits & 7u)); nbits++; }
    for (i = 0; i < n * 8u; i++) { if (ltp_c6_au0[i >> 3] & (0x80u >> (i & 7u))) over[nbits >> 3] |= (uint8_t)(0x80u >> (nbits & 7u)); nbits++; }
    sauti_aac_lcm_state_init(&st, &cfg7); before = st; for (i = 0; i < 8 * 1024; i++) pcm[i] = 12345.0f;
    r = sauti_aac_lcm_decode(&cfg7, &st, over, (nbits + 7u) / 8u, pcm, &why);
    CHECK(r != 1 && memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "9 channels of elements in an 8 channel configuration must be refused (%d) and change nothing", r);
    { SautiAacLcnConfig e = cfg; memset(&e, 0, sizeof e); e.rate_index = 3; e.channel_config = 6; e.main = 1; e.er = 1; CHECK(sauti_aac_lcm_channels(&e) == 0u, "Main and ER together");
      e.main = 0; e.ltp = 1; CHECK(sauti_aac_lcm_channels(&e) == 6u, "ER LTP is a valid combination"); e.er = 0; e.frame960 = 1; CHECK(sauti_aac_lcm_channels(&e) == 0u, "960 lines with more than two channels");
      e.ltp = 0; e.main = 1; CHECK(sauti_aac_lcm_channels(&e) == 0u, "Main 960 with more than two channels"); }
}

int main(int argc, char **argv) {
    static const uint8_t *const st_au[] = { ltp_st_au0, ltp_st_au1, ltp_st_au2, ltp_st_au3, ltp_st_au4, ltp_st_au5, ltp_st_au6, ltp_st_au7 };
    static const size_t st_n[] = { sizeof ltp_st_au0, sizeof ltp_st_au1, sizeof ltp_st_au2, sizeof ltp_st_au3, sizeof ltp_st_au4, sizeof ltp_st_au5, sizeof ltp_st_au6, sizeof ltp_st_au7 };
    static const uint8_t *const mono_au[] = { ltp_mono_au0, ltp_mono_au1, ltp_mono_au2, ltp_mono_au3, ltp_mono_au4, ltp_mono_au5, ltp_mono_au6, ltp_mono_au7 };
    static const size_t mono_n[] = { sizeof ltp_mono_au0, sizeof ltp_mono_au1, sizeof ltp_mono_au2, sizeof ltp_mono_au3, sizeof ltp_mono_au4, sizeof ltp_mono_au5, sizeof ltp_mono_au6, sizeof ltp_mono_au7 };
    const Stream streams[2] = {
        { "ltp stereo 48 kHz", 3, 2, st_au, st_n, 8, ltp_st_fp },
        { "ltp mono 44.1 kHz", 4, 1, mono_au, mono_n, 8, ltp_mono_fp } };
    unsigned k;
    if (argc > 1 && !strcmp(argv[1], "--dump-mc")) {                                                         /* build/test_aac_ltp --dump-mc > tests/test_aac_ltp_mc_fingerprints.h */
        printf("/* Per-channel fingerprints of every channel of the multichannel ltp files (same four sums as the other fingerprints), written by `build/test_aac_ltp --dump-mc` AFTER the decoder agreed with\n * FFmpeg (scripts/test_aac_ltp_vs_ffmpeg.sh, channel configurations 3..7); regenerate only after such a comparison. */\n");
        test_lcm_files(1); return g_failures ? 1 : 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--dump")) {                                                            /* build/test_aac_ltp --dump > tests/test_aac_ltp_fingerprints.h */
        g_dump = 1;
        printf("/* Fingerprints of the decoded PCM (sum, sum of squares, position-weighted sum, energy of the second difference), written by `build/test_aac_ltp --dump` AFTER the decoder agreed with\n * FFmpeg (~136 dB) and FAAD2 (~60-80 dB) on these streams (scripts/test_aac_ltp_vs_ffmpeg.sh); regenerate only after such a comparison. */\n");
        printf("static const double ltp_st_fp[8][2][4] = {\n"); test_vectors(&streams[0]); printf("};\n");
        printf("static const double ltp_mono_fp[8][2][4] = {\n"); test_vectors(&streams[1]); printf("};\n");
        g_eol = ", \\\n"; printf("#define LTP_FILE_FINGERPRINTS { \\\n"); test_files(); printf("}\n");
        return g_failures ? 1 : 0;
    }
    test_ics_info(); test_transforms(); test_prediction(); test_config();
    for (k = 0; k < 2; k++) { test_vectors(&streams[k]); test_atomic(&streams[k]); }
    test_files(); test_seek(); test_lcm_config(); test_lcm_atomic(); test_lcm_seek(); test_lcm_files(0); test_lcm_edges(); test_crc_and_adif(); test_refusals_files();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_aac_ltp: all passed\n"); return 0;
}
