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

static const SautiUsacMpsConfig cfg1 = { 1, 0, 0, 10, 0, 0, 0, 0, 1, 0, 0, 0 };          /* 28 bands, one parameter set per frame */

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

/* the IPD quantiser flag of a set without coded data is the flag of the last frame that coded that set index (found with a real file of the Fraunhofer encoder, docs SBR-128): a frame that keeps its IPD values after one coded
 * with the coarse quantiser smooths the phases with the coarse flag */
static void test_ipd_flag_hold(void) {
    static SautiUsacMpsState st; static SautiUsacMpsFrame f;
    SautiUsacMpsConfig c = cfg1;
    Bw w; unsigned cv[28], iv[28], pv[10], i;
    c.phase_coding = 1u; c.bands_phase = 10u;
    for (i = 0; i < 28u; i++) { cv[i] = (i * 5u + 3u) % 31u; iv[i] = (i * 3u) % 8u; }
    for (i = 0; i < 10u; i++) pv[i] = i % 8u;
    memset(&w, 0, sizeof w); memset(&st, 0, sizeof st);
    put_pcm_set(&w, 0, 0, 28, cv, 5); put_pcm_set(&w, 0, 0, 28, iv, 3);
    put(&w, 1, 1); put(&w, 0, 1);                                                                    /* bsPhaseMode = 1, bsOpdSmoothingMode = 0 */
    put_pcm_set(&w, 1, 0, 10, pv, 3);                                                                /* IPD: new data, coarse quantiser */
    CHECK(parse(&w, &c, 32, 1, &st, &f) == 1, "coarse IPD frame refused");
    CHECK(f.ipd_coarse[0] == 1u, "the coarse flag of the coded IPD set is %u", f.ipd_coarse[0]);
    memset(&w, 0, sizeof w);
    put(&w, 0, 1);                                                                                   /* bsIndependencyFlag = 0 */
    put(&w, 1, 2); put(&w, 1, 2);                                                                    /* CLD keep, ICC keep */
    put(&w, 1, 1); put(&w, 0, 1);                                                                    /* bsPhaseMode = 1, bsOpdSmoothingMode = 0 */
    put(&w, 1, 2);                                                                                   /* IPD keep: no data, no quantiser flag */
    CHECK(parse(&w, &c, 32, 0, &st, &f) == 1, "keep IPD frame refused");
    CHECK(f.ipd_coarse[0] == 1u, "a kept IPD set loses the coarse flag of the frame that coded it (%u)", f.ipd_coarse[0]);
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
        {   /* STP (bsTempShapeConfig 1): bsTempShapeEnable and one flag per output channel are read; GES (2) and TSD (3) frames that use their tool are reported as unsupported */
            SautiUsacMpsConfig ts = cfg1; ts.temp_shape = 1;
            memset(&w, 0, sizeof w); put(&w, 0, 2); put(&w, 0, 2); put(&w, 1, 1); put(&w, 1, 1); put(&w, 0, 1);
            CHECK(parse(&w, &ts, 32, 1, &st, &f) == 1 && f.stp_enable == 1u && f.stp_ch[0] == 1u && f.stp_ch[1] == 0u, "STP flags: enable %u channels %u %u", f.stp_enable, f.stp_ch[0], f.stp_ch[1]);
            memset(&w, 0, sizeof w); put(&w, 0, 2); put(&w, 0, 2); put(&w, 0, 1);
            CHECK(parse(&w, &ts, 32, 1, &st, &f) == 1 && f.stp_enable == 0u && f.stp_ch[0] == 0u && f.stp_ch[1] == 0u, "STP disabled in the frame");
            {   /* TSD (bsTempShapeConfig 3): a codeword that places ntr transient slots (combinatorial number system), then a 3 bit phase for each; round trip over random placements */
                static uint64_t bn[65][33]; unsigned slots, trial, rs = 12345u;
                { unsigned n, r; for (n = 0; n <= 64u; n++) for (r = 0; r <= 32u; r++) bn[n][r] = r == 0u ? 1u : (n == 0u ? 0u : bn[n - 1u][r - 1u] + bn[n - 1u][r]); }
                ts.temp_shape = 3;
                for (slots = 32; slots <= 64u; slots += 32u) for (trial = 0; trial < 200u; trial++) {
                    unsigned ntr, k, sep[64] = { 0 }, ph[64] = { 0 }, cnt = 0, i, len = 0, idx = 0;
                    uint64_t cw = 0, cc;
                    rs = rs * 1664525u + 1013904223u; ntr = 1u + (rs >> 16) % (slots == 32u ? 16u : 32u);
                    while (cnt < ntr) { rs = rs * 1664525u + 1013904223u; k = (rs >> 16) % slots; if (!sep[k]) { sep[k] = 1; cnt++; } }
                    for (k = 0; k < slots; k++) if (sep[k]) { idx++; cw += bn[k][idx]; }
                    cc = bn[slots][ntr]; while (len < 64u && ((cc - 1u) >> len) != 0u) len++;
                    memset(&w, 0, sizeof w); put(&w, 0, 2); put(&w, 0, 2); put(&w, 1, 1);                /* CLD and ICC data mode 0 (default), then bsTsdEnable */
                    put(&w, ntr - 1u, slots == 32u ? 4u : 5u);
                    for (i = len; i-- > 0u;) put(&w, (uint32_t)((cw >> i) & 1u), 1);
                    for (k = 0; k < slots; k++) if (sep[k]) { rs = rs * 1664525u + 1013904223u; ph[k] = (rs >> 16) & 7u; put(&w, ph[k], 3); }
                    memset(&st, 0, sizeof st);
                    CHECK(parse(&w, &ts, slots, 1, &st, &f) == 1, "TSD frame refused (slots %u, ntr %u)", slots, ntr);
                    for (k = 0; k < slots; k++) { CHECK((unsigned)f.tsd_sep[k] == sep[k], "TSD slot %u: parsed %u, coded %u (slots %u, ntr %u)", k, (unsigned)f.tsd_sep[k], sep[k], slots, ntr); if (sep[k]) CHECK((unsigned)f.tsd_phase[k] == ph[k], "TSD phase of slot %u", k); }
                }
            }
            {   /* GES (bsTempShapeConfig 2): one channel with a run-length envelope; leaves found by walking the reshape table */
                int leaf[64], leafn = 0, path[64][16], plen[64], i;
                {   /* iterative DFS over the Huffman nodes: collect the bit path of every leaf */
                    int stk_node[64], stk_len[64], stk_bits[64][16], sp = 0;
                    stk_node[0] = 0; stk_len[0] = 0; sp = 1;
                    while (sp > 0) {
                        int nd = stk_node[--sp], ln = stk_len[sp], bit, cur[16], q0;
                        for (q0 = 0; q0 < ln; q0++) cur[q0] = stk_bits[sp][q0];
                        for (bit = 0; bit < 2; bit++) {
                            const int next = sauti_usac_mps_reshape[nd][bit];
                            int bits[16], q;
                            for (q = 0; q < ln; q++) bits[q] = cur[q];
                            bits[ln] = bit;
                            if (next <= 0) { if (leafn < 64) { leaf[leafn] = next; plen[leafn] = ln + 1; for (q = 0; q <= ln; q++) path[leafn][q] = bits[q]; leafn++; } }
                            else { stk_node[sp] = next; stk_len[sp] = ln + 1; for (q = 0; q <= ln; q++) stk_bits[sp][q] = bits[q]; sp++; }
                        }
                    }
                }
                ts.temp_shape = 2; ts.env_quant = 1;
                CHECK(leafn >= 3, "reshape table leaves %d", leafn);
                memset(&w, 0, sizeof w); put(&w, 0, 2); put(&w, 0, 2); put(&w, 1, 1); put(&w, 0, 1); put(&w, 1, 1);          /* enable, channel 0 off, channel 1 on */
                { unsigned got = 0, k; int firstlen = 0, firstval = 0;
                  for (i = 0; got < 32u && i < leafn; i++) {
                      int len, val;
                      if (leaf[i] == 0) { val = 0; len = 2; } else { const int v8 = -(leaf[i] + 1); val = v8 >> 4; len = (v8 & 15) + 1; }
                      if (got + (unsigned)len > 32u) continue;
                      for (k = 0; k < (unsigned)plen[i]; k++) put(&w, (uint32_t)path[i][k], 1);
                      if (!firstlen) { firstlen = len; firstval = val; }
                      got += (unsigned)len;
                  }
                  while (got < 32u) { int e0 = -1; for (i = 0; i < leafn; i++) if (leaf[i] == 0) e0 = i; if (e0 < 0) break; for (k = 0; k < (unsigned)plen[e0]; k++) put(&w, (uint32_t)path[e0][k], 1); got += 2u; }
                  CHECK(parse(&w, &ts, 32, 1, &st, &f) == 1 && f.ges_ch[0] == 0u && f.ges_ch[1] == 1u, "GES frame refused");
                  CHECK(fabs((double)f.ges_env[1][0] - pow(2.0, (double)firstval / 3.0 - 1.0)) < 1e-6 && (firstlen < 2 || fabs((double)f.ges_env[1][firstlen - 1] - pow(2.0, (double)firstval / 3.0 - 1.0)) < 1e-6), "GES envelope value %g, expected %g", (double)f.ges_env[1][0], pow(2.0, (double)firstval / 3.0 - 1.0));
                }
            }
        }
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
    memset(&d, 0, sizeof d); c.phase_coding = 1; c.bands_phase = 29;
    CHECK(!sauti_usac_mps212_apply(&d, &c, &f, 40, 32, in, NULL, 0, o0, o1), "29 phase bands accepted"); c.phase_coding = 0; c.bands_phase = 10;
    c.residual_bands = 3; CHECK(!sauti_usac_mps212_apply(&d, &c, &f, 40, 32, in, NULL, 0, o0, o1), "residual bands accepted"); c.residual_bands = 0;
    c.temp_shape = 0;
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
        sauti_usac_mps212_residual_grid(&dr, low1, 32, 0, 6, g1); sauti_usac_mps212_residual_grid(&dr, low2, 32, 0, 6, g2);
        for (s = 0; s < 32u; s++) for (b = 0; b < 64u; b++) {
            const SautiAacSbrComplex *x1 = &g1[s * 64 + b], *x2 = &g2[s * 64 + b];
            SautiAacSbrComplex e1 = { 0, 0 }, e2 = { 0, 0 };
            if (b < 3u) { e1 = low1[s * 32 + b]; e2 = low2[s * 32 + b]; }
            else if (b < 32u) { if (s >= 6u) e1 = low1[(s - 6u) * 32 + b]; e2 = s < 6u ? low1[(26u + s) * 32 + b] : low2[(s - 6u) * 32 + b]; }
            if (x1->re != e1.re || x1->im != e1.im || x2->re != e2.re || x2->im != e2.im) ok = 0;
        }
        CHECK(ok, "residual grid alignment");
        {   /* eSBR 4:1: bands 0..2 delayed by 6 slots and bands 3..31 by 12 (the previous frame's last slots first) */
            static SautiUsacMpsDec d4; static SautiAacSbrComplex l1[64 * 32], l2[64 * 32], q1[64 * 64], q2[64 * 64]; unsigned ok4 = 1;
            for (i = 0; i < 64u * 32u; i++) { l1[i].re = (float)(i + 1u); l1[i].im = -(float)i; l2[i].re = (float)(9000u + i); l2[i].im = (float)(3000u + i); }
            sauti_usac_mps212_residual_grid(&d4, l1, 64, 6, 12, q1); sauti_usac_mps212_residual_grid(&d4, l2, 64, 6, 12, q2);
            for (s = 0; s < 64u; s++) for (b = 0; b < 32u; b++) {
                const unsigned lag = b < 3u ? 6u : 12u;
                SautiAacSbrComplex e1 = { 0, 0 }, e2;
                if (s >= lag) e1 = l1[(s - lag) * 32 + b];
                e2 = s < lag ? l1[(64u - lag + s) * 32 + b] : l2[(s - lag) * 32 + b];
                if (q1[s * 64 + b].re != e1.re || q1[s * 64 + b].im != e1.im || q2[s * 64 + b].re != e2.re || q2[s * 64 + b].im != e2.im) ok4 = 0;
            }
            CHECK(ok4, "residual grid alignment at eSBR 4:1");
        }
    }
    c.residual_bands = 29; CHECK(!sauti_usac_mps212_apply(&da, &c, &f, 40, 32, in, r0, 32, a0, a1), "more residual bands than parameter bands accepted");
    c.residual_bands = 6; CHECK(!sauti_usac_mps212_apply(&da, &c, &f, 40, 32, in, r0, 2, a0, a1), "a residual grid of 2 bands accepted");
}

