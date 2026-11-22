/* USAC (xHE-AAC) AudioPreRoll (usacExtElementType 3, the first element of a random access access unit): the preceding access units are carried inside the first one so that a decoder that starts there
 * can prime its state (overlap, arithmetic coder context, noise generator, eSBR history ...) before the first PCM block is produced.  Real encoders (libxaac) put them into the first access unit
 * of every stream; without them the noise sequence of the eSBR chain starts at the wrong phase (4.7 dB against the reference decoders on a white noise stream, 79 dB with them).
 *   1. the configuration: the EXT element comes first;
 *   2. stream B (pre-roll 1..4) decodes to exactly the PCM of stream A (no pre-roll) from the access unit the pre-roll ends at, bit for bit: mono, stereo, eSBR mono, eSBR stereo;
 *   3. pre-roll payloads in the middle of a stream (random access points) are ignored by sequential decoding (bit for bit the same PCM as without them), and starting at one converges;
 *   4. damaged payloads: truncations and bit flips of the first access unit leave state and output alone, a pre-roll whose first access unit is not independent is refused;
 *   5. the engine: MP4 with pre-roll equals the direct decode, seeks (to the start: primed again; over checkpoints: already primed) give the sequential PCM. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "sauti_usac.h"
#include "test_usac_pr_vectors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

typedef struct { const char *name; const uint8_t *asc; size_t asc_n; const uint8_t *const *au; const size_t *au_n; size_t frames; } Stream;
#define ST(n) { "usac_" #n, usac_##n##_asc, sizeof usac_##n##_asc, usac_##n##_aus, usac_##n##_n, sizeof usac_##n##_n / sizeof usac_##n##_n[0] }

/* decodes every access unit; returns the PCM (frames * out_frame_len * channels floats) */
static float *decode_stream(const Stream *s, SautiUsacConfig *cfg, size_t *samples) {
    static SautiUsacState st; float *pcm, *one; size_t f, per; SautiAacReason why;
    if (sauti_usac_asc_parse(s->asc, s->asc_n, cfg) != 1) { CHECK(0, "%s: asc", s->name); return NULL; }
    sauti_usac_state_init(&st, cfg);
    per = (size_t)cfg->out_frame_len * cfg->out_channels;
    pcm = (float *)malloc(s->frames * per * sizeof(float)); one = (float *)malloc(per * sizeof(float));
    for (f = 0; f < s->frames; f++) {
        const int r = sauti_usac_decode(cfg, &st, s->au[f], s->au_n[f], one, &why);
        if (r != 1) { CHECK(0, "%s frame %zu: result %d reason %d", s->name, f, r, (int)why); free(pcm); free(one); return NULL; }
        memcpy(pcm + f * per, one, per * sizeof(float));
    }
    free(one); *samples = s->frames * per; return pcm;
}

static void test_pair(const Stream *a, const Stream *b, unsigned pre, unsigned channels) {
    SautiUsacConfig ca, cb; size_t na = 0, nb = 0, per; float *pa = decode_stream(a, &ca, &na), *pb = decode_stream(b, &cb, &nb);
    CHECK(ca.num_elements == 1u && cb.num_elements == ca.num_elements + 1u && cb.e[0].type == SAUTI_USAC_EXT && cb.e[0].ext_type == 3u && ca.out_channels == channels && cb.out_channels == channels,
          "%s: elements %u (A) / %u (B), first type %u ext %u", b->name, ca.num_elements, cb.num_elements, cb.e[0].type, cb.e[0].ext_type);
    CHECK(b->au_n[0] > a->au_n[pre] + 30u, "%s: the first access unit carries the pre-roll (%zu vs %zu bytes)", b->name, b->au_n[0], a->au_n[pre]);
    if (pa && pb) {
        per = (size_t)ca.out_frame_len * channels;
        CHECK(na == (b->frames + pre) * per && nb == b->frames * per, "%s: %zu / %zu samples", b->name, na, nb);
        CHECK(memcmp(pa + (size_t)pre * per, pb, nb * sizeof(float)) == 0, "%s: PCM differs from %s from access unit %u on", b->name, a->name, pre);
        CHECK(memcmp(pa, pb, per * sizeof(float)) != 0, "%s: the first block must not equal the first block without pre-roll (priming has an effect)", b->name);
    }
    free(pa); free(pb);
}

