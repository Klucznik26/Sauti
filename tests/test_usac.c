/* USAC (xHE-AAC) frequency-domain decoder (sauti_usac.c, sauti_usac_ac.c): UsacConfig parser, arithmetic spectral coding, scale factors, noise filling, TNS, M/S stereo, long / short windows.
 * The fixtures come from the repository's own stream generator (tests/data/make_usac_fixtures.sh); the decoded PCM agrees with FFmpeg (usac_m1, usac_m2, usac_s1: 137-139 dB) and with FDK AAC
 * (every stream, ~88-92 dB, the noise floor of its 32-bit build; scripts/test_usac_vs_ffmpeg.sh), so the fingerprints pin a verified result.
 *   1. UsacConfig: fields of the five fixtures, what is refused, SBR / MPS configurations (parsed, decoding them is unsupported);
 *   2. fingerprints of every channel of every access unit;
 *   3. atomic on failure: every truncation and random bit flips leave state and output alone;
 *   4. unsupported tools (LPD frames, an SBR configuration) are reported as -1 and change nothing. */
#define _POSIX_C_SOURCE 200809L
#include "sauti_usac.h"
#include "sauti_usac_sbr.h"
#include "test_usac_vectors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures, g_dump;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int near(double a, double b) { return fabs(a - b) <= 1e-5 * (1.0 + fabs(b)); }

typedef struct { const char *name; const uint8_t *asc; size_t asc_n; const uint8_t *const *au; const size_t *au_n; size_t frames; unsigned channels, noise_fill, rate, rate_index, sbr; const double (*fp)[2][4]; } Stream;
#define OUT_LEN(s) ((s)->sbr ? 2048u : 1024u)
#define ASC_BITS 19u                                                          /* object type (11 bits: escape), sampling index (4), channel configuration (4) */
#include "test_usac_fingerprints.h"

static void fingerprint(const float *pcm, unsigned n, unsigned ch, unsigned chan, double out[4]) {
    double s1 = 0, s2 = 0, s3 = 0, s4 = 0; unsigned i;
    for (i = 0; i < n; i++) { const double v = (double)pcm[i * ch + chan]; s1 += v; s2 += v * v; s3 += v * (double)(i + 1) / (double)n; if (i >= 2) { const double d = v - 2.0 * (double)pcm[(i - 1) * ch + chan] + (double)pcm[(i - 2) * ch + chan]; s4 += d * d; } }
    out[0] = s1; out[1] = s2; out[2] = s3; out[3] = s4;
}

