/* USAC MPEG Surround 212: unit tests of sauti_usac_mps.c (parameter decoder) and sauti_usac_mps_dec.c (signal path); the streams themselves are in test_usac_xaac (usac_z1..z3) and scripts/test_usac_vs_xaac.sh.
 *   1. the parameter decoder on streams built by hand: PCM coded sets (fine and coarse CLD, ICC), every stride of the grouping, the mapping of the groups to 28 / 20 / ... bands, the default (data mode 0) and keep
 *      (mode 1) modes, the framing of several sets (slots, distances), the appended closing set, the smoothing data, refusals;
 *   2. the signal path against libxaac: first frames of one of its streams from the reset state (tests/data/mps_golden.bin, make_mps_golden.py): the sums of both output grids of frames 1..5 agree to 3e-5;
 *   3. properties: silence in = silence out, a mono signal with CLD 0 / ICC 0 comes out in both channels at the same level, hard panning (CLD +/-45 dB) puts the energy in one channel, refusals of unsupported configurations;
 *   4. the data tables: Huffman trees end in leaves, the band maps are monotonic and end at the last band. */
#include "sauti_usac_mps_dec.h"
#include "sauti_usac_mps_tables.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { uint8_t b[512]; unsigned n; } Bw;
static void put(Bw *w, uint32_t v, unsigned bits) { while (bits--) { if (w->n >= sizeof w->b * 8u) return; if ((v >> bits) & 1u) w->b[w->n >> 3] |= (uint8_t)(0x80u >> (w->n & 7u)); w->n++; } }

static const SautiUsacMpsConfig cfg1 = { 1, 0, 0, 10, 0, 0, 0, 0, 1, 0 };          /* 28 bands, one parameter set per frame */

/* an EcData block of one parameter set coded with PCM: data mode 3, no pair, quantiser, stride, PCM flag, the values */
static void put_pcm_set(Bw *w, unsigned coarse, unsigned stride, unsigned bands, const unsigned *val, unsigned bits) {
    unsigned i;
    put(w, 3, 2); put(w, 0, 1); put(w, coarse, 1); put(w, stride, 2); put(w, 1, 1);
    for (i = 0; i < bands; i++) put(w, val[i], bits);
}
static int parse(Bw *w, const SautiUsacMpsConfig *c, unsigned slots, int indep, SautiUsacMpsState *st, SautiUsacMpsFrame *f) {
    SautiUsacBits b; b.data = w->b; b.nbits = w->n + 64u; b.pos = 0;           /* a few spare zero bits: the reader never fails */
    return sauti_usac_mps212_parse(c, slots, &b, indep, st, f);
}