/* stereoConfigIndex 1 (rotation upmix), with and without phase coding: golden files of libxaac's upmix for usac_y4..y6, and the phase properties */
static void test_golden_phase(void) {
    FILE *fp = fopen("tests/data/mps_phase_golden.bin", "rb");
    int32_t ns, si;
    static SautiUsacMpsDec d; static SautiUsacMpsFrame f; static float inb[32 * 54 * 2], rb_[32 * 32 * 2], genv[64]; static SautiAacSbrComplex in[32 * 64], rin[32 * 64], o0[32 * 64], o1[32 * 64];
    if (!fp) { CHECK(0, "cannot read tests/data/mps_phase_golden.bin (run from the repository root)"); return; }
    { char m[4]; if (fread(m, 1, 4, fp) != 4 || memcmp(m, "MPSF", 4) || fread(&ns, 4, 1, fp) != 1) { CHECK(0, "phase golden header"); fclose(fp); return; } }
    for (si = 0; si < ns; si++) {
        int32_t sh[6], fr;
        SautiUsacMpsConfig c = cfg1;
        if (fread(sh, 4, 6, fp) != 6) { CHECK(0, "phase golden set %d header", (int)si); break; }
        memset(&d, 0, sizeof d); c.residual = (unsigned)sh[4]; c.residual_bands = (unsigned)sh[3]; c.phase_coding = (unsigned)sh[0]; c.bands_phase = c.residual_bands > 10u ? c.residual_bands : 10u; c.decorr = (unsigned)sh[1]; c.temp_shape = (unsigned)sh[5];
        for (fr = 0; fr < sh[2]; fr++) {
            int32_t hd[12], tsdv[32], cld[28], icc[28], ipd[28], i, s, b, ch;
            if (fread(hd, 4, 12, fp) != 12 || fread(genv, 4, 64, fp) != 64 || fread(tsdv, 4, 32, fp) != 32 || fread(cld, 4, 28, fp) != 28 || fread(icc, 4, 28, fp) != 28 || fread(ipd, 4, 28, fp) != 28 || fread(inb, 4, 32u * 54u * 2u, fp) != 32u * 54u * 2u || (sh[3] && fread(rb_, 4, 32u * 32u * 2u, fp) != 32u * 32u * 2u)) { CHECK(0, "phase golden set %d frame %d truncated", (int)si, (int)fr); break; }
            memset(&f, 0, sizeof f); f.num_sets = (unsigned)hd[1]; f.param_bands = (unsigned)hd[2]; f.param_slot[0] = (unsigned)hd[3]; f.phase_mode = (unsigned)hd[4]; f.opd_smoothing = (unsigned)hd[5]; f.ipd_coarse[0] = (unsigned)hd[6]; f.stp_ch[0] = (unsigned)hd[7]; f.stp_ch[1] = (unsigned)hd[8]; f.stp_enable = (unsigned)(hd[7] | hd[8]); f.tsd_enable = (unsigned)hd[11]; { unsigned q; for (q = 0; q < 32u; q++) { f.tsd_sep[q] = (uint8_t)(tsdv[q] & 1); f.tsd_phase[q] = (uint8_t)((tsdv[q] >> 1) & 7); } } f.ges_ch[0] = (unsigned)hd[9]; f.ges_ch[1] = (unsigned)hd[10]; { unsigned q; for (q = 0; q < 32u; q++) { f.ges_env[0][q] = genv[q]; f.ges_env[1][q] = genv[32u + q]; } }
            for (i = 0; i < 28; i++) { f.cld_idx[0][i] = cld[i]; f.icc_idx[0][i] = icc[i]; f.ipd_idx[0][i] = ipd[i]; }
            memset(in, 0, sizeof in); memset(rin, 0, sizeof rin);
            if (sh[3]) for (s = 0; s < 32; s++) for (b = 0; b < 32; b++) { rin[s * 64 + b].re = (float)(rb_[(s * 32 + b) * 2] / (32768.0 * 1.4142135623730951)); rin[s * 64 + b].im = (float)(rb_[(s * 32 + b) * 2 + 1] / (32768.0 * 1.4142135623730951)); }
            for (s = 0; s < 32; s++) for (b = 0; b < 54; b++) { in[s * 64 + b].re = (float)(inb[(s * 54 + b) * 2] / (32768.0 * 1.4142135623730951)); in[s * 64 + b].im = (float)(inb[(s * 54 + b) * 2 + 1] / (32768.0 * 1.4142135623730951)); }
            CHECK(sauti_usac_mps212_apply(&d, &c, &f, (unsigned)hd[0], 32, in, sh[3] ? rin : NULL, 32, o0, o1), "phase golden set %d frame %d refused", (int)si, (int)fr);
            for (ch = 0; ch < 2; ch++) {
                double exp4[4], got[4] = { 0, 0, 0, 0 }, scale, tol = sh[5] == 2 ? 3e-3 : 3e-5;      /* GES: the smoothed energies converge slowly from the reference's different first frame */ const SautiAacSbrComplex *o = ch ? o1 : o0;
                if (fread(exp4, 8, 4, fp) != 4) { CHECK(0, "phase golden set %d frame %d sums", (int)si, (int)fr); break; }
                if (fr < (sh[5] == 2 ? 10 : 3)) continue;                                                                /* GES energies are smoothed over about 10 frames: the start-up of the reference differs until then */
                for (s = 0; s < 32; s++) for (b = 0; b < 54; b++) {
                    const double re = o[s * 64 + b].re * 32768.0 * 1.4142135623730951, im = o[s * 64 + b].im * 32768.0 * 1.4142135623730951, w = ((double)s * 131.0 + (double)b * 7.0 + 1.0) / (32.0 * 64.0);
                    got[0] += re; got[1] += im; got[2] += re * re + im * im; got[3] += (re + 2.0 * im) * w;
                }
                scale = sqrt(exp4[2] * 32.0 * 54.0) + 1e-9;
                for (i = 0; i < 4; i++) CHECK(fabs(got[i] - exp4[i]) <= (i == 2 ? tol * exp4[2] : tol * scale) + 1e-9, "phase golden set %d (phase %d, decorr %d, res %d, pred %d) frame %d channel %d sum %d: %.9g, libxaac %.9g", (int)si, (int)sh[0], (int)sh[1], (int)sh[3], (int)sh[4], (int)fr, (int)ch, (int)i, got[i], exp4[i]);
            }
        }
    }
    fclose(fp);
}

