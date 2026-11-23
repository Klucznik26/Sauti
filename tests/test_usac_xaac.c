/* USAC (xHE-AAC) streams made by an independent encoder (libxaac, from synthetic signals: tests/data/make_usac_xaac_fixtures.sh; usac_x1 white noise mono, x2 tone comb mono, x3 stereo, all eSBR 2:1, x4 stereo
 * without SBR) decoded through the engine.  The decode agrees with libxaac's own decoder at 73-83 dB (scripts/test_usac_vs_xaac.sh), which is what the fingerprints in test_usac_xaac_fingerprints.h were written
 * after (`build/test_usac_xaac --dump`); this test pins them so that a regression shows up without libxaac:
 *   1. the description (channels, rate, profile, length), the first access unit is much larger than the others (AudioPreRoll);
 *   2. per-block fingerprints of every channel (sum, sum of squares, position weighted sum, second difference energy);
 *   3. reads of any size, exact seeks (to the start the pre-roll primes the decoder again) and reset give the same PCM. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures, g_dump;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int near(double a, double b) { return fabs(a - b) <= 1e-5 * (1.0 + fabs(b)); }
#ifndef MAXB
#define MAXB 40
#endif
#include "test_usac_xaac_fingerprints.h"
typedef struct { const char *name, *path; unsigned channels, block; unsigned blocks, rate; const double (*fp)[2][4]; } Fx;

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *b; long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET); b = (uint8_t *)malloc((size_t)len + 1u);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)len; return b;
}
static maktabasauti_decoder_t *open_mem(const uint8_t *data, size_t n, maktabasauti_stream_info_t *info) {
    maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
    if (maktabasauti_io_from_memory(data, n, NULL, &p.io) != MAKTABASAUTI_OK) return NULL;
    if (maktabasauti_decoder_open(&p, &d) != MAKTABASAUTI_OK) { p.io.close(p.io.user); return NULL; }
    info->struct_size = sizeof *info; (void)maktabasauti_decoder_get_info(d, info);
    return d;
}
static float *read_all(maktabasauti_decoder_t *d, unsigned channels, size_t chunk, size_t *frames_out) {
    float *pcm = NULL, *buf = (float *)malloc(chunk * channels * sizeof(float)); size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, chunk, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * channels * sizeof(float)); } memcpy(pcm + total * channels, buf, got * channels * sizeof(float)); total += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    free(buf); *frames_out = total; return pcm;
}
static void fingerprint(const float *pcm, unsigned n, unsigned ch, unsigned chan, double out[4]) {
    double s1 = 0, s2 = 0, s3 = 0, s4 = 0; unsigned i;
    for (i = 0; i < n; i++) { const double v = (double)pcm[(size_t)i * ch + chan]; s1 += v; s2 += v * v; s3 += v * (double)(i + 1) / (double)n; if (i >= 2) { const double d = v - 2.0 * (double)pcm[(size_t)(i - 1) * ch + chan] + (double)pcm[(size_t)(i - 2) * ch + chan]; s4 += d * d; } }
    out[0] = s1; out[1] = s2; out[2] = s3; out[3] = s4;
}

static void test_fixture(const Fx *x) {
    size_t n, frames, frames2, got, i; unsigned b, c, k; uint8_t *data = slurp(x->path, &n); maktabasauti_stream_info_t info; maktabasauti_decoder_t *d; float *pcm, *pcm2, a[2 * 700];
    if (!data) { CHECK(0, "cannot read %s (run from the repository root)", x->path); return; }
    d = open_mem(data, n, &info);
    if (!d) { CHECK(0, "%s: open", x->name); free(data); return; }
    CHECK(info.channels == x->channels && info.sample_rate == x->rate && strcmp(info.codec_profile, "USAC") == 0 && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s: %u ch %u Hz profile '%s'", x->name, (unsigned)info.channels, (unsigned)info.sample_rate, info.codec_profile);
    pcm = read_all(d, x->channels, 4096, &frames);
    CHECK(pcm && frames >= (size_t)x->blocks * x->block && frames < (size_t)(x->blocks + 1u) * x->block + 1u, "%s: %zu frames (expected %u blocks of %u)", x->name, frames, x->blocks, x->block);
    if (pcm) {
        if (g_dump) {
            printf("static const double %s_fp[MAXB][2][4] = {\n", x->name);
            for (b = 0; b < x->blocks && b < MAXB; b++) {
                double f[2][4]; memset(f, 0, sizeof f);
                for (c = 0; c < x->channels; c++) fingerprint(pcm + (size_t)b * x->block * x->channels, x->block, x->channels, c, f[c]);
                printf("    { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} },\n", f[0][0], f[0][1], f[0][2], f[0][3], f[1][0], f[1][1], f[1][2], f[1][3]);
            }
            printf("};\n");
        } else {
            for (b = 0; b < x->blocks && b < MAXB; b++) {
                double f[4];
                for (c = 0; c < x->channels; c++) {
                    fingerprint(pcm + (size_t)b * x->block * x->channels, x->block, x->channels, c, f);
                    for (k = 0; k < 4; k++) CHECK(near(f[k], x->fp[b][c][k]), "%s block %u ch %u fingerprint %u: %.12e vs %.12e", x->name, b, c, k, f[k], x->fp[b][c][k]);
                }
            }
        }
        for (i = 0; i < frames * x->channels; i++) if (!isfinite(pcm[i])) { CHECK(0, "%s: non-finite sample", x->name); break; }
        pcm2 = NULL;
        if (!g_dump) {
            maktabasauti_decoder_close(d); d = open_mem(data, n, &info);
            pcm2 = d ? read_all(d, x->channels, 333, &frames2) : NULL;
            CHECK(pcm2 && frames2 == frames && memcmp(pcm, pcm2, frames * x->channels * sizeof(float)) == 0, "%s: reads of another size give other PCM", x->name);
            if (d && pcm) {                                                                              /* exact seeks: the start primes the decoder again from the pre-roll, mid-stream blocks come from checkpoints or the start */
                const uint64_t t[] = { 0, 3u * x->block + 17u, x->block, (20u + 1u) * x->block + 600u <= frames ? 20u * x->block : 9u * x->block, 2u * x->block - 1u, 0 };
                for (i = 0; i < sizeof t / sizeof t[0]; i++) {
                    const size_t want = t[i] + 600u <= frames ? 600u : (size_t)(frames - t[i]);
                    CHECK(maktabasauti_decoder_seek(d, t[i], MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "%s: seek %llu", x->name, (unsigned long long)t[i]);
                    (void)maktabasauti_decoder_read(d, a, want, MAKTABASAUTI_FORMAT_F32, &got);
                    CHECK(got == want && memcmp(a, pcm + t[i] * x->channels, want * x->channels * sizeof(float)) == 0, "%s: seek to %llu differs from sequential decoding", x->name, (unsigned long long)t[i]);
                }
            }
            free(pcm2);
        }
        free(pcm);
    }
    if (d) maktabasauti_decoder_close(d);
    free(data);
}

