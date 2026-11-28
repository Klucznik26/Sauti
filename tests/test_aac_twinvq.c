/* TwinVQ (MPEG-4 Audio object type 7, base layer): sauti_aac_twinvq and its use through the engine (MP4).  The fixtures in tests/data/twinvq/ are our own data: access units written by
 * tools/twinvq_gen.c (random but plausible values of every field of the syntax: window sequences, long term prediction of long and short windows, TNS, band limiting, pitch, stereo masks;
 * seeds in the table below) and the PCM that the ISO/IEC 14496-5 reference decoder produces from them, rounded to 16 bit; the decoder must agree to half a step (the rounding of the golden data).  The same agreement (0.002 step,
 * 135 - 140 dB) holds for the seven conformance streams ac08 .. ac20 of ISO/IEC 14496-26 (not in the repository; see docs/AAC_COMPLETENESS_PLAN.md).  Besides the fixtures: copies of the
 * decoder state, atomic refusal of damaged units, parameter checks and a sweep of random units (ASan / UBSan). */
#include "sauti_aac_twinvq.h"
#include "maktabasauti/maktabasauti.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(c, ...) do { if (!(c)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { const char *name; unsigned rate_index, channels, block; } Fixture;
/* name: tools/twinvq_gen SEED RATE_INDEX CHANNELS BLOCK AU_BYTES FRAMES: m22k 1 7 1 1024 94 20, m12k 3 9 1 1024 85 20, s44k 5 4 2 1024 160 16, m88k_960 4 1 1 960 21 20, m8k_960 9 11 1 960 60 20, s24k 10 6 2 1024 90 16, m8k 11 11 1 1024 74 24 (8 kHz at the width where the pitch branches of 3 and 4 differ) */
static const Fixture fixtures[] = {
    { "m22k", 7, 1, 1024 }, { "m12k", 9, 1, 1024 }, { "m8k", 11, 1, 1024 }, { "s44k", 4, 2, 1024 }, { "m88k_960", 1, 1, 960 }, { "m8k_960", 11, 1, 960 }, { "s24k", 6, 2, 1024 },
};

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *b; long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET); b = (uint8_t *)malloc((size_t)len + 1u);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)len; return b;
}
/* the access units of an .aus file: 4-byte big-endian length + bytes */
static size_t split(const uint8_t *d, size_t n, const uint8_t **au, size_t *len, size_t max) {
    size_t at = 0, k = 0;
    while (at + 4u <= n && k < max) {
        const size_t l = ((size_t)d[at] << 24) | ((size_t)d[at + 1] << 16) | ((size_t)d[at + 2] << 8) | d[at + 3];
        if (at + 4u + l > n) break;
        au[k] = d + at + 4u; len[k] = l; k++; at += 4u + l;
    }
    return k;
}
static double worst_step(const float *pcm, const int16_t *gold, size_t n) {
    double w = 0;
    for (size_t i = 0; i < n; i++) { const double e = fabs((double)pcm[i] * 32768.0 - (double)gold[i]); if (e > w) w = e; }
    return w;
}