/* bsHighRateMode: several parameter sets per frame, smoothing of the upmix matrices over time.  The golden file holds, for three streams of `sauti_usac_gen --mps-hr` (stereoConfigIndex 1 and 2, residual bands 0 and 6; the first
 * frame of each has no smoothing: libxaac runs a start-up path there), the inputs of libxaac's upmix and four sums of its output of every frame, see tests/data/make_mps_hr_golden.py. */
static void test_golden_hr(void) {
    FILE *fp = fopen("tests/data/mps_hr_golden.bin", "rb");
    int32_t ns, si;
    static SautiUsacMpsDec d; static SautiUsacMpsFrame f; static float inb[32 * 54 * 2], rb_[32 * 32 * 2]; static SautiAacSbrComplex in[32 * 64], rin[32 * 64], o0[32 * 64], o1[32 * 64];
    if (!fp) { CHECK(0, "cannot read tests/data/mps_hr_golden.bin (run from the repository root)"); return; }
    { char m[4]; if (fread(m, 1, 4, fp) != 4 || memcmp(m, "MPSH", 4) || fread(&ns, 4, 1, fp) != 1) { CHECK(0, "hr golden header"); fclose(fp); return; } }
    for (si = 0; si < ns; si++) {
        int32_t sh[4], fr;
        SautiUsacMpsConfig c = cfg1;
        if (fread(sh, 4, 4, fp) != 4) { CHECK(0, "hr golden stream %d header", (int)si); break; }
        memset(&d, 0, sizeof d); c.decorr = (unsigned)sh[0]; c.residual_bands = (unsigned)sh[2]; c.residual = (unsigned)sh[3];
        for (fr = 0; fr < sh[1]; fr++) {
            int32_t hd[4], slots[9], cld[9 * 28], icc[9 * 28], st[9], sd[9 * 28], i, s, b, ch;
            if (fread(hd, 4, 4, fp) != 4 || fread(slots, 4, 9, fp) != 9 || fread(cld, 4, 9u * 28u, fp) != 9u * 28u || fread(icc, 4, 9u * 28u, fp) != 9u * 28u || fread(st, 4, 9, fp) != 9 || fread(sd, 4, 9u * 28u, fp) != 9u * 28u || fread(inb, 4, 32u * 54u * 2u, fp) != 32u * 54u * 2u || (sh[2] && fread(rb_, 4, 32u * 32u * 2u, fp) != 32u * 32u * 2u)) { CHECK(0, "hr golden stream %d frame %d", (int)si, (int)fr); break; }
            memset(&f, 0, sizeof f); f.num_sets = (unsigned)hd[1]; f.param_bands = (unsigned)hd[2]; f.ext_frame = (unsigned)hd[3];
            for (s = 0; s < 9; s++) { f.param_slot[s] = (unsigned)slots[s]; f.smooth_time[s] = (unsigned)st[s]; for (i = 0; i < 28; i++) { f.cld_idx[s][i] = cld[s * 28 + i]; f.icc_idx[s][i] = icc[s * 28 + i]; f.smooth_data[s][i] = (uint8_t)sd[s * 28 + i]; } }
            memset(in, 0, sizeof in); memset(rin, 0, sizeof rin);
            if (sh[2]) for (s = 0; s < 32; s++) for (b = 0; b < 32; b++) { rin[s * 64 + b].re = (float)(rb_[(s * 32 + b) * 2] / (32768.0 * 1.4142135623730951)); rin[s * 64 + b].im = (float)(rb_[(s * 32 + b) * 2 + 1] / (32768.0 * 1.4142135623730951)); }
            for (s = 0; s < 32; s++) for (b = 0; b < 54; b++) { in[s * 64 + b].re = (float)(inb[(s * 54 + b) * 2] / (32768.0 * 1.4142135623730951)); in[s * 64 + b].im = (float)(inb[(s * 54 + b) * 2 + 1] / (32768.0 * 1.4142135623730951)); }
            CHECK(sauti_usac_mps212_apply(&d, &c, &f, (unsigned)hd[0], 32, in, sh[2] ? rin : NULL, 32, o0, o1), "hr golden stream %d frame %d refused", (int)si, (int)fr);
            for (ch = 0; ch < 2; ch++) {
                double exp4[4], got[4] = { 0, 0, 0, 0 }, scale; const SautiAacSbrComplex *o = ch ? o1 : o0;
                if (fread(exp4, 8, 4, fp) != 4) { CHECK(0, "hr golden stream %d frame %d sums", (int)si, (int)fr); break; }
                if (fr < 3) continue;                                                                      /* the start-up of the reference (its first frame runs a pre-matrix ramp) leaves traces for two frames */
                for (s = 0; s < 32; s++) for (b = 0; b < 54; b++) {
                    const double re = o[s * 64 + b].re * 32768.0 * 1.4142135623730951, im = o[s * 64 + b].im * 32768.0 * 1.4142135623730951, w = ((double)s * 131.0 + (double)b * 7.0 + 1.0) / (32.0 * 64.0);
                    got[0] += re; got[1] += im; got[2] += re * re + im * im; got[3] += (re + 2.0 * im) * w;
                }
                scale = sqrt(exp4[2] * 32.0 * 54.0) + 1e-9;
                for (i = 0; i < 4; i++) CHECK(fabs(got[i] - exp4[i]) <= (i == 2 ? 3e-5 * exp4[2] : 3e-5 * scale) + 1e-9, "hr golden stream %d (decorr %d, res %d, kind %d) frame %d channel %d sum %d: %.9g, libxaac %.9g", (int)si, (int)sh[0], (int)sh[2], (int)sh[3], (int)fr, (int)ch, (int)i, got[i], exp4[i]);
            }
        }
    }
    fclose(fp);
}