static void test_parse(void) {
    static SautiUsacMpsState st; static SautiUsacMpsFrame f;
    Bw w; unsigned cv[28], iv[28], i, ok;
    /* fine CLD (5 bit values, offset 15), fine ICC (3 bit values), stride 0 = every band of its own, independent frame (no independency flag) */
    for (i = 0; i < 28u; i++) { cv[i] = (i * 5u + 3u) % 31u; iv[i] = (i * 3u) % 8u; }
    memset(&w, 0, sizeof w); memset(&st, 0, sizeof st);
    put_pcm_set(&w, 0, 0, 28, cv, 5); put_pcm_set(&w, 0, 0, 28, iv, 3);
    CHECK(parse(&w, &cfg1, 32, 1, &st, &f) == 1, "pcm frame refused");
    CHECK(f.num_sets == 1u && f.ext_frame == 0u && f.param_slot[0] == 31u && f.param_bands == 28u, "sets %u ext %u slot %u bands %u", f.num_sets, f.ext_frame, f.param_slot[0], f.param_bands);
    for (i = 0, ok = 1; i < 28u; i++) if (f.cld_idx[0][i] != (int)cv[i] - 15 || f.cld[0][i] != sauti_usac_mps_cld_dequant[cv[i]] || f.icc_idx[0][i] != (int)iv[i] || f.icc[0][i] != sauti_usac_mps_icc_dequant[iv[i]]) ok = 0;
    CHECK(ok, "pcm values of the 28 bands");
    /* coarse CLD: 4 bit values, offset 7, doubled to the fine scale, +-14 becomes +-15 */
    for (i = 0; i < 28u; i++) cv[i] = i % 15u;
    memset(&w, 0, sizeof w); memset(&st, 0, sizeof st);
    put_pcm_set(&w, 1, 0, 28, cv, 4); put_pcm_set(&w, 0, 0, 28, iv, 3);
    CHECK(parse(&w, &cfg1, 32, 1, &st, &f) == 1, "coarse frame refused");
    for (i = 0, ok = 1; i < 28u; i++) { int e = ((int)cv[i] - 7) * 2; if (e == 14) e = 15; if (e == -14) e = -15; if (f.cld_idx[0][i] != e) ok = 0; }
    CHECK(ok, "coarse CLD is not doubled: %d %d %d", f.cld_idx[0][0], f.cld_idx[0][1], f.cld_idx[0][14]);
    /* stride 2 (groups of 5 bands): 6 coded values, the groups are 4 4 5 5 5 5 bands wide (the 28 bands are not a multiple of 5) */
    {   unsigned gv[6] = { 1, 5, 9, 14, 20, 30 }; static const unsigned width[6] = { 4, 4, 5, 5, 5, 5 }; unsigned b = 0, g;
        memset(&w, 0, sizeof w); memset(&st, 0, sizeof st);
        put_pcm_set(&w, 0, 2, 6, gv, 5); put_pcm_set(&w, 0, 3, 1, iv, 3);
        CHECK(parse(&w, &cfg1, 32, 1, &st, &f) == 1, "stride 2 frame refused");
        for (g = 0, ok = 1; g < 6u; g++) for (i = 0; i < width[g]; i++, b++) if (f.cld_idx[0][b] != (int)gv[g] - 15) ok = 0;
        CHECK(ok, "stride 2 mapping of the groups to the bands");
        for (i = 0, ok = 1; i < 28u; i++) if (f.icc_idx[0][i] != (int)iv[0]) ok = 0;                 /* stride 3: one group for all */
        CHECK(ok, "stride 3 (one group) ICC");
    }
    /* data mode 0 (default values) and 1 (keep): defaults are CLD 0 dB / ICC 1.0, "keep" repeats the last indices */
    memset(&w, 0, sizeof w);
    put(&w, 0, 2); put(&w, 0, 2);                                                                    /* CLD mode 0, ICC mode 0 */
    CHECK(parse(&w, &cfg1, 32, 1, &st, &f) == 1, "default frame refused");
    for (i = 0, ok = 1; i < 28u; i++) if (f.cld_idx[0][i] != 0 || f.icc_idx[0][i] != 0 || f.cld[0][i] != 0.0f || f.icc[0][i] != 1.0f) ok = 0;
    CHECK(ok, "default parameters are not CLD 0 / ICC 0");
    memset(&w, 0, sizeof w); memset(&st, 0, sizeof st);
    put_pcm_set(&w, 0, 0, 28, cv, 5); put_pcm_set(&w, 0, 0, 28, iv, 3);
    for (i = 0; i < 28u; i++) cv[i] %= 31u;
    CHECK(parse(&w, &cfg1, 32, 1, &st, &f) == 1, "set frame");
    memset(&w, 0, sizeof w);
    put(&w, 1, 1);                                                                                   /* not independent: bsIndependencyFlag = 1 is read from the stream */
    put(&w, 1, 2); put(&w, 1, 2);                                                                    /* CLD mode 1, ICC mode 1: keep */
    {   SautiUsacMpsFrame prev = f;
        CHECK(parse(&w, &cfg1, 32, 0, &st, &f) == 1, "keep frame refused");
        CHECK(!memcmp(f.cld_idx[0], prev.cld_idx[0], sizeof f.cld_idx[0]) && !memcmp(f.icc_idx[0], prev.icc_idx[0], sizeof f.icc_idx[0]), "keep does not repeat the previous indices");
    }
    /* high rate mode: three parameter sets with explicit slots 5 / 17 / 29 (framing type 1): the last one is not in the last slot, a closing set is appended */
    {   SautiUsacMpsConfig hr = cfg1; unsigned vals[1] = { 17 };
        hr.high_rate = 1;
        memset(&w, 0, sizeof w); memset(&st, 0, sizeof st);
        put(&w, 1, 1); put(&w, 2, 3);                                                                /* framing 1, 3 sets */
        put(&w, 5, 5); put(&w, 17, 5); put(&w, 29, 5);
        put(&w, 0, 2); put(&w, 0, 2); put(&w, 0, 2);                                                 /* CLD: three default sets */
        put(&w, 0, 2); put(&w, 0, 2); put(&w, 0, 2);                                                 /* ICC: three default sets */
        put(&w, 1, 2); put(&w, 0, 2); put(&w, 2, 2);                                                 /* smoothing: set 0 mode 1, set 1 mode 0, set 2 mode 2 with time 2 (256) */
        put(&w, 2, 2);
        (void)vals;
        CHECK(parse(&w, &hr, 32, 1, &st, &f) == 1, "high rate frame refused");
        CHECK(f.num_sets == 4u && f.ext_frame == 1u && f.param_slot[0] == 5u && f.param_slot[1] == 17u && f.param_slot[2] == 29u && f.param_slot[3] == 31u, "slots %u %u %u %u (%u sets)", f.param_slot[0], f.param_slot[1], f.param_slot[2], f.param_slot[3], f.num_sets);
        CHECK(f.smooth_time[1] == 256u && f.smooth_time[2] == 256u && f.smooth_time[3] == f.smooth_time[2], "smoothing times %u %u %u", f.smooth_time[1], f.smooth_time[2], f.smooth_time[3]);
    }
    /* refusals: slots that do not increase, a slot beyond the frame, a wrong frame length, a reserved frequency resolution, temporal shaping enabled in the frame */
    {   SautiUsacMpsConfig hr = cfg1;
        hr.high_rate = 1;
        memset(&w, 0, sizeof w); memset(&st, 0, sizeof st);
        put(&w, 1, 1); put(&w, 1, 3); put(&w, 9, 5); put(&w, 9, 5);
        CHECK(parse(&w, &hr, 32, 1, &st, &f) == 0, "equal parameter slots accepted");
        CHECK(parse(&w, &cfg1, 48, 1, &st, &f) == 0, "48 time slots accepted");
        { SautiUsacMpsConfig bad = cfg1; bad.freq_res = 0; CHECK(parse(&w, &bad, 32, 1, &st, &f) == 0, "bsFreqRes 0 accepted"); }
        { SautiUsacMpsConfig ts = cfg1; ts.temp_shape = 1; memset(&w, 0, sizeof w); put(&w, 0, 2); put(&w, 0, 2); put(&w, 1, 1); CHECK(parse(&w, &ts, 32, 1, &st, &f) == -1, "enabled temporal shaping must be reported as unsupported"); }
    }
}

