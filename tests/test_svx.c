/* Amiga IFF 8SVX (PCM, Fibonacci delta, exponential delta; mono and stereo) decoded through the engine.  The expected PCM of tests/data/svx_*.pcm comes from FFmpeg (tests/data/make_svx_fixtures.py): it must be
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
        if (pcm[i] != (float)g * (1.0f / 32768.0f)) { CHECK(0, "%s: sample %zu: %.9g, FFmpeg %d", x->name, i, (double)pcm[i], g); break; }
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

int main(void) {
    static const Fx fx[] = { { "svx_fib_mono", 1, 22050, 18000 }, { "svx_exp_mono", 1, 11025, 6000 }, { "svx_fib_stereo", 2, 44100, 12000 }, { "svx_pcm_stereo", 2, 8363, 5001 } };
    size_t k;
    for (k = 0; k < sizeof fx / sizeof fx[0]; k++) check_fixture(&fx[k]);
    if (g_failures) { printf("test_svx: %d FAILURES\n", g_failures); return 1; }
    printf("test_svx: all passed\n");
    return 0;
}
