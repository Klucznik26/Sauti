/* Atari Falcon DVSM Pack 5 (ADPCM = IMA, stereo 16-bit, one byte per frame: left high nibble, right low nibble, no blocks, state from zero) and Pack 4 (Voice, stereo, blocks with 16-bit seeds and
 * one 4-bit code per channel per frame) decoded through the engine.  tests/data/dvsm_adpcm.* and dvsm_voice.* come from the ORIGINAL decoder routines of WinRec Pro 2.11H run in an M68K emulation
 * (tests/data/make_dvsm_adpcm_fixture.py): the PCM must be reproduced bit for bit, also after seeks and with reads of any size.  Mono / 8-bit ADPCM (no such variant in WinRec), a PACK table with Pack 5 and unknown methods are refused, not guessed. */
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
static float *read_all(maktabasauti_decoder_t *d, size_t chunk, size_t *frames_out) {
    float *pcm = NULL, *buf = (float *)malloc(chunk * 2u * sizeof(float)); size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, chunk, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * 2u * sizeof(float)); } memcpy(pcm + total * 2u, buf, got * 2u * sizeof(float)); total += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    free(buf); *frames_out = total; return pcm;
}
static void expect_refused(const char *what, const uint8_t *data, size_t n) {
    maktabasauti_decoder_t *d; const maktabasauti_result_t r = open_mem(data, n, &d, NULL);
    CHECK(r != MAKTABASAUTI_OK, "%s: opened, but must be refused", what);
    if (r == MAKTABASAUTI_OK) maktabasauti_decoder_close(d);
}

