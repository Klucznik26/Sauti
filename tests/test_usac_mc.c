/* USAC (xHE-AAC) with more than two channels: channel configurations 5, 6 and 7 (5.0, 5.1 and 7.1; SCE, CPE and LFE elements, up to 8 channels, output in the WAVE / FFmpeg order).
 *   1. the elements of each configuration;
 *   2. per-channel fingerprints of the decoded PCM, written by `build/test_usac_mc --dump` AFTER the decoder agreed with the references: usac_k5 with FFmpeg (139 dB on every channel) and
 *      every element of usac_k5 / k6 / k7 with FDK AAC as a standalone mono / stereo stream (scripts/test_usac_vs_ffmpeg.sh);
 *   3. atomicity: a truncated or damaged access unit leaves the state and the output alone, a successful decode matches the state the same frame gives on a copy;
 *   4. the engine: stream description, PCM bit for bit equal to the direct decode, reads of any size, exact seeks over a checkpoint (70 blocks of 6 channels) and reset;
 *   5. what is refused: configurations whose output order is not mapped, elements that do not add up. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "sauti_usac.h"
#include "test_usac_mc_vectors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures, g_dump;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int near(double a, double b) { return fabs(a - b) <= 1e-5 * (1.0 + fabs(b)); }

typedef struct { const char *name; const uint8_t *asc; size_t asc_n; const uint8_t *const *au; const size_t *au_n; size_t frames; unsigned config, channels, elements; const unsigned *types; const double (*fp)[8][4]; } Stream;
#define ASC_BITS 19u                                                          /* object type (11 bits: escape), sampling index (4), channel configuration (4) */
#include "test_usac_mc_fingerprints.h"

static const unsigned types5[] = { SAUTI_USAC_SCE, SAUTI_USAC_CPE, SAUTI_USAC_CPE };
static const unsigned types6[] = { SAUTI_USAC_SCE, SAUTI_USAC_CPE, SAUTI_USAC_CPE, SAUTI_USAC_LFE };
static const unsigned types7[] = { SAUTI_USAC_SCE, SAUTI_USAC_CPE, SAUTI_USAC_CPE, SAUTI_USAC_CPE, SAUTI_USAC_LFE };

static void fingerprint(const float *pcm, unsigned n, unsigned ch, unsigned chan, double out[4]) {
    double s1 = 0, s2 = 0, s3 = 0, s4 = 0; unsigned i;
    for (i = 0; i < n; i++) { const double v = (double)pcm[(size_t)i * ch + chan]; s1 += v; s2 += v * v; s3 += v * (double)(i + 1) / (double)n; if (i >= 2) { const double d = v - 2.0 * (double)pcm[(size_t)(i - 1) * ch + chan] + (double)pcm[(size_t)(i - 2) * ch + chan]; s4 += d * d; } }
    out[0] = s1; out[1] = s2; out[2] = s3; out[3] = s4;
}

