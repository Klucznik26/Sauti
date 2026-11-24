/* MPEG-D DRC (uniDRC) bitstream parsers (stage U1 of docs/UNIDRC_PLAN.md) on a REAL stream: tests/data/usac_d1.{asc,aus}, made by libxaac's encoder (-drc:1, impd_drc_config_params.txt) from a synthetic signal
 * (tests/data/make_usac_xaac_fixtures.sh).  Checks: the configuration (3 gain sets, 2 instructions, 2 channels), the LoudnessInfoSet (-16 LUFS, the value of the encoder's configuration file), and that UniDrcGain() of every
 * access unit is consumed up to its padding byte; plus hand-made rejections.  usage: build/test_unidrc (from the repository root). */
#include "maktabasauti/maktabasauti.h"
#include "sauti_unidrc.h"
#include "sauti_usac.h"
#include <math.h>
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


/* decode a whole file through the engine with the given options; returns the interleaved float PCM (malloc) */
static float *decode_file(const char *path, const maktabasauti_option_t *opts, size_t nopts, size_t *frames, unsigned *channels) {
    size_t n; uint8_t *data = slurp(path, &n); maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; float *pcm = NULL, *buf; size_t total = 0, got, cap = 0;
    if (!data) return NULL;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.options = opts; p.option_count = nopts;
    if (maktabasauti_io_from_memory(data, n, NULL, &p.io) != MAKTABASAUTI_OK) { free(data); return NULL; }
    if (maktabasauti_decoder_open(&p, &d) != MAKTABASAUTI_OK) { p.io.close(p.io.user); free(data); return NULL; }
    info.struct_size = sizeof info; (void)maktabasauti_decoder_get_info(d, &info);
    *channels = info.channels; buf = (float *)malloc(4096u * info.channels * sizeof(float));
    for (;;) {
        const maktabasauti_result_t r = maktabasauti_decoder_read(d, buf, 4096, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * info.channels * sizeof(float)); } memcpy(pcm + total * info.channels, buf, got * info.channels * sizeof(float)); total += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    free(buf); maktabasauti_decoder_close(d); free(data); *frames = total; return pcm;
}
static double rms(const float *p, size_t n) { double s = 0; size_t i; for (i = 0; i < n; i++) s += (double)p[i] * p[i]; return sqrt(s / (double)n) * 32768.0; }