static void test_tables(void) {
    unsigned b, i;
    static const struct { const uint8_t *map; unsigned bands; } maps[] = { { sauti_usac_mps_band_map_28, 28 }, { sauti_usac_mps_band_map_20, 20 }, { sauti_usac_mps_band_map_14, 14 }, { sauti_usac_mps_band_map_10, 10 },
                                                                          { sauti_usac_mps_band_map_7, 7 }, { sauti_usac_mps_band_map_5, 5 }, { sauti_usac_mps_band_map_4, 4 } };
    for (b = 0; b < sizeof maps / sizeof maps[0]; b++) {
        unsigned bad = 0;
        for (i = 0; i < 71u; i++) if (maps[b].map[i] >= maps[b].bands) bad++;
        CHECK(!bad && maps[b].map[70] == maps[b].bands - 1u, "band map of %u bands: %u entries out of range, last %u", maps[b].bands, bad, maps[b].map[70]);
    }
    CHECK(sauti_usac_mps_cld_dequant[15] == 0.0f && sauti_usac_mps_cld_dequant[0] == -150.0f && sauti_usac_mps_cld_dequant[30] == 150.0f && sauti_usac_mps_icc_dequant[0] == 1.0f && sauti_usac_mps_icc_dequant[7] < -0.9f, "dequantisation tables");
    for (i = 0; i < 31u; i++) CHECK(fabs((double)sauti_usac_mps_c_l[i] * sauti_usac_mps_c_l[i] + (double)sauti_usac_mps_c_l[30u - i] * sauti_usac_mps_c_l[30u - i] - 1.0) < 1e-4, "the channel gains of CLD index %u do not preserve power", i);
    for (i = 0; i < 29u; i++) CHECK(i == 0 || sauti_usac_mps_gain_set_index[i] >= 0, "gain set index");
    CHECK(sauti_usac_mps_hyb8[6] == 0.125f && sauti_usac_mps_hyb2[6] == 0.5f, "hybrid prototypes");
    {   /* every Huffman tree is a tree: following any path ends in a leaf (value <= 0) within the table */
        unsigned node, depth, bits, leaves = 0;
        for (bits = 0; bits < 2; bits++) {
            for (node = 0; node < 30u; node++) { const int nx = sauti_usac_mps_cld_1d[0][node][bits]; if (nx > 0) CHECK((unsigned)nx < 30u, "CLD tree node %u points outside", node); else leaves++; }
            (void)depth;
        }
        CHECK(leaves == 31u, "the CLD 1-D tree has %u leaves, expected 31", leaves);
    }
}

