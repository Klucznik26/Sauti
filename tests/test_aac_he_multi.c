/* HE-AAC 5.1 (sauti_aac_he_multi): tests/data/aac_he_51.mp4 (AudioSpecificConfig with explicit SBR) and aac_he_51.aac (the same access units in ADTS: implicit SBR, found by the engine on the first
 * frame) are made from a synthetic 5-tone signal with the FDK encoder (tools/sauti_fdk_aac_encoder.c, MODE_1_2_2_1, 24 kHz core / 48 kHz out): C 1 kHz + 15 kHz (above the core band: SBR only),
 * L 2 kHz, R 3 kHz, Ls 500 Hz, Rs 4 kHz, LFE 700 Hz.  Checked: 6 channels at 48 kHz in the output order FL FR FC LFE BL BR, the tone of every channel, the SBR tone of the centre channel,
 * ADTS = MP4 bit for bit, and seeking = sequential decoding.  The PCM against FFmpeg (135 dB, 70 dB for the centre channel's HF) and the ISO streams: scripts/test_iso_conformance_vs_ffmpeg.py. */
#include "maktabasauti/maktabasauti.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(c, ...) do { if (!(c)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
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
static float *decode_all(const uint8_t *data, size_t n, size_t *frames, maktabasauti_stream_info_t *info, const char *what) {
    maktabasauti_decoder_t *d = open_mem(data, n, info); float *pcm = NULL, buf[6 * 1024]; size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    *frames = 0;
    CHECK(d != NULL, "%s: open", what);
    if (!d) return NULL;
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, 1024, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if ((total + got) * 6u > cap) { cap = (total + got) * 12u + 6144u; pcm = (float *)realloc(pcm, cap * sizeof(float)); } memcpy(pcm + total * 6u, buf, got * 6u * sizeof(float)); total += got; }
        if (r != MAKTABASAUTI_OK) { CHECK(r == MAKTABASAUTI_EOF, "%s: read %d", what, (int)r); break; }
    }
    maktabasauti_decoder_close(d); *frames = total; return pcm;
}
static double bin(const float *pcm, size_t at, size_t len, unsigned ch, double hz) {          /* Goertzel-like single-frequency magnitude */
    double re = 0, im = 0; size_t i;
    for (i = 0; i < len; i++) { const double w = 2.0 * 3.14159265358979323846 * hz * (double)i / 48000.0, v = pcm[(at + i) * 6u + ch]; re += v * cos(w); im += v * sin(w); }
    return sqrt(re * re + im * im) * 2.0 / (double)len;
}
int main(void) {
    size_t nm, na, fm, fa, k; maktabasauti_stream_info_t im, ia; float *pm, *pa;
    uint8_t *mp4 = slurp("tests/data/aac_he_51.mp4", &nm), *adts = slurp("tests/data/aac_he_51.aac", &na);
    static const double tone[6] = { 2000.0, 3000.0, 1000.0, 0.0, 500.0, 4000.0 };         /* FL FR FC LFE BL BR (the LFE is not checked) */
    if (!mp4 || !adts) { printf("FAIL cannot read the fixtures (run from the repository root)\n"); return 1; }
    pm = decode_all(mp4, nm, &fm, &im, "mp4"); pa = decode_all(adts, na, &fa, &ia, "adts");
    CHECK(im.channels == 6u && im.sample_rate == 48000u && ia.channels == 6u && ia.sample_rate == 48000u, "stream %u ch %u Hz / %u ch %u Hz", (unsigned)im.channels, (unsigned)im.sample_rate, (unsigned)ia.channels, (unsigned)ia.sample_rate);
    CHECK(fm == 98304u && fa == fm, "lengths %zu / %zu", fm, fa);
    if (pm && pa && fm == fa) {
        CHECK(memcmp(pm, pa, fm * 6u * sizeof(float)) == 0, "ADTS (implicit SBR) differs from MP4 (explicit SBR)");
        for (k = 0; k < 6u; k++) {
            if (k == 3u) continue;
            const double want = bin(pm, 20000, 4800, (unsigned)k, tone[k]), other = bin(pm, 20000, 4800, (unsigned)k, tone[k] * 1.5);
            CHECK(want > 0.15 && other < 0.01, "channel %zu: tone %.0f Hz %.4f, off-tone %.4f", k, tone[k], want, other);
        }
        { const double hf = bin(pm, 20000, 4800, 2u, 14750.0), none = bin(pm, 20000, 4800, 0u, 14750.0);                 /* the 15 kHz tone of the centre exists only through SBR */
          CHECK(hf > 0.02 && none < 0.002, "SBR band of the centre: %.4f, of the left: %.4f", hf, none); }
    }
    { maktabasauti_stream_info_t i2; maktabasauti_decoder_t *d = open_mem(adts, na, &i2); float buf[6 * 512]; size_t got = 0;
      if (d && pm) {
          CHECK(maktabasauti_decoder_seek(d, 30000, 0u, &got) == MAKTABASAUTI_OK && got == 30000u, "seek");
          CHECK(maktabasauti_decoder_read(d, buf, 512, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 512u, "read after seek");
          CHECK(memcmp(buf, pm + 30000u * 6u, 512u * 6u * sizeof(float)) == 0, "seek differs from sequential decoding");
      }
      if (d) maktabasauti_decoder_close(d); }
    free(pm); free(pa); free(mp4); free(adts);
    if (g_failures) { printf("%d failure(s)\n", g_failures); return 1; }
    puts("PASS HE-AAC 5.1: explicit and implicit SBR, tones per channel, SBR band, seek");
    return 0;
}