static void test_config(const Stream *s) {
    SautiUsacConfig c; unsigned i;
    const int r = sauti_usac_config_parse(s->asc, s->asc_n, ASC_BITS, &c);
    CHECK(r == 1, "%s: config parse %d", s->name, r);
    if (r != 1) return;
    CHECK(c.channel_config == s->config && c.out_channels == s->channels && c.num_elements == s->elements && c.sample_rate == 48000u && c.out_frame_len == 1024u && c.sbr_mode == 0u, "%s: config %u channels %u elements %u", s->name, c.channel_config, c.out_channels, c.num_elements);
    for (i = 0; i < s->elements && i < c.num_elements; i++) CHECK(c.e[i].type == s->types[i], "%s: element %u is type %u", s->name, i, c.e[i].type);
}
static void test_config_edges(void) {
    SautiUsacConfig c; uint8_t b[64]; size_t n;
    #define PUT(v, w) do { for (int k_ = (int)(w) - 1; k_ >= 0; k_--) { if (((v) >> k_) & 1u) b[n >> 3] |= (uint8_t)(0x80u >> (n & 7u)); n++; } } while (0)
    #define NEW() do { memset(b, 0, sizeof b); n = 0; } while (0)
    /* configuration 6 with the elements of configuration 5: the channel count does not add up */
    NEW(); PUT(3u, 5); PUT(1u, 3); PUT(6u, 5); PUT(2u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(1u, 2); PUT(0u, 1); PUT(0u, 1); PUT(1u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 0, "elements and configuration 6 disagree");
    /* configuration 6 written out by hand: SCE, CPE, CPE, LFE */
    NEW(); PUT(3u, 5); PUT(1u, 3); PUT(6u, 5); PUT(3u, 4); PUT(0u, 2); PUT(0u, 1); PUT(1u, 1); PUT(1u, 2); PUT(0u, 1); PUT(1u, 1); PUT(1u, 2); PUT(0u, 1); PUT(1u, 1); PUT(2u, 2); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 1 && c.out_channels == 6u && c.e[3].type == SAUTI_USAC_LFE && c.e[0].noise_fill == 1u && c.e[3].noise_fill == 0u, "hand-made 5.1 configuration");
    /* configuration 11 (6.1) is valid but its output order is not mapped: -1, and the decoder refuses it */
    NEW(); PUT(3u, 5); PUT(1u, 3); PUT(11u, 5); PUT(4u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(1u, 2); PUT(0u, 1); PUT(0u, 1); PUT(1u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(2u, 2); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == -1 && c.channel_config == 11u, "configuration 11 is not mapped");
    { static SautiUsacState st; float pcm[8 * 1024]; SautiAacReason why; uint8_t au[8] = { 0 };
      sauti_usac_state_init(&st, &c);
      CHECK(sauti_usac_decode(&c, &st, au, sizeof au, pcm, &why) == 0, "the decoder does not decode a configuration whose output order is not mapped"); }
    /* a configuration the decoder cannot map, with a REAL 5.1 access unit: refused before any element is read, state and output untouched */
    { static SautiUsacConfig k6; static SautiUsacState st, before; float pcm[8 * 1024], keep[8 * 1024]; SautiAacReason why; size_t i;
      CHECK(sauti_usac_asc_parse(usac_k6_asc, sizeof usac_k6_asc, &k6) == 1, "k6 config");
      k6.channel_config = 11u; sauti_usac_state_init(&st, &k6);
      for (i = 0; i < 8 * 1024; i++) keep[i] = pcm[i] = 99.0f;
      before = st;
      CHECK(sauti_usac_decode(&k6, &st, usac_k6_au0, sizeof usac_k6_au0, pcm, &why) == 0 && memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "configuration 11 with a real access unit is refused and leaves everything alone"); }
    /* an LFE together with eSBR: the LFE has no eSBR data (the reference upsamples it through the QMF), not implemented: unsupported, not decoded wrongly */
    NEW(); PUT(3u, 5); PUT(3u, 3); PUT(1u, 5); PUT(0u, 4); PUT(2u, 2); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 1 && c.sbr_mode == 1u && c.e[0].type == SAUTI_USAC_LFE, "an LFE configuration with eSBR parses");
    { static SautiUsacState st; float pcm[8 * 2048]; SautiAacReason why; uint8_t au[16]; memset(au, 0x55, sizeof au); au[0] = 0x80u;
      sauti_usac_state_init(&st, &c);
      CHECK(sauti_usac_decode(&c, &st, au, sizeof au, pcm, &why) == -1, "an LFE with eSBR is unsupported"); }
    #undef PUT
    #undef NEW
}