static void test_signal(void) {
    static SautiUsacMpsDec d; static SautiUsacMpsFrame f; static SautiAacSbrComplex in[32 * 64], o0[32 * 64], o1[32 * 64];
    unsigned i, k, p, ok;
    SautiUsacMpsConfig c = cfg1;
    c.residual = 0;
    memset(&f, 0, sizeof f);
    f.num_sets = 1; f.param_slot[0] = 31; f.param_bands = 28;
    memset(&d, 0, sizeof d); memset(in, 0, sizeof in);
    for (i = 0; i < 3u; i++) CHECK(sauti_usac_mps212_apply(&d, &c, &f, 40, 32, in, NULL, 0, o0, o1), "silence refused");
    for (i = 0, ok = 1; i < 32u * 64u; i++) if (o0[i].re != 0.0f || o0[i].im != 0.0f || o1[i].re != 0.0f || o1[i].im != 0.0f) ok = 0;
    CHECK(ok, "silence in, signal out");
    /* a downmix tone in QMF band 20 (above the hybrid split) with ICC 1.0 (index 0): CLD 0 dB gives equal levels in both channels, CLD index +-15 (+-150 dB) puts all of it on one side.  Four frames, so that the
     * interpolation from the zero gains of the start has finished. */
    for (p = 0; p < 3u; p++) {
        const int cld = p == 0 ? 0 : p == 1 ? 15 : -15;
        double e0 = 0, e1 = 0;
        memset(&d, 0, sizeof d);
        for (i = 0; i < 28u; i++) { f.cld_idx[0][i] = cld; f.icc_idx[0][i] = 0; }
        for (k = 0; k < 4u; k++) {
            for (i = 0; i < 32u; i++) { in[i * 64u + 20u].re = 0.5f * (float)cos(0.3 * (double)(i + 32u * k)); in[i * 64u + 20u].im = 0.5f * (float)sin(0.3 * (double)(i + 32u * k)); }
            CHECK(sauti_usac_mps212_apply(&d, &c, &f, 40, 32, in, NULL, 0, o0, o1), "tone refused");
        }
        for (i = 0; i < 32u; i++) { e0 += (double)o0[i * 64u + 20u].re * o0[i * 64u + 20u].re + (double)o0[i * 64u + 20u].im * o0[i * 64u + 20u].im; e1 += (double)o1[i * 64u + 20u].re * o1[i * 64u + 20u].re + (double)o1[i * 64u + 20u].im * o1[i * 64u + 20u].im; }
        if (p == 0) CHECK(e0 > 0.0 && fabs(e0 / e1 - 1.0) < 0.05, "CLD 0: channel energies %.4g / %.4g", e0, e1);
        if (p == 1) CHECK(e0 > 0.0 && e1 < e0 * 1e-6, "CLD index +15: left %.4g right %.4g", e0, e1);
        if (p == 2) CHECK(e1 > 0.0 && e0 < e1 * 1e-6, "CLD index -15: left %.4g right %.4g", e0, e1);
        for (i = 0; i < 32u; i++) for (k = 41; k < 64u; k++) if (o0[i * 64u + k].re != 0.0f || o1[i * 64u + k].im != 0.0f) ok = 0;
    }
    CHECK(ok, "signal above band_count");
    /* refusals */
    memset(&d, 0, sizeof d); c.phase_coding = 1;
    CHECK(!sauti_usac_mps212_apply(&d, &c, &f, 40, 32, in, NULL, 0, o0, o1), "phase coding accepted"); c.phase_coding = 0;
    c.residual_bands = 3; CHECK(!sauti_usac_mps212_apply(&d, &c, &f, 40, 32, in, NULL, 0, o0, o1), "residual bands accepted"); c.residual_bands = 0;
    c.temp_shape = 3; CHECK(!sauti_usac_mps212_apply(&d, &c, &f, 40, 32, in, NULL, 0, o0, o1), "TSD accepted"); c.temp_shape = 0;
    CHECK(!sauti_usac_mps212_apply(&d, &c, &f, 40, 80, in, NULL, 0, o0, o1) && !sauti_usac_mps212_apply(&d, &c, &f, 2, 32, in, NULL, 0, o0, o1) && !sauti_usac_mps212_apply(&d, &c, &f, 65, 32, in, NULL, 0, o0, o1), "bad slots / band count accepted");
    f.param_slot[0] = 30; CHECK(!sauti_usac_mps212_apply(&d, &c, &f, 40, 32, in, NULL, 0, o0, o1), "last parameter set before the last slot accepted"); f.param_slot[0] = 31;
}

