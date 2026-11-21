/* USAC (xHE-AAC) in MP4 through the maktabasauti engine (maktabasauti_aac_mp4.c, maktabasauti_aac_engine.c): the container hands the access units to sauti_usac, the engine adds the
 * sample-accurate seeking (checkpoints every 64 blocks that carry the arithmetic context, the noise generator, the window overlap and shape of every channel), reset, flush and the
 * gapless handling.  The PCM must be bit for bit what sauti_usac_decode gives for the same access units.
 *   1. stream description and exact equality with the direct decode (mono, stereo, independent windows, noise filling, TNS);
 *   2. reads of any size give the same samples;
 *   3. exact seeks over a 150-block stream (checkpoints) and reset;
 *   4. a frame the decoder cannot decode (LPD) is refused at open; truncated files never crash and return what was complete. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "sauti_usac.h"
#include "test_usac_vectors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

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
static float *decode_all(const uint8_t *data, size_t n, unsigned channels, size_t chunk, size_t *frames_out, const char *name) {
    maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, NULL); float *pcm = NULL, *buf; size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    *frames_out = 0;
    if (!d) { CHECK(0, "%s: open", name); return NULL; }
    CHECK(info.channels == channels && info.sample_rate == 48000u && strcmp(info.codec_profile, "USAC") == 0 && strcmp(info.container_name, "mp4") == 0, "%s: %u ch %u Hz profile '%s' container '%s'", name, (unsigned)info.channels, (unsigned)info.sample_rate, info.codec_profile, info.container_name);
    buf = (float *)malloc(chunk * channels * sizeof(float));
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, chunk, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * channels * sizeof(float)); } memcpy(pcm + total * channels, buf, got * channels * sizeof(float)); total += got; }
        if (r == MAKTABASAUTI_EOF) break;
        if (r != MAKTABASAUTI_OK) { CHECK(0, "%s: read -> %d", name, (int)r); break; }
    }
    free(buf); maktabasauti_decoder_close(d); *frames_out = total; return pcm;
}

static void test_equal(const char *path, unsigned channels, unsigned flen, const uint8_t *asc, size_t asc_n, const uint8_t *const *au, const size_t *au_n, size_t frames) {
    static SautiUsacConfig cfg; static SautiUsacState st; static float one[2 * 2048];
    size_t n, got, f; uint8_t *data = slurp(path, &n); float *pcm; SautiAacReason why; maktabasauti_stream_info_t info; maktabasauti_decoder_t *d;
    if (!data) { CHECK(0, "cannot read %s (run from the repository root)", path); return; }
    pcm = decode_all(data, n, channels, 4096, &got, path);
    CHECK(got == frames * flen, "%s: %zu frames, expected %zu", path, got, frames * flen);
    CHECK(sauti_usac_asc_parse(asc, asc_n, &cfg) == 1, "%s: asc", path);
    sauti_usac_state_init(&st, &cfg);
    for (f = 0; pcm && f < frames && f * flen + flen <= got; f++) {
        CHECK(sauti_usac_decode(&cfg, &st, au[f], au_n[f], one, &why) == 1, "%s frame %zu", path, f);
        CHECK(memcmp(one, pcm + f * flen * channels, flen * channels * sizeof(float)) == 0, "%s: block %zu differs from the direct decode", path, f);
    }
    d = open_mem(data, n, &info, NULL);                                                                     /* the description */
    if (d) { CHECK((info.flags & MAKTABASAUTI_STREAM_SEEKABLE) && info.total_frames == frames * flen && info.native_format == MAKTABASAUTI_FORMAT_F32, "%s: flags %u total %llu", path, (unsigned)info.flags, (unsigned long long)info.total_frames); maktabasauti_decoder_close(d); }
    free(pcm); free(data);
}
static void test_chunks(void) {
    size_t n, a_n, b_n; uint8_t *data = slurp("tests/data/usac_m3.m4a", &n); float *a, *b;
    if (!data) { CHECK(0, "cannot read usac_m3.m4a"); return; }
    a = decode_all(data, n, 1, 4096, &a_n, "chunk 4096"); b = decode_all(data, n, 1, 1, &b_n, "1-sample reads");
    CHECK(a && b && a_n == b_n && memcmp(a, b, a_n * sizeof(float)) == 0, "reads of 1 sample and of 4096 samples differ");
    free(a); free(b); b = decode_all(data, n, 1, 777, &b_n, "chunk 777");
    a = decode_all(data, n, 1, 1024, &a_n, "chunk 1024");
    CHECK(a && b && a_n == b_n && memcmp(a, b, a_n * sizeof(float)) == 0, "reads of 777 and 1024 samples differ");
    free(a); free(b); free(data);
}
static void test_seek(const char *path, size_t blocks, size_t flen, unsigned rate) {                          /* checkpoints every 64 blocks */
    size_t n, frames, got, i; uint8_t *data = slurp(path, &n); maktabasauti_stream_info_t info; maktabasauti_decoder_t *d; float *ref, a[600];
    const uint64_t b64 = 64u * flen;
    const uint64_t targets[] = { b64 + 2000u, b64, b64 - 1u, 100000u % (blocks * flen), 1000, 0, (blocks - 1u) * flen - 3000u, b64 + 2000u, 2u * b64 > blocks * flen ? 1u : 2u * b64, 40000, blocks * flen - 700u };
    if (!data) { CHECK(0, "cannot read %s", path); return; }
    ref = decode_all(data, n, 1, 4096, &frames, "seek reference");
    CHECK(frames == blocks * flen && rate == 48000u, "%s: %zu frames", path, frames);
    d = open_mem(data, n, &info, NULL);
    if (d && ref) {
        for (i = 0; i < sizeof targets / sizeof targets[0]; i++) {
            const size_t want = targets[i] + 500u <= frames ? 500u : (size_t)(frames - targets[i]);
            maktabasauti_result_t r = maktabasauti_decoder_seek(d, targets[i], MAKTABASAUTI_SEEK_EXACT, NULL);
            CHECK(r == MAKTABASAUTI_OK, "seek %llu -> %d", (unsigned long long)targets[i], (int)r);
            r = maktabasauti_decoder_read(d, a, want, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == want && memcmp(a, ref + targets[i], want * sizeof(float)) == 0, "%s: seek to %llu differs from sequential decoding", path, (unsigned long long)targets[i]);
        }
        CHECK(maktabasauti_decoder_seek(d, frames + 1u, MAKTABASAUTI_SEEK_EXACT, NULL) != MAKTABASAUTI_OK, "a seek past the end must fail");
        CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK && maktabasauti_decoder_read(d, a, 500, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 500u && memcmp(a, ref, 500 * sizeof(float)) == 0, "reset must start over");
    } else CHECK(0, "seek test setup");
    if (d) maktabasauti_decoder_close(d);
    free(ref); free(data);
}
static void test_refusals(void) {
    size_t n, i, at = (size_t)-1; uint8_t *data = slurp("tests/data/usac_m1.m4a", &n), *copy; int r;
    if (!data) { CHECK(0, "cannot read usac_m1.m4a"); return; }
    for (i = 0; i + sizeof usac_m1_au0 <= n; i++) if (memcmp(data + i, usac_m1_au0, 16) == 0) { at = i; break; }
    CHECK(at != (size_t)-1, "the first access unit is in the file");
    if (at != (size_t)-1) {
        copy = (uint8_t *)malloc(n); memcpy(copy, data, n);
        copy[at] ^= 0x40u;                                                                                   /* core_mode of the first channel = 1: an LPD frame, not decodable */
        r = 0; { maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(copy, n, &info, &r); if (d) maktabasauti_decoder_close(d); }
        CHECK(r == (int)MAKTABASAUTI_E_UNSUPPORTED_FEATURE || r == (int)MAKTABASAUTI_E_UNSUPPORTED_FORMAT, "an LPD first frame must be refused at open (%d)", r);
        free(copy);
    }
    for (i = 0; i < 60; i++) {                                                                               /* truncated files: no crash, whatever was complete is decoded */
        const size_t cut = (n * i) / 60u; size_t got = 0; float *pcm = NULL; int rr = 0; maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, cut, &info, &rr);
        if (d) {
            maktabasauti_result_t r2; float buf[1024]; size_t g;
            for (;;) { r2 = maktabasauti_decoder_read(d, buf, 1024, MAKTABASAUTI_FORMAT_F32, &g); got += g; if (r2 != MAKTABASAUTI_OK) break; }
            CHECK(r2 == MAKTABASAUTI_EOF || r2 == MAKTABASAUTI_E_TRUNCATED || r2 == MAKTABASAUTI_E_CORRUPT_DATA, "truncated file (%zu bytes): result %d", cut, (int)r2);
            CHECK(got <= 12u * 1024u, "truncated file: %zu frames", got);
            maktabasauti_decoder_close(d);
        }
        (void)pcm;
    }
    free(data);
}

