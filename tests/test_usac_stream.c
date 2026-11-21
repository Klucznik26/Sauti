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

static uint32_t g_rng = 88172645u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
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

/* ---- LOAS / LATM: the same access units in an AudioSyncStream ------------------------------------------------------------------------------------------------------------------------------- */
static float *read_all(const uint8_t *data, size_t n, unsigned *channels, size_t chunk, size_t *frames_out, maktabasauti_result_t *last, char *container) {
    maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, NULL); float *pcm = NULL, *buf; size_t total = 0, got, cap = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames_out = 0; *last = MAKTABASAUTI_E_UNSUPPORTED_FORMAT; if (container) container[0] = 0; *channels = 0;
    if (!d) return NULL;
    *channels = info.channels; if (container) { strncpy(container, info.container_name, 15); container[15] = 0; }
    buf = (float *)malloc(chunk * info.channels * sizeof(float));
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, chunk, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * info.channels * sizeof(float)); } memcpy(pcm + total * info.channels, buf, got * info.channels * sizeof(float)); total += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    free(buf); maktabasauti_decoder_close(d); *frames_out = total; *last = r; return pcm;
}
static void test_loas_equal(const char *mp4, const char *loas, unsigned channels) {                               /* the engine decodes LOAS / LATM exactly like MP4: same PCM, any read size */
    size_t n1, n2, f1, f2, f3; uint8_t *a = slurp(mp4, &n1), *b = slurp(loas, &n2); unsigned c1, c2, c3; maktabasauti_result_t r1, r2, r3; char cn1[16], cn2[16], cn3[16]; float *p1, *p2, *p3;
    if (!a || !b) { CHECK(0, "cannot read %s / %s", mp4, loas); free(a); free(b); return; }
    p1 = read_all(a, n1, &c1, 4096, &f1, &r1, cn1); p2 = read_all(b, n2, &c2, 4096, &f2, &r2, cn2); p3 = read_all(b, n2, &c3, 333, &f3, &r3, cn3);
    CHECK(p1 && p2 && p3 && c2 == channels && c1 == channels && c3 == channels && !strcmp(cn1, "mp4") && !strcmp(cn2, "loas"), "%s: %u ch, container '%s'", loas, c2, cn2);
    CHECK(r2 == MAKTABASAUTI_EOF && f2 == f1 && f3 == f1 && f1 > 0, "%s: %zu / %zu / %zu frames, end %d", loas, f2, f3, f1, (int)r2);
    if (p1 && p2 && p3 && f1 == f2 && f1 == f3) CHECK(memcmp(p1, p2, f1 * channels * sizeof(float)) == 0 && memcmp(p1, p3, f1 * channels * sizeof(float)) == 0, "%s: PCM differs from the MP4 decode", loas);
    free(p1); free(p2); free(p3); free(a); free(b);
}
static void test_loas_hostile(const char *mp4, const char *loas, unsigned channels) {
    size_t n1, n2, f1, f, i; uint8_t *a = slurp(mp4, &n1), *b = slurp(loas, &n2), *w; unsigned c; maktabasauti_result_t r; float *ref, *p; int round;
    if (!a || !b) { CHECK(0, "cannot read %s / %s", mp4, loas); free(a); free(b); return; }
    ref = read_all(a, n1, &c, 4096, &f1, &r, NULL);
    for (i = 40; i < n2; i += 211u) {                                                                           /* every 211th truncation: what was complete decodes identically, nothing crashes */
        p = read_all(b, i, &c, 4096, &f, &r, NULL);
        CHECK(f <= f1 && (!f || (p && ref && memcmp(p, ref, f * channels * sizeof(float)) == 0)), "%s truncated at %zu: %zu frames differ from the reference", loas, i, f);
        free(p);
    }
    w = (uint8_t *)malloc(n2);
    for (round = 0; round < 60; round++) {                                                                      /* bit flips: no crash, no unbounded output */
        memcpy(w, b, n2); w[rnd() % n2] ^= (uint8_t)(1u << (rnd() & 7u)); if (round & 1) w[rnd() % n2] ^= (uint8_t)(1u << (rnd() & 7u));
        p = read_all(w, n2, &c, 4096, &f, &r, NULL); CHECK(f <= f1 + 2048u, "%s flipped: %zu frames", loas, f); free(p);
    }
    free(w); free(ref); free(a); free(b);
}
static size_t put_bits(uint8_t *buf, size_t pos, uint32_t v, unsigned n) { while (n--) { if ((v >> n) & 1u) buf[pos >> 3] |= (uint8_t)(0x80u >> (pos & 7u)); pos++; } return pos; }
static void test_loas_long_config(void) {                                                                      /* an AudioSpecificConfig longer than the engine keeps (96 bytes): refused, never copied past the buffer */
    static SautiUsacConfig c; static uint8_t el[2048], out[3 * 2048 + 8]; size_t asc_bits = 0, pos, k, o = 0, total; unsigned i; size_t n; uint8_t *data; maktabasauti_stream_info_t info; maktabasauti_decoder_t *d;
    CHECK(sauti_usac_asc_parse_at(usac_m1_asc, sizeof usac_m1_asc, 0, &c, &asc_bits) == 1 && asc_bits > 20u, "m1 asc");
    for (i = 0; i < 3u; i++) {
        memset(el, 0, sizeof el); pos = 0;
        pos = put_bits(el, pos, 0, 1);                                                                           /* useSameStreamMux = 0 */
        pos = put_bits(el, pos, 0, 1); pos = put_bits(el, pos, 1, 1); pos = put_bits(el, pos, 0, 6); pos = put_bits(el, pos, 0, 4); pos = put_bits(el, pos, 0, 3);
        for (k = 0; k + 1u < asc_bits; k++) pos = put_bits(el, pos, (usac_m1_asc[k >> 3] >> (7u - (k & 7u))) & 1u, 1);   /* the ASC up to its usacConfigExtensionPresent bit */
        pos = put_bits(el, pos, 1, 1); pos = put_bits(el, pos, 0, 2); pos = put_bits(el, pos, 0, 4);              /* extension present, one extension, type 0 */
        pos = put_bits(el, pos, 15, 4); pos = put_bits(el, pos, 235, 8);                                         /* length 250 bytes */
        for (k = 0; k < 250u; k++) pos = put_bits(el, pos, 0, 8);
        pos = put_bits(el, pos, 0, 3); pos = put_bits(el, pos, 0xff, 8); pos = put_bits(el, pos, 0, 1); pos = put_bits(el, pos, 0, 1);
        { size_t l = sizeof usac_m1_au0; pos = put_bits(el, pos, (uint32_t)l, 8); for (k = 0; k < l; k++) pos = put_bits(el, pos, usac_m1_au0[k], 8); }
        if (i == 0) { size_t bits = 0; SautiUsacConfig lc; CHECK(sauti_usac_asc_parse_at(el, (pos + 7u) / 8u, 1u + 1u + 1u + 6u + 4u + 3u, &lc, &bits) == 1 && bits > 96u * 8u, "the long configuration is a valid UsacConfig (%zu bits)", bits); }
        total = (pos + 7u) / 8u; out[o++] = 0x56u; out[o++] = (uint8_t)(0xE0u | (total >> 8)); out[o++] = (uint8_t)total; memcpy(out + o, el, total); o += total;
    }
    data = out; n = o;
    d = open_mem(data, n, &info, NULL);
    CHECK(d == NULL, "a configuration longer than the engine keeps must be refused at open");
    if (d) maktabasauti_decoder_close(d);
}
static void loas_change(const char *first, const char *second) {                                               /* a different configuration in the middle: the first part decodes, then the stream ends with an error (FORMAT_CHANGED, or UNSUPPORTED_FEATURE when the engine cannot reconfigure USAC; also when only the UsacConfig differs, not the rate or channels) */
    size_t n1, n2, f1, f; uint8_t *a = slurp(first, &n1), *b = slurp(second, &n2), *w; unsigned c; maktabasauti_result_t r; float *ref, *p;
    if (!a || !b) { CHECK(0, "cannot read %s / %s", first, second); free(a); free(b); return; }
    ref = read_all(a, n1, &c, 4096, &f1, &r, NULL);
    w = (uint8_t *)malloc(n1 + n2); memcpy(w, a, n1); memcpy(w + n1, b, n2);
    p = read_all(w, n1 + n2, &c, 4096, &f, &r, NULL);
    CHECK(f == f1 && p && ref && memcmp(p, ref, f1 * c * sizeof(float)) == 0 && (r == MAKTABASAUTI_E_FORMAT_CHANGED || r == MAKTABASAUTI_E_UNSUPPORTED_FEATURE), "%s then %s: %zu frames (first part %zu), end %d", first, second, f, f1, (int)r);
    free(p); free(ref); free(w); free(a); free(b);
}
static void test_loas_config_change(void) { loas_change("tests/data/usac_m3.loas", "tests/data/usac_s1.loas"); loas_change("tests/data/usac_m3.loas", "tests/data/usac_m1.loas"); }

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
    test_loas_equal("tests/data/usac_m3.m4a", "tests/data/usac_m3.loas", 1); test_loas_equal("tests/data/usac_s1.m4a", "tests/data/usac_s1.loas", 2); test_loas_equal("tests/data/usac_c1.m4a", "tests/data/usac_c1.loas", 2);
    test_loas_equal("tests/data/usac_e2.m4a", "tests/data/usac_e2.loas", 2); test_loas_equal("tests/data/usac_k6.m4a", "tests/data/usac_k6.loas", 6); test_loas_equal("tests/data/usac_k6.m4a", "tests/data/usac_k6_same.loas", 6);
    test_loas_long_config(); test_loas_hostile("tests/data/usac_s1.m4a", "tests/data/usac_s1.loas", 2); test_loas_hostile("tests/data/usac_k6.m4a", "tests/data/usac_k6_same.loas", 6); test_loas_config_change();
    test_chunks(); test_seek("tests/data/usac_long.m4a", 150, 1024, 48000); test_seek("tests/data/usac_elong.m4a", 100, 2048, 48000); test_refusals();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_stream: all passed\n"); return 0;
}