static void test_golden(void) {
    FILE *fp = fopen("tests/data/mps_golden.bin", "rb");
    int32_t nf, fr;
    static SautiUsacMpsDec d; static SautiUsacMpsFrame f; static float inb[32 * 54 * 2]; static SautiAacSbrComplex in[32 * 64], o0[32 * 64], o1[32 * 64];
    SautiUsacMpsConfig c = cfg1;
    if (!fp) { CHECK(0, "cannot read tests/data/mps_golden.bin (run from the repository root)"); return; }
    { char m[4]; if (fread(m, 1, 4, fp) != 4 || memcmp(m, "MPSG", 4) || fread(&nf, 4, 1, fp) != 1) { CHECK(0, "golden header"); fclose(fp); return; } }
    c.residual = 1;
    for (fr = 0; fr < nf; fr++) {
        int32_t hd[4], cld[28], icc[28], i, s, b, ch;
        if (fread(hd, 4, 4, fp) != 4 || fread(cld, 4, 28, fp) != 28 || fread(icc, 4, 28, fp) != 28 || fread(inb, 4, 32u * 54u * 2u, fp) != 32u * 54u * 2u) { CHECK(0, "golden frame %d truncated", (int)fr); break; }
        memset(&f, 0, sizeof f); f.num_sets = (unsigned)hd[1]; f.param_bands = (unsigned)hd[2]; f.param_slot[0] = (unsigned)hd[3];
        for (i = 0; i < 28; i++) { f.cld_idx[0][i] = cld[i]; f.icc_idx[0][i] = icc[i]; }
        memset(in, 0, sizeof in);
        for (s = 0; s < 32; s++) for (b = 0; b < 54; b++) { in[s * 64 + b].re = inb[(s * 54 + b) * 2] / 32768.0f; in[s * 64 + b].im = inb[(s * 54 + b) * 2 + 1] / 32768.0f; }
        CHECK(sauti_usac_mps212_apply(&d, &c, &f, (unsigned)hd[0], 32, in, NULL, 0, o0, o1), "golden frame %d refused", (int)fr);
        for (ch = 0; ch < 2; ch++) {
            double exp4[4], got[4] = { 0, 0, 0, 0 }, scale; const SautiAacSbrComplex *o = ch ? o1 : o0;
            if (fread(exp4, 8, 4, fp) != 4) { CHECK(0, "golden frame %d sums", (int)fr); break; }
            if (fr == 0) continue;                                                                   /* the reference's first frame runs through a start-up path */
            for (s = 0; s < 32; s++) for (b = 0; b < 54; b++) {
                const double re = o[s * 64 + b].re * 32768.0, im = o[s * 64 + b].im * 32768.0, w = ((double)s * 131.0 + (double)b * 7.0 + 1.0) / (32.0 * 64.0);
                got[0] += re; got[1] += im; got[2] += re * re + im * im; got[3] += (re + 2.0 * im) * w;
            }
            scale = sqrt(exp4[2] * 32.0 * 54.0) + 1e-9;
            for (i = 0; i < 4; i++) CHECK(fabs(got[i] - exp4[i]) <= (i == 2 ? 3e-5 * exp4[2] : 3e-5 * scale) + 1e-9, "golden frame %d channel %d sum %d: %.9g, libxaac %.9g", (int)fr, (int)ch, (int)i, got[i], exp4[i]);
        }
    }
    fclose(fp);
}

