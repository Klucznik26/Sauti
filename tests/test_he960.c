/* HE-AAC with 960-sample frames (DAB+ framing: AAC-LC 960 core + SBR, 15 SBR time slots, 1920 output samples per frame, implicit signalling in a 2-byte AudioSpecificConfig) made by the libxaac encoder from
 * synthetic signals (tests/data/make_he960_fixtures.sh), decoded through the engine.  The decode agrees with libxaac's decoder at 68 dB (mono) / 60 dB (stereo) (scripts/test_he960_vs_xaac.sh) and the mono stream
 * with FDK at 70 - 85 dB; the fingerprints in test_he960_fingerprints.h were written after that (`build/test_he960 --dump`) and pin the decode so that a regression shows up without libxaac:
 *   1. the description (channels, rate, profile HE-AAC, length);
 *   2. per-block fingerprints of every channel (sum, sum of squares, position weighted sum, second difference energy);
 *   3. reads of any size and exact seeks give the same PCM. */
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
#include "test_he960_fingerprints.h"
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
    CHECK(info.channels == x->channels && info.sample_rate == x->rate && strcmp(info.codec_profile, "HE-AAC") == 0 && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s: %u ch %u Hz profile '%s'", x->name, (unsigned)info.channels, (unsigned)info.sample_rate, info.codec_profile);
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
    static const Fx fx[] = { { "he960_mono", "tests/data/he960_mono.m4a", 1, 1920, 100, 48000, he960_mono_fp }, { "he960_stereo", "tests/data/he960_stereo.m4a", 2, 1920, 100, 48000, he960_stereo_fp } };
    unsigned k;
    if (argc > 1 && !strcmp(argv[1], "--dump")) {
        g_dump = 1;
        printf("/* Per-block fingerprints of the HE-AAC 960 fixtures, written by `build/test_he960 --dump` after the decode agreed with libxaac (scripts/test_he960_vs_xaac.sh). Only the first MAXB blocks. */\n#define MAXB 40\n");
        for (k = 0; k < 2; k++) test_fixture(&fx[k]);
        return 0;
    }
    for (k = 0; k < 2; k++) test_fixture(&fx[k]);
    if (g_failures) { printf("test_he960: %d FAILURES\n", g_failures); return 1; }
    printf("test_he960: all passed\n");
    return 0;
}