static void test_config(const Stream *s) {
    SautiUsacConfig c;
    const int r = sauti_usac_config_parse(s->asc, s->asc_n, ASC_BITS, &c);
    CHECK(r == 1, "%s: config parse %d", s->name, r);
    if (r != 1) return;
    CHECK(c.sample_rate == s->rate && c.core_rate == (s->sbr ? s->rate / 2u : s->rate) && c.aac_rate_index == s->rate_index, "%s: rate %u / %u / index %u", s->name, c.sample_rate, c.core_rate, c.aac_rate_index);
    CHECK(c.core_frame_len == 1024u && c.out_frame_len == OUT_LEN(s) && c.sbr_mode == s->sbr && c.core_sbr_frame_len_idx == (s->sbr ? 3u : 1u), "%s: frame lengths", s->name);
    CHECK(c.channel_config == s->channels && c.out_channels == s->channels && c.num_elements == 1u, "%s: channels %u elements %u", s->name, c.out_channels, c.num_elements);
    CHECK(c.e[0].type == (s->channels == 1u ? SAUTI_USAC_SCE : SAUTI_USAC_CPE) && c.e[0].noise_fill == s->noise_fill && c.e[0].tw_mdct == 0u && c.e[0].stereo_config_index == 0u, "%s: element", s->name);
    CHECK(c.bits_used > 0 && ASC_BITS + c.bits_used <= s->asc_n * 8u, "%s: bits used %zu", s->name, c.bits_used);
}
static void test_config_edges(void) {
    SautiUsacConfig c;
    uint8_t b[64]; size_t n, i;
    /* a hand-assembled UsacConfig: rate index, coreSbrFrameLengthIndex, channel configuration, numElements - 1 (escaped 4/8/16), element type, then the element's fields */
    #define PUT(v, w) do { for (int k_ = (int)(w) - 1; k_ >= 0; k_--) { if (((v) >> k_) & 1u) b[n >> 3] |= (uint8_t)(0x80u >> (n & 7u)); n++; } } while (0)
    #define NEW() do { memset(b, 0, sizeof b); n = 0; } while (0)
    NEW(); PUT(3u, 5); PUT(1u, 3); PUT(2u, 5); PUT(0u, 4); PUT(1u, 2); PUT(0u, 1); PUT(1u, 1); PUT(0u, 1);       /* 48 kHz, 1024, stereo, one CPE, noise filling, no extension */
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 1 && c.e[0].type == SAUTI_USAC_CPE && c.e[0].noise_fill == 1u, "hand-made stereo config");
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u - 1u, 0, &c) == 0, "truncated config");
    NEW(); PUT(3u, 5); PUT(0u, 3); PUT(1u, 5); PUT(0u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 1);       /* coreSbrFrameLengthIndex 0: 768 lines */
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 1 && c.core_frame_len == 768u && c.out_frame_len == 768u && c.sbr_mode == 0u, "768 lines");
    NEW(); PUT(13u, 5); PUT(1u, 3); PUT(1u, 5); PUT(0u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 0, "reserved sampling frequency index");
    NEW(); PUT(3u, 5); PUT(5u, 3); PUT(1u, 5); PUT(0u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 0, "reserved coreSbrFrameLengthIndex");
    NEW(); PUT(31u, 5); PUT(48000u, 24); PUT(1u, 3); PUT(1u, 5); PUT(0u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 1 && c.sample_rate == 48000u && c.aac_rate_index == 3u, "explicit sampling frequency");
    NEW(); PUT(3u, 5); PUT(1u, 3); PUT(0u, 5); PUT(2u, 5); PUT(0u, 5); PUT(1u, 5); PUT(0u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 1);   /* channelConfigurationIndex 0: explicit positions */
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == -1, "explicit channel positions are not mapped");
    NEW(); PUT(3u, 5); PUT(1u, 3); PUT(2u, 5); PUT(0u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 1);       /* stereo configuration with one SCE: the elements do not add up */
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 0, "elements and channel configuration disagree");
    /* 2:1 SBR (coreSbrFrameLengthIndex 3): SCE with the SBR configuration (harmonic, interTES, PVC, start / stop, no extras) */
    NEW(); PUT(3u, 5); PUT(3u, 3); PUT(1u, 5); PUT(0u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 3); PUT(5u, 4); PUT(9u, 4); PUT(0u, 2); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 1 && c.sbr_mode == 1u && c.out_frame_len == 2048u && c.core_rate == 24000u && c.e[0].sbr_start_freq == 5u && c.e[0].sbr_stop_freq == 9u, "SBR configuration");
    /* 8:3 SBR: 768 core lines, 2048 output samples, core rate 3/8 of the output rate */
    NEW(); PUT(3u, 5); PUT(2u, 3); PUT(1u, 5); PUT(0u, 4); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 3); PUT(5u, 4); PUT(9u, 4); PUT(0u, 2); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 1 && c.sbr_mode == 2u && c.core_frame_len == 768u && c.out_frame_len == 2048u && c.core_rate == 18000u, "8:3 SBR configuration");
    /* a stereo CPE with SBR and MPEG Surround 212 (stereoConfigIndex 1) */
    NEW(); PUT(3u, 5); PUT(3u, 3); PUT(2u, 5); PUT(0u, 4); PUT(1u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 3); PUT(5u, 4); PUT(9u, 4); PUT(0u, 2); PUT(1u, 2);
    PUT(2u, 3); PUT(0u, 3); PUT(0u, 2); PUT(0u, 2); PUT(0u, 1); PUT(0u, 1); PUT(0u, 1); PUT(0u, 1);
    CHECK(sauti_usac_config_parse(b, (n + 7u) / 8u, 0, &c) == 1 && c.e[0].stereo_config_index == 1u && c.e[0].mps_freq_res == 2u && c.e[0].mps_otts_bands_phase == 10u, "MPS212 configuration");
    for (i = 0; i < 100u; i++) { uint8_t junk[16]; size_t k; for (k = 0; k < sizeof junk; k++) junk[k] = (uint8_t)rnd(); (void)sauti_usac_config_parse(junk, sizeof junk, 0, &c); }   /* must not crash on garbage */
    #undef PUT
    #undef NEW
}