int main(void) {
    test_equal("tests/data/usac_m1.m4a", 1, 1024, usac_m1_asc, sizeof usac_m1_asc, usac_m1_aus, usac_m1_n, sizeof usac_m1_n / sizeof usac_m1_n[0]);
    test_equal("tests/data/usac_m2.m4a", 1, 1024, usac_m2_asc, sizeof usac_m2_asc, usac_m2_aus, usac_m2_n, sizeof usac_m2_n / sizeof usac_m2_n[0]);
    test_equal("tests/data/usac_m3.m4a", 1, 1024, usac_m3_asc, sizeof usac_m3_asc, usac_m3_aus, usac_m3_n, sizeof usac_m3_n / sizeof usac_m3_n[0]);
    test_equal("tests/data/usac_s1.m4a", 2, 1024, usac_s1_asc, sizeof usac_s1_asc, usac_s1_aus, usac_s1_n, sizeof usac_s1_n / sizeof usac_s1_n[0]);
    test_equal("tests/data/usac_s2.m4a", 2, 1024, usac_s2_asc, sizeof usac_s2_asc, usac_s2_aus, usac_s2_n, sizeof usac_s2_n / sizeof usac_s2_n[0]);
    test_equal("tests/data/usac_s3.m4a", 2, 1024, usac_s3_asc, sizeof usac_s3_asc, usac_s3_aus, usac_s3_n, sizeof usac_s3_n / sizeof usac_s3_n[0]);
    test_equal("tests/data/usac_c1.m4a", 2, 1024, usac_c1_asc, sizeof usac_c1_asc, usac_c1_aus, usac_c1_n, sizeof usac_c1_n / sizeof usac_c1_n[0]);
    test_equal("tests/data/usac_e1.m4a", 1, 2048, usac_e1_asc, sizeof usac_e1_asc, usac_e1_aus, usac_e1_n, sizeof usac_e1_n / sizeof usac_e1_n[0]);
    test_equal("tests/data/usac_e2.m4a", 2, 2048, usac_e2_asc, sizeof usac_e2_asc, usac_e2_aus, usac_e2_n, sizeof usac_e2_n / sizeof usac_e2_n[0]);
    test_equal("tests/data/usac_e4.m4a", 2, 2048, usac_e4_asc, sizeof usac_e4_asc, usac_e4_aus, usac_e4_n, sizeof usac_e4_n / sizeof usac_e4_n[0]);
    test_chunks(); test_seek("tests/data/usac_long.m4a", 150, 1024, 48000); test_seek("tests/data/usac_elong.m4a", 100, 2048, 48000); test_refusals();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_stream: all passed\n"); return 0;
}
