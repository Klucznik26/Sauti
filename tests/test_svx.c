/* Amiga IFF 8SVX (PCM, Fibonacci delta, exponential delta; mono and stereo) decoded through the engine.  The expected PCM of tests/data/svx_*.pcm comes from FFmpeg for PCM and from a model for the delta files (tests/data/make_svx_fixtures.py); svx_wavepak_* are files written by wavepak on an emulated Amiga, with the PCM of its own decoder: it must be
 * reproduced bit for bit, also after seeks (checkpoints of the delta state) and with reads of any size.  Unknown compressions, a missing BODY / VHDR and rate 0 are refused. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
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
static maktabasauti_result_t open_mem(const uint8_t *data, size_t n, maktabasauti_decoder_t **out, maktabasauti_stream_info_t *info) {
    maktabasauti_open_params_t p; maktabasauti_result_t r;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; *out = NULL;
    if (maktabasauti_io_from_memory(data, n, NULL, &p.io) != MAKTABASAUTI_OK) return MAKTABASAUTI_E_IO;
    r = maktabasauti_decoder_open(&p, out);
    if (r != MAKTABASAUTI_OK) { p.io.close(p.io.user); return r; }
    if (info) { info->struct_size = sizeof *info; (void)maktabasauti_decoder_get_info(*out, info); }
    return MAKTABASAUTI_OK;
}
static float *read_all(maktabasauti_decoder_t *d, unsigned ch, size_t chunk, size_t *frames_out) {
    float *pcm = NULL, *buf = (float *)malloc(chunk * ch * sizeof(float)); size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, chunk, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * ch * sizeof(float)); } memcpy(pcm + total * ch, buf, got * ch * sizeof(float)); total += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    free(buf); *frames_out = total; return pcm;
}
static void expect_refused(const char *what, const uint8_t *data, size_t n, maktabasauti_result_t want) {
    maktabasauti_decoder_t *d; const maktabasauti_result_t r = open_mem(data, n, &d, NULL);
    CHECK(r != MAKTABASAUTI_OK, "%s: opened, but must be refused", what);
    if (want && r != MAKTABASAUTI_OK) CHECK(r == want, "%s: result %d, expected %d", what, (int)r, (int)want);
    if (r == MAKTABASAUTI_OK) maktabasauti_decoder_close(d);
}

typedef struct { const char *name; unsigned ch, rate; uint64_t frames; } Fx;
static void check_fixture(const Fx *x) {
    char p1[128], p2[128]; size_t n, npcm, frames, frames2, i, k; uint8_t *f, *gold; maktabasauti_decoder_t *d; maktabasauti_stream_info_t info; float *pcm, *pcm2;
    snprintf(p1, sizeof p1, "tests/data/%s.8svx", x->name); snprintf(p2, sizeof p2, "tests/data/%s.pcm", x->name);
    f = slurp(p1, &n); gold = slurp(p2, &npcm);
    if (!f || !gold) { CHECK(0, "cannot read %s / %s (run from the repository root)", p1, p2); free(f); free(gold); return; }
    CHECK(open_mem(f, n, &d, &info) == MAKTABASAUTI_OK, "%s: open", x->name);
    if (!d) { free(f); free(gold); return; }
    CHECK(info.channels == x->ch && info.sample_rate == x->rate && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE) && info.total_frames == x->frames, "%s: %u ch %u Hz %llu frames", x->name, (unsigned)info.channels, (unsigned)info.sample_rate, (unsigned long long)info.total_frames);
    pcm = read_all(d, x->ch, 4096, &frames);
    CHECK(pcm && frames == x->frames && npcm == frames * x->ch * 2u, "%s: %zu frames (expected %llu)", x->name, frames, (unsigned long long)x->frames);
    for (i = 0; pcm && i < frames * x->ch && 2u * i + 1u < npcm; i++) {
        const int16_t g = (int16_t)((unsigned)gold[2u * i] | ((unsigned)gold[2u * i + 1u] << 8));
        if (pcm[i] != (float)g * (1.0f / 32768.0f)) { CHECK(0, "%s: sample %zu: %.9g, expected %d", x->name, i, (double)pcm[i], g); break; }
    }
    maktabasauti_decoder_close(d);
    for (k = 0; k < 3; k++) {                                                                                   /* reads of other sizes */
        static const size_t chunks[3] = { 1, 333, 1000 };
        CHECK(open_mem(f, n, &d, NULL) == MAKTABASAUTI_OK, "reopen");
        pcm2 = d ? read_all(d, x->ch, chunks[k], &frames2) : NULL;
        CHECK(pcm && pcm2 && frames2 == frames && memcmp(pcm, pcm2, frames * x->ch * sizeof(float)) == 0, "%s: reads of %zu frames give other PCM", x->name, chunks[k]);
        free(pcm2); if (d) maktabasauti_decoder_close(d);
    }
    if (pcm && open_mem(f, n, &d, NULL) == MAKTABASAUTI_OK) {                                                    /* exact seeks: before / on / after the 4096-frame checkpoints, odd and even frames */
        const uint64_t t[] = { 1, 4095, 4096, 4097, 8191, 8192, 777, 0, x->frames - 1u, 5001, 12, 3 };
        float a[2 * 50];
        for (i = 0; i < sizeof t / sizeof t[0]; i++) {
            size_t got, want;
            if (t[i] >= x->frames) continue;
            want = t[i] + 50u <= x->frames ? 50u : (size_t)(x->frames - t[i]);
            CHECK(maktabasauti_decoder_seek(d, t[i], MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "%s: seek %llu", x->name, (unsigned long long)t[i]);
            (void)maktabasauti_decoder_read(d, a, want, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == want && memcmp(a, pcm + t[i] * x->ch, want * x->ch * sizeof(float)) == 0, "%s: seek to %llu differs from sequential decoding", x->name, (unsigned long long)t[i]);
        }
        maktabasauti_decoder_close(d);
    }
    free(pcm);
    if (!strcmp(x->name, "svx_fib_mono")) {                                                                     /* refusals, from a copy of the file: VHDR is the first chunk (offset 12), sCompression at 12 + 8 + 15 */
        uint8_t *m = (uint8_t *)malloc(n);
        memcpy(m, f, n);
        m[12 + 8 + 15] = 3; expect_refused("sCompression 3", m, n, MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
        m[12 + 8 + 15] = 1; m[12 + 8 + 12] = 0; m[12 + 8 + 13] = 0; expect_refused("rate 0", m, n, 0);
        memcpy(m, f, n); memcpy(m + 12, "VHDX", 4); expect_refused("no VHDR", m, n, 0);
        memcpy(m, f, n); m[8] = 'A'; expect_refused("not 8SVX", m, n, 0);
        memcpy(m, f, n); expect_refused("header only", m, 60u, 0);
        free(m);
    }
    free(f); free(gold);
}

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint8_t *form8(size_t *out_n, unsigned rate, unsigned octave, unsigned comp, int chan, uint32_t one_shot, const uint8_t *body, size_t body_n) {
    size_t n = 12u + 28u + (chan ? 12u : 0u) + 8u + body_n + (body_n & 1u);
    uint8_t *b = (uint8_t *)calloc(1, n); size_t o = 12;
    if (!b) return NULL;
    memcpy(b, "FORM", 4); memcpy(b + 8, "8SVX", 4); put32(b + 4, (uint32_t)(n - 8u));
    memcpy(b + o, "VHDR", 4); put32(b + o + 4, 20); put32(b + o + 8, one_shot); put32(b + o + 20, (uint32_t)rate << 16);
    b[o + 22] = (uint8_t)octave; b[o + 23] = (uint8_t)comp; put32(b + o + 24, 0x10000u); o += 28;
    if (chan) { memcpy(b + o, "CHAN", 4); put32(b + o + 4, 4); put32(b + o + 8, 6); o += 12; }
    memcpy(b + o, "BODY", 4); put32(b + o + 4, (uint32_t)body_n); if (body_n) memcpy(b + o + 8, body, body_n);
    *out_n = n; return b;
}
static void check_octaves(void) {
    uint8_t body[8], *b; size_t n = 0, got = 0, i; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; float pcm[8];
    const float u = 0.0078125f;
    memcpy(body, "\x01\x02\x03\x04\x05\x06", 6);
    b = form8(&n, 8000, 2, 0, 0, 2, body, 6);
    CHECK(open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.channels == 1 && info.total_frames == 4, "pcm octave open");
    if (d) {
        CHECK(maktabasauti_decoder_read(d, pcm, 4, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 4, "pcm octave read");
        CHECK(pcm[0] == 3 * u && pcm[1] == 4 * u && pcm[2] == 5 * u && pcm[3] == 6 * u, "pcm octave samples");
        CHECK(maktabasauti_decoder_seek(d, 2, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "pcm octave seek");
        CHECK(maktabasauti_decoder_read(d, pcm, 2, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 2 && pcm[0] == 5 * u && pcm[1] == 6 * u, "pcm octave after seek");
        maktabasauti_decoder_close(d); d = NULL;
    }
    free(b);
    b = form8(&n, 8000, 1, 0, 0, 1, body, 3);
    CHECK(open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 3, "ctOctave 1 keeps the whole BODY");
    if (d) maktabasauti_decoder_close(d);
    free(b); d = NULL;
    b = form8(&n, 8000, 2, 0, 0, 2, body, 2);
    CHECK(open_mem(b, n, &d, NULL) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "lowest octave missing");
    if (d) maktabasauti_decoder_close(d);
    free(b); d = NULL;
    /* Fibonacci, cala seria: stan 0, szesc kodow +1.  Najnizsza oktawa to probki 3,4,5,6. */
    memcpy(body, "\x00\x00\x99\x99\x99", 5);
    b = form8(&n, 8000, 2, 1, 0, 2, body, 5);
    CHECK(open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 4, "fib octave open");
    if (d) {
        CHECK(maktabasauti_decoder_read(d, pcm, 4, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 4, "fib octave read");
        for (i = 0; i < 4; i++) CHECK(pcm[i] == (float)(3 + i) * u, "fib octave sample %zu", i);
        CHECK(maktabasauti_decoder_seek(d, 3, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "fib octave seek");
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 1 && pcm[0] == 6 * u, "fib octave after seek");
        maktabasauti_decoder_close(d); d = NULL;
    }
    free(b);
    memcpy(body, "\x0a\x14\x1e\x28\x32\x3c", 6);
    b = form8(&n, 8000, 2, 0, 1, 1, body, 6);
    CHECK(open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.channels == 2 && info.total_frames == 2, "stereo octave open");
    if (d) {
        CHECK(maktabasauti_decoder_read(d, pcm, 2, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 2, "stereo octave read");
        CHECK(pcm[0] == 0x14 * u && pcm[1] == 0x32 * u && pcm[2] == 0x1e * u && pcm[3] == 0x3c * u, "stereo octave samples");
        maktabasauti_decoder_close(d);
    }
    free(b);
}

static uint8_t *form_seq(size_t *out_n, unsigned octave, unsigned comp, int chan, uint32_t one_shot,
                         const uint8_t *body, size_t body_n, const uint32_t *pairs, unsigned npairs, int seq_after) {
    const size_t seq_bytes = (size_t)npairs * 8u;
    const size_t n = 12u + 28u + (chan ? 12u : 0u) + 8u + seq_bytes + 8u + body_n + (body_n & 1u);
    uint8_t *b = (uint8_t *)calloc(1, n); size_t o = 12; unsigned i;
    if (!b) return NULL;
    memcpy(b, "FORM", 4); memcpy(b + 8, "8SVX", 4); put32(b + 4, (uint32_t)(n - 8u));
    memcpy(b + o, "VHDR", 4); put32(b + o + 4, 20); put32(b + o + 8, one_shot); put32(b + o + 20, (uint32_t)8000u << 16);
    b[o + 22] = (uint8_t)octave; b[o + 23] = (uint8_t)comp; put32(b + o + 24, 0x10000u); o += 28;
    if (chan) { memcpy(b + o, "CHAN", 4); put32(b + o + 4, 4); put32(b + o + 8, 6); o += 12; }
    if (!seq_after) {
        memcpy(b + o, "SEQN", 4); put32(b + o + 4, (uint32_t)seq_bytes);
        for (i = 0; i < npairs; i++) { put32(b + o + 8u + (size_t)i * 8u, pairs[2u * i]); put32(b + o + 12u + (size_t)i * 8u, pairs[2u * i + 1u]); }
        o += 8u + seq_bytes;
    }
    memcpy(b + o, "BODY", 4); put32(b + o + 4, (uint32_t)body_n); if (body_n) memcpy(b + o + 8, body, body_n);
    o += 8u + body_n + (body_n & 1u);
    if (seq_after) {
        memcpy(b + o, "SEQN", 4); put32(b + o + 4, (uint32_t)seq_bytes);
        for (i = 0; i < npairs; i++) { put32(b + o + 8u + (size_t)i * 8u, pairs[2u * i]); put32(b + o + 12u + (size_t)i * 8u, pairs[2u * i + 1u]); }
    }
    *out_n = n; return b;
}
static void expect_samples(const char *what, maktabasauti_decoder_t *d, const uint8_t *want, size_t nframes, unsigned ch) {
    float pcm[32]; size_t got = 0, i; const float u = 0.0078125f;
    CHECK(nframes * ch <= 32u, "%s: buffer", what);
    CHECK(maktabasauti_decoder_read(d, pcm, nframes, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == nframes, "%s: read", what);
    for (i = 0; i < got * ch && i < 32u; i++) CHECK(pcm[i] == (float)want[i] * u, "%s: sample %zu", what, i);
}
static void check_seqn(void) {
    uint8_t body[16], *b; size_t n = 0, got = 0, i; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info;
    float pcm[8]; const float u = 0.0078125f;
    static const uint32_t rep[] = { 0, 4, 0, 4 };
    static const uint32_t hole[] = { 0, 4, 4, 4, 4, 8 };
    static const uint32_t st[] = { 0, 4, 0, 4 };
    static const uint32_t fibp[] = { 4, 8, 0, 4 };
    static const uint32_t one[] = { 0, 4 };
    for (i = 0; i < 8; i++) body[i] = (uint8_t)(i + 1u);
    b = form_seq(&n, 1, 0, 0, 8, body, 8, rep, 2, 0);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.channels == 1 && info.total_frames == 8, "seq repeat open");
    if (d) {
        uint8_t want[8] = { 1, 2, 3, 4, 1, 2, 3, 4 };
        for (i = 0; i < 8; i++) {
            CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 1 && pcm[0] == want[i] * u, "seq repeat sample %zu", i);
        }
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_EOF && got == 0, "seq repeat end");
        CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "seq reset");
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 1 && pcm[0] == u, "seq reset sample");
        CHECK(maktabasauti_decoder_seek(d, 6, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seq seek 6");
        CHECK(maktabasauti_decoder_read(d, pcm, 2, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 2 && pcm[0] == 3 * u && pcm[1] == 4 * u, "seq after seek");
        CHECK(maktabasauti_decoder_seek(d, 8, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seq seek end");
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_EOF && got == 0, "seq at end");
        CHECK(maktabasauti_decoder_seek(d, 9, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_E_OUT_OF_RANGE, "seq past end");
        maktabasauti_decoder_close(d); d = NULL;
    }
    free(b);
    b = form_seq(&n, 1, 0, 0, 8, body, 8, hole, 3, 0);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "seq hole open");
    if (d) { uint8_t want[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }; expect_samples("seq hole", d, want, 8, 1); maktabasauti_decoder_close(d); d = NULL; }
    free(b);
    b = form_seq(&n, 1, 0, 0, 8, body, 8, NULL, 0, 0);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "empty SEQN plays the body");
    if (d) { expect_samples("empty SEQN", d, body, 8, 1); maktabasauti_decoder_close(d); d = NULL; }
    free(b);
    for (i = 0; i < 16; i++) body[i] = (uint8_t)(i + 1u);
    b = form_seq(&n, 1, 0, 1, 8, body, 16, st, 2, 1);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.channels == 2 && info.total_frames == 8, "seq stereo open");
    if (d) {
        uint8_t want[16] = { 1, 9, 2, 10, 3, 11, 4, 12, 1, 9, 2, 10, 3, 11, 4, 12 };
        expect_samples("seq stereo", d, want, 8, 2);
        CHECK(maktabasauti_decoder_seek(d, 6, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seq stereo seek");
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 1 && pcm[0] == 3 * u && pcm[1] == 11 * u, "seq stereo after seek");
        maktabasauti_decoder_close(d); d = NULL;
    }
    free(b);
    memcpy(body, "\x00\x00\x99\x99\x99\x99", 6);
    b = form_seq(&n, 1, 1, 0, 8, body, 6, fibp, 2, 0);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "seq fib open");
    if (d) {
        uint8_t want[8] = { 5, 6, 7, 8, 1, 2, 3, 4 };
        expect_samples("seq fib", d, want, 8, 1);
        CHECK(maktabasauti_decoder_seek(d, 4, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seq fib seek");
        CHECK(maktabasauti_decoder_read(d, pcm, 2, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 2 && pcm[0] == u && pcm[1] == 2 * u, "seq fib after seek");
        maktabasauti_decoder_close(d); d = NULL;
    }
    free(b);
    for (i = 0; i < 16; i++) body[i] = (uint8_t)(i + 1u);
    b = form_seq(&n, 1, 0, 0, 16, body, 16, (const uint32_t[]){ 0, 16 }, 1, 0);
    CHECK(b && n > 8u && open_mem(b, n - 8u, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "seq truncated open");
    if (d) {
        uint8_t want[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        expect_samples("seq truncated", d, want, 8, 1);
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_E_TRUNCATED && got == 0, "seq truncated sticky");
        CHECK(maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seq truncated seek");
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 1 && pcm[0] == u, "seq truncated replay");
        maktabasauti_decoder_close(d); d = NULL;
    }
    free(b);
    b = form_seq(&n, 1, 0, 0, 8, body, 8, (const uint32_t[]){ 0, 12 }, 1, 0);
    expect_refused("seq end past the waveform", b, n, MAKTABASAUTI_E_UNSUPPORTED_FORMAT); free(b);
    b = form_seq(&n, 1, 0, 0, 8, body, 8, (const uint32_t[]){ 1, 4 }, 1, 0);
    expect_refused("seq unaligned", b, n, MAKTABASAUTI_E_UNSUPPORTED_FORMAT); free(b);
    b = form_seq(&n, 1, 0, 0, 8, body, 8, (const uint32_t[]){ 4, 0 }, 1, 0);
    expect_refused("seq start after end", b, n, MAKTABASAUTI_E_UNSUPPORTED_FORMAT); free(b);
    b = form_seq(&n, 1, 0, 0, 8, body, 8, (const uint32_t[]){ 4, 4 }, 1, 0);
    expect_refused("seq empty sum", b, n, MAKTABASAUTI_E_UNSUPPORTED_FORMAT); free(b);
    b = form_seq(&n, 2, 0, 0, 2, body, 6, one, 1, 0);
    expect_refused("seq with ctOctave 2", b, n, MAKTABASAUTI_E_UNSUPPORTED_FEATURE); free(b);
    b = form_seq(&n, 1, 0, 0, 8, body, 8, one, 1, 0);
    CHECK(b && memcmp(b + 40, "SEQN", 4) == 0, "seq size field");
    if (b) { put32(b + 44, 4); expect_refused("seq size not multiple of 8", b, n, MAKTABASAUTI_E_UNSUPPORTED_FORMAT); }
    free(b);
    b = form_seq(&n, 1, 0, 0, 8, body, 8, one, 1, 1);
    if (b) {
        uint8_t *g = (uint8_t *)realloc(b, n + 16u); unsigned k;
        static const uint32_t extra[] = { 0, 4 };
        if (!g) { free(b); CHECK(0, "seq second realloc"); return; }
        b = g; memcpy(b + n, "SEQN", 4); put32(b + n + 4, 8);
        for (k = 0; k < 1u; k++) { put32(b + n + 8u + k * 8u, extra[2u * k]); put32(b + n + 12u + k * 8u, extra[2u * k + 1u]); }
        n += 16u; put32(b + 4, (uint32_t)(n - 8u));
        expect_refused("second SEQN", b, n, MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    }
    free(b);
}

/* FADE: jeden ULONG, numer petli od 1.  Probka 64 daje /128 = 0.5, a (R-i)/R dla R=4 to 1, 0.75, 0.5, 0.25. */
static uint8_t *form_fade(size_t *out_n, unsigned comp, int chan, const uint8_t *body, size_t body_n,
                          const uint32_t *pairs, unsigned npairs, int with_seq, uint32_t fade_loop) {
    const size_t seq_bytes = with_seq ? (size_t)npairs * 8u : 0u;
    const size_t n = 12u + 28u + (chan ? 12u : 0u) + (with_seq ? 8u + seq_bytes : 0u) + 12u + 8u + body_n + (body_n & 1u);
    uint8_t *b = (uint8_t *)calloc(1, n); size_t o = 12; unsigned i;
    if (!b) return NULL;
    memcpy(b, "FORM", 4); memcpy(b + 8, "8SVX", 4); put32(b + 4, (uint32_t)(n - 8u));
    memcpy(b + o, "VHDR", 4); put32(b + o + 4, 20); put32(b + o + 8, chan ? (uint32_t)(body_n / 2u) : (uint32_t)body_n);
    put32(b + o + 20, (uint32_t)8000u << 16);
    b[o + 22] = 1; b[o + 23] = (uint8_t)comp; put32(b + o + 24, 0x10000u); o += 28;
    if (chan) { memcpy(b + o, "CHAN", 4); put32(b + o + 4, 4); put32(b + o + 8, 6); o += 12; }
    if (with_seq) {
        memcpy(b + o, "SEQN", 4); put32(b + o + 4, (uint32_t)seq_bytes);
        for (i = 0; i < npairs; i++) { put32(b + o + 8u + (size_t)i * 8u, pairs[2u * i]); put32(b + o + 12u + (size_t)i * 8u, pairs[2u * i + 1u]); }
        o += 8u + seq_bytes;
    }
    memcpy(b + o, "FADE", 4); put32(b + o + 4, 4); put32(b + o + 8, fade_loop);
    o += 12;
    memcpy(b + o, "BODY", 4); put32(b + o + 4, (uint32_t)body_n); if (body_n) memcpy(b + o + 8, body, body_n);
    *out_n = n; return b;
}
static size_t find_tag(const uint8_t *b, size_t n, const char *tag) {
    size_t i;
    for (i = 0; i + 4u <= n; i++) if (memcmp(b + i, tag, 4) == 0) return i;
    return (size_t)-1;
}
static void expect_f32(const char *what, maktabasauti_decoder_t *d, const float *want, size_t nframes, unsigned ch) {
    float pcm[32]; size_t got = 0, i;
    CHECK(nframes * ch <= 32u, "%s: buffer", what);
    CHECK(maktabasauti_decoder_read(d, pcm, nframes, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == nframes, "%s: read", what);
    for (i = 0; i < got * ch && i < 32u; i++) CHECK(pcm[i] == want[i], "%s: sample %zu got %.9g want %.9g", what, i, (double)pcm[i], (double)want[i]);
}
static void check_fade(void) {
    uint8_t body[16], *b; size_t n = 0, got = 0, at; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info;
    float pcm[4]; const float u = 0.0078125f;
    static const uint32_t two[] = { 0, 4, 4, 8 };
    static const float loop2[] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.375f, 0.25f, 0.125f };
    static const float loop1[] = { 0.5f, 0.4375f, 0.375f, 0.3125f, 0.25f, 0.1875f, 0.125f, 0.0625f };
    static const float flat[] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    static const float ramp4[] = { 0.5f, 0.375f, 0.25f, 0.125f };
    memset(body, 0x40, sizeof body);
    b = form_fade(&n, 0, 0, body, 8, two, 2, 1, 2);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "fade loop 2 open");
    if (d) {
        expect_f32("fade loop 2", d, loop2, 8, 1);
        CHECK(maktabasauti_decoder_seek(d, 6, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "fade seek 6");
        CHECK(maktabasauti_decoder_read(d, pcm, 2, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 2 && pcm[0] == 0.25f && pcm[1] == 0.125f, "fade after seek");
        CHECK(maktabasauti_decoder_seek(d, 4, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "fade seek first of ramp");
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 1 && pcm[0] == 0.5f, "fade first sample stays full");
        CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "fade reset");
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 1 && pcm[0] == 0.5f, "fade reset sample");
        maktabasauti_decoder_close(d); d = NULL;
    }
    free(b);
    b = form_fade(&n, 0, 0, body, 8, two, 2, 1, 1);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "fade loop 1 open");
    if (d) { expect_f32("fade loop 1", d, loop1, 8, 1); maktabasauti_decoder_close(d); d = NULL; }
    free(b);
    b = form_fade(&n, 0, 0, body, 8, two, 2, 1, 0);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "fade 0 open");
    if (d) { expect_f32("fade 0", d, flat, 8, 1); maktabasauti_decoder_close(d); d = NULL; }
    free(b);
    b = form_fade(&n, 0, 0, body, 8, two, 2, 1, 3);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "fade past last loop open");
    if (d) { expect_f32("fade past last loop", d, flat, 8, 1); maktabasauti_decoder_close(d); d = NULL; }
    free(b);
    b = form_fade(&n, 0, 0, body, 4, NULL, 0, 0, 2);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 4, "fade without SEQN open");
    if (d) { expect_f32("fade without SEQN", d, flat, 4, 1); maktabasauti_decoder_close(d); d = NULL; }
    free(b);
    b = form_fade(&n, 0, 0, body, 8, (const uint32_t[]){ 4, 4, 0, 4 }, 2, 1, 1);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 4, "fade on empty loop open");
    if (d) { expect_f32("fade on empty loop", d, ramp4, 4, 1); maktabasauti_decoder_close(d); d = NULL; }
    free(b);
    {
        float st[16]; size_t i;
        for (i = 8; i < 16; i++) body[i] = 0x20;
        for (i = 0; i < 8; i++) { st[2u * i] = loop2[i]; st[2u * i + 1u] = loop2[i] * 0.5f; }
        b = form_fade(&n, 0, 1, body, 16, two, 2, 1, 2);
        CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.channels == 2 && info.total_frames == 8, "fade stereo open");
        if (d) { expect_f32("fade stereo", d, st, 8, 2); maktabasauti_decoder_close(d); d = NULL; }
        free(b);
    }
    memcpy(body, "\x00\x00\x99\x99\x99\x99", 6);
    b = form_fade(&n, 1, 0, body, 6, two, 2, 1, 2);
    CHECK(b && open_mem(b, n, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "fade fib open");
    if (d) {
        float fib[8];
        fib[0] = 1 * u; fib[1] = 2 * u; fib[2] = 3 * u; fib[3] = 4 * u;
        fib[4] = 5 * u; fib[5] = 6 * u * 0.75f; fib[6] = 7 * u * 0.5f; fib[7] = 8 * u * 0.25f;
        expect_f32("fade fib", d, fib, 8, 1);
        CHECK(maktabasauti_decoder_seek(d, 5, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "fade fib seek");
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 1 && pcm[0] == fib[5], "fade fib after seek");
        maktabasauti_decoder_close(d); d = NULL;
    }
    free(b);
    memset(body, 0x40, sizeof body);
    b = form_fade(&n, 0, 0, body, 16, (const uint32_t[]){ 0, 4, 4, 12 }, 2, 1, 2);
    CHECK(b && n > 8u && open_mem(b, n - 8u, &d, &info) == MAKTABASAUTI_OK && info.total_frames == 8, "fade truncated open");
    if (d) {
        expect_f32("fade truncated", d, loop2, 8, 1);
        CHECK(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_E_TRUNCATED && got == 0, "fade truncated sticky");
        maktabasauti_decoder_close(d); d = NULL;
    }
    free(b);
    b = form_fade(&n, 0, 0, body, 8, two, 2, 1, 2);
    at = b ? find_tag(b, n, "FADE") : (size_t)-1;
    CHECK(at != (size_t)-1, "fade tag");
    if (b && at != (size_t)-1) { put32(b + at + 4, 0); expect_refused("fade size 0", b, n, MAKTABASAUTI_E_UNSUPPORTED_FORMAT); }
    free(b);
    b = form_fade(&n, 0, 0, body, 8, two, 2, 1, 2);
    at = b ? find_tag(b, n, "FADE") : (size_t)-1;
    if (b && at != (size_t)-1) { put32(b + at + 4, 8); expect_refused("fade size 8", b, n, MAKTABASAUTI_E_UNSUPPORTED_FORMAT); }
    free(b);
    b = form_fade(&n, 0, 0, body, 8, two, 2, 1, 2);
    if (b) {
        uint8_t *g = (uint8_t *)realloc(b, n + 12u);
        if (!g) { free(b); CHECK(0, "fade second realloc"); return; }
        b = g; memcpy(b + n, "FADE", 4); put32(b + n + 4, 4); put32(b + n + 8, 1);
        n += 12u; put32(b + 4, (uint32_t)(n - 8u));
        expect_refused("second FADE", b, n, MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    }
    free(b);
}

int main(void) {
    static const Fx fx[] = { { "svx_fib_mono", 1, 22050, 18000 }, { "svx_exp_mono", 1, 11025, 6000 }, { "svx_fib_stereo", 2, 44100, 12000 }, { "svx_pcm_stereo", 2, 8363, 5001 },
                              { "svx_wavepak_fib_mono", 1, 22050, 11800 }, { "svx_wavepak_exp_mono", 1, 22050, 11800 }, { "svx_wavepak_fib_stereo", 2, 44100, 10000 }, { "svx_wavepak_exp_stereo", 2, 44100, 10000 } };
    size_t k;
    for (k = 0; k < sizeof fx / sizeof fx[0]; k++) check_fixture(&fx[k]);
    check_octaves();
    check_seqn();
    check_fade();
    if (g_failures) { printf("test_svx: %d FAILURES\n", g_failures); return 1; }
    printf("test_svx: all passed\n");
    return 0;
}