int main(int argc, char **argv) {
    const Fx fx[] = { { "x1", "tests/data/usac_x1.m4a", 1, 2048, 34, 48000, x1_fp }, { "x2", "tests/data/usac_x2.m4a", 1, 2048, 34, 48000, x2_fp }, { "x3", "tests/data/usac_x3.m4a", 2, 2048, 34, 48000, x3_fp }, { "x4", "tests/data/usac_x4.m4a", 2, 1024, 69, 48000, x4_fp },
                      { "x5", "tests/data/usac_x5.m4a", 2, 2048, NB5, 48000, x5_fp }, { "x6", "tests/data/usac_x6.m4a", 2, 2048, NB6, 44100, x6_fp }, { "x7", "tests/data/usac_x7.m4a", 1, 2048, NB7, 32000, x7_fp },
                      { "l1", "tests/data/usac_l1.m4a", 1, 1024, NBL1, 16000, l1_fp }, { "l2", "tests/data/usac_l2.m4a", 1, 1024, NBL2, 24000, l2_fp }, { "l3", "tests/data/usac_l3.m4a", 1, 1024, NBL3, 16000, l3_fp },
                      { "l4", "tests/data/usac_l4.m4a", 1, 1024, NBL4, 24000, l4_fp }, { "l5", "tests/data/usac_l5.m4a", 1, 1024, NBL5, 24000, l5_fp }, { "l6", "tests/data/usac_l6.m4a", 1, 1024, NBL6, 24000, l6_fp },
                      { "l7", "tests/data/usac_l7.m4a", 2, 1024, NBL7, 24000, l7_fp },
                      { "x8", "tests/data/usac_x8.m4a", 1, 4096, NB8, 48000, x8_fp }, { "x9", "tests/data/usac_x9.m4a", 2, 4096, NB9, 48000, x9_fp },
                      { "t1", "tests/data/usac_t1.m4a", 1, 2048, NBT1, 48000, t1_fp },
                      { "p1", "tests/data/usac_p1.m4a", 1, 2048, NBP, 24000, p1_fp }, { "p2", "tests/data/usac_p2.m4a", 1, 2048, NBP, 24000, p2_fp }, { "p3", "tests/data/usac_p3.m4a", 1, 2048, NBP, 24000, p3_fp },
                      { "v1", "tests/data/usac_v1.m4a", 1, 2048, NBV1, 48000, v1_fp }, { "v2", "tests/data/usac_v2.m4a", 1, 2048, NBV2, 48000, v2_fp }, { "v3", "tests/data/usac_v3.m4a", 1, 2048, NBV1, 48000, v3_fp },
                      { "h1", "tests/data/usac_h1.m4a", 1, 2048, NBH, 24000, h1_fp }, { "h2", "tests/data/usac_h2.m4a", 1, 2048, NBH2, 16000, h2_fp }, { "h3", "tests/data/usac_h3.m4a", 2, 2048, NBH, 44100, h3_fp },
                      { "h4", "tests/data/usac_h4.m4a", 1, 2048, NBH, 24000, h4_fp },
                      { "h5", "tests/data/usac_h5.m4a", 1, 4096, NBH, 48000, h5_fp }, { "h6", "tests/data/usac_h6.m4a", 2, 4096, NBH, 44100, h6_fp },
                      { "w1", "tests/data/usac_w1.m4a", 1, 2048, NBW1, 48000, w1_fp }, { "w2", "tests/data/usac_w2.m4a", 1, 2048, NBW1, 48000, w2_fp }, { "w3", "tests/data/usac_w3.m4a", 2, 2048, NBW3, 48000, w3_fp },
                      { "z1", "tests/data/usac_z1.m4a", 2, 2048, NBZ, 48000, z1_fp }, { "z2", "tests/data/usac_z2.m4a", 2, 2048, NBZ, 44100, z2_fp }, { "z3", "tests/data/usac_z3.m4a", 2, 2048, NBZ, 32000, z3_fp },
                      { "z4", "tests/data/usac_z4.m4a", 2, 2048, NBZ, 48000, z4_fp }, { "y1", "tests/data/usac_y1.m4a", 2, 2048, NBZ, 48000, y1_fp }, { "y2", "tests/data/usac_y2.m4a", 2, 2048, NBZ, 48000, y2_fp }, { "y3", "tests/data/usac_y3.m4a", 2, 2048, NBZ, 48000, y3_fp },
                      { "y4", "tests/data/usac_y4.m4a", 2, 2048, NBZ, 48000, y4_fp }, { "y5", "tests/data/usac_y5.m4a", 2, 2048, NBZ, 48000, y5_fp }, { "y6", "tests/data/usac_y6.m4a", 2, 2048, NBZ, 48000, y6_fp },
                      { "y7", "tests/data/usac_y7.m4a", 2, 2048, NBZ, 48000, y7_fp }, { "y8", "tests/data/usac_y8.m4a", 2, 2048, NBZ, 48000, y8_fp }, { "y9", "tests/data/usac_y9.m4a", 2, 2048, NBZ, 48000, y9_fp },
                      { "y10", "tests/data/usac_y10.m4a", 2, 2048, NBZ, 48000, y10_fp }, { "y11", "tests/data/usac_y11.m4a", 2, 2048, NBZ, 48000, y11_fp },
                      { "y12", "tests/data/usac_y12.m4a", 2, 2048, NBG, 48000, y12_fp }, { "y13", "tests/data/usac_y13.m4a", 2, 2048, NBG, 48000, y13_fp },
                      { "y14", "tests/data/usac_y14.m4a", 2, 2048, NBZ, 48000, y14_fp }, { "y15", "tests/data/usac_y15.m4a", 2, 2048, NBZ, 48000, y15_fp },
                      { "y16", "tests/data/usac_y16.m4a", 2, 2048, NBZ, 48000, y16_fp },
                      { "z5", "tests/data/usac_z5.m4a", 2, 4096, NBZ, 48000, z5_fp }, { "z6", "tests/data/usac_z6.m4a", 2, 4096, NBZ, 48000, z6_fp },
                      { "z7", "tests/data/usac_z7.m4a", 2, 2048, NBZ, 48000, z7_fp }, { "z8", "tests/data/usac_z8.m4a", 2, 2048, NBZ, 48000, z8_fp },
                      { "z9", "tests/data/usac_z9.m4a", 2, 2048, NBZ, 48000, z9_fp },
                      { "y17", "tests/data/usac_y17.m4a", 2, 2048, NBZ, 48000, y17_fp }, { "y18", "tests/data/usac_y18.m4a", 2, 2048, NBZ, 48000, y18_fp }, { "y19", "tests/data/usac_y19.m4a", 2, 2048, NBZ, 48000, y19_fp },
                      { "y20", "tests/data/usac_y20.m4a", 2, 2048, NBZ, 48000, y20_fp }, { "y21", "tests/data/usac_y21.m4a", 2, 2048, NBZ, 48000, y21_fp },
                      { "z10", "tests/data/usac_z10.m4a", 2, 2048, NBG, 48000, z10_fp },
                      { "z11", "tests/data/usac_z11.m4a", 2, 1024, NBG, 24000, z11_fp },
                      { "z12", "tests/data/usac_z12.m4a", 2, 2048, NBZ, 48000, z12_fp },
                      { "z13", "tests/data/usac_z13.m4a", 2, 2048, NBG, 48000, z13_fp },
                      { "z14", "tests/data/usac_z14.m4a", 2, 2048, NBG, 48000, z14_fp },
                      { "r1", "tests/data/usac_r1.m4a", 2, 2048, 60, 48000, r1_fp }, { "r2", "tests/data/usac_r2.m4a", 2, 2048, 30, 48000, r2_fp },
                      { "r3", "tests/data/usac_r3.m4a", 2, 2048, 36, 48000, r3_fp }, { "r4", "tests/data/usac_r4.m4a", 2, 2048, 60, 48000, r4_fp },
                      { "r5", "tests/data/usac_r5.m4a", 2, 2048, 30, 48000, r5_fp } };
    unsigned k;
    if (argc > 1 && !strcmp(argv[1], "--dump")) {                                                       /* build/test_usac_xaac --dump > tests/test_usac_xaac_fingerprints.h */
        g_dump = 1;
        printf("/* Per-block fingerprints (sum, sum of squares, position weighted sum, second difference energy) of the decoded PCM of the libxaac-encoded fixtures, written by `build/test_usac_xaac --dump` AFTER the decode agreed\n * with libxaac's decoder (scripts/test_usac_vs_xaac.sh: 73-79 dB). Only the first MAXB blocks of each stream. */\n#define MAXB 40\n");
        for (k = 0; k < 72; k++) test_fixture(&fx[k]);
        return g_failures ? 1 : 0;
    }
    for (k = 0; k < 72; k++) test_fixture(&fx[k]);
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_xaac: all passed\n"); return 0;
}