static void test_rap(void) {
    static const Stream ra = ST(qra), rb = ST(qrb);
    SautiUsacConfig ca, cb; size_t na = 0, nb = 0; float *pa = decode_stream(&ra, &ca, &na), *pb = decode_stream(&rb, &cb, &nb);
    size_t i; unsigned bigger = 0;
    for (i = 0; i < ra.frames; i++) if (rb.au_n[i] > ra.au_n[i] + 30u) bigger++;
    CHECK(bigger == 4u, "%u access units carry an extra payload (expected every 8th after the first)", bigger);
    CHECK(pa && pb && na == nb && memcmp(pa, pb, na * sizeof(float)) == 0, "sequential decoding must ignore the pre-roll payloads of random access points");
    if (pb) {                                                                                               /* start at the random access point 16: the pre-roll primes, the output converges to the sequential one */
        static SautiUsacState st; float *one = (float *)malloc((size_t)cb.out_frame_len * sizeof(float)); SautiAacReason why; size_t k; double snr_last = 0;
        sauti_usac_state_init(&st, &cb);
        for (k = 16; k < 26; k++) {
            double s = 0, e = 0; size_t j; const float *ref = pb + k * cb.out_frame_len;
            CHECK(sauti_usac_decode(&cb, &st, rb.au[k], rb.au_n[k], one, &why) == 1, "decoding from the random access point: block %zu", k);
            for (j = 0; j < cb.out_frame_len; j++) { s += (double)ref[j] * ref[j]; e += ((double)ref[j] - one[j]) * ((double)ref[j] - one[j]); }
            if (k == 16) CHECK(e < s, "the first block after the random access point is closer to the sequential PCM than silence");
            snr_last = e > 0 ? 10.0 * log10(s / e) : 999.0;
        }
        CHECK(snr_last > 20.0, "converged after 10 blocks: %.1f dB", snr_last);
        free(one);
    }
    free(pa); free(pb);
}

static void test_damage(const Stream *s, unsigned pre) {
    static SautiUsacConfig cfg; static SautiUsacState st, before; static float pcm[8 * 2048], keep[8 * 2048]; SautiAacReason why; size_t len, i; int round; long accepted = 0, rejected = 0; uint8_t b[16384];
    CHECK(sauti_usac_asc_parse(s->asc, s->asc_n, &cfg) == 1, "asc");
    sauti_usac_state_init(&st, &cfg);
    for (i = 0; i < sizeof pcm / sizeof pcm[0]; i++) keep[i] = pcm[i] = 31337.0f;
    for (len = 0; len <= s->au_n[0]; len += (len < 48u || s->au_n[0] - len < 48u) ? 1u : 5u) {                 /* the first access unit: the pre-roll payload is inside it */
        int r; before = st; memcpy(pcm, keep, sizeof pcm);
        r = sauti_usac_decode(&cfg, &st, s->au[0], len, pcm, &why);
        if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s len %zu: failure (%d) must leave state and output alone", s->name, len, r); rejected++; }
        else { st = before; accepted++; }
    }
    for (round = 0; round < 200; round++) {
        int r, nflip = 1 + (int)(rnd() % 3u);
        memcpy(b, s->au[0], s->au_n[0]);
        while (nflip--) { const size_t at = rnd() % s->au_n[0]; b[at] ^= (uint8_t)(1u << (rnd() & 7u)); }
        before = st; memcpy(pcm, keep, sizeof pcm);
        r = sauti_usac_decode(&cfg, &st, b, s->au_n[0], pcm, &why);
        if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s flip: failure (%d) must leave state and output alone", s->name, r); rejected++; }
        else { st = before; accepted++; }
    }
    printf("%s: %ld accepted, %ld rejected mutated or truncated first access units\n", s->name, accepted, rejected);
    CHECK(rejected > 100, "%s: mutations never fail?", s->name);
    /* the first pre-roll access unit must be independent: clear its independence flag.  Layout of the payload of a generated stream: usacIndependencyFlag (1), present (1), default length (1),
     * length (8 bits, or 8 + 16 from 255), then the payload: configLen (4), applyCrossfade (1), reserved (1), numPreRollFrames (2), auLen (16), the access unit (its first bit: the independence flag) */
    memcpy(b, s->au[0], s->au_n[0]);
    { const size_t base = 3u + (s->au_n[0] < 280u ? 8u : 24u), indep_bit = base + 4u + 1u + 1u + (pre < 3u ? 2u : 6u) + 16u;      /* numPreRollFrames = escapedValue(2, 4, 0): 2 bits, 6 from three frames */
      CHECK((b[indep_bit >> 3] >> (7u - (indep_bit & 7u))) & 1u, "%s: the first pre-roll access unit is independent in the fixture", s->name);
      memcpy(b, s->au[0], s->au_n[0]);                                                                       /* a valid pre-roll access unit the decoder cannot decode (here: the stream configured with MPEG Surround 212 and phase coding, not implemented): the access unit is unsupported (-1), not malformed */
      { SautiUsacConfig cfg4 = cfg; unsigned ei;
        cfg4.sbr_mode = 1u;
        for (ei = 0; ei < cfg4.num_elements; ei++) { cfg4.e[ei].stereo_config_index = 3u; }                 /* stereoConfigIndex 3 (pseudo-LR, eSBR after the upmix): not implemented */
        sauti_usac_state_init(&st, &cfg4); before = st;
        CHECK(sauti_usac_decode(&cfg4, &st, b, s->au_n[0], pcm, &why) == -1 && memcmp(&before, &st, sizeof st) == 0, "%s: an unsupported pre-roll frame makes the access unit unsupported", s->name);
        memcpy(b, s->au[0], s->au_n[0]); }
      { size_t k, lb = indep_bit - 16u;                                                                      /* auLen (16 bits) = 65535: longer than the payload, refused without reading past it */
        memcpy(b, s->au[0], s->au_n[0]);
        for (k = 0; k < 16u; k++) b[(lb + k) >> 3] |= (uint8_t)(0x80u >> ((lb + k) & 7u));
        sauti_usac_state_init(&st, &cfg); before = st;
        CHECK(sauti_usac_decode(&cfg, &st, b, s->au_n[0], pcm, &why) == 0 && memcmp(&before, &st, sizeof st) == 0, "%s: a pre-roll access unit longer than the payload is refused", s->name);
        memcpy(b, s->au[0], s->au_n[0]); }
      b[indep_bit >> 3] &= (uint8_t)~(0x80u >> (indep_bit & 7u));
      sauti_usac_state_init(&st, &cfg); before = st;
      CHECK(sauti_usac_decode(&cfg, &st, b, s->au_n[0], pcm, &why) == 0 && memcmp(&before, &st, sizeof st) == 0, "%s: a pre-roll whose first access unit is not independent is refused", s->name); }
}

