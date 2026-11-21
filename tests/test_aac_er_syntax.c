/* ER AAC LC (object type 17) and ER AAC LTP (object type 19), error-resilient syntax without resilience tools: no element ids / ID_END, an instance tag per element, tns_data after the
 * gain-control flag (src/decoders/modern/aac/sauti_aac_lc_frame.c, sauti_aac_cpe.c, sauti_aac_lcn.c, sauti_aac_lcm.c).  The fixtures tests/data/er*.{m4a,loas} come from the repository's own
 * stream generator (tests/data/make_er_fixtures.sh).  References: FFmpeg decodes ER AAC LC (agreement ~136 dB, mono / stereo / 960 lines / 3.0 / 5.1) and FAAD2 too; neither decodes ER AAC LTP
 * stereo (FFmpeg refuses object type 19, FAAD2 fails on it; FAAD2 mono agrees to ~67 dB like for plain LTP), so the ER LTP files are checked by EQUIVALENCE: the same random content written in the
 * plain syntax (AAC LTP, which FFmpeg verifies to ~136 dB) must decode to the identical PCM.
 *   1. configurations: frame lengths, multichannel counts, what is refused;
 *   2. the equivalence of ER and plain syntax (ER LC == AAC-LC, ER LTP == AAC LTP; stereo and 5.1), fingerprints of every file;
 *   3. LOAS == MP4, atomic on failure (every truncation and bit flips leave state and output alone, stereo and 5.1);
 *   4. refusals: resilience flags, epConfig, 960 lines with more than two channels, ER LTP with 960 lines. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "sauti_aac_lcn.h"
#include "sauti_aac_lcm.h"
#include "test_aac_er_syntax_vectors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures, g_dump;
static const char *g_eol = ",\n";
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int near(double a, double b) { return fabs(a - b) <= 1e-5 * (1.0 + fabs(b)); }

static void fingerprint(const float *pcm, unsigned n, unsigned ch, double out[2][4]) {                    /* channel 0 and (for more than two channels) the last channel */
    unsigned c, i;
    for (c = 0; c < (ch < 2 ? ch : 2u); c++) {
        const unsigned chan = c == 0 ? 0 : ch - 1;
        double s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        for (i = 0; i < n; i++) { const double v = (double)pcm[i * ch + chan]; s1 += v; s2 += v * v; s3 += v * (double)(i + 1) / (double)n; if (i >= 2) { const double d = v - 2.0 * (double)pcm[(i - 1) * ch + chan] + (double)pcm[(i - 2) * ch + chan]; s4 += d * d; } }
        out[c][0] = s1; out[c][1] = s2; out[c][2] = s3; out[c][3] = s4;
    }
}

#include "test_aac_er_syntax_fingerprints.h"
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
static const char *g_profile = "ER-LC";
static float *decode_all(const uint8_t *data, size_t n, unsigned want_channels, unsigned want_rate, size_t chunk, size_t *frames_out, const char *name) {
    maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, NULL); float *pcm = NULL, *buf; size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    *frames_out = 0;
    if (!d) { CHECK(0, "%s: open", name); return NULL; }
    CHECK(info.channels == want_channels && info.sample_rate == want_rate, "%s: %u ch %u Hz, expected %u ch %u Hz", name, (unsigned)info.channels, (unsigned)info.sample_rate, want_channels, want_rate);
    CHECK(strcmp(info.codec_profile, g_profile) == 0, "%s: profile '%s'", name, info.codec_profile);
    buf = (float *)malloc(chunk * want_channels * sizeof(float));
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, chunk, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * want_channels * sizeof(float)); } memcpy(pcm + total * want_channels, buf, got * want_channels * sizeof(float)); total += got; }
        if (r == MAKTABASAUTI_EOF) break;
        if (r != MAKTABASAUTI_OK) { CHECK(0, "%s: read -> %d", name, (int)r); break; }
    }
    free(buf); maktabasauti_decoder_close(d); *frames_out = total; return pcm;
}