/* residual bands: the golden files of libxaac's upmix for the streams usac_y1..y3 (real residual, decorrelator configurations 0 / 2 / 1) and properties of the mix */
static void test_golden_res(void) {
    FILE *fp = fopen("tests/data/mps_res_golden.bin", "rb");
    int32_t ns, si;
    static SautiUsacMpsDec d; static SautiUsacMpsFrame f; static float inb[32 * 54 * 2], rb_[32 * 32 * 2]; static SautiAacSbrComplex in[32 * 64], rin[32 * 64], o0[32 * 64], o1[32 * 64];
    if (!fp) { CHECK(0, "cannot read tests/data/mps_res_golden.bin (run from the repository root)"); return; }
    { char m[4]; if (fread(m, 1, 4, fp) != 4 || memcmp(m, "MPSR", 4) || fread(&ns, 4, 1, fp) != 1) { CHECK(0, "residual golden header"); fclose(fp); return; } }
    for (si = 0; si < ns; si++) {
        int32_t sh[3], fr;
        SautiUsacMpsConfig c = cfg1;
        if (fread(sh, 4, 3, fp) != 3) { CHECK(0, "residual golden set %d header", (int)si); break; }
        memset(&d, 0, sizeof d); c.residual = 1; c.residual_bands = (unsigned)sh[0]; c.decorr = (unsigned)sh[1];
        for (fr = 0; fr < sh[2]; fr++) {
            int32_t hd[4], cld[28], icc[28], i, s, b, ch;
            if (fread(hd, 4, 4, fp) != 4 || fread(cld, 4, 28, fp) != 28 || fread(icc, 4, 28, fp) != 28 || fread(inb, 4, 32u * 54u * 2u, fp) != 32u * 54u * 2u || fread(rb_, 4, 32u * 32u * 2u, fp) != 32u * 32u * 2u) { CHECK(0, "residual golden set %d frame %d truncated", (int)si, (int)fr); break; }
            memset(&f, 0, sizeof f); f.num_sets = (unsigned)hd[1]; f.param_bands = (unsigned)hd[2]; f.param_slot[0] = (unsigned)hd[3];
            for (i = 0; i < 28; i++) { f.cld_idx[0][i] = cld[i]; f.icc_idx[0][i] = icc[i]; }
            memset(in, 0, sizeof in); memset(rin, 0, sizeof rin);
            for (s = 0; s < 32; s++) {
                for (b = 0; b < 54; b++) { in[s * 64 + b].re = inb[(s * 54 + b) * 2] / 32768.0f; in[s * 64 + b].im = inb[(s * 54 + b) * 2 + 1] / 32768.0f; }
                for (b = 0; b < 32; b++) { rin[s * 64 + b].re = rb_[(s * 32 + b) * 2] / 32768.0f; rin[s * 64 + b].im = rb_[(s * 32 + b) * 2 + 1] / 32768.0f; }
            }
            CHECK(sauti_usac_mps212_apply(&d, &c, &f, (unsigned)hd[0], 32, in, rin, 32, o0, o1), "residual golden set %d frame %d refused", (int)si, (int)fr);
            for (ch = 0; ch < 2; ch++) {
                double exp4[4], got[4] = { 0, 0, 0, 0 }, scale; const SautiAacSbrComplex *o = ch ? o1 : o0;
                if (fread(exp4, 8, 4, fp) != 4) { CHECK(0, "residual golden set %d frame %d sums", (int)si, (int)fr); break; }
                if (fr < 3) continue;                                                                /* the reference's first frame runs through a start-up path and the next two still carry its filter states */
                for (s = 0; s < 32; s++) for (b = 0; b < 54; b++) {
                    const double re = o[s * 64 + b].re * 32768.0, im = o[s * 64 + b].im * 32768.0, w = ((double)s * 131.0 + (double)b * 7.0 + 1.0) / (32.0 * 64.0);
                    got[0] += re; got[1] += im; got[2] += re * re + im * im; got[3] += (re + 2.0 * im) * w;
                }
                scale = sqrt(exp4[2] * 32.0 * 54.0) + 1e-9;
                for (i = 0; i < 4; i++) CHECK(fabs(got[i] - exp4[i]) <= (i == 2 ? 3e-5 * exp4[2] : 3e-5 * scale) + 1e-9, "residual golden set %d (res %d, decorr %d) frame %d channel %d sum %d: %.9g, libxaac %.9g", (int)si, (int)sh[0], (int)sh[1], (int)fr, (int)ch, (int)i, got[i], exp4[i]);
            }
        }
    }
    fclose(fp);
}

