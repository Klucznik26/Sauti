/* SBR-57 diagnostic-only 64-band QMF synthesis exporter.
 * Reads records written by the disposable FFmpeg SBR-57 trace:
 * 32 slots * 64 bands * (real,imaginary) f32le values per record.
 * SCALE converts those reference QMF units into Sauti's float units.
 * It is not linked into the decoder. */
#include "sauti_aac_sbr_qmf_synth_mod.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SLOTS 32u
#define BANDS 64u
#define WORDS (SLOTS * BANDS * 2u)
#define PCM (SLOTS * 64u)

static int write_all(FILE *file, const float *values, size_t count) {
    return fwrite(values, sizeof(*values), count, file) == count && fflush(file) == 0;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s FF_X.f32le SAUTI_PCM.f32le SCALE\n", argv[0]);
        return 2;
    }
    char *end = NULL;
    errno = 0;
    const float scale = strtof(argv[3], &end);
    if (errno || !end || *end || !isfinite(scale) || scale <= 0.0f) {
        fputs("SBR-57: SCALE must be finite and positive\n", stderr);
        return 2;
    }
    FILE *input = fopen(argv[1], "rb");
    if (!input) { perror(argv[1]); return 2; }
    FILE *output = fopen(argv[2], "wb");
    if (!output) { perror(argv[2]); fclose(input); return 2; }

    float raw[WORDS], pcm[PCM];
    SautiAacSbrComplex bands[SLOTS * BANDS];
    SautiAacSbrQmfSynthWindowState state = {0};
    size_t frames = 0;
    int ok = 1;
    while (fread(raw, sizeof(*raw), WORDS, input) == WORDS) {
        for (unsigned slot = 0; slot < SLOTS; ++slot)
            for (unsigned band = 0; band < BANDS; ++band) {
                const size_t word = ((size_t)slot * BANDS + band) * 2u;
                if (!isfinite(raw[word]) || !isfinite(raw[word + 1u])) { ok = 0; break; }
                bands[(size_t)slot * BANDS + band] = (SautiAacSbrComplex){
                    raw[word] / scale, raw[word + 1u] / scale
                };
            }
        if (!ok || !sauti_aac_sbr_qmf_synth_64(
                bands, SLOTS * BANDS, SLOTS, pcm, PCM, &state) ||
            !write_all(output, pcm, PCM)) { ok = 0; break; }
        ++frames;
    }
    if (!feof(input) || ferror(input) || !frames) ok = 0;
    if (fclose(input) != 0 || fclose(output) != 0) ok = 0;
    if (!ok) {
        (void)remove(argv[2]);
        fputs("SBR-57 synthesis export rejected; partial output removed\n", stderr);
        return 2;
    }
    fprintf(stderr, "SBR-57 Sauti synthesis exported frames=%zu slots=%zu\n", frames, frames * (size_t)SLOTS);
    return 0;
}