static int open_result(const uint8_t *data, size_t n) { int r = 0; maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, &r); if (d) maktabasauti_decoder_close(d); return r; }


static void test_config(void) {
    SautiAacLcnConfig c; unsigned r, cc;
    for (r = 0; r <= 13; r++) for (cc = 0; cc <= 8; cc++) {
        memset(&c, 0, sizeof c); c.rate_index = r; c.channel_config = cc; c.er = 1;
        CHECK(sauti_aac_lcn_frame_samples(&c) == ((r <= 12 && (cc == 1 || cc == 2)) ? 1024u : 0u), "ER-LC rate %u config %u", r, cc);
        c.frame960 = 1; CHECK(sauti_aac_lcn_frame_samples(&c) == ((r <= 12 && (cc == 1 || cc == 2)) ? 960u : 0u), "ER-LC 960 rate %u config %u", r, cc);
        c.ltp = 1; CHECK(sauti_aac_lcn_frame_samples(&c) == 0u, "ER-LTP has no 960 variant");
        c.frame960 = 0; CHECK(sauti_aac_lcn_frame_samples(&c) == ((r <= 12 && (cc == 1 || cc == 2)) ? 1024u : 0u), "ER-LTP rate %u config %u", r, cc);
        c.main = 1; CHECK(sauti_aac_lcn_frame_samples(&c) == 0u, "no ER Main");
        memset(&c, 0, sizeof c); c.rate_index = r; c.channel_config = cc; c.er = 1;
        { static const unsigned want[8] = { 0, 0, 0, 3, 4, 5, 6, 8 }; CHECK(sauti_aac_lcm_channels(&c) == ((cc >= 3 && cc <= 7 && r <= 12) ? want[cc] : 0u), "ER-LC multichannel rate %u config %u", r, cc);
          c.ltp = 1; CHECK(sauti_aac_lcm_channels(&c) == ((cc >= 3 && cc <= 7 && r <= 12) ? want[cc] : 0u), "ER-LTP multichannel rate %u config %u", r, cc);
          c.frame960 = 1; CHECK(sauti_aac_lcm_channels(&c) == 0u, "no multichannel 960"); }
    }
}
static void test_files(void) {
    static const struct { const char *path; const char *profile; unsigned channels, rate, samples; double fp[2][4]; } files[] = ER_FILE_FINGERPRINTS;
    unsigned k;
    for (k = 0; k < sizeof files / sizeof files[0]; k++) {
        size_t n, frames; uint8_t *data = slurp(files[k].path, &n); float *pcm; double fp[2][4] = { { 0 } }; unsigned c, j;
        if (!data) { CHECK(0, "cannot read %s (run from the repository root)", files[k].path); continue; }
        g_profile = files[k].profile;
        pcm = decode_all(data, n, files[k].channels, files[k].rate, 4096, &frames, files[k].path);
        CHECK(frames == files[k].samples, "%s: %zu frames, expected %u", files[k].path, frames, files[k].samples);
        fingerprint(pcm, (unsigned)frames, files[k].channels, fp);
        if (g_dump) printf("    { \"%s\", \"%s\", %u, %u, %u, { {%.12e, %.12e, %.12e, %.12e}, {%.12e, %.12e, %.12e, %.12e} } }%s", files[k].path, files[k].profile, files[k].channels, files[k].rate, files[k].samples, fp[0][0], fp[0][1], fp[0][2], fp[0][3], fp[1][0], fp[1][1], fp[1][2], fp[1][3], g_eol);
        else for (c = 0; c < (files[k].channels < 2 ? files[k].channels : 2u); c++) for (j = 0; j < 4; j++) CHECK(near(fp[c][j], files[k].fp[c][j]), "%s ch %u fingerprint %u: %.12e vs %.12e", files[k].path, c, j, fp[c][j], files[k].fp[c][j]);
        free(pcm); free(data);
    }
}
static float *decode_file(const char *path, const char *profile, unsigned channels, unsigned rate, size_t *frames) {
    size_t n; uint8_t *data = slurp(path, &n); float *pcm;
    *frames = 0;
    if (!data) { CHECK(0, "cannot read %s", path); return NULL; }
    g_profile = profile; pcm = decode_all(data, n, channels, rate, 4096, frames, path); free(data); return pcm;
}
static void test_equivalence(void) {
    static const struct { const char *er, *er_profile, *plain, *plain_profile; unsigned channels; } pairs[] = {
        { "tests/data/erlc_st.m4a", "ER-LC", "tests/data/erplain_lc_st.m4a", "LC", 2 }, { "tests/data/erlc_c6.m4a", "ER-LC", "tests/data/erplain_lc_c6.m4a", "LC", 6 },
        { "tests/data/erltp_st.m4a", "ER-LTP", "tests/data/erplain_ltp_st.m4a", "LTP", 2 }, { "tests/data/erltp_c6.m4a", "ER-LTP", "tests/data/erplain_ltp_c6.m4a", "LTP", 6 },
        { "tests/data/erlc_c7.m4a", "ER-LC", "tests/data/erplain_lc_c7.m4a", "LC", 8 }, { "tests/data/erltp_c7.m4a", "ER-LTP", "tests/data/erplain_ltp_c7.m4a", "LTP", 8 } };
    unsigned k;
    for (k = 0; k < sizeof pairs / sizeof pairs[0]; k++) {
        size_t fa, fb; float *a = decode_file(pairs[k].er, pairs[k].er_profile, pairs[k].channels, 48000, &fa), *b = decode_file(pairs[k].plain, pairs[k].plain_profile, pairs[k].channels, 48000, &fb);
        CHECK(a && b && fa == fb && fa > 0 && memcmp(a, b, fa * pairs[k].channels * sizeof(float)) == 0, "%s must decode identically to %s", pairs[k].er, pairs[k].plain);
        free(a); free(b);
    }
}
static void test_loas(void) {
    static const struct { const char *loas, *mp4, *profile; } pairs[] = { { "tests/data/erlc_st.loas", "tests/data/erlc_st.m4a", "ER-LC" }, { "tests/data/erltp_st.loas", "tests/data/erltp_st.m4a", "ER-LTP" } };
    unsigned k;
    for (k = 0; k < 2; k++) {
        size_t fa, fb; float *a = decode_file(pairs[k].loas, pairs[k].profile, 2, 48000, &fa), *b = decode_file(pairs[k].mp4, pairs[k].profile, 2, 48000, &fb);
        CHECK(a && b && fa == fb && fa > 0 && memcmp(a, b, fa * 2 * sizeof(float)) == 0, "%s (LOAS) must decode identically to %s", pairs[k].loas, pairs[k].mp4);
        free(a); free(b);
    }
}
static void test_atomic_stereo(const char *name, int ltp, const uint8_t *const *au, const size_t *au_n, size_t frames) {
    SautiAacLcnConfig cfg; static SautiAacLcnState st, before; float pcm[2 * 1024], keep[2 * 1024]; SautiAacReason why; size_t f, len, i; int round; long accepted = 0, rejected = 0;
    memset(&cfg, 0, sizeof cfg); cfg.rate_index = 3; cfg.channel_config = 2; cfg.er = 1; cfg.ltp = (unsigned)ltp;
    sauti_aac_lcn_state_init(&st);
    for (i = 0; i < 2 * 1024; i++) keep[i] = pcm[i] = 12345.0f;
    for (f = 0; f < frames; f++) {
        for (len = 0; len <= au_n[f]; len++) {
            int r; before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcn_decode(&cfg, &st, au[f], len, pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu len %zu: failure (%d) must leave state and output alone", name, f, len, r); rejected++; }
            else { for (i = 0; i < 2048u; i++) CHECK(isfinite(pcm[i]), "non-finite output"); st = before; accepted++; }
        }
        for (round = 0; round < 150; round++) {
            uint8_t b[2048]; int r, nflip = 1 + (int)(rnd() % 4u);
            memcpy(b, au[f], au_n[f]);
            while (nflip--) { const size_t at = rnd() % au_n[f]; b[at] ^= (uint8_t)(1u << (rnd() & 7u)); }
            before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcn_decode(&cfg, &st, b, au_n[f], pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "%s frame %zu flip: failure (%d) must leave state and output alone", name, f, r); rejected++; }
            else { st = before; accepted++; }
        }
        { SautiAacLcnState adv = st; sauti_aac_lcn_decode(&cfg, &adv, au[f], au_n[f], pcm, &why); st = adv; }
    }
    printf("%s: %ld accepted, %ld rejected mutated or truncated access units\n", name, accepted, rejected);
    CHECK(rejected > 100, "%s: mutations never fail?", name);
}
static void test_atomic_c6(void) {
    static SautiAacLcmState st, before; SautiAacLcnConfig cfg; float pcm[8 * 1024], keep[8 * 1024]; SautiAacReason why; size_t f, len, i; int round; long accepted = 0, rejected = 0;
    static const uint8_t *const au[] = { erlc_c6_au0, erlc_c6_au1, erlc_c6_au2 }; static const size_t au_n[] = { sizeof erlc_c6_au0, sizeof erlc_c6_au1, sizeof erlc_c6_au2 };
    memset(&cfg, 0, sizeof cfg); cfg.rate_index = 3; cfg.channel_config = 6; cfg.er = 1;
    CHECK(sauti_aac_lcm_channels(&cfg) == 6u, "ER-LC 5.1");
    sauti_aac_lcm_state_init(&st, &cfg);
    for (i = 0; i < 8 * 1024; i++) keep[i] = pcm[i] = 12345.0f;
    for (f = 0; f < 3; f++) {
        for (len = 0; len <= au_n[f]; len++) {
            int r; before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcm_decode(&cfg, &st, au[f], len, pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "ER-LC 5.1 frame %zu len %zu: failure (%d) must leave state and output alone", f, len, r); rejected++; }
            else { st = before; accepted++; }
        }
        for (round = 0; round < 60; round++) {
            uint8_t b[4096]; int r, nflip = 1 + (int)(rnd() % 4u);
            memcpy(b, au[f], au_n[f]);
            while (nflip--) { const size_t at = rnd() % au_n[f]; b[at] ^= (uint8_t)(1u << (rnd() & 7u)); }
            before = st; memcpy(pcm, keep, sizeof pcm);
            r = sauti_aac_lcm_decode(&cfg, &st, b, au_n[f], pcm, &why);
            if (r != 1) { CHECK(memcmp(&before, &st, sizeof st) == 0 && memcmp(pcm, keep, sizeof pcm) == 0, "ER-LC 5.1 frame %zu flip: failure (%d) must leave state and output alone", f, r); rejected++; }
            else { st = before; accepted++; }
        }
        { SautiAacLcmState adv = st; sauti_aac_lcm_decode(&cfg, &adv, au[f], au_n[f], pcm, &why); st = adv; }
    }
    printf("ER-LC 5.1: %ld accepted, %ld rejected mutated or truncated access units\n", accepted, rejected);
    CHECK(rejected > 50, "ER-LC 5.1: mutations never fail?");
}
static int patch_asc(uint8_t *data, size_t n, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t n1, uint8_t n2) {                    /* DecoderSpecificInfo of three bytes: b0 b1 b2 -> b0 n1 n2 */
    size_t i; int hits = 0;
    for (i = 0; i + 4 < n; i++) if (data[i] == 0x03 && data[i + 1] == b0 && data[i + 2] == b1 && data[i + 3] == b2) { data[i + 2] = n1; data[i + 3] = n2; hits++; }
    return hits;
}
static void test_refusals(void) {
    size_t n, n2; uint8_t *data = slurp("tests/data/erlc_st.m4a", &n), *ltp = slurp("tests/data/erltp_st.m4a", &n2);
    if (!data || !ltp) { CHECK(0, "cannot read the ER fixtures"); free(data); free(ltp); return; }
    CHECK(open_result(data, n) == (int)MAKTABASAUTI_OK, "the unpatched ER LC fixture opens");
    CHECK(patch_asc(data, n, 0x89, 0x91, 0x00, 0x91, 0x80) == 1 && open_result(data, n) != (int)MAKTABASAUTI_OK, "an ER resilience flag (HCR / RVLC / VCB11) is refused");
    CHECK(patch_asc(data, n, 0x89, 0x91, 0x80, 0x91, 0x04) == 1 && open_result(data, n) != (int)MAKTABASAUTI_OK, "epConfig 1 is refused");
    CHECK(patch_asc(data, n, 0x89, 0x91, 0x04, 0x9d, 0x00) == 1 && open_result(data, n) != (int)MAKTABASAUTI_OK, "ER LC with 960 lines and three channels is refused");
    CHECK(patch_asc(data, n, 0x89, 0x9d, 0x00, 0x91, 0x00) == 1 && open_result(data, n) == (int)MAKTABASAUTI_OK, "restored ASC opens again");
    CHECK(open_result(ltp, n2) == (int)MAKTABASAUTI_OK, "the unpatched ER LTP fixture opens");
    CHECK(patch_asc(ltp, n2, 0x99, 0x91, 0x00, 0x95, 0x00) == 1 && open_result(ltp, n2) != (int)MAKTABASAUTI_OK, "ER LTP with 960 lines is refused");
    free(data); free(ltp);
}