static void test_vectors(const Stream *s) {
    static SautiUsacConfig cfg; static SautiUsacState st; static float pcm[8 * 2048]; double fp[64][8][4]; size_t f; unsigned c, k;
    SautiAacReason why;
    memset(fp, 0, sizeof fp);
    CHECK(sauti_usac_config_parse(s->asc, s->asc_n, ASC_BITS, &cfg) == 1, "%s: config", s->name);
    sauti_usac_state_init(&st, &cfg);
    for (f = 0; f < s->frames; f++) {
        if (sauti_usac_decode(&cfg, &st, s->au[f], s->au_n[f], pcm, &why) != 1) { CHECK(0, "%s frame %zu: reason %d", s->name, f, (int)why); return; }
        for (c = 0; c < s->channels; c++) fingerprint(pcm, 1024u, s->channels, c, fp[f][c]);
        for (c = 0; c < s->channels * 1024u; c++) CHECK(isfinite(pcm[c]), "%s: non-finite output", s->name);
        if (g_dump) {
            printf("    {\n");
            for (c = 0; c < 8; c++) printf("        {%.12e, %.12e, %.12e, %.12e},\n", fp[f][c][0], fp[f][c][1], fp[f][c][2], fp[f][c][3]);
            printf("    },\n"); continue;
        }
        for (c = 0; c < s->channels; c++) for (k = 0; k < 4; k++) CHECK(near(fp[f][c][k], s->fp[f][c][k]), "%s frame %zu ch %u fingerprint %u: %.12e vs %.12e", s->name, f, c, k, fp[f][c][k], s->fp[f][c][k]);
    }
}

