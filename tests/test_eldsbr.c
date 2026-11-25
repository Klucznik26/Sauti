/* AAC-ELD with the LOW-DELAY SBR (CLDFB), single rate (core and output 48 kHz) and dual rate (core 24 kHz, 64-band synthesis, output 48 kHz), made by the FDK encoder from a synthetic signal
 * (tests/data/make_eldsbr_fixtures.sh), decoded through the engine.  The decode agrees with the FDK decoder (32-bit build, tools/sauti_fdk_raw_oracle.c): 70 dB (single) and 74 dB (dual) median over the frames of
 * the fixtures, 77 - 83 dB on a real music recording (docs/AAC_FAMILY_ASM.md SBR-131); the fingerprints in test_eldsbr_fingerprints.h were written after that (`build/test_eldsbr --dump`) and pin the decode so that a
 * regression shows up without FDK:
 *   1. the description (channels, rate, profile ELD, length);
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
#define MAXB 70
#endif
#include "test_eldsbr_fingerprints.h"
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
    CHECK(info.channels == x->channels && info.sample_rate == x->rate && strcmp(info.codec_profile, "ELD") == 0 && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s: %u ch %u Hz profile '%s'", x->name, (unsigned)info.channels, (unsigned)info.sample_rate, info.codec_profile);
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

/* the same access units in a LOAS / LATM stream (the first 120 of the fixture) must decode to exactly the PCM of the MP4 */
static void test_loas(const char *loas, const char *m4a, unsigned per_au) {
    size_t nl, nm, fl, fm; uint8_t *dl = slurp(loas, &nl), *dm = slurp(m4a, &nm); maktabasauti_decoder_t *d; maktabasauti_stream_info_t info; float *pl = NULL, *pm = NULL;
    if (!dl || !dm) { CHECK(0, "cannot read %s / %s", loas, m4a); free(dl); free(dm); return; }
    d = open_mem(dl, nl, &info);
    CHECK(d && info.channels == 2 && info.sample_rate == 48000 && strcmp(info.codec_profile, "ELD") == 0, "%s: open (%u ch %u Hz)", loas, d ? (unsigned)info.channels : 0u, d ? (unsigned)info.sample_rate : 0u);
    if (d) { pl = read_all(d, 2, 4096, &fl); maktabasauti_decoder_close(d); }
    d = open_mem(dm, nm, &info);
    if (d) { pm = read_all(d, 2, 4096, &fm); maktabasauti_decoder_close(d); }
    CHECK(pl && pm && fl == (size_t)120 * per_au && fm >= fl && memcmp(pl, pm, fl * (size_t)info.channels * sizeof(float)) == 0, "%s: %zu frames, differs from the MP4 decode", loas, fl);
    free(pl); free(pm); free(dl); free(dm);
}

/* PNS noise: the generator seed is not normative.  Default = FFmpeg's 0x1f2e3d4c, option aac.pns_seed=fdk = FDK's 0 (checked: the fixture, a mono ELD stream at 48 kbit/s with PNS bands, decodes
 * to 88 - 90 dB against FDK with the option and to 10 - 15 dB without it, while the default agrees with FFmpeg to 138 dB). */