int main(int argc, char **argv) {
    static const uint8_t *const lc_au[] = { erlc_st_au0, erlc_st_au1, erlc_st_au2, erlc_st_au3, erlc_st_au4, erlc_st_au5 };
    static const size_t lc_n[] = { sizeof erlc_st_au0, sizeof erlc_st_au1, sizeof erlc_st_au2, sizeof erlc_st_au3, sizeof erlc_st_au4, sizeof erlc_st_au5 };
    static const uint8_t *const ltp_au[] = { erltp_st_au0, erltp_st_au1, erltp_st_au2, erltp_st_au3, erltp_st_au4, erltp_st_au5 };
    static const size_t ltp_n[] = { sizeof erltp_st_au0, sizeof erltp_st_au1, sizeof erltp_st_au2, sizeof erltp_st_au3, sizeof erltp_st_au4, sizeof erltp_st_au5 };
    if (argc > 1 && !strcmp(argv[1], "--dump")) {                                                            /* build/test_aac_er_syntax --dump > tests/test_aac_er_syntax_fingerprints.h */
        g_dump = 1;
        printf("/* Fingerprints of the decoded PCM (sum, sum of squares, position-weighted sum, energy of the second difference), written by `build/test_aac_er_syntax --dump` AFTER the decoder agreed\n * with FFmpeg (ER LC, ~136 dB) / matched the plain-syntax equivalent (ER LTP); regenerate only after such a comparison. */\n");
        g_eol = ", \\\n"; printf("#define ER_FILE_FINGERPRINTS { \\\n"); test_files(); printf("}\n");
        return g_failures ? 1 : 0;
    }
    test_config(); test_files(); test_equivalence(); test_loas();
    test_atomic_stereo("ER-LC stereo", 0, lc_au, lc_n, 6); test_atomic_stereo("ER-LTP stereo", 1, ltp_au, ltp_n, 6); test_atomic_c6();
    test_refusals();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_aac_er_syntax: all passed\n"); return 0;
}
