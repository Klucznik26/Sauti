/* AAC Main (object type 1): ics_info predictor_data, the backward-adaptive predictor (src/decoders/modern/aac/sauti_aac_pred.c) and the Main decode path of sauti_aac_lcn.c.  The access units
 * (tests/test_aac_main_vectors.h) and the fixtures tests/data/main_*.{m4a,aac,loas} come from the repository's own stream generator (tests/data/make_main_fixtures.sh), NOT from an encoder; their
 * decoded PCM agrees with FFmpeg to ~138 dB (constant max_sfb) and with FAAD2 to ~135 dB (scripts/test_aac_main_vs_ffmpeg.sh), so the fingerprints pin a verified result.
 *   1. ics_info: predictor_data of a Main index (flags, reset group, band limit per rate), refusals (LC index, reset group 31, 7350 Hz, truncation at every length);
 *   2. the predictor: a second implementation of the standard's recursion (written differently) agrees bit for bit on long random sequences; semantics: unused predictors still adapt, reset groups,
 *      EIGHT_SHORT resets everything, PNS bands are not predicted and start over, bands above PRED_SFB_MAX are untouched;
 *   3. fingerprints of the first access units (stereo 48 kHz with M/S, TNS, short windows, KBD; mono 44.1 kHz), atomic on failure: every truncation and random bit flips leave the state (incl. the
 *      predictors) and the output untouched;
 *   4. the public API: stream description (profile Main), length, MP4 == ADTS == LOAS bit for bit, 1-sample reads, exact seek over the checkpoints of a 150-frame stream, one file per
 *      sampling rate (PRED_SFB_MAX), PNS file, varying max_sfb;
 *   5. refusals: SBR in the access units, frameLengthFlag 1, channel configuration 3, 7350 Hz, predictor_data in an LC stream. */
#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES
#include "maktabasauti/maktabasauti.h"
#include "sauti_aac_lcn.h"
#include "sauti_aac_pred.h"
#include "sauti_aac_syntax.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_layout.h"
#include "test_aac_main_vectors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures, g_dump;
static const char *g_eol = ",\n";
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int near(double a, double b) { return fabs(a - b) <= 1e-5 * (1.0 + fabs(b)); }

typedef struct { uint8_t b[4096]; unsigned n; } Bits;
static void put(Bits *x, unsigned value, unsigned width) { while (width--) { const unsigned bit = (value >> width) & 1u; if ((x->n & 7u) == 0) x->b[x->n >> 3] = 0; if (bit) x->b[x->n >> 3] |= (uint8_t)(0x80u >> (x->n & 7u)); x->n++; } }