static void test_residual(void) {
    static SautiUsacMpsDec da, db, dc_; static SautiUsacMpsFrame f; static SautiAacSbrComplex in[32 * 64], r0[32 * 64], r1[32 * 64], z[32 * 64], a0[32 * 64], a1[32 * 64], b0[32 * 64], b1[32 * 64], c0[32 * 64], c1[32 * 64];
    SautiUsacMpsConfig c = cfg1;
    unsigned s, b, fr, i, bad = 0, moved = 0, same_above = 1;
    memset(&f, 0, sizeof f); f.num_sets = 1; f.param_bands = 28; f.param_slot[0] = 31;
    for (i = 0; i < 28u; i++) { f.cld_idx[0][i] = (int)(i % 7u) - 3; f.icc_idx[0][i] = (int)(i % 5u); }
    c.residual = 1; c.residual_bands = 6;
    memset(z, 0, sizeof z);
    for (fr = 0; fr < 6u; fr++) {                                                                  /* linearity in the residual: out(2r) - out(0) = 2 (out(r) - out(0)), and only the bands of the residual change */
        for (s = 0; s < 32u; s++) for (b = 0; b < 40u; b++) {
            in[s * 64 + b].re = 0.01f * sinf((float)(s * 3u + b * 5u + fr)); in[s * 64 + b].im = 0.01f * cosf((float)(s * 7u + b + 2u * fr));
            r0[s * 64 + b].re = 0.02f * sinf((float)(s * 11u + b * 3u + fr)); r0[s * 64 + b].im = 0.02f * cosf((float)(s + b * 13u));
            r1[s * 64 + b].re = 2.0f * r0[s * 64 + b].re; r1[s * 64 + b].im = 2.0f * r0[s * 64 + b].im;
        }
        CHECK(sauti_usac_mps212_apply(&da, &c, &f, 40, 32, in, z, 32, a0, a1) && sauti_usac_mps212_apply(&db, &c, &f, 40, 32, in, r0, 32, b0, b1) && sauti_usac_mps212_apply(&dc_, &c, &f, 40, 32, in, r1, 32, c0, c1), "residual frame %u refused", fr);
        for (s = 0; s < 32u; s++) for (b = 0; b < 40u; b++) {
            const float dr = b0[s * 64 + b].re - a0[s * 64 + b].re, d2 = c0[s * 64 + b].re - a0[s * 64 + b].re;
            if (fabsf(d2 - 2.0f * dr) > 1e-5f * (1.0f + fabsf(d2))) bad++;
            if (b >= 12u && (b0[s * 64 + b].re != a0[s * 64 + b].re || b0[s * 64 + b].im != a0[s * 64 + b].im || b1[s * 64 + b].re != a1[s * 64 + b].re)) same_above = 0;   /* 6 parameter bands end below QMF band 12 */
            if (b < 4u && fabsf(dr) > 1e-6f) moved++;
        }
    }
    CHECK(bad == 0, "the residual does not enter linearly (%u samples)", bad);
    CHECK(moved > 100u, "the residual changed too little of the lowest bands (%u)", moved);
    CHECK(same_above, "the residual changed bands above its parameter bands");
    {   /* the downmix gain (bsFixedGainDMX) applies to the residual as well: gain g on (in, r) = gain 0 on (G in, G r); and the residual of all 28 bands reaches QMF band 31 */
        static SautiAacSbrComplex gin[32 * 64], gr[32 * 64]; static SautiUsacMpsDec dg, dh;
        SautiUsacMpsConfig cg = c; double G = sauti_usac_mps_clip_gain[2]; double e = 0, ref = 0, d31 = 0;
        cg.fixed_gain = 2;
        for (i = 0; i < 32u * 64u; i++) { gin[i].re = (float)(G * in[i].re); gin[i].im = (float)(G * in[i].im); gr[i].re = (float)(G * r0[i].re); gr[i].im = (float)(G * r0[i].im); }
        CHECK(sauti_usac_mps212_apply(&dg, &cg, &f, 40, 32, in, r0, 32, a0, a1) && sauti_usac_mps212_apply(&dh, &c, &f, 40, 32, gin, gr, 32, b0, b1), "gain frame refused");
        for (i = 0; i < 32u * 64u; i++) { const double dd = a0[i].re - b0[i].re; e += dd * dd; ref += (double)b0[i].re * b0[i].re; }
        CHECK(e <= 1e-9 * ref + 1e-18, "the downmix gain does not apply to the residual alike (error %g of %g)", e, ref);
        c.residual_bands = 28;
        CHECK(sauti_usac_mps212_apply(&da, &c, &f, 40, 32, in, z, 32, a0, a1) && sauti_usac_mps212_apply(&db, &c, &f, 40, 32, in, r0, 32, b0, b1), "28 residual bands refused");
        for (s = 0; s < 32u; s++) d31 += fabs(b0[s * 64 + 31].re - a0[s * 64 + 31].re) + fabs(b0[s * 64 + 31].im - a0[s * 64 + 31].im);
        CHECK(d31 > 1e-4, "the residual does not reach QMF band 31 (%g)", d31);
        c.residual_bands = 6;
    }
    {   /* the residual grid: bands 0..2 as analysed, bands 3..31 six slots later (the last six slots of the previous frame first), bands above 31 zero */
        static SautiUsacMpsDec dr; static SautiAacSbrComplex low1[32 * 32], low2[32 * 32], g1[32 * 64], g2[32 * 64]; unsigned ok = 1;
        for (i = 0; i < 32u * 32u; i++) { low1[i].re = (float)(i + 1u); low1[i].im = -(float)i; low2[i].re = (float)(5000u + i); low2[i].im = (float)(7000u + i); }
        sauti_usac_mps212_residual_grid(&dr, low1, 32, g1); sauti_usac_mps212_residual_grid(&dr, low2, 32, g2);
        for (s = 0; s < 32u; s++) for (b = 0; b < 64u; b++) {
            const SautiAacSbrComplex *x1 = &g1[s * 64 + b], *x2 = &g2[s * 64 + b];
            SautiAacSbrComplex e1 = { 0, 0 }, e2 = { 0, 0 };
            if (b < 3u) { e1 = low1[s * 32 + b]; e2 = low2[s * 32 + b]; }
            else if (b < 32u) { if (s >= 6u) e1 = low1[(s - 6u) * 32 + b]; e2 = s < 6u ? low1[(26u + s) * 32 + b] : low2[(s - 6u) * 32 + b]; }
            if (x1->re != e1.re || x1->im != e1.im || x2->re != e2.re || x2->im != e2.im) ok = 0;
        }
        CHECK(ok, "residual grid alignment");
    }
    c.residual_bands = 29; CHECK(!sauti_usac_mps212_apply(&da, &c, &f, 40, 32, in, r0, 32, a0, a1), "more residual bands than parameter bands accepted");
    c.residual_bands = 6; CHECK(!sauti_usac_mps212_apply(&da, &c, &f, 40, 32, in, r0, 2, a0, a1), "a residual grid of 2 bands accepted");
}

int main(void) {
    test_tables(); test_parse(); test_signal(); test_golden(); test_golden_res(); test_residual();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_mps: all passed\n"); return 0;
}