static float *decode_opts(const char *path, const maktabasauti_option_t *opts, size_t nopts, unsigned channels, size_t *frames) {
    size_t n, total = 0, got, cap = 0; uint8_t *data = slurp(path, &n); maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; float *pcm = NULL, buf[2 * 4096];
    if (!data) return NULL;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.options = opts; p.option_count = nopts;
    if (maktabasauti_io_from_memory(data, n, NULL, &p.io) != MAKTABASAUTI_OK) { free(data); return NULL; }
    if (maktabasauti_decoder_open(&p, &d) != MAKTABASAUTI_OK) { p.io.close(p.io.user); free(data); return NULL; }
    for (;;) {
        const maktabasauti_result_t r = maktabasauti_decoder_read(d, buf, 4096, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * channels * sizeof(float)); } memcpy(pcm + total * channels, buf, got * channels * sizeof(float)); total += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    maktabasauti_decoder_close(d); free(data); *frames = total; return pcm;
}
static double rms_of(const float *p, size_t n) { double s = 0; size_t i; for (i = 0; i < n; i++) s += (double)p[i] * p[i]; return sqrt(s / (double)n) * 32768.0; }
static void test_pns_seed(void) {
    static const maktabasauti_option_t o_fdk[] = { { "aac.pns_seed", "fdk" } };
    size_t f0 = 0, f1 = 0, i; float *a = decode_opts("tests/data/eld_pns_mono_480.m4a", NULL, 0, 1, &f0), *b = decode_opts("tests/data/eld_pns_mono_480.m4a", o_fdk, 1, 1, &f1);
    double sig = 0, err = 0;
    CHECK(a && b && f0 == f1 && f0 == 288480, "pns seed: %zu / %zu frames", f0, f1);
    if (a && b && f0 == f1) {
        for (i = 0; i < f0; i++) { sig += (double)a[i] * a[i]; err += ((double)a[i] - b[i]) * ((double)a[i] - b[i]); }
        CHECK(fabs(rms_of(a, f0) - 1633.905) < 0.01 && fabs(rms_of(b, f1) - 1633.860) < 0.01, "pns seed rms %.3f / %.3f (1633.905 / 1633.860; FDK 1633.846)", rms_of(a, f0), rms_of(b, f1));
        CHECK(err > 0 && fabs(10.0 * log10(sig / err) - 15.33) < 0.2, "the two seeds differ only in the noise bands: %.2f dB (15.33)", 10.0 * log10(sig / err));
    }
    free(a); free(b);
}

/* AAC-LD at 480 lines (FDK encoder, granule 480): the low-overlap window has a 120 sample slope at 480 (FDK: LowOverlapWindow480 = SineWindow120) and the LEFT half of a window comes from the
 * previous frame's shape (the stream switches the shape in its second frame).  Against FDK: 90 dB from block 1 on (median), the whole stream 72 dB only because FDK outputs exact zeros in block 0. */
static void test_ld480(void) {
    size_t f = 0; float *p = decode_opts("tests/data/ld_480_st.m4a", NULL, 0, 2, &f);
    CHECK(p && f == 288480, "ld 480: %zu frames", f);
    if (p && f == 288480) {
        CHECK(fabs(rms_of(p, f * 2) - 829.827) < 0.01, "ld 480 rms %.3f (829.827; FDK 829.819)", rms_of(p, f * 2));
        CHECK(fabs(rms_of(p + 960, 960) - 999.853) < 0.01, "ld 480 block 1 (the shape change) rms %.3f (999.853; FDK 999.851)", rms_of(p + 960, 960));
    }
    free(p);
}

int main(int argc, char **argv) {
    static const Fx fx[] = { { "eldsbr_single", "tests/data/eldsbr_single.m4a", 2, 4096, 70, 48000, eldsbr_single_fp }, { "eldsbr_dual", "tests/data/eldsbr_dual.m4a", 2, 4096, 70, 48000, eldsbr_dual_fp },
                              { "eldsbr_v2", "tests/data/eldsbr_v2.m4a", 2, 4096, 70, 48000, eldsbr_v2_fp },
                              { "eldsbr_v2_480", "tests/data/eldsbr_v2_480.m4a", 2, 4096, 70, 48000, eldsbr_v2_480_fp },
                              { "eldsbr_v2_single", "tests/data/eldsbr_v2_single.m4a", 2, 4096, 35, 24000, eldsbr_v2_single_fp } };
    unsigned k;
    if (argc > 1 && !strcmp(argv[1], "--dump")) {
        g_dump = 1;
        printf("/* Per-block fingerprints of the ELD-SBR fixtures, written by `build/test_eldsbr --dump` after the decode agreed with the FDK decoder. Only the first MAXB blocks. */\n#define MAXB 70\n");
        for (k = 0; k < sizeof fx / sizeof fx[0]; k++) test_fixture(&fx[k]);
        return 0;
    }
    for (k = 0; k < sizeof fx / sizeof fx[0]; k++) test_fixture(&fx[k]);
    test_pns_seed();
    test_ld480();
    test_loas("tests/data/eldsbr_single.loas", "tests/data/eldsbr_single.m4a", 512);
    test_loas("tests/data/eldsbr_dual.loas", "tests/data/eldsbr_dual.m4a", 1024);
    if (g_failures) { printf("test_eldsbr: %d FAILURES\n", g_failures); return 1; }
    printf("test_eldsbr: all passed\n");
    return 0;
}