static void test_phase(void) {
    /* a steady tone in hybrid band 10 only (QMF band 3, parameter band 8: inside the 10 bands that carry phases), the same parameters in every band, IPD index 4 (pi / 2), CLD 0 and ICC 0: the two outputs are the tone rotated by their own phases, so the phase difference
     * between them (left minus right) is the IPD; with phase coding off the outputs are in phase (ICC 0 = fully coherent, no decorrelated part) */
    static SautiUsacMpsDec da, db; static SautiUsacMpsFrame f; static SautiAacSbrComplex in[32 * 64], a0[32 * 64], a1[32 * 64], b0[32 * 64], b1[32 * 64];
    SautiUsacMpsConfig c = cfg1;
    unsigned s, fr, i;
    double pa = 0, pb = 0, ea = 0, eb = 0;
    memset(&f, 0, sizeof f); f.num_sets = 1; f.param_bands = 28; f.param_slot[0] = 31; f.phase_mode = 1;
    for (i = 0; i < 28u; i++) { f.cld_idx[0][i] = 0; f.icc_idx[0][i] = 0; f.ipd_idx[0][i] = 4; }
    c.residual = 0;
    for (fr = 0; fr < 6u; fr++) {
        for (s = 0; s < 32u; s++) { const double ph = 0.4 * (double)(fr * 32u + s); in[s * 64 + 3].re = (float)(0.01 * cos(ph)); in[s * 64 + 3].im = (float)(0.01 * sin(ph)); }
        c.phase_coding = 0; CHECK(sauti_usac_mps212_apply(&da, &c, &f, 40, 32, in, NULL, 0, a0, a1), "no phase refused");
        c.phase_coding = 1; c.bands_phase = 10; CHECK(sauti_usac_mps212_apply(&db, &c, &f, 40, 32, in, NULL, 0, b0, b1), "phase refused");
        if (fr >= 3u) for (s = 0; s < 32u; s++) {
            const double ar = a0[s * 64 + 3].re, ai = a0[s * 64 + 3].im, br = a1[s * 64 + 3].re, bi = a1[s * 64 + 3].im;                  /* without phase coding: left and right in phase */
            const double cr = b0[s * 64 + 3].re, ci = b0[s * 64 + 3].im, dr = b1[s * 64 + 3].re, di = b1[s * 64 + 3].im;
            pa += (ar * br + ai * bi); ea += sqrt((ar * ar + ai * ai) * (br * br + bi * bi));
            pb += (cr * dr + ci * di); eb += sqrt((cr * cr + ci * ci) * (dr * dr + di * di));
        }
    }
    CHECK(pa > 0.9 * ea, "without phase coding left and right are not in phase (%g of %g)", pa, ea);
    CHECK(fabs(pb) < 0.2 * eb, "an IPD of pi/2 does not put the outputs in quadrature (cos %g)", pb / (eb + 1e-30));
    {   /* bsPhaseMode = 0: the IPD data of the frame is not used, the outputs stay in phase */
        static SautiUsacMpsDec dm; double pm = 0, em = 0;
        f.phase_mode = 0; c.phase_coding = 1; c.residual = 0; c.bands_phase = 10;
        for (fr = 0; fr < 6u; fr++) {
            for (s = 0; s < 32u; s++) { const double ph = 0.4 * (double)(fr * 32u + s); in[s * 64 + 3].re = (float)(0.01 * cos(ph)); in[s * 64 + 3].im = (float)(0.01 * sin(ph)); }
            CHECK(sauti_usac_mps212_apply(&dm, &c, &f, 40, 32, in, NULL, 0, a0, a1), "phase mode 0 refused");
            if (fr >= 3u) for (s = 0; s < 32u; s++) { pm += a0[s * 64 + 3].re * a1[s * 64 + 3].re + a0[s * 64 + 3].im * a1[s * 64 + 3].im; em += sqrt(((double)a0[s * 64 + 3].re * a0[s * 64 + 3].re + (double)a0[s * 64 + 3].im * a0[s * 64 + 3].im) * ((double)a1[s * 64 + 3].re * a1[s * 64 + 3].re + (double)a1[s * 64 + 3].im * a1[s * 64 + 3].im)); }
        }
        CHECK(pm > 0.9 * em, "bsPhaseMode 0 still rotates the outputs (%g of %g)", pm, em);
        f.phase_mode = 1;
    }
    {   /* the refusal: more phase bands than parameter bands */
        c.residual = 0; c.bands_phase = 29; CHECK(!sauti_usac_mps212_apply(&da, &c, &f, 40, 32, in, NULL, 0, a0, a1), "29 phase bands accepted");
    }
}