static void test_atomic(const Stream *s) {
    static SautiUsacConfig cfg; static SautiUsacState st, before, adv; static float pcm[8 * 1024], keep[8 * 1024]; SautiAacReason why; size_t f, len, i; int round; long accepted = 0, rejected = 0;
    CHECK(sauti_usac_config_parse(s->asc, s->asc_n, ASC_BITS, &cfg) == 1, "config");
    sauti_usac_state_init(&st, &cfg);
    for (i = 0; i < 8 * 1024; i++) keep[i] = pcm[i] = 12345.0f;
    for (f = 0; f < 3 && f < s->frames; f++) {
        for (len = 0; len <= s->au_n[f]; len += (len < 64u || s->au_n[f] - len < 64u) ? 1u : 7u) {                      /* truncations: every length near both ends, every 7th between */
            int r; before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_usac_decode(&cfg, &st, s->au[f], len, pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu len %zu: failure (%d) must leave state and output alone", s->name, f, len, r); rejected++; }
            else { for (i = 0; i < s->channels * 1024u; i++) CHECK(isfinite(pcm[i]), "non-finite output"); st = before; accepted++; }
        }
        for (round = 0; round < 40; round++) {                                                                       /* random bit flips */
            uint8_t b[8192]; int r, nflip = 1 + (int)(rnd() % 4u);
            memcpy(b, s->au[f], s->au_n[f]);
            while (nflip--) { const size_t at = rnd() % s->au_n[f]; b[at] ^= (uint8_t)(1u << (rnd() & 7u)); }
            before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_usac_decode(&cfg, &st, b, s->au_n[f], pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu flip: failure (%d) must leave state and output alone", s->name, f, r); rejected++; }
            else { st = before; accepted++; }
        }
        adv = st; (void)sauti_usac_decode(&cfg, &adv, s->au[f], s->au_n[f], pcm, &why); st = adv;
    }
    printf("%s: %ld accepted, %ld rejected mutated or truncated access units\n", s->name, accepted, rejected);
    CHECK(rejected > 50, "%s: mutations never fail?", s->name);
}

/* ---- the engine ------------------------------------------------------------------------------------------------------------------------------------------------------------------ */
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
static float *decode_all_c(const uint8_t *data, size_t n, unsigned channels, size_t chunk, size_t *frames_out, const char *name, const char *container) {
    maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, NULL); float *pcm = NULL, *buf; size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    *frames_out = 0;
    if (!d) { CHECK(0, "%s: open", name); return NULL; }
    CHECK(info.channels == channels && info.sample_rate == 48000u && strcmp(info.codec_profile, "USAC") == 0 && strcmp(info.container_name, container) == 0, "%s: %u ch %u Hz profile '%s' container '%s'", name, (unsigned)info.channels, (unsigned)info.sample_rate, info.codec_profile, info.container_name);
    buf = (float *)malloc(chunk * channels * sizeof(float));
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, chunk, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * channels * sizeof(float)); } memcpy(pcm + total * channels, buf, got * channels * sizeof(float)); total += got; }
        if (r == MAKTABASAUTI_EOF) break;
        if (r != MAKTABASAUTI_OK) { CHECK(0, "%s: read -> %d", name, (int)r); break; }
    }
    free(buf); maktabasauti_decoder_close(d); *frames_out = total; return pcm;
}
static float *decode_all(const uint8_t *data, size_t n, unsigned channels, size_t chunk, size_t *frames_out, const char *name) { return decode_all_c(data, n, channels, chunk, frames_out, name, "mp4"); }
static void test_engine(const Stream *s, const char *path) {
    static SautiUsacConfig cfg; static SautiUsacState st; static float one[8 * 1024];
    size_t n, got, got2, f; uint8_t *data = slurp(path, &n); float *pcm, *pcm2; SautiAacReason why; maktabasauti_stream_info_t info; maktabasauti_decoder_t *d;
    if (!data) { CHECK(0, "cannot read %s (run from the repository root)", path); return; }
    pcm = decode_all(data, n, s->channels, 4096, &got, path);
    pcm2 = decode_all(data, n, s->channels, 777, &got2, path);                                                                       /* reads of another size */
    CHECK(got == s->frames * 1024u && got2 == got && pcm && pcm2 && memcmp(pcm, pcm2, got * s->channels * sizeof(float)) == 0, "%s: %zu / %zu frames, expected %zu; chunk sizes must not matter", path, got, got2, s->frames * 1024u);
    CHECK(sauti_usac_asc_parse(s->asc, s->asc_n, &cfg) == 1, "%s: asc", path);
    sauti_usac_state_init(&st, &cfg);
    for (f = 0; pcm && f < s->frames && f * 1024u + 1024u <= got; f++) {
        CHECK(sauti_usac_decode(&cfg, &st, s->au[f], s->au_n[f], one, &why) == 1, "%s frame %zu", path, f);
        CHECK(memcmp(one, pcm + f * 1024u * s->channels, 1024u * s->channels * sizeof(float)) == 0, "%s: block %zu differs from the direct decode", path, f);
    }
    d = open_mem(data, n, &info, NULL);
    if (d) { CHECK((info.flags & MAKTABASAUTI_STREAM_SEEKABLE) && info.total_frames == s->frames * 1024u && info.native_format == MAKTABASAUTI_FORMAT_F32, "%s: flags %u total %llu", path, (unsigned)info.flags, (unsigned long long)info.total_frames); maktabasauti_decoder_close(d); }
    free(pcm); free(pcm2); free(data);
}
static void test_seek(const char *path, unsigned channels, size_t blocks, const char *container) {                                                         /* checkpoints every 64 blocks: the channel state is heap data copied deeply */
    size_t n, frames, got, i; uint8_t *data = slurp(path, &n); maktabasauti_stream_info_t info; maktabasauti_decoder_t *d; float *ref, a[6 * 500];
    const uint64_t b64 = 64u * 1024u;
    const uint64_t targets[] = { b64 + 2000u, b64, b64 - 1u, 30000u, 1000, 0, blocks * 1024u - 3000u, b64 + 2000u, 40000, blocks * 1024u - 700u };
    if (!data) { CHECK(0, "cannot read %s", path); return; }
    ref = decode_all_c(data, n, channels, 4096, &frames, "seek reference", container);
    CHECK(frames == blocks * 1024u, "%s: %zu frames", path, frames);
    d = open_mem(data, n, &info, NULL);
    if (d && ref) {
        for (i = 0; i < sizeof targets / sizeof targets[0]; i++) {
            const size_t want = targets[i] + 500u <= frames ? 500u : (size_t)(frames - targets[i]);
            maktabasauti_result_t r = maktabasauti_decoder_seek(d, targets[i], MAKTABASAUTI_SEEK_EXACT, NULL);
            CHECK(r == MAKTABASAUTI_OK, "seek %llu -> %d", (unsigned long long)targets[i], (int)r);
            r = maktabasauti_decoder_read(d, a, want, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == want && memcmp(a, ref + targets[i] * channels, want * channels * sizeof(float)) == 0, "%s: seek to %llu differs from sequential decoding (multichannel state)", path, (unsigned long long)targets[i]);
        }
        CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK && maktabasauti_decoder_read(d, a, 500, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 500u && memcmp(a, ref, 500 * channels * sizeof(float)) == 0, "reset must start again at the first block");
    } else CHECK(0, "seek test setup");
    if (d) maktabasauti_decoder_close(d);
    free(ref); free(data);
}