/* ---- 1. ics_info ------------------------------------------------------------------------------------------------------------------------------------------------ */
static void test_ics_info(void) {
    const unsigned main48 = SAUTI_AAC_IDX(3, SAUTI_AAC_LAYOUT_1024) | SAUTI_AAC_IDX_MAIN_FLAG; SautiAacIcsInfo ics; Bits x; size_t pos; unsigned i;
    memset(&x, 0, sizeof x);                                                                                 /* ONLY_LONG, sine, max_sfb 12, predictor present, reset, group 9, 12 flags 101010101010 */
    put(&x, 0, 1); put(&x, 0, 2); put(&x, 0, 1); put(&x, 12, 6); put(&x, 1, 1); put(&x, 1, 1); put(&x, 9, 5); for (i = 0; i < 12; i++) put(&x, (i & 1u) ? 0u : 1u, 1);
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, (x.n + 7) / 8, &pos, main48, &ics) == 1 && pos == x.n, "Main predictor_data parses to the end (pos %zu, %u bits)", pos, x.n);
    CHECK(ics.predictor_present == 1 && ics.predictor_reset == 1 && ics.predictor_reset_group == 9 && ics.max_sfb == 12, "fields: present %u reset %u group %u max_sfb %u", ics.predictor_present, ics.predictor_reset, ics.predictor_reset_group, ics.max_sfb);
    for (i = 0; i < 12; i++) CHECK(ics.prediction_used[i] == ((i & 1u) ? 0u : 1u), "prediction_used[%u]", i);
    CHECK(ics.prediction_used[12] == 0, "no flag above max_sfb");
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, (x.n + 7) / 8, &pos, SAUTI_AAC_IDX(3, SAUTI_AAC_LAYOUT_1024), &ics) == 0, "the same bits in an LC stream are refused");
    pos = 0; CHECK(sauti_aac_ics_info_parse(x.b, (x.n + 7) / 8, &pos, &ics) == 0, "the plain parser refuses predictor_data");
    for (unsigned len = 0; len * 8 < x.n; len++) { pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, len, &pos, main48, &ics) == 0 && pos == 0, "truncated to %u bytes must fail and leave pos", len); }
    memset(&x, 0, sizeof x); put(&x, 0, 1); put(&x, 0, 2); put(&x, 0, 1); put(&x, 60, 6); put(&x, 1, 1); put(&x, 0, 1);       /* max_sfb 60 > PRED_SFB_MAX 40: only 40 flags follow */
    for (i = 0; i < 40; i++) put(&x, 1, 1);
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, (x.n + 7) / 8, &pos, main48, &ics) == 1 && pos == x.n && ics.prediction_used[39] == 1, "flags stop at PRED_SFB_MAX (40 at 48 kHz): pos %zu of %u", pos, x.n);
    memset(&x, 0, sizeof x); put(&x, 0, 1); put(&x, 0, 2); put(&x, 0, 1); put(&x, 4, 6); put(&x, 1, 1); put(&x, 1, 1); put(&x, 31, 5);   /* reset group 31 is reserved */
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, 8, &pos, main48, &ics) == 0, "reset group 31 is refused");
    memset(&x, 0, sizeof x); put(&x, 0, 1); put(&x, 0, 2); put(&x, 0, 1); put(&x, 4, 6); put(&x, 1, 1); put(&x, 1, 1); put(&x, 0, 5); for (i = 0; i < 4; i++) put(&x, 1, 1);
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, 8, &pos, main48, &ics) == 1 && ics.predictor_reset_group == 0, "reset group 0 = nothing to reset");
    memset(&x, 0, sizeof x); put(&x, 0, 1); put(&x, 0, 2); put(&x, 0, 1); put(&x, 4, 6); put(&x, 1, 1); put(&x, 0, 1); for (i = 0; i < 4; i++) put(&x, 1, 1);
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, 8, &pos, SAUTI_AAC_IDX(12, SAUTI_AAC_LAYOUT_1024) | SAUTI_AAC_IDX_MAIN_FLAG, &ics) == 0, "7350 Hz has no PRED_SFB_MAX");
    memset(&x, 0, sizeof x); put(&x, 0, 1); put(&x, 2, 2); put(&x, 1, 1); put(&x, 5, 4); put(&x, 0x55, 7);                  /* EIGHT_SHORT: no predictor_data at all */
    pos = 0; CHECK(sauti_aac_ics_info_parse_idx(x.b, 8, &pos, main48, &ics) == 1 && ics.predictor_present == 0 && pos == 1 + 2 + 1 + 4 + 7, "short windows carry no predictor_data");
    { static const unsigned want[13] = { 33, 33, 38, 40, 40, 40, 41, 41, 37, 37, 37, 34, 0 }; for (i = 0; i < 13; i++) CHECK(sauti_aac_pred_sfb_max(i) == want[i], "PRED_SFB_MAX[%u]", i); }
    CHECK(sauti_aac_pred_sfb_max(13) == 0 && sauti_aac_pred_sfb_max(255) == 0, "PRED_SFB_MAX out of range");
}