static void run(const Stream *s, SautiUsacConfig *cfg, SautiUsacState *st, size_t frames, float *pcm, double (*fp)[2][4]) {
    size_t f; SautiAacReason why;
    CHECK(sauti_usac_config_parse(s->asc, s->asc_n, ASC_BITS, cfg) == 1, "%s: config", s->name);
    sauti_usac_state_init(st, cfg);
    for (f = 0; f < frames; f++) {
        unsigned c;
        const int r = sauti_usac_decode(cfg, st, s->au[f], s->au_n[f], pcm, &why);
        CHECK(r == 1, "%s frame %zu: decode %d (reason %d)", s->name, f, r, (int)why);
        if (r != 1) return;
        if (fp) for (c = 0; c < s->channels; c++) fingerprint(pcm, cfg->out_frame_len, s->channels, c, fp[f][c]);
    }
}
static void test_vectors(const Stream *s) {
    static SautiUsacConfig cfg; static SautiUsacState st; static float pcm[2 * 2048]; double fp[64][2][4]; size_t f; unsigned c, k;
    memset(fp, 0, sizeof fp);
    run(s, &cfg, &st, s->frames, pcm, NULL);
    sauti_usac_state_init(&st, &cfg);
    for (f = 0; f < s->frames; f++) {
        SautiAacReason why;
        if (sauti_usac_decode(&cfg, &st, s->au[f], s->au_n[f], pcm, &why) != 1) { CHECK(0, "%s frame %zu", s->name, f); return; }
        for (c = 0; c < s->channels; c++) fingerprint(pcm, cfg.out_frame_len, s->channels, c, fp[f][c]);
        for (c = 0; c < s->channels * OUT_LEN(s); c++) CHECK(isfinite(pcm[c]), "%s: non-finite output", s->name);
        if (g_dump) { printf("    { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} },\n", fp[f][0][0], fp[f][0][1], fp[f][0][2], fp[f][0][3], fp[f][1][0], fp[f][1][1], fp[f][1][2], fp[f][1][3]); continue; }
        for (c = 0; c < s->channels; c++) for (k = 0; k < 4; k++) CHECK(near(fp[f][c][k], s->fp[f][c][k]), "%s frame %zu ch %u fingerprint %u: %.12e vs %.12e", s->name, f, c, k, fp[f][c][k], s->fp[f][c][k]);
    }
}
static void test_atomic(const Stream *s) {
    static SautiUsacConfig cfg; static SautiUsacState st, before; static float pcm[2 * 2048], keep[2 * 2048]; SautiAacReason why; size_t f, len, i; int round; long accepted = 0, rejected = 0;
    CHECK(sauti_usac_config_parse(s->asc, s->asc_n, ASC_BITS, &cfg) == 1, "config");
    sauti_usac_state_init(&st, &cfg);
    for (i = 0; i < 2 * 2048; i++) keep[i] = pcm[i] = 12345.0f;
    for (f = 0; f < s->frames; f++) {
        for (len = 0; len <= s->au_n[f]; len++) {                                                           /* every truncation */
            int r; before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_usac_decode(&cfg, &st, s->au[f], len, pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu len %zu: failure (%d) must leave state and output alone", s->name, f, len, r); rejected++; }
            else { for (i = 0; i < s->channels * OUT_LEN(s); i++) CHECK(isfinite(pcm[i]), "non-finite output"); st = before; accepted++; }
        }
        for (round = 0; round < 80; round++) {                                                              /* random bit flips */
            uint8_t b[4096]; int r, nflip = 1 + (int)(rnd() % 4u);
            memcpy(b, s->au[f], s->au_n[f]);
            while (nflip--) { const size_t at = rnd() % s->au_n[f]; b[at] ^= (uint8_t)(1u << (rnd() & 7u)); }
            before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_usac_decode(&cfg, &st, b, s->au_n[f], pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu flip: failure (%d) must leave state and output alone", s->name, f, r); rejected++; }
            else { for (i = 0; i < s->channels * OUT_LEN(s); i++) CHECK(isfinite(pcm[i]), "non-finite output after a flip"); st = before; accepted++; }
        }
        { SautiUsacState adv = st; (void)sauti_usac_decode(&cfg, &adv, s->au[f], s->au_n[f], pcm, &why); st = adv; }
    }
    printf("%s: %ld accepted, %ld rejected mutated or truncated access units\n", s->name, accepted, rejected);
    CHECK(rejected > 100, "%s: mutations never fail?", s->name);
}
static void test_unsupported(const Stream *s) {
    static SautiUsacConfig cfg; static SautiUsacState st, before; static float pcm[2 * 2048]; uint8_t b[4096]; SautiAacReason why; int r;
    CHECK(sauti_usac_config_parse(s->asc, s->asc_n, ASC_BITS, &cfg) == 1, "config");
    sauti_usac_state_init(&st, &cfg);
    memcpy(b, s->au[0], s->au_n[0]);
    b[0] ^= 0x40u;                                                                                          /* bit 1: core_mode of the first channel = 1: an LPD frame */
    before = st;
    r = sauti_usac_decode(&cfg, &st, b, s->au_n[0], pcm, &why);
    CHECK((r == 1 || memcmp(&before, &st, sizeof st) == 0) && r >= -1 && r <= 1, "%s: a frame forced to core_mode 1 (LPD) is decoded or refused, and a refusal leaves the state untouched (%d)", s->name, r);
    if (r == 1) st = before;
    if (!s->sbr) {                                                                                           /* a plain stream read as an SBR stream: the bits after the channel streams are not SBR data */
        cfg.sbr_mode = 1u;
        r = sauti_usac_decode(&cfg, &st, s->au[0], s->au_n[0], pcm, &why);
        CHECK(r != 1 && memcmp(&before, &st, sizeof st) == 0, "%s: a plain stream must not be decoded as SBR (%d)", s->name, r);
    } else {                                                                                                 /* an SBR stream configured as 4:1: its data are no 4:1 data, refused, state untouched */
        cfg.sbr_mode = 3u;
        r = sauti_usac_decode(&cfg, &st, s->au[0], s->au_n[0], pcm, &why);
        CHECK(r != 1 && memcmp(&before, &st, sizeof st) == 0, "%s: a 2:1 stream read as eSBR 4:1 must not be decoded, state untouched (%d)", s->name, r);
    }
}
static void test_arguments(void) {
    static SautiUsacConfig cfg; static SautiUsacState st; static float pcm[2 * 2048]; SautiAacReason why;
    CHECK(sauti_usac_config_parse(usac_m1_asc, sizeof usac_m1_asc, ASC_BITS, &cfg) == 1, "config");
    sauti_usac_state_init(&st, &cfg);
    CHECK(sauti_usac_decode(NULL, &st, usac_m1_au0, sizeof usac_m1_au0, pcm, &why) == 0 && sauti_usac_decode(&cfg, NULL, usac_m1_au0, sizeof usac_m1_au0, pcm, &why) == 0 &&
          sauti_usac_decode(&cfg, &st, NULL, 1, pcm, &why) == 0 && sauti_usac_decode(&cfg, &st, usac_m1_au0, sizeof usac_m1_au0, NULL, &why) == 0 && sauti_usac_decode(&cfg, &st, usac_m1_au0, 0, pcm, NULL) == 0, "invalid arguments");
    st.channels = 2u;
    CHECK(sauti_usac_decode(&cfg, &st, usac_m1_au0, sizeof usac_m1_au0, pcm, &why) == 0, "state of another channel count");
}

/* A configuration with a fill extension element ahead of the SCE; access units built bit by bit from the access units of usac_m1: the extension payload with length 255 + escape, a default
 * length, a missing payload and a payload that overruns the access unit. */
static void test_extension(void) {
    static SautiUsacConfig cfg, ref; static SautiUsacState st, st_ref; static float pcm[1024], pcm_ref[1024];
    uint8_t asc[32], au[4096]; size_t n = 0, m, i; SautiAacReason why; int r;
    #define PUT(buf, v, w) do { for (int k_ = (int)(w) - 1; k_ >= 0; k_--) { if (((v) >> k_) & 1u) (buf)[(*(&n)) >> 3] |= (uint8_t)(0x80u >> (n & 7u)); n++; } } while (0)
    memset(asc, 0, sizeof asc);
    PUT(asc, 3u, 5); PUT(asc, 1u, 3); PUT(asc, 1u, 5); PUT(asc, 1u, 4);                                         /* 48 kHz, 1024, mono, two elements */
    PUT(asc, 3u, 2); PUT(asc, 0u, 4); PUT(asc, 0u, 4); PUT(asc, 1u, 1); PUT(asc, 99u, 8); PUT(asc, 0u, 1);      /* EXT: type fill, config length 0, default length present: 100 bytes, no fragmentation */
    PUT(asc, 0u, 2); PUT(asc, 0u, 1); PUT(asc, 0u, 1); PUT(asc, 0u, 1);                                         /* SCE, no timewarp, no noise filling, no config extension */
    CHECK(sauti_usac_config_parse(asc, (n + 7u) / 8u, 0, &cfg) == 1 && cfg.num_elements == 2u && cfg.e[0].type == SAUTI_USAC_EXT && cfg.e[0].ext_default_len == 100u && cfg.e[1].type == SAUTI_USAC_SCE, "config with an extension element");
    CHECK(sauti_usac_config_parse(usac_m1_asc, sizeof usac_m1_asc, ASC_BITS, &ref) == 1, "reference config");
    sauti_usac_state_init(&st, &cfg); sauti_usac_state_init(&st_ref, &ref);
    CHECK(sauti_usac_decode(&ref, &st_ref, usac_m1_au0, sizeof usac_m1_au0, pcm_ref, &why) == 1, "reference decode");
    for (int variant = 0; variant < 5; variant++) {
        const size_t extra = variant == 0 ? 300u : variant == 1 ? 100u : 40u;                                    /* 0: 255 + escape (300 bytes), 1: default length (100), 2: explicit 40 bytes, 3: absent, 4: overrun */
        memset(au, 0, sizeof au); n = 0;
        PUT(au, 1u, 1);                                                                                         /* usacIndependencyFlag */
        if (variant == 3) { PUT(au, 0u, 1); }
        else if (variant == 1) { PUT(au, 1u, 1); PUT(au, 1u, 1); for (i = 0; i < 100u; i++) PUT(au, 0xa5u, 8); }
        else if (variant == 0) { PUT(au, 1u, 1); PUT(au, 0u, 1); PUT(au, 255u, 8); PUT(au, extra + 2u - 255u + 0u, 16); for (i = 0; i < extra; i++) PUT(au, 0xa5u, 8); }
        else if (variant == 4) { PUT(au, 1u, 1); PUT(au, 0u, 1); PUT(au, 255u, 8); PUT(au, 60000u, 16); }
        else { PUT(au, 1u, 1); PUT(au, 0u, 1); PUT(au, extra, 8); for (i = 0; i < extra; i++) PUT(au, 0xa5u, 8); }
        for (i = 1; i < sizeof usac_m1_au0 * 8u; i++) PUT(au, (usac_m1_au0[i >> 3] >> (7u - (i & 7u))) & 1u, 1);   /* the SCE of the reference access unit */
        m = (n + 7u) / 8u;
        sauti_usac_state_init(&st, &cfg);
        r = sauti_usac_decode(&cfg, &st, au, m, pcm, &why);
        if (variant == 4) CHECK(r == 0, "an extension payload longer than the access unit must be refused (%d)", r);
        else CHECK(r == 1 && memcmp(pcm, pcm_ref, sizeof pcm) == 0, "extension variant %d decodes like the plain access unit (%d)", variant, r);
    }
    #undef PUT
}

/* max_sfb = 0: an empty spectrum (no arithmetic code at all, silence), and the bits after it are read as what they are */
static void test_empty_spectrum(void) {
    static SautiUsacConfig cfg; static SautiUsacState st; static float pcm[1024]; uint8_t au[16]; size_t n = 0, i; SautiAacReason why; int r;
    #define PUT(v, w) do { for (int k_ = (int)(w) - 1; k_ >= 0; k_--) { if (((v) >> k_) & 1u) au[n >> 3] |= (uint8_t)(0x80u >> (n & 7u)); n++; } } while (0)
    memset(au, 0, sizeof au);
    CHECK(sauti_usac_config_parse(usac_m1_asc, sizeof usac_m1_asc, ASC_BITS, &cfg) == 1, "config");
    sauti_usac_state_init(&st, &cfg);
    PUT(1u, 1); PUT(0u, 1); PUT(0u, 1); PUT(100u, 8); PUT(0u, 2); PUT(0u, 1); PUT(0u, 6);                       /* independent, core_mode 0, no TNS, global_gain, ONLY_LONG, sine, max_sfb 0 */
    PUT(0u, 1); PUT(15u, 4);                                                                                    /* fac_data_present = 0, then padding ones */
    r = sauti_usac_decode(&cfg, &st, au, (n + 7u) / 8u, pcm, &why);
    CHECK(r == 1, "an empty spectrum decodes (%d)", r);
    for (i = 0; i < 1024u; i++) if (pcm[i] != 0.0f) { CHECK(0, "an empty spectrum is silent"); break; }
    #undef PUT
}

/* usac_sbr_data(): what is not implemented is reported (-1), never decoded as something else */
static void test_sbr_unsupported(void) {
    SautiUsacElement e; SautiUsacSbrState st; SautiAacSbrPayloadFrame pl[2]; SautiUsacBits b; unsigned cpl; uint8_t d[8];
    memset(&e, 0, sizeof e); memset(&st, 0, sizeof st); e.sbr_start_freq = 5; e.sbr_stop_freq = 9; e.sbr_freq_scale = 2; e.sbr_alter_scale = 1; e.sbr_noise_bands = 2; e.sbr_limiter_bands = 2; e.sbr_limiter_gains = 2; e.sbr_interpol_freq = 1; e.sbr_smoothing_mode = 1;
    memset(d, 0, sizeof d); d[0] = 0x04u;                                                                    /* independent frame: amp_res 0, xover 0000, bs_sbr_preprocessing 1 */
    b.data = d; b.nbits = sizeof d * 8u; b.pos = 0;
    CHECK(sauti_usac_sbr_parse(&e, 1, &b, 1, 48000, &st, pl, &cpl) == -1, "bs_sbr_preprocessing = 1 is not implemented");
    d[0] = 0x00u; b.pos = 0;
    CHECK(sauti_usac_sbr_parse(&e, 1, &b, 1, 48000, &st, pl, &cpl) != -1, "preprocessing 0 is not refused for that reason");
    e.sbr_harmonic = 1; b.pos = 0; CHECK(sauti_usac_sbr_parse(&e, 1, &b, 1, 48000, &st, pl, &cpl) == -1, "the harmonic transposer is not implemented");
    e.sbr_harmonic = 0; e.sbr_pvc = 1; b.pos = 0; CHECK(sauti_usac_sbr_parse(&e, 1, &b, 1, 48000, &st, pl, &cpl) == -1, "PVC is not implemented");
    e.sbr_pvc = 0; e.sbr_intertes = 1; memset(d, 0xffu, sizeof d); b.pos = 0; (void)sauti_usac_sbr_parse(&e, 1, &b, 1, 48000, &st, pl, &cpl);   /* inter-TES is implemented: garbage with the shaping bits must not crash either */
    e.sbr_intertes = 0; memset(d, 0xffu, sizeof d); b.pos = 0; (void)sauti_usac_sbr_parse(&e, 2, &b, 1, 48000, &st, pl, &cpl);   /* garbage must not crash */
}

/* The AudioSpecificConfigs libxaac writes for eSBR 8:3 (768-line core) at five output rates, from tests/data/make_usac_xaac_fixtures.sh-style synthetic input (`xaacenc -aot:42 -usac:1 -esbr:1 -ccfl_idx:2`).  Which scalefactor-band
 * table the 768-line core uses was found by decoding those streams with every candidate table: only the one of 4/3 of the core rate leaves every access unit decodable (16 kHz out -> core 6 kHz: tables 11-12; 24 -> 9: 8-10;
 * 32 -> 12: 8-10; 44.1 -> 16.5: 6-7; 48 -> 18: 6-7) - docs SBR-109. */
static void test_768_band_tables(void) {
    static const struct { uint8_t asc[23]; unsigned rate, core, rate_index; } t[5] = {
        { { 0xf9,0x50,0x48,0x42,0x2c,0xc0,0x40,0x08,0x18,0x01,0x5c,0x80,0x00,0x0a,0x00,0x04,0x00,0x12,0xb7,0x08,0xcb,0x16,0x00 }, 16000, 6000, 11 },
        { { 0xf9,0x4c,0x46,0x42,0x2c,0xc0,0x40,0x08,0x18,0x01,0x5c,0x80,0x00,0x0a,0x00,0x04,0x00,0x12,0xbc,0x08,0xcc,0x16,0x00 }, 24000, 9000, 9 },
        { { 0xf9,0x4a,0x45,0x42,0x2c,0xc0,0x40,0x08,0x18,0x01,0x5c,0x80,0x00,0x0a,0x00,0x04,0x00,0x12,0xc1,0x08,0xcd,0x96,0x00 }, 32000, 12000, 8 },
        { { 0xf9,0x48,0x44,0x42,0x2c,0xc0,0x40,0x08,0x18,0x01,0x5c,0x80,0x00,0x0a,0x00,0x04,0x00,0x13,0xca,0x08,0xcf,0x16,0x00 }, 44100, 16537, 7 },
        { { 0xf9,0x46,0x43,0x42,0x2c,0xc0,0x40,0x08,0x18,0x01,0x5c,0x80,0x00,0x0a,0x00,0x04,0x00,0x12,0xc2,0x08,0xcd,0x96,0x00 }, 48000, 18000, 6 } };
    unsigned i;
    for (i = 0; i < 5; i++) {
        SautiUsacConfig c;
        CHECK(sauti_usac_asc_parse(t[i].asc, sizeof t[i].asc, &c) == 1, "768 asc %u parses", i);
        CHECK(c.core_frame_len == 768u && c.sbr_mode == 2u && c.sample_rate == t[i].rate && c.core_rate == t[i].core, "768 asc %u: rate %u core %u", i, c.sample_rate, c.core_rate);
        CHECK(c.aac_rate_index == t[i].rate_index, "768 asc %u: band table index %u, expected %u", i, c.aac_rate_index, t[i].rate_index);
    }
}

int main(int argc, char **argv) {
    #define S(n, ch, nf, rate, idx) { "usac_" #n, usac_##n##_asc, sizeof usac_##n##_asc, usac_##n##_aus, usac_##n##_n, sizeof usac_##n##_n / sizeof usac_##n##_n[0], ch, nf, rate, idx, 0, usac_##n##_fp }
    #define SB(n, ch, nf, rate, idx) { "usac_" #n, usac_##n##_asc, sizeof usac_##n##_asc, usac_##n##_aus, usac_##n##_n, sizeof usac_##n##_n / sizeof usac_##n##_n[0], ch, nf, rate, idx, 1, usac_##n##_fp }
    const Stream streams[23] = { S(m1, 1, 0, 48000, 3), S(m2, 1, 0, 48000, 3), S(m3, 1, 1, 48000, 3), S(m4, 1, 0, 24000, 6), S(m5, 1, 0, 48000, 3), S(s1, 2, 0, 48000, 3), S(s2, 2, 0, 48000, 3), S(s3, 2, 0, 48000, 3), S(s4, 2, 1, 48000, 3), S(c1, 2, 0, 48000, 3), S(c2, 2, 0, 48000, 3), S(c3, 2, 0, 48000, 3), S(c4, 2, 1, 48000, 3), S(c5, 2, 0, 48000, 3), SB(e1, 1, 0, 48000, 6), SB(e2, 2, 0, 48000, 6), SB(e3, 1, 1, 48000, 6), SB(e4, 2, 0, 48000, 6), SB(e5, 1, 0, 48000, 6), SB(e6, 1, 0, 48000, 6), SB(e7, 2, 0, 48000, 6), SB(e8, 1, 0, 48000, 6), SB(e9, 2, 0, 48000, 6) };
    #undef S
    #undef SB
    unsigned k;
    if (argc > 1 && !strcmp(argv[1], "--dump")) {                                                            /* build/test_usac --dump > tests/test_usac_fingerprints.h */
        g_dump = 1;
        printf("/* Fingerprints (sum, sum of squares, position-weighted sum, energy of the second difference) of the decoded PCM, written by `build/test_usac --dump` AFTER the decoder agreed with FFmpeg\n * (usac_m1, m2, m4, s1, s3: 134-139 dB) and FDK AAC (all streams, ~83-92 dB) on these streams (scripts/test_usac_vs_ffmpeg.sh); regenerate only after such a comparison. */\n");
        for (k = 0; k < 23; k++) { printf("static const double usac_%s_fp[64][2][4] = {\n", streams[k].name + 5); test_vectors(&streams[k]); printf("};\n"); }
        return g_failures ? 1 : 0;
    }
    for (k = 0; k < 23; k++) { test_config(&streams[k]); test_vectors(&streams[k]); test_atomic(&streams[k]); test_unsupported(&streams[k]); }
    test_config_edges(); test_768_band_tables(); test_arguments(); test_extension(); test_empty_spectrum(); test_sbr_unsupported();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac: all passed\n"); return 0;
}