typedef struct { const char *dvs, *pcm; uint64_t frames; unsigned rate; const uint64_t *seeks; size_t nseeks; int refusals; } Fx;
static void check_fixture(const Fx *x) {
    size_t n, npcm, frames, frames2, i, k; uint8_t *dvs = slurp(x->dvs, &n), *gold = slurp(x->pcm, &npcm);
    maktabasauti_decoder_t *d; maktabasauti_stream_info_t info; float *pcm, *pcm2;
    if (!dvs || !gold) { CHECK(0, "cannot read %s / %s (run from the repository root)", x->dvs, x->pcm); free(dvs); free(gold); return; }
    CHECK(open_mem(dvs, n, &d, &info) == MAKTABASAUTI_OK, "%s: open", x->dvs);
    if (!d) { free(dvs); free(gold); return; }
    CHECK(info.channels == 2 && info.sample_rate == x->rate && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s: %u ch %u Hz", x->dvs, (unsigned)info.channels, (unsigned)info.sample_rate);
    pcm = read_all(d, 4096, &frames);
    CHECK(pcm && frames == npcm / 4u && frames == x->frames, "%s: %zu frames (expected %llu)", x->dvs, frames, (unsigned long long)x->frames);
    for (i = 0; pcm && i < frames * 2u && i < npcm / 2u; i++) {
        const int16_t g = (int16_t)(((unsigned)gold[2u * i] << 8) | gold[2u * i + 1u]);
        if (pcm[i] != (float)g * (1.0f / 32768.0f)) { CHECK(0, "%s: sample %zu: %.9g, WinRec %d", x->dvs, i, (double)pcm[i], g); break; }
    }
    maktabasauti_decoder_close(d);
    for (k = 0; k < 3; k++) {                                                                                   /* reads of other sizes */
        static const size_t chunks[3] = { 1, 333, 1000 };
        CHECK(open_mem(dvs, n, &d, NULL) == MAKTABASAUTI_OK, "reopen");
        pcm2 = d ? read_all(d, chunks[k], &frames2) : NULL;
        CHECK(pcm && pcm2 && frames2 == frames && memcmp(pcm, pcm2, frames * 2u * sizeof(float)) == 0, "%s: reads of %zu frames give other PCM", x->dvs, chunks[k]);
        free(pcm2); if (d) maktabasauti_decoder_close(d);
    }
    if (pcm && open_mem(dvs, n, &d, NULL) == MAKTABASAUTI_OK) {                                                  /* exact seeks (checkpoints, block starts) */
        float a[2 * 100];
        for (i = 0; i < x->nseeks; i++) {
            size_t got, want = x->seeks[i] + 100u <= frames ? 100u : (size_t)(frames - x->seeks[i]);
            CHECK(maktabasauti_decoder_seek(d, x->seeks[i], MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seek %llu", (unsigned long long)x->seeks[i]);
            (void)maktabasauti_decoder_read(d, a, want, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == want && memcmp(a, pcm + x->seeks[i] * 2u, want * 2u * sizeof(float)) == 0, "%s: seek to %llu differs from sequential decoding", x->dvs, (unsigned long long)x->seeks[i]);
        }
        maktabasauti_decoder_close(d);
    }
    free(pcm);
    if (x->refusals) {                                                                                         /* mono, 8-bit, a PACK table, an unknown method */
        uint8_t *m = (uint8_t *)malloc(n + 600u); size_t head = 16u;
        memcpy(m, dvs, n);
        m[11] = 3; expect_refused("mono ADPCM", m, n);
        m[11] = 0; expect_refused("8-bit ADPCM", m, n);
        m[11] = 1; m[10] = 3; expect_refused("unknown method 3", m, n);
        m[10] = 6; expect_refused("unknown method 6", m, n);
        m[10] = 5;
        memcpy(m, dvs, 16u); m[6] = (uint8_t)((head + 518u) >> 8); m[7] = (uint8_t)(head + 518u);                    /* a PACK cookie (6 + 512 bytes) before the data */
        memcpy(m + 16, "PACK", 4); m[20] = (uint8_t)((6 + 512) >> 8); m[21] = (uint8_t)(6 + 512);
        memset(m + 22, 0, 512); memcpy(m + 16 + 518, dvs + 16, n - 16u);
        expect_refused("Pack 5 with a PACK table", m, n + 518u);
        free(m);
    }
    free(dvs); free(gold);
}

int main(void) {
    static const uint64_t adpcm_seeks[] = { 1, 777, 4500, 8999, 1234, 0, 8192, 4096, 8191, 12, 6000 };        /* the engine keeps a checkpoint every 4096 frames: seeks before / after / exactly at them */
    static const uint64_t voice_seeks[] = { 1000, 1001, 1002, 3303, 2002, 0, 3003, 500, 2001 };               /* block starts and their neighbours (1001 frames per full block, the last one 301) */
    static const uint64_t delta_seeks[] = { 500, 501, 502, 1002, 2672 - 1, 0, 1503, 250 };                    /* 501 frames per full block, the last one 167 */
    static const uint64_t stress_seeks[] = { 1000, 1001, 1002, 5337, 2002, 0, 4004, 500 };                    /* 1001 frames per full block, the last one 333 */
    static const uint64_t real2_seeks[] = { 38399, 38400, 38401, 76799, 1, 0, 19200, 4096, 50000 };            /* WinRec-written Pack 2: 38401 frames per full block */
    static const uint64_t real4_seeks[] = { 38399, 38400, 38401, 76800, 115199, 1, 0, 60000, 4096 };          /* WinRec-written Pack 4: 38400 frames per block */
    static const Fx fx[] = { { "tests/data/dvsm_adpcm.dvs", "tests/data/dvsm_adpcm.pcm", 9000, 49170, adpcm_seeks, sizeof adpcm_seeks / sizeof adpcm_seeks[0], 1 },
                             { "tests/data/dvsm_voice.dvs", "tests/data/dvsm_voice.pcm", 3304, 49170, voice_seeks, sizeof voice_seeks / sizeof voice_seeks[0], 0 },
                             { "tests/data/dvsm_delta_stress.dvs", "tests/data/dvsm_delta_stress.pcm", 2672, 49170, delta_seeks, sizeof delta_seeks / sizeof delta_seeks[0], 0 },
                             { "tests/data/dvsm_voice_stress.dvs", "tests/data/dvsm_voice_stress.pcm", 5338, 49170, stress_seeks, sizeof stress_seeks / sizeof stress_seeks[0], 0 },
                             { "tests/data/dvsm_winrec_real_pack2.dvs", "tests/data/dvsm_winrec_real_pack2.pcm", 76800, 49170, real2_seeks, sizeof real2_seeks / sizeof real2_seeks[0], 0 },
                             { "tests/data/dvsm_winrec_real_pack4.dvs", "tests/data/dvsm_winrec_real_pack4.pcm", 115200, 49170, real4_seeks, sizeof real4_seeks / sizeof real4_seeks[0], 0 } };
    size_t k;
    for (k = 0; k < sizeof fx / sizeof fx[0]; k++) check_fixture(&fx[k]);
    if (g_failures) { printf("test_dvsm_winrec: %d FAILURES\n", g_failures); return 1; }
    printf("test_dvsm_winrec: all passed\n");
    return 0;
}