/* ---- 2. the predictor ------------------------------------------------------------------------------------------------------------------------------------------- */
/* a second implementation of the recursion (integer view of the floats, explicit steps) */
typedef struct { float r0, r1, cor0, cor1, var0, var1; } RefLine;
static uint32_t u32(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static float ref_cut(float f) { return f32(u32(f) & 0xffff0000u); }
static float ref_near(float f) { uint32_t u = u32(f); u += 0x8000u; return f32(u & 0xffff0000u); }
static float ref_even(float f) { uint32_t u = u32(f); const uint32_t tie = (u >> 16) & 1u; u += 0x7fffu + tie; return f32(u & 0xffff0000u); }
static void ref_init(RefLine *l) { l->r0 = l->r1 = l->cor0 = l->cor1 = 0.0f; l->var0 = l->var1 = 1.0f; }
static void ref_step(RefLine *l, float *x, int use) {
    float k1 = 0.0f, k2 = 0.0f, pv, e0, e1, v;
    if (l->var0 > 1.0f) k1 = l->cor0 * ref_even(0.953125f / l->var0);
    if (l->var1 > 1.0f) k2 = l->cor1 * ref_even(0.953125f / l->var1);
    pv = ref_near(k1 * l->r0 + k2 * l->r1);
    if (use) *x = *x + pv;
    v = *x; e0 = v; e1 = e0 - k1 * l->r0;
    { const float c1 = 0.90625f * l->cor1 + l->r1 * e1, w1 = 0.90625f * l->var1 + 0.5f * (l->r1 * l->r1 + e1 * e1), c0 = 0.90625f * l->cor0 + l->r0 * e0, w0 = 0.90625f * l->var0 + 0.5f * (l->r0 * l->r0 + e0 * e0);
      const float n1 = 0.953125f * (l->r0 - k1 * e0), n0 = 0.953125f * e0;
      l->cor1 = ref_cut(c1); l->var1 = ref_cut(w1); l->cor0 = ref_cut(c0); l->var0 = ref_cut(w0); l->r1 = ref_cut(n1); l->r0 = ref_cut(n0); }
}
static int is_init(const SautiAacPredLine *l) { return l->r0 == 0 && l->r1 == 0 && l->cor0 == 0 && l->cor1 == 0 && l->var0 == 1.0f && l->var1 == 1.0f; }
static SautiAacIcsInfo long_ics(unsigned max_sfb, unsigned used_all) {
    SautiAacIcsInfo ics; memset(&ics, 0, sizeof ics); ics.window_count = 1; ics.group_count = 1; ics.group_length[0] = 1; ics.max_sfb = max_sfb; ics.predictor_present = 1;
    memset(ics.prediction_used, used_all ? 1 : 0, sizeof ics.prediction_used); return ics;
}
static void test_predictor(void) {
    const unsigned idx = SAUTI_AAC_IDX(3, SAUTI_AAC_LAYOUT_1024) | SAUTI_AAC_IDX_MAIN_FLAG; const uint16_t *off = NULL; unsigned bands = 0, t, k;
    static SautiAacPredState st, st2; static RefLine ref[1024]; static float a[1024], b[1024], keep[1024]; SautiAacIcsInfo ics = long_ics(40, 1);
    CHECK(sauti_aac_band_offsets(idx, 0, &off, &bands) == 1 && bands == 49, "band table with the Main flag (%u bands)", bands);
    sauti_aac_pred_reset(&st); for (k = 0; k < 1024; k++) { CHECK(is_init(&st.line[k]), "reset line %u", k); ref_init(&ref[k]); }
    /* (a) 300 frames of random spectra (all prediction_used, then a mix, with reset groups): the two implementations agree bit for bit */
    for (t = 0; t < 300; t++) {
        const unsigned mix = t >= 100; SautiAacIcsInfo c = long_ics(40, 1); unsigned sfb;
        if (mix) { c.predictor_reset = (t % 7) == 0; c.predictor_reset_group = (uint8_t)(1 + t % 30); for (sfb = 0; sfb < 40; sfb++) c.prediction_used[sfb] = (uint8_t)(rnd() & 1u); }
        for (k = 0; k < 1024; k++) { const float v = ((float)((int)(rnd() % 20001u) - 10000)) * (1.0f + (float)(k % 17)) * ((t % 5) == 0 ? 0.01f : 1.0f); a[k] = b[k] = v; }
        CHECK(sauti_aac_pred_apply(&st, &c, idx, a) == 1, "apply %u", t);
        for (sfb = 0; sfb < 40; sfb++) for (k = off[sfb]; k < off[sfb + 1]; k++) ref_step(&ref[k], &b[k], c.prediction_used[sfb]);
        if (mix && c.predictor_reset) for (k = (unsigned)c.predictor_reset_group - 1u; k < 1024; k += 30u) ref_init(&ref[k]);
        CHECK(memcmp(a, b, sizeof a) == 0, "frame %u: output differs from the reference model", t);
        CHECK(memcmp(st.line, ref, sizeof ref) == 0, "frame %u: state differs from the reference model", t);
        if (g_failures) return;
    }
    /* (b) predictors adapt even where nothing is used, and then change nothing in the output */
    sauti_aac_pred_reset(&st); ics = long_ics(40, 0);
    for (k = 0; k < 1024; k++) a[k] = keep[k] = (float)(k + 1) * 3.5f;
    CHECK(sauti_aac_pred_apply(&st, &ics, idx, a) == 1 && memcmp(a, keep, sizeof a) == 0, "nothing used: output unchanged");
    CHECK(!is_init(&st.line[10]) && is_init(&st.line[off[40]]), "unused bands adapt (line 10), lines above PRED_SFB_MAX (%u) stay untouched", off[40]);
    ics.predictor_present = 0; ics.max_sfb = 49; sauti_aac_pred_reset(&st);
    CHECK(sauti_aac_pred_apply(&st, &ics, idx, a) == 1 && !is_init(&st.line[10]), "no predictor_data_present: still adapting");
    /* (c) reset groups: lines g-1, g-1+30, ... start over, the others do not */
    ics = long_ics(40, 1);
    for (unsigned g = 1; g <= 30; g += 11) {
        sauti_aac_pred_reset(&st); for (k = 0; k < 1024; k++) a[k] = (float)(k + 2) * 11.0f;
        sauti_aac_pred_apply(&st, &ics, idx, a); for (k = 0; k < 1024; k++) a[k] = (float)(k + 2) * -7.0f;
        ics.predictor_reset = 1; ics.predictor_reset_group = (uint8_t)g; sauti_aac_pred_apply(&st, &ics, idx, a); ics.predictor_reset = 0;
        for (k = 0; k < 1024; k++) { const int member = (k % 30u) == g - 1u; if (k < off[40]) CHECK(is_init(&st.line[k]) == member, "group %u line %u: init %d expected %d", g, k, is_init(&st.line[k]), member); }
    }
    /* (d) EIGHT_SHORT resets every predictor and leaves the spectrum alone */
    sauti_aac_pred_reset(&st); for (k = 0; k < 1024; k++) a[k] = keep[k] = (float)(k + 2) * 9.0f;
    sauti_aac_pred_apply(&st, &ics, idx, a); ics.window_sequence = 2; memcpy(a, keep, sizeof a);
    CHECK(sauti_aac_pred_apply(&st, &ics, idx, a) == 1 && memcmp(a, keep, sizeof a) == 0, "short window: spectrum untouched");
    for (k = 0; k < 1024; k++) if (!is_init(&st.line[k])) { CHECK(0, "short window: line %u not reset", k); break; }
    ics.window_sequence = 0;
    /* (e) noise bands: no prediction added, predictors start over */
    { uint8_t noise[64]; memset(noise, 0, sizeof noise); noise[3] = noise[7] = 1;
      sauti_aac_pred_reset(&st); sauti_aac_pred_reset(&st2);
      for (t = 0; t < 6; t++) {
          for (k = 0; k < 1024; k++) a[k] = b[k] = (float)((int)(rnd() % 2001u) - 1000) * 2.0f;
          sauti_aac_pred_apply_pns(&st, &ics, idx, a, noise); sauti_aac_pred_apply(&st2, &ics, idx, b);
          for (k = off[3]; k < off[4]; k++) CHECK(is_init(&st.line[k]), "noise band 3 line %u: state starts over", k);
          for (k = off[7]; k < off[8]; k++) CHECK(is_init(&st.line[k]), "noise band 7 line %u: state starts over", k);
          for (k = off[4]; k < off[7]; k++) if (t == 0) CHECK(a[k] == b[k], "other bands match the plain predictor at t=0");
      }
      for (k = 0; k < 1024; k++) a[k] = (float)(k + 5);
      memcpy(keep, a, sizeof a); sauti_aac_pred_apply_pns(&st, &ics, idx, a, noise);
      for (k = off[3]; k < off[4]; k++) if (a[k] != keep[k]) { CHECK(0, "noise band line %u: a prediction was added", k); break; } }
    /* (g) thresholds of the recursion on crafted states: var exactly 1 gives no gain (k = 0), above 1 it does; the model decides */
    for (unsigned which = 0; which < 4; which++) {
        static float v1[1024], v2[1024]; RefLine r; SautiAacIcsInfo u = long_ics(40, 1);
        sauti_aac_pred_reset(&st); for (k = 0; k < 1024; k++) { ref_init(&ref[k]); v1[k] = v2[k] = (float)(k % 13) - 6.0f; }
        st.line[0].r0 = ref[0].r0 = 4.0f; st.line[0].r1 = ref[0].r1 = -3.0f; st.line[0].cor0 = ref[0].cor0 = 2.0f; st.line[0].cor1 = ref[0].cor1 = -1.5f;
        st.line[0].var0 = ref[0].var0 = (which & 1u) ? 1.0f : 2.0f; st.line[0].var1 = ref[0].var1 = (which & 2u) ? 1.0f : 2.0f; r = ref[0];
        CHECK(sauti_aac_pred_apply(&st, &u, idx, v1) == 1, "crafted apply");
        ref_step(&r, &v2[0], 1);
        CHECK(v1[0] == v2[0] && memcmp(&st.line[0], &r, sizeof r) == 0, "crafted thresholds %u: output %.9g vs %.9g", which, (double)v1[0], (double)v2[0]);
    }
    /* (h) the flags decide only inside max_sfb and only with predictor_data_present (FFmpeg keeps stale flags above max_sfb: wrong) */
    { static float v1[1024], v2[1024]; SautiAacIcsInfo u = long_ics(10, 1); unsigned n10 = off[10];
      for (t = 0; t < 4; t++) {                                                                                  /* a few frames so that the predictors have something to say */
          for (k = 0; k < 1024; k++) v1[k] = (float)((int)(rnd() % 2001u) - 1000);
          memcpy(v2, v1, sizeof v1);
          if (t == 0) { sauti_aac_pred_reset(&st); sauti_aac_pred_reset(&st2); }
          u.predictor_present = 1; sauti_aac_pred_apply(&st, &u, idx, v1);                                       /* flags set above max_sfb = 10 as well */
          { SautiAacIcsInfo w = u; memset(w.prediction_used + 10, 0, sizeof w.prediction_used - 10); sauti_aac_pred_apply(&st2, &w, idx, v2); }
          CHECK(memcmp(v1, v2, sizeof v1) == 0, "frame %u: flags above max_sfb must not predict (line %u..)", t, n10);
      }
      CHECK(memcmp(&st, &st2, sizeof st) == 0, "state with stale flags above max_sfb equals the state without");
      sauti_aac_pred_reset(&st); sauti_aac_pred_reset(&st2); u.predictor_present = 0;
      for (t = 0; t < 4; t++) { for (k = 0; k < 1024; k++) v1[k] = v2[k] = (float)((int)(rnd() % 2001u) - 1000); u.predictor_present = 0; sauti_aac_pred_apply(&st, &u, idx, v1); CHECK(memcmp(v1, v2, sizeof v1) == 0, "predictor_data_present = 0 with flags set: no prediction"); }
      u.predictor_reset = 1; u.predictor_reset_group = 5; u.predictor_present = 0; for (k = 0; k < 1024; k++) v1[k] = 100.0f + (float)k; sauti_aac_pred_apply(&st, &u, idx, v1);
      CHECK(!is_init(&st.line[4]), "a reset group without predictor_data_present does nothing"); }
    /* (i) a noise flag above max_sfb is not a noise band of this frame */
    { static float v1[1024], v2[1024]; SautiAacIcsInfo u = long_ics(10, 1); uint8_t nz[64]; memset(nz, 0, sizeof nz); nz[20] = 1;
      sauti_aac_pred_reset(&st); sauti_aac_pred_reset(&st2);
      for (t = 0; t < 3; t++) { for (k = 0; k < 1024; k++) v1[k] = v2[k] = (float)((int)(rnd() % 2001u) - 1000); sauti_aac_pred_apply_pns(&st, &u, idx, v1, nz); sauti_aac_pred_apply(&st2, &u, idx, v2); }
      CHECK(memcmp(&st, &st2, sizeof st) == 0, "a noise flag above max_sfb changes nothing"); }
    /* (f) invalid arguments */
    CHECK(sauti_aac_pred_apply(NULL, &ics, idx, a) == 0 && sauti_aac_pred_apply(&st, NULL, idx, a) == 0 && sauti_aac_pred_apply(&st, &ics, idx, NULL) == 0, "NULL arguments");
    ics.window_sequence = 4; CHECK(sauti_aac_pred_apply(&st, &ics, idx, a) == 0, "window sequence 4"); ics.window_sequence = 0;
    CHECK(sauti_aac_pred_apply(&st, &ics, SAUTI_AAC_IDX(12, 0) | SAUTI_AAC_IDX_MAIN_FLAG, a) == 0, "7350 Hz");
}

/* ---- 3. access units through sauti_aac_lcn_decode -------------------------------------------------------------------------------------------------------------- */
typedef struct { const char *name; unsigned rate_index, channels; const uint8_t *const *au; const size_t *au_n; size_t frames; const double (*fp)[2][4]; } Stream;
static void fingerprint(const float *pcm, unsigned n, unsigned ch, double out[2][4]) {
    unsigned c, i;
    for (c = 0; c < ch; c++) {
        double s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        for (i = 0; i < n; i++) { const double v = (double)pcm[i * ch + c]; s1 += v; s2 += v * v; s3 += v * (double)(i + 1) / (double)n; if (i >= 2) { const double d = v - 2.0 * (double)pcm[(i - 1) * ch + c] + (double)pcm[(i - 2) * ch + c]; s4 += d * d; } }
        out[c][0] = s1; out[c][1] = s2; out[c][2] = s3; out[c][3] = s4;
    }
}
#include "test_aac_main_fingerprints.h"
static void test_vectors(const Stream *s) {
    SautiAacLcnConfig cfg; static SautiAacLcnState st; float pcm[2 * 1024]; size_t f; SautiAacReason why;
    cfg.rate_index = s->rate_index; cfg.channel_config = s->channels; cfg.main = 1;
    CHECK(sauti_aac_lcn_frame_samples(&cfg) == 1024u, "%s: frame length", s->name);
    sauti_aac_lcn_state_init(&st);
    for (unsigned c = 0; c < 2; c++) for (unsigned k = 0; k < 1024; k++) if (!is_init(&st.pred[c].line[k])) { CHECK(0, "%s: predictor %u line %u not initialised", s->name, c, k); c = 2; break; }
    for (f = 0; f < s->frames; f++) {
        double fp[2][4] = { { 0 } }; unsigned c, k;
        CHECK(sauti_aac_lcn_decode(&cfg, &st, s->au[f], s->au_n[f], pcm, &why) == 1, "%s frame %zu: decode", s->name, f);
        fingerprint(pcm, 1024, s->channels, fp);
        if (g_dump) { printf("    { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} }%s", fp[0][0], fp[0][1], fp[0][2], fp[0][3], fp[1][0], fp[1][1], fp[1][2], fp[1][3], g_eol); continue; }
        for (c = 0; c < s->channels; c++) for (k = 0; k < 4; k++) CHECK(near(fp[c][k], s->fp[f][c][k]), "%s frame %zu ch %u fingerprint %u: %.12e vs %.12e", s->name, f, c, k, fp[c][k], s->fp[f][c][k]);
    }
}
static void test_atomic(const Stream *s) {
    SautiAacLcnConfig cfg; static SautiAacLcnState st, before; float pcm[2 * 1024], keep[2 * 1024]; SautiAacReason why; size_t f, len, i; int round; long accepted = 0, rejected = 0;
    cfg.rate_index = s->rate_index; cfg.channel_config = s->channels; cfg.main = 1;
    sauti_aac_lcn_state_init(&st);
    for (i = 0; i < 2 * 1024; i++) keep[i] = pcm[i] = 12345.0f;
    for (f = 0; f < s->frames; f++) {
        for (len = 0; len <= s->au_n[f]; len++) {                                                           /* every truncation */
            int r; before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcn_decode(&cfg, &st, s->au[f], len, pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu len %zu: failure (%d) must leave state and output alone", s->name, f, len, r); rejected++; }
            else { for (i = 0; i < 1024u * s->channels; i++) CHECK(isfinite(pcm[i]), "%s: non-finite output", s->name); st = before; accepted++; }
        }
        for (round = 0; round < 200; round++) {                                                             /* random bit flips */
            uint8_t au[2048]; int r, nflip = 1 + (int)(rnd() % 4u);
            memcpy(au, s->au[f], s->au_n[f]);
            while (nflip--) { const size_t at = rnd() % s->au_n[f]; au[at] ^= (uint8_t)(1u << (rnd() & 7u)); }
            before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcn_decode(&cfg, &st, au, s->au_n[f], pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu flip: failure (%d) must leave state and output alone", s->name, f, r); rejected++; }
            else { for (i = 0; i < 1024u * s->channels; i++) CHECK(isfinite(pcm[i]), "%s: non-finite output after flips", s->name); st = before; accepted++; }
        }
        { SautiAacLcnState adv = st; sauti_aac_lcn_decode(&cfg, &adv, s->au[f], s->au_n[f], pcm, &why); st = adv; }      /* advance with the intact unit */
    }
    printf("%s: %ld accepted, %ld rejected mutated or truncated access units\n", s->name, accepted, rejected);
    CHECK(rejected > 100, "%s: mutations never fail?", s->name);
}

static void test_config(void) {
    SautiAacLcnConfig c; unsigned r, cc;
    for (r = 0; r <= 13; r++) for (cc = 0; cc <= 8; cc++) {
        c.rate_index = r; c.channel_config = cc; c.main = 1; CHECK(sauti_aac_lcn_frame_samples(&c) == ((r <= 11 && (cc == 1 || cc == 2)) ? 1024u : 0u), "Main rate %u config %u: %u", r, cc, sauti_aac_lcn_frame_samples(&c));
        c.main = 0; CHECK(sauti_aac_lcn_frame_samples(&c) == ((r <= 12 && (cc == 1 || cc == 2)) ? 960u : 0u), "LC 960 rate %u config %u: %u", r, cc, sauti_aac_lcn_frame_samples(&c));
    }
}

/* ---- 4. the public API on the fixtures -------------------------------------------------------------------------------------------------------------------------- */
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
    CHECK(strcmp(info.codec_profile, "Main") == 0, "%s: profile '%s'", name, info.codec_profile);
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
    static const struct { const char *path; unsigned channels, rate, frames; double fp[2][4]; } files[] = MAIN_FILE_FINGERPRINTS;
    float *first[3] = { NULL, NULL, NULL }; size_t first_n[3] = { 0, 0, 0 }; unsigned k; const unsigned nfiles = (unsigned)(sizeof files / sizeof files[0]);
    for (k = 0; k < nfiles; k++) {
        size_t n, frames, one_frames, at; uint8_t *data = slurp(files[k].path, &n); float *pcm, *one; double fp[2][4] = { { 0 } }; unsigned c, j; int finite = 1; float peak = 0;
        if (!data) { CHECK(0, "cannot read %s (run from the repository root)", files[k].path); continue; }
        pcm = decode_all(data, n, files[k].channels, files[k].rate, 4096, &frames, files[k].path);
        CHECK(frames == files[k].frames * 1024u, "%s: %zu frames, expected %u", files[k].path, frames, files[k].frames * 1024u);
        for (at = 0; at < frames * files[k].channels; at++) { if (!isfinite(pcm[at])) finite = 0; if (fabsf(pcm[at]) > peak) peak = fabsf(pcm[at]); }
        CHECK(finite && peak > 0.0005f && peak < 1.5f, "%s: peak %.4f (not silent, finite)", files[k].path, (double)peak);
        fingerprint(pcm, (unsigned)frames, files[k].channels, fp);
        if (g_dump) printf("    { \"%s\", %u, %u, %u, { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} } }%s", files[k].path, files[k].channels, files[k].rate, files[k].frames, fp[0][0], fp[0][1], fp[0][2], fp[0][3], fp[1][0], fp[1][1], fp[1][2], fp[1][3], g_eol);
        else for (c = 0; c < files[k].channels; c++) for (j = 0; j < 4; j++) CHECK(near(fp[c][j], files[k].fp[c][j]), "%s ch %u whole-file fingerprint %u: %.12e vs %.12e", files[k].path, c, j, fp[c][j], files[k].fp[c][j]);
        if (k < 3) {                                                                                         /* the three containers of main_st carry the same access units */
            one = decode_all(data, n, files[k].channels, files[k].rate, 1, &one_frames, "1-sample reads");
            CHECK(one && one_frames == frames && memcmp(one, pcm, frames * files[k].channels * sizeof(float)) == 0, "%s: 1-sample reads differ", files[k].path);
            free(one); first[k] = pcm; first_n[k] = frames * files[k].channels;
        } else free(pcm);
        free(data);
    }
    if (first[0] && first[1]) CHECK(first_n[0] == first_n[1] && memcmp(first[0], first[1], first_n[0] * sizeof(float)) == 0, "ADTS and MP4 of the same access units must decode identically");
    if (first[0] && first[2]) CHECK(first_n[0] == first_n[2] && memcmp(first[0], first[2], first_n[0] * sizeof(float)) == 0, "LOAS and MP4 of the same access units must decode identically");
    for (k = 0; k < 3; k++) free(first[k]);
}
static void test_seek(void) {                                                                                /* 150 frames: checkpoints at 64 and 128 carry the predictor state */
    size_t n, frames, got, i; uint8_t *data = slurp("tests/data/main_long.m4a", &n); maktabasauti_stream_info_t info; maktabasauti_decoder_t *d; float *ref, a[500];
    static const uint64_t targets[] = { 100000, 131072, 131000, 70000, 64 * 1024, 64 * 1024 - 3, 1000, 0, 153000, 140000 };
    if (!data) { CHECK(0, "cannot read tests/data/main_long.m4a"); return; }
    ref = decode_all(data, n, 1, 48000, 4096, &frames, "seek reference");
    d = open_mem(data, n, &info, NULL);
    if (d && ref) {
        for (i = 0; i < sizeof targets / sizeof targets[0]; i++) {
            const size_t want = targets[i] + 500u <= frames ? 500u : (size_t)(frames - targets[i]);
            maktabasauti_result_t r = maktabasauti_decoder_seek(d, targets[i], MAKTABASAUTI_SEEK_EXACT, NULL);
            CHECK(r == MAKTABASAUTI_OK, "seek %llu -> %d", (unsigned long long)targets[i], (int)r);
            r = maktabasauti_decoder_read(d, a, want, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == want && memcmp(a, ref + targets[i], want * sizeof(float)) == 0, "seek to %llu differs from sequential decoding (predictor state)", (unsigned long long)targets[i]);
        }
    } else CHECK(0, "seek test setup");
    if (d) maktabasauti_decoder_close(d);
    free(ref); free(data);
}
static int open_result(const uint8_t *data, size_t n) { int r = 0; maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, &r); if (d) maktabasauti_decoder_close(d); return r; }
static void test_refusals_files(void) {
    size_t n, i; uint8_t *data; unsigned patched = 0;
    data = slurp("tests/data/main_sbr.m4a", &n);
    if (!data) CHECK(0, "cannot read tests/data/main_sbr.m4a"); else { const int r = open_result(data, n); CHECK(r == (int)MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "an SBR fill element in a Main stream: expected unsupported feature, got %d", r); free(data); }
    data = slurp("tests/data/main_r12.m4a", &n);
    if (!data) CHECK(0, "cannot read tests/data/main_r12.m4a"); else { const int r = open_result(data, n); CHECK(r == (int)MAKTABASAUTI_E_UNSUPPORTED_FEATURE || r == (int)MAKTABASAUTI_E_UNSUPPORTED_FORMAT, "Main at 7350 Hz must be refused, got %d", r); free(data); }
    data = slurp("tests/data/main_st.m4a", &n);
    if (!data) { CHECK(0, "cannot read tests/data/main_st.m4a"); return; }
    CHECK(open_result(data, n) == (int)MAKTABASAUTI_OK, "the unpatched stereo fixture opens");
    /* ASC: AOT 1 (00001), rate 3 (0011), channels 2 (0010), frameLengthFlag 0, dependsOnCoreCoder 0, extensionFlag 0 = 0x09 0x90 (DecoderSpecificInfo, length 2) */
    for (i = 0; i + 3 < n; i++) if (data[i] == 0x02 && data[i + 1] == 0x09 && data[i + 2] == 0x90) {
        patched++;
        data[i + 2] = 0x94;                                                                                  /* frameLengthFlag 1: 960 lines are not defined for Main here */
        CHECK(open_result(data, n) != (int)MAKTABASAUTI_OK, "Main with frameLengthFlag 1 is refused");
        data[i + 2] = 0x98;                                                                                  /* channel_configuration 3 */
        CHECK(open_result(data, n) != (int)MAKTABASAUTI_OK, "Main with channel_configuration 3 is refused");
        data[i + 2] = 0x80;                                                                                  /* channel_configuration 0 (PCE) */
        CHECK(open_result(data, n) != (int)MAKTABASAUTI_OK, "Main with channel_configuration 0 is refused");
        data[i + 2] = 0x90;
    }
    CHECK(patched == 1u, "the AudioSpecificConfig of the fixture was found %u times", patched);
    free(data);
}

int main(int argc, char **argv) {
    static const uint8_t *const st_au[] = { main_st_au0, main_st_au1, main_st_au2, main_st_au3, main_st_au4, main_st_au5 };
    static const size_t st_n[] = { sizeof main_st_au0, sizeof main_st_au1, sizeof main_st_au2, sizeof main_st_au3, sizeof main_st_au4, sizeof main_st_au5 };
    static const uint8_t *const mono_au[] = { main_mono_au0, main_mono_au1, main_mono_au2, main_mono_au3, main_mono_au4, main_mono_au5 };
    static const size_t mono_n[] = { sizeof main_mono_au0, sizeof main_mono_au1, sizeof main_mono_au2, sizeof main_mono_au3, sizeof main_mono_au4, sizeof main_mono_au5 };
    const Stream streams[2] = {
        { "main stereo 48 kHz", 3, 2, st_au, st_n, 6, main_st_fp },
        { "main mono 44.1 kHz", 4, 1, mono_au, mono_n, 6, main_mono_fp } };
    unsigned k;
    if (argc > 1 && !strcmp(argv[1], "--dump")) {                                                            /* build/test_aac_main --dump > tests/test_aac_main_fingerprints.h */
        g_dump = 1;
        printf("/* Fingerprints of the decoded PCM (sum, sum of squares, position-weighted sum, energy of the second difference), written by `build/test_aac_main --dump` AFTER the decoder agreed with\n * FFmpeg (~138 dB) and FAAD2 (~135 dB) on these streams (scripts/test_aac_main_vs_ffmpeg.sh); regenerate only after such a comparison. */\n");
        printf("static const double main_st_fp[6][2][4] = {\n"); test_vectors(&streams[0]); printf("};\n");
        printf("static const double main_mono_fp[6][2][4] = {\n"); test_vectors(&streams[1]); printf("};\n");
        g_eol = ", \\\n"; printf("#define MAIN_FILE_FINGERPRINTS { \\\n"); test_files(); printf("}\n");
        return g_failures ? 1 : 0;
    }
    test_ics_info(); test_predictor(); test_config();
    for (k = 0; k < 2; k++) { test_vectors(&streams[k]); test_atomic(&streams[k]); }
    test_files(); test_seek(); test_refusals_files();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_aac_main: all passed\n"); return 0;
}