static void test_fixtures(void) {
    for (size_t f = 0; f < sizeof fixtures / sizeof fixtures[0]; f++) {
        char path[256]; size_t na, ng, k, nau; const uint8_t *au[64]; size_t len[64];
        snprintf(path, sizeof path, "tests/data/twinvq/twinvq_%s.aus", fixtures[f].name);
        uint8_t *aus = slurp(path, &na);
        snprintf(path, sizeof path, "tests/data/twinvq/twinvq_%s.s16", fixtures[f].name);
        int16_t *gold = (int16_t *)slurp(path, &ng);
        if (!aus || !gold) { CHECK(0, "%s: fixtures missing (run from the repository root)", fixtures[f].name); free(aus); free(gold); continue; }
        nau = split(aus, na, au, len, 64);
        const Fixture *x = &fixtures[f];
        const size_t per = (size_t)x->block * x->channels;
        CHECK(nau * per * 2u == ng, "%s: %zu units, %zu golden bytes", x->name, nau, ng);
        SautiAacTwinVq *t = sauti_aac_twinvq_new(x->rate_index, x->channels, x->block, len[0]);
        float *pcm = (float *)malloc(per * sizeof(float));
        double worst = 0;
        CHECK(t != NULL, "%s: new", x->name);
        for (k = 0; t && k < nau; k++) {
            SautiAacReason why;
            if (!sauti_aac_twinvq_decode(t, au[k], len[k], pcm, &why)) { CHECK(0, "%s: unit %zu refused", x->name, k); break; }
            const double w = worst_step(pcm, gold + k * per, per);
            if (w > worst) worst = w;
            CHECK(w <= 0.502, "%s: unit %zu differs from the reference decoder by %.3f steps", x->name, k, w);
        }
        printf("TwinVQ %-9s %zu units, %u ch, %u lines: worst difference to the reference decoder %.4f of a 16-bit step\n", x->name, nau, x->channels, x->block, worst);
        /* a copy of the state in the middle continues like the original */
        if (t && nau > 6u) {
            SautiAacTwinVq *a = sauti_aac_twinvq_new(x->rate_index, x->channels, x->block, len[0]), *b;
            float *p2 = (float *)malloc(per * sizeof(float));
            SautiAacReason why;
            for (k = 0; k < 5u; k++) (void)sauti_aac_twinvq_decode(a, au[k], len[k], pcm, &why);
            b = sauti_aac_twinvq_dup(a);
            CHECK(b != NULL, "dup");
            for (k = 5; b && k < nau; k++) {
                CHECK(sauti_aac_twinvq_decode(a, au[k], len[k], pcm, &why) && sauti_aac_twinvq_decode(b, au[k], len[k], p2, &why), "%s: continued decode", x->name);
                CHECK(memcmp(pcm, p2, per * sizeof(float)) == 0, "%s: copy differs at unit %zu", x->name, k);
            }
            /* a damaged unit is refused and changes nothing */
            if (b) {
                SautiAacTwinVq *c = sauti_aac_twinvq_dup(a);
                CHECK(!sauti_aac_twinvq_decode(c, au[0], 3u, p2, &why), "%s: a 3-byte unit must be refused", x->name);
                sauti_aac_twinvq_free(c);
            }
            sauti_aac_twinvq_free(a); sauti_aac_twinvq_free(b); free(p2);
        }
        sauti_aac_twinvq_free(t); free(pcm); free(aus); free(gold);
    }
}

/* atomic refusal: a unit that cannot be decoded leaves the state as it was */
static void test_atomic(void) {
    size_t na, ng; const uint8_t *au[64]; size_t len[64];
    uint8_t *aus = slurp("tests/data/twinvq/twinvq_s44k.aus", &na);
    int16_t *gold = (int16_t *)slurp("tests/data/twinvq/twinvq_s44k.s16", &ng);
    if (!aus || !gold) { CHECK(0, "fixtures missing"); free(aus); free(gold); return; }
    const size_t nau = split(aus, na, au, len, 64), per = 2048u;
    SautiAacTwinVq *t = sauti_aac_twinvq_new(4, 2, 1024, len[0]), *clone;
    float pcm[2048], pcm2[2048];
    SautiAacReason why;
    unsigned refused = 0;
    for (size_t k = 0; t && k < nau; k++) {
        clone = sauti_aac_twinvq_dup(t);
        /* units too short for their side information are refused; a refusal must not touch the state (any longer unit is a valid frame of another size: the fields have fixed widths) */
        static const size_t tiny[] = { 1u, 5u, 12u, 30u };
        for (size_t i = 0; i < sizeof tiny / sizeof tiny[0]; i++)
            if (!sauti_aac_twinvq_decode(t, au[k], tiny[i], pcm2, &why)) refused++;
            else { sauti_aac_twinvq_free(t); t = sauti_aac_twinvq_dup(clone); }          /* decoded as a frame of its own: go back to the copy */
        CHECK(sauti_aac_twinvq_decode(t, au[k], len[k], pcm, &why), "unit %zu", k);
        CHECK(sauti_aac_twinvq_decode(clone, au[k], len[k], pcm2, &why) && memcmp(pcm, pcm2, per * sizeof(float)) == 0, "unit %zu: the state changed by a refused unit", k);
        CHECK(worst_step(pcm, gold + k * per, per) <= 0.502, "unit %zu after refused units", k);
        sauti_aac_twinvq_free(clone);
    }
    CHECK(refused > 0, "no unit was refused");
    sauti_aac_twinvq_free(t); free(aus); free(gold);
    /* parameters */
    CHECK(sauti_aac_twinvq_new(13, 1, 1024, 100) == NULL, "rate index 13");
    CHECK(sauti_aac_twinvq_new(7, 0, 1024, 100) == NULL, "0 channels");
    CHECK(sauti_aac_twinvq_new(7, 3, 1024, 100) == NULL, "3 channels");
    CHECK(sauti_aac_twinvq_new(7, 1, 512, 100) == NULL, "512 lines");
    CHECK(sauti_aac_twinvq_new(7, 1, 1024, 2) == NULL, "2-byte unit");
}

