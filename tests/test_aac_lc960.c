/* AAC-LC with 960-sample frames (frameLengthFlag = 1, DAB+ / DRM style): src/decoders/modern/aac/sauti_aac_lcn.c and the layout plumbing through the band tables, the transforms and the window
 * synthesis.  The access units (tests/test_aac_lc960_vectors.h) and the fixtures tests/data/lc960_*.{m4a,loas} come from the repository's own stream generator (tests/data/make_lc960_fixtures.sh),
 * NOT from an encoder; their decoded PCM agrees with FFmpeg to ~138 dB and with FDK AAC to ~82 dB (scripts/test_aac_lc960_vs_ffmpeg.sh), so the fingerprints below pin a verified result.
 *   1. fingerprints of the decoded PCM of the first access units (mono 16 kHz and stereo 48 kHz, short windows, KBD, M/S, TNS);
 *   2. atomic on failure: every truncation and random bit flips leave state and output untouched, accepted output is finite;
 *   3. the public API: stream description, length, LOAS == MP4 bit for bit, whole-file fingerprint, seek back to the start repeats the PCM, 1-sample reads;
 *   4. refusals: unsupported configurations, SBR in a fill element, wrong element for the channel configuration;
 *   5. the window geometry: the 960 filterbank against a direct implementation of the definition (long and short, every window sequence). */
#define _POSIX_C_SOURCE 200809L
#define _USE_MATH_DEFINES
#include "maktabasauti/maktabasauti.h"
#include "sauti_aac_lcn.h"
#include "sauti_aac_window.h"
#include "test_aac_lc960_vectors.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures, g_dump;
static const char *g_eol = ",\n";                                                                              /* line end in --dump output: array rows or macro lines */
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int near(double a, double b) { return fabs(a - b) <= 1e-5 * (1.0 + fabs(b)); }

typedef struct { const char *name; const uint8_t *asc; unsigned rate_index, channels; const uint8_t *const *au; const size_t *au_n; size_t frames; const double (*fp)[2][4]; } Stream;

static void fingerprint(const float *pcm, unsigned n, unsigned ch, double out[2][4]) {            /* sum, sum of squares, position-weighted sum, energy of the second difference */
    unsigned c, i;
    for (c = 0; c < ch; c++) {
        double s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        for (i = 0; i < n; i++) { const double v = (double)pcm[i * ch + c]; s1 += v; s2 += v * v; s3 += v * (double)(i + 1) / (double)n; if (i >= 2) { const double d = v - 2.0 * (double)pcm[(i - 1) * ch + c] + (double)pcm[(i - 2) * ch + c]; s4 += d * d; } }
        out[c][0] = s1; out[c][1] = s2; out[c][2] = s3; out[c][3] = s4;
    }
}

#include "test_aac_lc960_fingerprints.h"

static void test_vectors(const Stream *s, int dump) {
    SautiAacLcnConfig cfg; SautiAacLcnState st; float pcm[2 * 960]; size_t f; SautiAacReason why;
    cfg.rate_index = s->rate_index; cfg.channel_config = s->channels; cfg.main = 0;
    CHECK(sauti_aac_lcn_frame_samples(&cfg) == 960u, "%s: frame length", s->name);
    sauti_aac_lcn_state_init(&st);
    for (f = 0; f < s->frames; f++) {
        double fp[2][4] = { { 0 } }; unsigned c, k;
        CHECK(sauti_aac_lcn_decode(&cfg, &st, s->au[f], s->au_n[f], pcm, &why) == 1, "%s frame %zu: decode", s->name, f);
        fingerprint(pcm, 960, s->channels, fp);
        if (dump) { printf("    { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} }%s", fp[0][0], fp[0][1], fp[0][2], fp[0][3], fp[1][0], fp[1][1], fp[1][2], fp[1][3], g_eol); continue; }
        for (c = 0; c < s->channels; c++) for (k = 0; k < 4; k++) CHECK(near(fp[c][k], s->fp[f][c][k]), "%s frame %zu ch %u fingerprint %u: %.12e vs %.12e", s->name, f, c, k, fp[c][k], s->fp[f][c][k]);
    }
}