int main(void) {
    size_t na, nu, pos = 0, bit0 = 5; uint8_t *asc = slurp("tests/data/usac_d1.asc", &na), *aus = slurp("tests/data/usac_d1.aus", &nu);
    static SautiUsacConfig cfg; static SautiUniDrcConfig dc; static SautiUniDrcLoudnessSet ls; static SautiUniDrcGain g; SautiUsacState *st; float *pcm;
    unsigned n = 0, ok = 0, seq1_min_ok = 0;
    if (!asc || !aus) { printf("cannot read tests/data/usac_d1.* (run from the repository root)\n"); return 1; }
    if ((asc[0] >> 3) == 31) bit0 = 11;
    bit0 += 8;
    CHECK(sauti_usac_config_parse(asc, na, bit0, &cfg) == 1, "USAC config");
    CHECK(cfg.drc_cfg_len > 0 && cfg.loud_len > 0, "uniDRC config / loudness not located in the USAC config (%u / %u bytes)", cfg.drc_cfg_len, cfg.loud_len);
    {
        SautiUsacBits b; b.data = asc; b.nbits = na * 8u; b.pos = cfg.drc_cfg_pos;
        CHECK(sauti_unidrc_config_parse(&b, &dc) == 1 && b.pos - cfg.drc_cfg_pos <= (size_t)cfg.drc_cfg_len * 8u && (size_t)cfg.drc_cfg_len * 8u - (b.pos - cfg.drc_cfg_pos) < 8u, "UniDrcConfig parse / length");
        CHECK(dc.sample_rate == 48000u && dc.base_channels == 2u && dc.dmx_count == 1u && dc.coeff_count == 1u && dc.instr_count == 2u, "config: rate %u ch %u dmx %u coeffs %u instr %u", dc.sample_rate, dc.base_channels, dc.dmx_count, dc.coeff_count, dc.instr_count);
        CHECK(dc.coeff[0].version == 1u && dc.coeff[0].location == 1u && dc.coeff[0].gain_set_count == 3u && dc.coeff[0].gain_seq_count == 3u, "coefficients");
        CHECK(dc.instr[0].set_id == 1u && dc.instr[0].effect == 0x1u && dc.instr[0].gain_set_index[0] == 0 && dc.instr[1].set_id == 2u && dc.instr[1].effect == 0x800u && dc.instr[1].gain_set_index[0] == 1, "instructions");
    }
    {
        SautiUsacBits b; b.data = asc; b.nbits = na * 8u; b.pos = cfg.loud_pos;
        CHECK(sauti_unidrc_loudness_parse(&b, &ls) == 1 && b.pos - cfg.loud_pos == (size_t)cfg.loud_len * 8u, "LoudnessInfoSet parse / length");
        CHECK(ls.count == 1u && ls.info[0].set_id == 1u && ls.info[0].measures == 1u && ls.info[0].measure[0].method == 1u && fabsf(ls.info[0].measure[0].value + 16.0f) < 1e-6f, "loudness: %.2f LUFS", (double)ls.info[0].measure[0].value);
    }
    st = (SautiUsacState *)malloc(sizeof *st); pcm = (float *)malloc(sizeof(float) * 4096u * 8u);
    sauti_usac_state_init(st, &cfg);
    while (pos + 4 <= nu) {
        const uint32_t len = ((uint32_t)aus[pos] << 24) | ((uint32_t)aus[pos + 1] << 16) | ((uint32_t)aus[pos + 2] << 8) | aus[pos + 3]; const uint8_t *au = aus + pos + 4; SautiAacReason why; SautiUsacBits b; size_t used, avail;
        pos += 4u + len; n++;
        CHECK(sauti_usac_decode(&cfg, st, au, len, pcm, &why) == 1, "AU %u decode", n);
        if (!st->drc_len) { CHECK(0, "AU %u: no UniDrcGain payload", n); continue; }
        b.data = au; b.nbits = (size_t)len * 8u; b.pos = st->drc_pos;
        if (sauti_unidrc_gain_parse(&dc, cfg.out_frame_len, cfg.sample_rate, &b, &g) != 1) { CHECK(0, "AU %u: UniDrcGain parse", n); continue; }
        used = b.pos - st->drc_pos; avail = (size_t)st->drc_len * 8u;
        CHECK(used <= avail && avail - used < 8u, "AU %u: used %zu of %zu bits", n, used, avail);
        CHECK(g.seq_count == 3u && g.seq[0].count > 0 && g.seq[1].count > 0 && g.seq[2].count > 0, "AU %u: sequences", n);
        ok++;
        if (g.seq[1].count && g.seq[1].gain_db[0] > -9.5f && g.seq[1].gain_db[0] < -6.5f) seq1_min_ok++;      /* the gain of the set 2 hovers around the -8 dB of the configuration */
    }
    CHECK(n == 34u && ok == 34u && seq1_min_ok >= 28u, "access units %u parsed %u, set 2 gain near -8 dB in %u", n, ok, seq1_min_ok);
    {   /* rejections: truncated and garbage data never crash and are refused */
        SautiUsacBits b; uint8_t junk[64]; unsigned i;
        b.data = asc; b.nbits = (cfg.drc_cfg_pos + 100u); b.pos = cfg.drc_cfg_pos;
        CHECK(sauti_unidrc_config_parse(&b, &dc) != 1, "truncated config accepted");
        for (i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 37u + 11u);
        for (i = 0; i < 40; i++) {
            SautiUsacBits jb; jb.data = junk + (i & 7u); jb.nbits = 8u * (sizeof junk - 8u); jb.pos = i;
            (void)sauti_unidrc_config_parse(&jb, &dc);
            jb.pos = i; (void)sauti_unidrc_loudness_parse(&jb, &ls);
        }
    }
    {   /* gain curve (U3): linear interpolation in the LINEAR domain between nodes; the output of a call is the PREVIOUS frame, completed by the first segment of this one */
        static SautiUniDrcGainSet gs; static SautiUniDrcNodes nd; static SautiUniDrcSeqState ss; static SautiUniDrcCurveParams cp; static float lin[2048];
        unsigned i;
        gs.interpolation = 1; gs.profile = 0; gs.band_count = 1;
        cp.boost = cp.compress = 1.0f;
        nd.count = 2; nd.time[0] = 1023; nd.gain_db[0] = -6.0f; nd.time[1] = 2015; nd.gain_db[1] = -6.0f;       /* frame A: the last node is 32 samples before the end */
        CHECK(sauti_unidrc_curve(&gs, &nd, 2048, 48000, &cp, &ss, lin) == 1, "curve 1");
        for (i = 0; i < 2048; i++) if (lin[i] != 1.0f) { CHECK(0, "first call must return unity (sample %u = %f)", i, (double)lin[i]); break; }
        nd.count = 2; nd.time[0] = 31; nd.gain_db[0] = 0.0f; nd.time[1] = 2047; nd.gain_db[1] = 0.0f;           /* frame B */
        CHECK(sauti_unidrc_curve(&gs, &nd, 2048, 48000, &cp, &ss, lin) == 1, "curve 2");
        CHECK(fabsf(lin[1023] - 0.5f) < 1e-6f && fabsf(lin[511] - 0.75f) < 1e-3f && fabsf(lin[0] - (1.0f - 0.5f / 1024.0f)) < 1e-3f, "frame A values %f %f %f", (double)lin[0], (double)lin[511], (double)lin[1023]);
        CHECK(fabsf(lin[2015] - 0.5f) < 1e-6f && fabsf(lin[2047] - 0.75f) < 1e-3f, "the tail of frame A is the first segment of frame B: %f %f", (double)lin[2015], (double)lin[2047]);
        nd.count = 1; nd.time[0] = 2047; nd.gain_db[0] = 0.0f;                                                   /* frame C returns frame B: 1.0 from sample 31 on */
        CHECK(sauti_unidrc_curve(&gs, &nd, 2048, 48000, &cp, &ss, lin) == 1 && fabsf(lin[31] - 1.0f) < 1e-6f && fabsf(lin[2047] - 1.0f) < 1e-6f, "frame B");
        cp.scaling_flag = 1; cp.attn = 0.5f; cp.ampl = 1.0f;                                                   /* attenuation scaled by one half: -6 dB -> -3 dB */
        memset(&ss, 0, sizeof ss); nd.count = 1; nd.time[0] = 2047; nd.gain_db[0] = -6.0f;
        CHECK(sauti_unidrc_curve(&gs, &nd, 2048, 48000, &cp, &ss, lin) == 1, "scaling frame 1");
        CHECK(sauti_unidrc_curve(&gs, &nd, 2048, 48000, &cp, &ss, lin) == 1 && fabsf(lin[2047] - (float)pow(2.0, -3.0 / 6.0)) < 1e-6f, "gain scaling");
        nd.time[0] = 5000; CHECK(sauti_unidrc_curve(&gs, &nd, 2048, 48000, &cp, &ss, lin) == 0, "a node far beyond the frame accepted");
    }
    {   /* the engine option: loudness normalisation reproduces FDK's constant gain exactly (target - the measured loudness: usac_x3 -16.25 LUFS -> -7.75 dB), DRC reproduces libxaac (see docs/UNIDRC_PLAN.md) */
        static const maktabasauti_option_t o_loud[] = { { "aac.loudness_target", "-24" } }, o_drc[] = { { "aac.drc", "on" } }, o_eff[] = { { "aac.drc", "on" }, { "aac.drc_effect", "1" } };
        size_t f0, f1, i; unsigned c0, c1; float *off = decode_file("tests/data/usac_x3.m4a", NULL, 0, &f0, &c0), *norm = decode_file("tests/data/usac_x3.m4a", o_loud, 1, &f1, &c1); double worst = 0;
        CHECK(off && norm && f0 == f1 && c0 == c1, "x3 decodes");
        if (off && norm && f0 == f1) {
            const float g = (float)pow(10.0, -7.75 / 20.0);
            for (i = 0; i < f0 * c0; i++) { const double e = fabs((double)norm[i] - (double)off[i] * (double)g); if (e > worst) worst = e; }
            CHECK(worst < 1e-6, "loudness normalisation: worst deviation from a gain of -7.75 dB is %g", worst);
        }
        free(off); free(norm);
        off = decode_file("tests/data/usac_d1.m4a", NULL, 0, &f0, &c0); norm = decode_file("tests/data/usac_d1.m4a", o_drc, 1, &f1, &c1);
        CHECK(off && norm && f0 == f1 && fabs(rms(off, f0 * c0) - 6201.369) < 0.01 && fabs(rms(norm, f1 * c1) - 2612.328) < 0.01, "DRC on usac_d1: rms %.3f -> %.3f (6201.369 -> 2612.328)", off ? rms(off, f0 * c0) : 0.0, norm ? rms(norm, f1 * c1) : 0.0);
        free(norm); norm = decode_file("tests/data/usac_d1.m4a", o_eff, 2, &f1, &c1);
        CHECK(norm && fabs(rms(norm, f1 * c1) - 1039.027) < 0.01, "DRC with the night effect: rms %.3f (1039.027)", norm ? rms(norm, f1 * c1) : 0.0);
        free(norm);
        {   /* FDK timeline: with SBR the gain runs 257 samples ahead of the PCM, so the ramp of the first access unit lands before sample 2048 (FDK: 0.84 / 0.52 / 0.40 at 1792 / 1856 / 1920; libxaac timeline: unity until 2048) */
            static const maktabasauti_option_t o_fdk[] = { { "aac.drc", "on" }, { "aac.drc_align", "fdk" } };
            float *fdk = decode_file("tests/data/usac_d1.m4a", o_fdk, 2, &f1, &c1);
            double num = 0, den = 0, num0 = 0; size_t i;
            norm = decode_file("tests/data/usac_d1.m4a", o_drc, 1, &f1, &c1);
            if (off && fdk && norm) for (i = 1984; i < 2048; i++) { num += (double)fdk[i * c0] * off[i * c0]; num0 += (double)norm[i * c0] * off[i * c0]; den += (double)off[i * c0] * off[i * c0]; }
            CHECK(off && fdk && norm && den > 0 && fabs(num / den - 0.40) < 0.02 && fabs(num0 / den - 1.0) < 1e-6, "lookahead 257: gain %.4f at 1984..2047 (FDK 0.40), libxaac timeline %.6f", den > 0 ? num / den : 0.0, den > 0 ? num0 / den : 0.0);
            free(fdk); free(norm);
        }
        free(off);
    }
    free(st); free(pcm); free(asc); free(aus);
    if (g_failures) { printf("test_unidrc: %d FAILURES\n", g_failures); return 1; }
    printf("test_unidrc: all passed\n");
    return 0;
}
