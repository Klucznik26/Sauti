/* SBR-58 diagnostic-only direct QMF modulation exporter. */
#include "sauti_aac_sbr_qmf_synth_mod.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SLOTS 32u
#define BANDS 64u
#define WORDS (SLOTS * BANDS * 2u)
#define VALUES (SLOTS * 128u)

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s FF_X.f32le SAUTI_V.f32le SCALE\n", argv[0]);
        return 2;
    }
    char *end = NULL;
    errno = 0;
    const float scale = strtof(argv[3], &end);
    if (errno || !end || *end || !isfinite(scale) || scale <= 0.0f) {
        fputs("SBR-58: SCALE must be finite and positive\n", stderr);
        return 2;
    }
    FILE *input = fopen(argv[1], "rb");
    if (!input) { perror(argv[1]); return 2; }
    FILE *output = fopen(argv[2], "wb");
    if (!output) { perror(argv[2]); fclose(input); return 2; }

    float raw[WORDS], modulation[VALUES];
    SautiAacSbrComplex bands[SLOTS * BANDS];
    size_t frames = 0;
    int ok = 1;
    while (fread(raw, sizeof(*raw), WORDS, input) == WORDS) {
        for (unsigned word = 0; word < WORDS; ++word)
            if (!isfinite(raw[word])) { ok = 0; break; }
        for (unsigned slot = 0; ok && slot < SLOTS; ++slot)
            for (unsigned band = 0; band < BANDS; ++band) {
                const size_t word = ((size_t)slot * BANDS + band) * 2u;
                bands[(size_t)slot * BANDS + band] = (SautiAacSbrComplex){
                    raw[word] / scale, raw[word + 1u] / scale};
            }
        if (!ok || !sauti_aac_sbr_qmf_synth_modulate_64(
                bands, SLOTS * BANDS, SLOTS, modulation, VALUES) ||
            fwrite(modulation, sizeof(*modulation), VALUES, output) != VALUES ||
            fflush(output) != 0) { ok = 0; break; }
        ++frames;
    }
    if (!feof(input) || ferror(input) || !frames) ok = 0;
    if (fclose(input) != 0 || fclose(output) != 0) ok = 0;
    if (!ok) {
        (void)remove(argv[2]);
        fputs("SBR-58 modulation export rejected; partial output removed\n", stderr);
        return 2;
    }
    fprintf(stderr, "SBR-58 Sauti modulation exported frames=%zu slots=%zu\n",
            frames, frames * (size_t)SLOTS);
    return 0;
}