static void test_atomic(const Stream *s) {
    SautiAacLcnConfig cfg; SautiAacLcnState st, before; float pcm[2 * 960], keep[2 * 960]; SautiAacReason why; size_t f, len, i; int round; long accepted = 0, rejected = 0;
    cfg.rate_index = s->rate_index; cfg.channel_config = s->channels; cfg.main = 0;
    sauti_aac_lcn_state_init(&st);
    for (i = 0; i < 2 * 960; i++) keep[i] = pcm[i] = 12345.0f;
    for (f = 0; f < s->frames; f++) {
        for (len = 0; len <= s->au_n[f]; len++) {                                                           /* every truncation */
            int r; before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcn_decode(&cfg, &st, s->au[f], len, pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu len %zu: failure (%d) must leave state and output alone", s->name, f, len, r); rejected++; }
            else { for (i = 0; i < 960u * s->channels; i++) CHECK(isfinite(pcm[i]), "%s: non-finite output", s->name); st = before; accepted++; }
        }
        for (round = 0; round < 400; round++) {                                                             /* random bit flips */
            uint8_t au[2048]; int r, nflip = 1 + (int)(rnd() % 4u);
            memcpy(au, s->au[f], s->au_n[f]);
            while (nflip--) { const size_t at = rnd() % s->au_n[f]; au[at] ^= (uint8_t)(1u << (rnd() & 7u)); }
            before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcn_decode(&cfg, &st, au, s->au_n[f], pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu flip: failure (%d) must leave state and output alone", s->name, f, r); rejected++; }
            else { for (i = 0; i < 960u * s->channels; i++) CHECK(isfinite(pcm[i]), "%s: non-finite output after flips", s->name); st = before; accepted++; }
        }
    }
    printf("%s: %ld accepted, %ld rejected mutated or truncated access units\n", s->name, accepted, rejected);
    CHECK(rejected > 100, "%s: mutations never fail?", s->name);
}

/* ---- the public API on the fixtures ---------------------------------------------------------------------------------------------------------------------------- */
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
static float *decode_all(const uint8_t *data, size_t n, unsigned want_channels, unsigned want_rate, size_t chunk, size_t *frames_out, const char *name) {
    maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info); float *pcm = NULL, *buf; size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    *frames_out = 0;
    if (!d) { CHECK(0, "%s: open", name); return NULL; }
    CHECK(info.channels == want_channels && info.sample_rate == want_rate, "%s: %u ch %u Hz, expected %u ch %u Hz", name, (unsigned)info.channels, (unsigned)info.sample_rate, want_channels, want_rate);
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
    static const struct { const char *path; unsigned channels, rate; double fp[2][4]; } files[] = LC960_FILE_FINGERPRINTS;
    float *first[8] = { NULL }; size_t first_n[8] = { 0 }; unsigned k; const unsigned nfiles = (unsigned)(sizeof files / sizeof files[0]);
    CHECK(nfiles <= 8u, "too many files");
    for (k = 0; k < nfiles && k < 8u; k++) {
        size_t n, frames, one_frames, at; uint8_t *data = slurp(files[k].path, &n); float *pcm, *one; double fp[2][4] = { { 0 } }; unsigned c, j; int finite = 1; float peak = 0;
        if (!data) { CHECK(0, "cannot read %s (run from the repository root)", files[k].path); continue; }
        pcm = decode_all(data, n, files[k].channels, files[k].rate, 4096, &frames, files[k].path);
        CHECK(frames == 38400u, "%s: %zu frames, expected 38400 (40 access units of 960)", files[k].path, frames);
        for (at = 0; at < frames * files[k].channels; at++) { if (!isfinite(pcm[at])) finite = 0; if (fabsf(pcm[at]) > peak) peak = fabsf(pcm[at]); }
        CHECK(finite && peak > 0.0005f && peak < 1.0f, "%s: peak %.4f (generated below full scale, not silent)", files[k].path, (double)peak);
        fingerprint(pcm, (unsigned)(frames < 38400u ? frames : 38400u), files[k].channels, fp);
        if (g_dump) printf("    { \"%s\", %u, %u, { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} } }%s", files[k].path, files[k].channels, files[k].rate, fp[0][0], fp[0][1], fp[0][2], fp[0][3], fp[1][0], fp[1][1], fp[1][2], fp[1][3], g_eol);
        else for (c = 0; c < files[k].channels; c++) for (j = 0; j < 4; j++) CHECK(near(fp[c][j], files[k].fp[c][j]), "%s ch %u whole-file fingerprint %u: %.12e vs %.12e", files[k].path, c, j, fp[c][j], files[k].fp[c][j]);
        one = decode_all(data, n, files[k].channels, files[k].rate, 1, &one_frames, "1-sample reads");                 /* the read size must not matter */
        CHECK(one && one_frames == frames && memcmp(one, pcm, frames * files[k].channels * sizeof(float)) == 0, "%s: 1-sample reads differ", files[k].path);
        first[k] = pcm; first_n[k] = frames * files[k].channels; free(one); free(data);
    }
    if (first[0] && first[2]) CHECK(first_n[0] == first_n[2] && memcmp(first[0], first[2], first_n[0] * sizeof(float)) == 0, "LOAS and MP4 of the same access units must decode identically");
    for (k = 0; k < 8; k++) free(first[k]);
}
/* Perceptual noise substitution with M/S: where both channels of a band are noise and ms_used is set the standard makes the two noise vectors identical (correlated noise); FDK AAC and FAAD2 do
 * that (L/R correlation 0.42-0.45 on tests/data/lc960_pnsall.m4a), FFmpeg draws independent noise (-0.01).  Without M/S the channels stay independent. */
static double lr_correlation(const float *pcm, size_t frames) {
    double ll = 0, rr = 0, lr = 0; size_t i;
    for (i = 4000; i + 4000 < frames; i++) { const double l = (double)pcm[2 * i], r = (double)pcm[2 * i + 1]; ll += l * l; rr += r * r; lr += l * r; }
    return lr / sqrt(ll * rr);
}
static void test_noise_correlation(void) {
    static const struct { const char *path; double lo, hi; } cases[] = { { "tests/data/lc960_pnsall.m4a", 0.35, 0.55 }, { "tests/data/lc960_pns.m4a", -0.05, 0.05 } }; unsigned k;
    for (k = 0; k < 2; k++) {
        size_t n, frames; uint8_t *data = slurp(cases[k].path, &n); float *pcm; double c;
        if (!data) { CHECK(0, "cannot read %s", cases[k].path); continue; }
        pcm = decode_all(data, n, 2, 48000, 4096, &frames, cases[k].path);
        c = pcm ? lr_correlation(pcm, frames) : 0.0;
        CHECK(pcm && c >= cases[k].lo && c <= cases[k].hi, "%s: L/R correlation %.4f outside [%.2f, %.2f]", cases[k].path, c, cases[k].lo, cases[k].hi);
        free(pcm); free(data);
    }
}
static void test_rates(void) {                                                                               /* every sampling_frequency_index: its own band tables (long 960 and short 120) */
    static const double rate_fp[13][2][4] = LC960_RATE_FINGERPRINTS; static const unsigned hz[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 }; unsigned r;
    for (r = 0; r < 13; r++) {
        char path[64]; size_t n, frames, i; uint8_t *data; float *pcm; double fp[2][4] = { { 0 } }; unsigned c, j; int finite = 1;
        snprintf(path, sizeof path, "tests/data/lc960_r%02u.m4a", r);
        data = slurp(path, &n);
        if (!data) { CHECK(0, "cannot read %s", path); continue; }
        pcm = decode_all(data, n, 2, hz[r], 4096, &frames, path);
        CHECK(frames == 16u * 960u, "%s: %zu frames, expected 15360", path, frames);
        for (i = 0; i < frames * 2u; i++) if (!isfinite(pcm[i])) finite = 0;
        CHECK(finite, "%s: non-finite output", path);
        fingerprint(pcm, (unsigned)frames, 2, fp);
        if (g_dump) printf("    { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} }%s", fp[0][0], fp[0][1], fp[0][2], fp[0][3], fp[1][0], fp[1][1], fp[1][2], fp[1][3], g_eol);
        else for (c = 0; c < 2; c++) for (j = 0; j < 4; j++) CHECK(near(fp[c][j], rate_fp[r][c][j]), "%s ch %u fingerprint %u: %.12e vs %.12e", path, c, j, fp[c][j], rate_fp[r][c][j]);
        free(pcm); free(data);
    }
}
static void test_seek(void) {
    size_t n, frames, got; uint8_t *data = slurp("tests/data/lc960_st.m4a", &n); maktabasauti_stream_info_t info; maktabasauti_decoder_t *d; float *ref, a[2 * 500], b[2 * 500];
    if (!data) { CHECK(0, "cannot read tests/data/lc960_st.m4a"); return; }
    ref = decode_all(data, n, 2, 48000, 4096, &frames, "seek reference");
    d = open_mem(data, n, &info);
    if (d && ref) {
        CHECK(maktabasauti_decoder_seek(d, 20000, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seek 20000");
        CHECK(maktabasauti_decoder_read(d, a, 500, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 500u, "read after seek");
        CHECK(memcmp(a, ref + 20000u * 2u, sizeof a) == 0, "seek to 20000 (mid frame, after a short-window frame boundary) differs from sequential decoding");
        CHECK(maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seek 0");
        CHECK(maktabasauti_decoder_read(d, b, 500, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 500u && memcmp(b, ref, sizeof b) == 0, "seek to 0 repeats the start");
    } else CHECK(0, "seek test setup");
    if (d) maktabasauti_decoder_close(d);
    free(ref); free(data);
}

static int open_result(const uint8_t *data, size_t n) {
    maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
    if (maktabasauti_io_from_memory(data, n, NULL, &p.io) != MAKTABASAUTI_OK) return 1;
    r = maktabasauti_decoder_open(&p, &d);
    if (r == MAKTABASAUTI_OK && d) maktabasauti_decoder_close(d); else p.io.close(p.io.user);                  /* after a failed open the input stays the caller's */
    return (int)r;
}
static void test_refused_files(void) {
    size_t n, i; uint8_t *data = slurp("tests/data/lc960_he_sbr.m4a", &n); unsigned patched = 0;
    if (!data) { CHECK(0, "cannot read tests/data/lc960_he_sbr.m4a"); } else {
        CHECK(open_result(data, n) == (int)MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "HE-AAC with 960-sample frames (SBR fill in the access units) must be refused as an unsupported feature, got %d", open_result(data, n));
        free(data);
    }
    data = slurp("tests/data/lc960_st.m4a", &n);
    if (!data) { CHECK(0, "cannot read tests/data/lc960_st.m4a"); return; }
    CHECK(open_result(data, n) == (int)MAKTABASAUTI_OK, "the unpatched stereo fixture opens");
    for (i = 0; i + 3 < n; i++) if (data[i] == 0x02 && data[i + 1] == 0x11 && data[i + 2] == 0x94) {           /* DecoderSpecificInfo: AOT 2, 48 kHz, stereo, frameLengthFlag 1 */
        patched++;
        data[i + 2] = 0x9c;                                                                                  /* channel_configuration 3 */
        CHECK(open_result(data, n) == (int)MAKTABASAUTI_E_UNSUPPORTED_FORMAT, "960 with channel_configuration 3 is not supported, got %d", open_result(data, n));
        data[i + 2] = 0x84;                                                                                  /* channel_configuration 0 (PCE) */
        CHECK(open_result(data, n) == (int)MAKTABASAUTI_E_UNSUPPORTED_FORMAT, "960 with channel_configuration 0 is not supported, got %d", open_result(data, n));
        data[i + 2] = 0x94;
    }
    CHECK(patched == 1u, "the AudioSpecificConfig of the fixture was found %u times", patched);
    free(data);
}

/* ---- refusals -------------------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct { uint8_t b[4096]; unsigned n; } Bits;
static void put(Bits *x, unsigned value, unsigned width) { while (width--) { const unsigned bit = (value >> width) & 1u; if ((x->n & 7u) == 0) x->b[x->n >> 3] = 0; if (bit) x->b[x->n >> 3] |= (uint8_t)(0x80u >> (x->n & 7u)); x->n++; } }
static void test_refusals(void) {
    SautiAacLcnConfig c; SautiAacLcnState st; float pcm[2 * 960]; SautiAacReason why; Bits x; unsigned rate;
    c.rate_index = 3; c.channel_config = 0; c.main = 0; CHECK(sauti_aac_lcn_frame_samples(&c) == 0u, "channel configuration 0");
    c.channel_config = 3; CHECK(sauti_aac_lcn_frame_samples(&c) == 0u, "channel configuration 3 (not supported with 960)");
    c.channel_config = 7; CHECK(sauti_aac_lcn_frame_samples(&c) == 0u, "channel configuration 7");
    c.channel_config = 2; c.rate_index = 13; CHECK(sauti_aac_lcn_frame_samples(&c) == 0u, "sampling index 13");
    CHECK(sauti_aac_lcn_frame_samples(NULL) == 0u, "NULL configuration");
    for (rate = 0; rate <= 12; rate++) { c.rate_index = rate; c.channel_config = 1; CHECK(sauti_aac_lcn_frame_samples(&c) == 960u, "rate %u mono", rate); c.channel_config = 2; CHECK(sauti_aac_lcn_frame_samples(&c) == 960u, "rate %u stereo", rate); }
    c.rate_index = 3; c.channel_config = 2; sauti_aac_lcn_state_init(&st);
    memset(&x, 0, sizeof x); put(&x, 7, 3);                                                                  /* ID_END only: no channel element */
    CHECK(sauti_aac_lcn_decode(&c, &st, x.b, (x.n + 7) / 8, pcm, &why) == 0, "an access unit without a channel element is malformed");
    memset(&x, 0, sizeof x); put(&x, 0, 3); put(&x, 0, 4);                                                   /* SCE in a stereo stream, then truncated */
    CHECK(sauti_aac_lcn_decode(&c, &st, x.b, (x.n + 7) / 8, pcm, &why) == -1, "SCE for channel configuration 2 is unsupported");
    c.channel_config = 1;
    memset(&x, 0, sizeof x); put(&x, 1, 3); put(&x, 0, 4);                                                   /* CPE in a mono stream */
    CHECK(sauti_aac_lcn_decode(&c, &st, x.b, (x.n + 7) / 8, pcm, &why) == -1, "CPE for channel configuration 1 is unsupported");
    memset(&x, 0, sizeof x); put(&x, 3, 3); put(&x, 0, 4);                                                   /* LFE */
    CHECK(sauti_aac_lcn_decode(&c, &st, x.b, (x.n + 7) / 8, pcm, &why) == -1, "LFE is unsupported");
    CHECK(sauti_aac_lcn_decode(&c, &st, NULL, 4, pcm, &why) == 0 && sauti_aac_lcn_decode(&c, &st, x.b, 0, pcm, &why) == 0, "NULL / empty input");
    memset(&x, 0, sizeof x); put(&x, 6, 3); put(&x, 2, 4); put(&x, 13, 4); put(&x, 0, 12);                  /* fill element: 2 bytes, extension_type 13 (SBR without CRC) */
    put(&x, 0, 3); put(&x, 0, 4);
    CHECK(sauti_aac_lcn_decode(&c, &st, x.b, (x.n + 7) / 8, pcm, &why) == -1, "a fill element announcing SBR is refused (not decoded as plain LC)");
    memset(&x, 0, sizeof x); put(&x, 6, 3); put(&x, 2, 4); put(&x, 14, 4); put(&x, 0, 12); put(&x, 0, 3); put(&x, 0, 4);
    CHECK(sauti_aac_lcn_decode(&c, &st, x.b, (x.n + 7) / 8, pcm, &why) == -1, "extension_type 14 (SBR with CRC) is refused too");
}

static void put_bits_of(Bits *x, const uint8_t *src, unsigned from, unsigned to) { unsigned i; for (i = from; i < to; i++) put(x, (src[i >> 3] >> (7u - (i & 7u))) & 1u, 1); }
static void put_dse(Bits *x, unsigned tag, unsigned align, unsigned count) {                                 /* data_stream_element with `count` payload bytes (escape count above 254) */
    unsigned i; put(x, 4, 3); put(x, tag, 4); put(x, align, 1); put(x, count < 255u ? count : 255u, 8); if (count >= 255u) put(x, count - 255u, 8);
    if (align) while (x->n & 7u) put(x, 0, 1);
    for (i = 0; i < count; i++) put(x, (i * 37u + 11u) & 255u, 8);
}
static void put_fil(Bits *x, unsigned type, unsigned count) {                                                /* fill element with `count` payload bytes: extension_type + nibble, then 0xA5 */
    unsigned i; put(x, 6, 3); put(x, count < 15u ? count : 15u, 4); if (count >= 15u) put(x, count - 14u, 8);
    if (count) { put(x, type, 4); put(x, 0, 4); } for (i = 1; i < count; i++) put(x, 0xa5, 8);
}
/* DSE and FIL elements around the channel element are skipped without changing a sample: the same access unit decodes bit for bit with and without them */
static void test_skipped_elements(const Stream *s) {
    SautiAacLcnConfig cfg; SautiAacLcnState base, aug; float want[2 * 960], got[2 * 960]; SautiAacReason why; size_t f; unsigned variant, checked = 0;
    cfg.rate_index = s->rate_index; cfg.channel_config = s->channels; cfg.main = 0;
    for (variant = 0; variant < 8; variant++) {
        sauti_aac_lcn_state_init(&base); sauti_aac_lcn_state_init(&aug);
        for (f = 0; f < s->frames; f++) {
            static Bits x; unsigned total = (unsigned)s->au_n[f] * 8u, end = total, k; int r1, r2;
            while (end > 0 && !((s->au[f][(end - 1) >> 3] >> (7u - ((end - 1) & 7u))) & 1u)) end--;              /* last set bit = last bit of ID_END (111), the rest is byte alignment */
            end -= 3;
            memset(&x, 0, sizeof x);
            if (variant == 0) put_dse(&x, 5, 0, 3);
            if (variant == 1) { put_fil(&x, 0, 1); put_dse(&x, 2, 1, 7); }                                      /* odd number of bits, then an aligned DSE */
            if (variant == 2) put_dse(&x, 0, 0, 300);                                                          /* escape count */
            if (variant == 3) put_fil(&x, 1, 20);                                                              /* escape count, EXT_FILL_DATA */
            if (variant == 4) { put_fil(&x, 0, 0); put_fil(&x, 1, 3); put_dse(&x, 15, 1, 0); }                  /* empty fill, empty DSE */
            put_bits_of(&x, s->au[f], 0, end);
            if (variant == 5) put_dse(&x, 9, 0, 5);                                                            /* after the channel element */
            if (variant == 6) { put_fil(&x, 1, 2); put_fil(&x, 0, 16); }
            if (variant == 7) { put_dse(&x, 1, 1, 255); put_fil(&x, 0, 5); }
            put(&x, 7, 3);
            for (k = 0; k < 2 * 960; k++) want[k] = got[k] = 777.0f;
            r1 = sauti_aac_lcn_decode(&cfg, &base, s->au[f], s->au_n[f], want, &why);
            r2 = sauti_aac_lcn_decode(&cfg, &aug, x.b, (x.n + 7) / 8, got, &why);
            CHECK(r1 == 1 && r2 == 1, "%s variant %u frame %zu: decode %d / %d", s->name, variant, f, r1, r2);
            if (r1 == 1 && r2 == 1) { CHECK(memcmp(want, got, 960u * s->channels * sizeof(float)) == 0, "%s variant %u frame %zu: skipped elements changed the output", s->name, variant, f); checked++; }
        }
    }
    CHECK(checked == 8u * s->frames, "%s: %u of %zu augmented access units compared", s->name, checked, 8u * s->frames);
}

/* ---- the window geometry against the definition (960 lines, 120-line short windows) ---------------------------------------------------------------------------- */
static double bessel_i0(double x) { double term = 1, sum = 1; int k; for (k = 1; k < 60; k++) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; } return sum; }
/* rising half of a window of length 2m (m samples): shape 0 sine, shape 1 Kaiser-Bessel derived (alpha 4 for long, 6 for short windows) */
static double slope(unsigned m, unsigned shape, unsigned i, double alpha) {
    if (shape == 0) return sin(M_PI / (2.0 * (double)m) * ((double)i + 0.5));
    { double num = 0, den = 0; unsigned j; for (j = 0; j <= m; j++) { const double r = 2.0 * (double)j / (double)m - 1.0; const double kv = bessel_i0(M_PI * alpha * sqrt(1.0 - r * r)); if (j <= i) num += kv; den += kv; } return sqrt(num / den); }
}
static void reference_window(unsigned n, unsigned seq, unsigned prev_shape, unsigned cur_shape, const float *x, const float *prev_overlap, double *pcm, double *next) {
    const unsigned ns = n / 8u, off = (n - ns) / 2u; static double full[2 * 960]; unsigned i, w;
    memset(full, 0, sizeof full);
    if (seq != 2u) {
        for (i = 0; i < 2 * n; i++) {
            double wv;
            if (seq == 0u) wv = i < n ? slope(n, prev_shape, i, 4.0) : slope(n, cur_shape, 2 * n - 1 - i, 4.0);
            else if (seq == 1u) wv = i < n ? slope(n, prev_shape, i, 4.0) : i < n + off ? 1.0 : i < n + off + ns ? slope(ns, cur_shape, n + off + ns - 1 - i, 6.0) : 0.0;
            else wv = i < off ? 0.0 : i < off + ns ? slope(ns, prev_shape, i - off, 6.0) : i < n ? 1.0 : slope(n, cur_shape, 2 * n - 1 - i, 4.0);
            full[i] = (double)x[i] * wv;
        }
    } else {
        for (w = 0; w < 8; w++) for (i = 0; i < 2 * ns; i++) {
            const double wv = i < ns ? slope(ns, w == 0 ? prev_shape : cur_shape, i, 6.0) : slope(ns, cur_shape, 2 * ns - 1 - i, 6.0);
            full[off + w * ns + i] += (double)x[w * 2 * ns + i] * wv;
        }
    }
    for (i = 0; i < n; i++) { pcm[i] = full[i] + (double)prev_overlap[i]; next[i] = full[n + i]; }
}
static void test_windows(void) {
    static float x[1920], prev[960], pcm[960], next[960]; static double rp[960], rn[960]; unsigned seq, ps, cs, i; int round;
    for (round = 0; round < 3; round++) for (seq = 0; seq < 4; seq++) for (ps = 0; ps < 2; ps++) for (cs = 0; cs < 2; cs++) {
        double worst = 0;
        for (i = 0; i < 1920; i++) x[i] = (float)((int)(rnd() % 2001u) - 1000) * 0.01f;
        for (i = 0; i < 960; i++) prev[i] = (float)((int)(rnd() % 2001u) - 1000) * 0.01f;
        CHECK(sauti_aac_window_synth_n(960, seq, ps, cs, x, prev, pcm, next) == 1, "synth seq %u shapes %u/%u", seq, ps, cs);
        reference_window(960, seq, ps, cs, x, prev, rp, rn);
        for (i = 0; i < 960; i++) { const double d1 = fabs(rp[i] - (double)pcm[i]), d2 = fabs(rn[i] - (double)next[i]); if (d1 > worst) worst = d1; if (d2 > worst) worst = d2; }
        CHECK(worst < 2e-5 * 10.0, "window sequence %u shapes %u/%u: worst deviation %.3e from the definition", seq, ps, cs, worst);
    }
    CHECK(sauti_aac_window_synth_n(1000, 0, 0, 0, x, prev, pcm, next) == 0, "length 1000 is refused");
    CHECK(sauti_aac_window_synth_n(960, 4, 0, 0, x, prev, pcm, next) == 0, "sequence 4 is refused");
    CHECK(sauti_aac_window_synth_n(960, 0, 2, 0, x, prev, pcm, next) == 0, "shape 2 is refused");
}

int main(int argc, char **argv) {
    static const uint8_t *const st_au[] = { lc960_st_au0, lc960_st_au1, lc960_st_au2, lc960_st_au3, lc960_st_au4, lc960_st_au5 };
    static const size_t st_n[] = { sizeof lc960_st_au0, sizeof lc960_st_au1, sizeof lc960_st_au2, sizeof lc960_st_au3, sizeof lc960_st_au4, sizeof lc960_st_au5 };
    static const uint8_t *const mono_au[] = { lc960_mono_au0, lc960_mono_au1, lc960_mono_au2, lc960_mono_au3, lc960_mono_au4, lc960_mono_au5 };
    static const size_t mono_n[] = { sizeof lc960_mono_au0, sizeof lc960_mono_au1, sizeof lc960_mono_au2, sizeof lc960_mono_au3, sizeof lc960_mono_au4, sizeof lc960_mono_au5 };
    const Stream streams[2] = {
        { "lc960 stereo 48 kHz", lc960_st_asc, 3, 2, st_au, st_n, 6, lc960_st_fp },
        { "lc960 mono 16 kHz", lc960_mono_asc, 8, 1, mono_au, mono_n, 6, lc960_mono_fp } };
    unsigned k;
    if (argc > 1 && !strcmp(argv[1], "--dump")) {                                                            /* writes tests/test_aac_lc960_fingerprints.h: build/test_aac_lc960 --dump > tests/test_aac_lc960_fingerprints.h */
        g_dump = 1;
        printf("/* Fingerprints of the decoded PCM (sum, sum of squares, position-weighted sum, energy of the second difference), written by `build/test_aac_lc960 --dump` AFTER the decoder agreed with\n * FFmpeg (~138 dB) and FDK AAC (~90 dB) on these streams (scripts/test_aac_lc960_vs_ffmpeg.sh); regenerate only after such a comparison. */\n");
        printf("static const double lc960_st_fp[6][2][4] = {\n"); test_vectors(&streams[0], 1); printf("};\n");
        printf("static const double lc960_mono_fp[6][2][4] = {\n"); test_vectors(&streams[1], 1); printf("};\n");
        g_eol = ", \\\n"; printf("#define LC960_FILE_FINGERPRINTS { \\\n"); test_files(); printf("}\n");
        printf("#define LC960_RATE_FINGERPRINTS { \\\n"); test_rates(); printf("}\n");
        return g_failures ? 1 : 0;
    }
    for (k = 0; k < 2; k++) { test_vectors(&streams[k], 0); test_atomic(&streams[k]); }
    test_files(); test_noise_correlation(); test_rates(); test_seek(); test_refused_files(); for (k = 0; k < 2; k++) test_skipped_elements(&streams[k]); test_refusals(); test_windows();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_aac_lc960: all passed\n"); return 0;
}