/* phase coding with the prediction upmix (stereoConfigIndex 2): the anti-phase special case, the bands that carry no phase, bsPhaseMode 0 */
static void test_phase_pred(void) {
    static SautiUsacMpsDec da, db, dc_, dd; static SautiUsacMpsFrame f; static SautiAacSbrComplex in[32 * 64], a0[32 * 64], a1[32 * 64], b0[32 * 64], b1[32 * 64], c0[32 * 64], c1[32 * 64], e0[32 * 64], e1[32 * 64];
    SautiUsacMpsConfig c = cfg1, off;
    unsigned s, fr, i, k, same13 = 1, same_pm0 = 1;
    double err_a = 0, err_b = 0, ref = 0, special_vs_icc = 0;
    memset(&f, 0, sizeof f); f.num_sets = 1; f.param_bands = 28; f.param_slot[0] = 31; f.phase_mode = 1;
    for (i = 0; i < 28u; i++) { f.cld_idx[0][i] = 0; f.icc_idx[0][i] = 0; f.ipd_idx[0][i] = 8; }
    c.residual = 1; c.phase_coding = 1; c.bands_phase = 10; c.residual_bands = 0; off = c; off.phase_coding = 0;
    for (fr = 0; fr < 6u; fr++) {
        for (s = 0; s < 32u; s++) for (k = 0; k < 40u; k++) { in[s * 64 + k].re = 0.01f * sinf((float)(s * 3u + k * 5u + fr)); in[s * 64 + k].im = 0.01f * cosf((float)(s * 7u + k + 2u * fr)); }
        CHECK(sauti_usac_mps212_apply(&da, &c, &f, 40, 32, in, NULL, 0, a0, a1), "special case refused");
        if (fr >= 3u) {                                                                                 /* QMF band 3 = hybrid band 10, parameter band 8: gain 5/12 and its mirror image on the direct signal alone (no decorrelator) */
            for (s = 0; s < 32u; s++) { err_a += fabs(a0[s * 64 + 3].re - 0.416666667f * in[s * 64 + 3].re) + fabs(a0[s * 64 + 3].im - 0.416666667f * in[s * 64 + 3].im); err_b += fabs(a1[s * 64 + 3].re + 0.416666667f * in[s * 64 + 3].re) + fabs(a1[s * 64 + 3].im + 0.416666667f * in[s * 64 + 3].im); ref += fabs(in[s * 64 + 3].re) + fabs(in[s * 64 + 3].im); }
        }
    }
    CHECK(err_a < 1e-4 * ref && err_b < 1e-4 * ref, "anti-phase special case: left error %g, right error %g of %g", err_a, err_b, ref);
    /* the same parameters with ICC 1: no special case, the tabulated weights */
    for (i = 0; i < 28u; i++) f.icc_idx[0][i] = 1;
    for (fr = 0; fr < 6u; fr++) {
        for (s = 0; s < 32u; s++) for (k = 0; k < 40u; k++) { in[s * 64 + k].re = 0.01f * sinf((float)(s * 3u + k * 5u + fr)); in[s * 64 + k].im = 0.01f * cosf((float)(s * 7u + k + 2u * fr)); }
        CHECK(sauti_usac_mps212_apply(&db, &c, &f, 40, 32, in, NULL, 0, b0, b1), "ICC 1 refused");
        if (fr >= 3u) for (s = 0; s < 32u; s++) special_vs_icc += fabs(b0[s * 64 + 3].re - 0.416666667f * in[s * 64 + 3].re) + fabs(b0[s * 64 + 3].im - 0.416666667f * in[s * 64 + 3].im);
    }
    CHECK(special_vs_icc > 1e-2 * ref, "ICC 1 still takes the anti-phase special case (%g)", special_vs_icc);
    /* bands beyond the phase bands (QMF band 13 = parameter band 11) ignore the IPD; so does a frame with bsPhaseMode 0 */
    for (i = 0; i < 28u; i++) { f.icc_idx[0][i] = 2; f.ipd_idx[0][i] = 5; }
    for (fr = 0; fr < 6u; fr++) {
        for (s = 0; s < 32u; s++) for (k = 0; k < 40u; k++) { in[s * 64 + k].re = 0.01f * sinf((float)(s * 3u + k * 5u + fr)); in[s * 64 + k].im = 0.01f * cosf((float)(s * 7u + k + 2u * fr)); }
        f.phase_mode = 1;
        CHECK(sauti_usac_mps212_apply(&dc_, &c, &f, 40, 32, in, NULL, 0, c0, c1) && sauti_usac_mps212_apply(&dd, &off, &f, 40, 32, in, NULL, 0, e0, e1), "phase / no phase refused");
        for (s = 0; s < 32u; s++) if (c0[s * 64 + 13].re != e0[s * 64 + 13].re || c0[s * 64 + 13].im != e0[s * 64 + 13].im || c1[s * 64 + 13].re != e1[s * 64 + 13].re) same13 = 0;
        { static SautiUsacMpsDec dp, dq;
          f.phase_mode = 0; CHECK(sauti_usac_mps212_apply(&dp, &c, &f, 40, 32, in, NULL, 0, a0, a1) && sauti_usac_mps212_apply(&dq, &off, &f, 40, 32, in, NULL, 0, e0, e1), "phase mode 0 refused");
          for (s = 0; s < 32u; s++) for (k = 0; k < 40u; k++) if (a0[s * 64 + k].re != e0[s * 64 + k].re || a1[s * 64 + k].im != e1[s * 64 + k].im) same_pm0 = 0; }
    }
    CHECK(same13, "a band beyond the phase bands reacts to the IPD");
    CHECK(same_pm0, "bsPhaseMode 0 changes the prediction upmix");
}