static void test_lfe_short(void) {                                                                                                 /* an LFE has long windows only (the reference decoder: parse error) */
    static SautiUsacConfig cfg; static SautiUsacState st, before; static float pcm[8 * 1024], keep[8 * 1024]; SautiAacReason why; size_t i;
    CHECK(sauti_usac_asc_parse(usac_k6bad_asc, sizeof usac_k6bad_asc, &cfg) == 1, "k6bad config");
    sauti_usac_state_init(&st, &cfg);
    CHECK(sauti_usac_decode(&cfg, &st, usac_k6bad_au0, sizeof usac_k6bad_au0, pcm, &why) == 1, "block 0 (a long LFE) decodes");
    for (i = 0; i < 8 * 1024; i++) keep[i] = pcm[i] = 4711.0f;
    before = st;
    CHECK(sauti_usac_decode(&cfg, &st, usac_k6bad_au1, sizeof usac_k6bad_au1, pcm, &why) == 0 && why == SAUTI_AAC_REASON_INVALID_SCE, "an LFE with a short window is refused (reason %d)", (int)why);
    CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "...and leaves state and output alone");
}

int main(int argc, char **argv) {
    #define S(n, cfg, ch, nel, ty) { "usac_" #n, usac_##n##_asc, sizeof usac_##n##_asc, usac_##n##_aus, usac_##n##_n, sizeof usac_##n##_n / sizeof usac_##n##_n[0], cfg, ch, nel, ty, usac_##n##_fp }
    const Stream streams[3] = { S(k5, 5, 5, 3, types5), S(k6, 6, 6, 4, types6), S(k7, 7, 8, 5, types7) };
    #undef S
    unsigned k;
    if (argc > 1 && !strcmp(argv[1], "--dump")) {                                                                                    /* build/test_usac_mc --dump > tests/test_usac_mc_fingerprints.h */
        g_dump = 1;
        printf("/* Per-channel fingerprints (sum, sum of squares, position-weighted sum, energy of the second difference) of the decoded PCM in the WAVE order, written by `build/test_usac_mc --dump` AFTER the decoder\n * agreed with FFmpeg (usac_k5: every channel 139 dB) and with FDK AAC on every element as a standalone stream (usac_k5 / k6 / k7, scripts/test_usac_vs_ffmpeg.sh). */\n");
        for (k = 0; k < 3; k++) { printf("static const double usac_%s_fp[64][8][4] = {\n", streams[k].name + 5); test_vectors(&streams[k]); printf("};\n"); }
        return g_failures ? 1 : 0;
    }
    for (k = 0; k < 3; k++) { test_config(&streams[k]); test_vectors(&streams[k]); test_atomic(&streams[k]); }
    test_config_edges(); test_lfe_short();
    test_engine(&streams[0], "tests/data/usac_k5.m4a"); test_engine(&streams[1], "tests/data/usac_k6.m4a"); test_engine(&streams[2], "tests/data/usac_k7.m4a");
    test_seek("tests/data/usac_k6long.m4a", 6, 70, "mp4"); test_seek("tests/data/usac_k6long.loas", 6, 70, "loas");
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_mc: all passed\n"); return 0;
}