/* random units of every size: no crash, no overflow (ASan / UBSan), the state stays usable */
static void test_random(void) {
    static const unsigned cfg[][3] = { { 7, 1, 1024 }, { 3, 2, 1024 }, { 1, 1, 960 }, { 11, 2, 960 }, { 9, 1, 1024 } };
    srand(7);
    for (size_t c = 0; c < sizeof cfg / sizeof cfg[0]; c++) {
        unsigned ok = 0, refused = 0;
        SautiAacTwinVq *t = sauti_aac_twinvq_new(cfg[c][0], cfg[c][1], cfg[c][2], 60);
        float *pcm = (float *)malloc(sizeof(float) * 2048u);
        for (unsigned i = 0; t && i < 3000u; i++) {
            uint8_t au[400]; const unsigned l = 2u + (unsigned)rand() % 398u; SautiAacReason why;
            for (unsigned j = 0; j < l; j++) au[j] = (uint8_t)(rand() >> 5);
            if (sauti_aac_twinvq_decode(t, au, l, pcm, &why)) ok++; else refused++;
        }
        CHECK(t != NULL && ok + refused == 3000u, "config %zu", c);
        printf("TwinVQ random units, config %zu: %u decoded, %u refused\n", c, ok, refused);
        sauti_aac_twinvq_free(t); free(pcm);
    }
}

/* through the engine: an MP4 with the AudioSpecificConfig of object type 7 */
static void test_engine(void) {
    maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; size_t ng;
    int16_t *gold = (int16_t *)slurp("tests/data/twinvq/twinvq_m22k.s16", &ng);
    size_t nfile; uint8_t *file = slurp("tests/data/twinvq/twinvq_m22k.mp4", &nfile);
    float *pcm = (float *)malloc(20u * 1024u * sizeof(float)); size_t got = 0, total = 0; maktabasauti_result_t r;
    if (!gold || !file || !pcm) { CHECK(0, "mp4 fixture missing"); free(gold); free(file); free(pcm); return; }
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
    CHECK(maktabasauti_io_from_memory(file, nfile, NULL, &p.io) == MAKTABASAUTI_OK, "io");
    r = maktabasauti_decoder_open(&p, &d);
    CHECK(r == MAKTABASAUTI_OK, "open %d", (int)r);
    if (d) {
        info.struct_size = sizeof info; (void)maktabasauti_decoder_get_info(d, &info);
        CHECK(info.channels == 1u && info.sample_rate == 22050u && !strcmp(info.codec_profile, "TwinVQ"), "info: %u ch %u Hz %s", (unsigned)info.channels, (unsigned)info.sample_rate, info.codec_profile);
        for (;;) {
            r = maktabasauti_decoder_read(d, pcm + total, 1024, MAKTABASAUTI_FORMAT_F32, &got);
            total += got;
            if (r != MAKTABASAUTI_OK) { CHECK(r == MAKTABASAUTI_EOF, "read %d", (int)r); break; }
        }
        CHECK(total == 20u * 1024u, "%zu frames", total);
        if (total == 20u * 1024u) CHECK(worst_step(pcm, gold, total) <= 0.502, "engine PCM differs from the reference decoder by %.3f steps", worst_step(pcm, gold, total));
        { uint64_t actual = 0; float buf[1024];
          CHECK(maktabasauti_decoder_seek(d, 7u * 1024u + 100u, 0u, &actual) == MAKTABASAUTI_OK && actual == 7u * 1024u + 100u, "seek");
          CHECK(maktabasauti_decoder_read(d, buf, 1024, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 1024u, "read after seek");
          if (got == 1024u) CHECK(worst_step(buf, gold + 7u * 1024u + 100u, 1024u) <= 0.502, "PCM after seek differs (LTP and overlap history rebuilt from the start)"); }
        maktabasauti_decoder_close(d);
    }
    free(gold); free(file); free(pcm);
}

int main(void) {
    test_fixtures();
    test_atomic();
    test_random();
    test_engine();
    if (g_failures) { printf("%d failure(s)\n", g_failures); return 1; }
    puts("PASS TwinVQ: fixtures vs the reference decoder, copies, atomic refusal, random units, engine and seek");
    return 0;
}