/* TSD: in a transient slot the downmix itself, rotated by 45 degrees times the transmitted phase, takes the place of the decorrelator output from hybrid band 7 on.  A tone that stops before the slot has no decorrelator tail in
 * it, so the output of the slot with TSD minus the output without it is the rotated downmix times the decorrelator gain: the eight phases are 45 degree steps of one another. */
static void test_tsd(void) {
    static SautiUsacMpsDec d0, dp[8]; static SautiUsacMpsFrame f; static SautiAacSbrComplex in[32 * 64], o0[32 * 64], o1[32 * 64], t0[8][32 * 64], t1[8][32 * 64];
    SautiUsacMpsConfig c = cfg1;
    unsigned p, s, band, i;
    memset(&f, 0, sizeof f); f.num_sets = 1; f.param_bands = 28; f.param_slot[0] = 31;
    for (i = 0; i < 28u; i++) { f.cld_idx[0][i] = 0; f.icc_idx[0][i] = 5; }
    c.residual = 0;
    for (band = 0; band <= 5u; band += (band == 0u ? 3u : 2u)) {          /* QMF bands 0 (hybrid bands below 7: unchanged), 3 and 5 (no hybrid filter delay, so the decorrelator output of the slot is empty) */
        memset(in, 0, sizeof in);
        for (s = 0; s < 32u; s++) { in[s * 64 + band].re = (s == 5u) ? 0.01f : 0.0f; in[s * 64 + band].im = (s == 5u) ? -0.004f : 0.0f; }
        memset(&d0, 0, sizeof d0); c.temp_shape = 0;
        CHECK(sauti_usac_mps212_apply(&d0, &c, &f, 40, 32, in, NULL, 0, o0, o1), "no TSD refused");
        c.temp_shape = 3; f.tsd_enable = 1;
        for (p = 0; p < 8u; p++) {
            memset(&dp[p], 0, sizeof dp[p]); memset(f.tsd_sep, 0, sizeof f.tsd_sep); f.tsd_sep[5] = 1; f.tsd_phase[5] = (uint8_t)p;
            CHECK(sauti_usac_mps212_apply(&dp[p], &c, &f, 40, 32, in, NULL, 0, t0[p], t1[p]), "TSD refused (phase %u)", p);
        }
        {   /* D_p = left output of the TSD run minus the left output without TSD, at slot 5 */
            double dre[8], dim[8], mag = 0.0; unsigned b;
            for (p = 0; p < 8u; p++) { dre[p] = 0; dim[p] = 0; for (b = band; b <= band; b++) { dre[p] += t0[p][5 * 64 + b].re - o0[5 * 64 + b].re; dim[p] += t0[p][5 * 64 + b].im - o0[5 * 64 + b].im; } }
            mag = sqrt(dre[0] * dre[0] + dim[0] * dim[0]);
            if (band == 0u) CHECK(mag < 1e-9, "TSD changed hybrid bands below 7 (QMF band 0: %g)", mag);
            else {
                CHECK(mag > 1e-6, "TSD has no effect (QMF band %u: %g)", band, mag);
                CHECK(fabs(dre[4] + dre[0]) < 1e-5 * mag && fabs(dim[4] + dim[0]) < 1e-5 * mag, "phase 4 is not the opposite of phase 0");
                CHECK(fabs(dre[2] + dim[0]) < 1e-5 * mag && fabs(dim[2] - dre[0]) < 1e-5 * mag, "phase 2 is not phase 0 rotated by 90 degrees");
                CHECK(fabs(dre[1] - 0.70710678 * (dre[0] - dim[0])) < 1e-5 * mag && fabs(dim[1] - 0.70710678 * (dre[0] + dim[0])) < 1e-5 * mag, "phase 1 is not phase 0 rotated by 45 degrees");
                CHECK(fabs(dre[6] - dim[0]) < 1e-5 * mag && fabs(dim[6] + dre[0]) < 1e-5 * mag, "phase 6 is not phase 0 rotated by 270 degrees");
                CHECK(fabs(dre[5] + 0.70710678 * (dre[0] - dim[0])) < 1e-5 * mag && fabs(dre[3] + 0.70710678 * (dre[0] + dim[0])) < 1e-5 * mag && fabs(dre[7] - 0.70710678 * (dre[0] + dim[0])) < 1e-5 * mag, "phases 3, 5 and 7");
            }
        }
    }
}

int main(void) {
    test_tables(); test_parse(); test_ipd_flag_hold(); test_signal(); test_golden(); test_golden_res(); test_residual(); test_golden_phase(); test_golden_hr(); test_phase(); test_phase_pred(); test_tsd();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_mps: all passed\n"); return 0;
}