/* ---- the engine ------------------------------------------------------------------------------------------------------------------------------------------------------------------ */
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *b; long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET); b = (uint8_t *)malloc((size_t)len + 1u);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)len; return b;
}
static maktabasauti_decoder_t *open_mem(const uint8_t *data, size_t n, maktabasauti_stream_info_t *info) {
    maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
    if (maktabasauti_io_from_memory(data, n, NULL, &p.io) != MAKTABASAUTI_OK) return NULL;
    r = maktabasauti_decoder_open(&p, &d);
    if (r != MAKTABASAUTI_OK) { p.io.close(p.io.user); return NULL; }
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
static void test_engine(const char *path, const Stream *s, unsigned seek_blocks) {
    SautiUsacConfig cfg; size_t n, direct_n = 0, frames = 0, got; uint8_t *data = slurp(path, &n); maktabasauti_stream_info_t info; maktabasauti_decoder_t *d; float *eng, *direct = NULL; unsigned ch;
    if (!data) { CHECK(0, "cannot read %s (run from the repository root)", path); return; }
    direct = decode_stream(s, &cfg, &direct_n); ch = cfg.out_channels;
    d = open_mem(data, n, &info);
    CHECK(d != NULL && info.channels == ch && strcmp(info.codec_profile, "USAC") == 0, "%s: open", path);
    if (d) {
        eng = read_all(d, ch, 777, &frames);
        CHECK(direct && eng && frames * ch == direct_n && memcmp(eng, direct, direct_n * sizeof(float)) == 0, "%s: the engine's PCM differs from the direct decode (%zu frames)", path, frames);
        if (eng && direct && seek_blocks) {                                                                   /* seeks: the start (primed again), over a checkpoint (already primed), the end */
            const uint64_t fl = cfg.out_frame_len, targets[] = { 0, 64u * fl + 500u, 64u * fl, 64u * fl - 1u, 30000u, 1000u, (uint64_t)(seek_blocks - 1u) * fl - 700u, 0 }; size_t i; float a[2 * 600];
            for (i = 0; i < sizeof targets / sizeof targets[0]; i++) {
                const size_t want = targets[i] + 500u <= frames ? 500u : (size_t)(frames - targets[i]);
                CHECK(maktabasauti_decoder_seek(d, targets[i], MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seek %llu", (unsigned long long)targets[i]);
                (void)maktabasauti_decoder_read(d, a, want, MAKTABASAUTI_FORMAT_F32, &got);
                CHECK(got == want && memcmp(a, eng + targets[i] * ch, want * ch * sizeof(float)) == 0, "%s: seek to %llu differs from sequential decoding", path, (unsigned long long)targets[i]);
            }
        }
        free(eng); maktabasauti_decoder_close(d);
    }
    free(direct); free(data);
}

int main(void) {
    static const Stream qa1 = ST(qa1), qb1 = ST(qb1), qa2 = ST(qa2), qb2 = ST(qb2), qa3 = ST(qa3), qb3 = ST(qb3), qa4 = ST(qa4), qb4 = ST(qb4), prlong = ST(prlong);
    test_pair(&qa1, &qb1, 3u, 1u); test_pair(&qa2, &qb2, 1u, 2u); test_pair(&qa3, &qb3, 2u, 1u); test_pair(&qa4, &qb4, 4u, 2u);
    test_rap();
    test_damage(&qb1, 3u); test_damage(&qb3, 2u);
    test_engine("tests/data/usac_qb2.m4a", &qb2, 0u); test_engine("tests/data/usac_qb3.m4a", &qb3, 0u); test_engine("tests/data/usac_prlong.m4a", &prlong, 70u);
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_preroll: all passed\n"); return 0;
}
