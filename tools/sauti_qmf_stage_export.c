/* SBR-55 diagnostic-only QMF stage exporter.  It mirrors the direct Sauti
 * analysis formula for shared f32le core PCM and writes one 64-float vector
 * after polyphase summation plus one 64-float vector in FFmpeg post-shuffle
 * coordinates per QMF slot.  It is not linked into the decoder. */
#include "sauti_aac_sbr_qmf_synth_window.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846264338327950288

static int load(FILE *file, float *dst, size_t count) {
    return fread(dst, sizeof(*dst), count, file) == count && !ferror(file);
}
static int store(FILE *file, const float *src, size_t count) {
    return fwrite(src, sizeof(*src), count, file) == count && fflush(file) == 0;
}
int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s CORE.f32le BRANCH0.f32le PRE.f32le POST.f32le\n", argv[0]);
        return 2;
    }
    FILE *core = fopen(argv[1], "rb"), *branch0 = NULL, *pre = NULL, *post = NULL;
    if (!core || !(branch0 = fopen(argv[2], "wb")) || !(pre = fopen(argv[3], "wb")) || !(post = fopen(argv[4], "wb"))) {
        perror("SBR-55 stage exporter");
        if (core) fclose(core);
        if (branch0) fclose(branch0);
        if (pre) fclose(pre);
        if (post) fclose(post);
        return 2;
    }
    float taps[320], history[320] = {0}, block[32], u[64], z[64];
    for (unsigned i = 0; i < 320; ++i)
        if (!sauti_aac_sbr_qmf_synth_window_coefficient(2u * i, &taps[i])) return 2;
    size_t slots = 0;
    while (load(core, block, 32)) {
        for (unsigned i = 0; i < 32; ++i) if (!isfinite(block[i])) return 2;
        memmove(history + 32, history, 288 * sizeof(*history));
        for (unsigned i = 0; i < 32; ++i) history[31u - i] = block[i];
        for (unsigned n = 0; n < 64; ++n) {
            double sum = 0.0;
            for (unsigned j = 0; j < 5; ++j) sum += (double)history[n + 64u*j] * taps[n + 64u*j];
            if (!isfinite(sum) || fabs(sum) > 3.4e38) return 2;
            u[n] = (float)sum;
        }
        for (unsigned k = 0; k < 32; ++k) {
            double re = 0.0, im = 0.0;
            for (unsigned n = 0; n < 64; ++n) {
                const double theta = (PI / 64.0) * ((double)k + 0.5) * (2.0 * n - 0.5);
                re += 2.0 * u[n] * cos(theta); im += 2.0 * u[n] * sin(theta);
            }
            z[k] = (float)im;
            z[63u-k] = (float)-re;
        }
        const float terms[5] = { history[0] * taps[0], history[64] * taps[64],
                                 history[128] * taps[128], history[192] * taps[192],
                                 history[256] * taps[256] };
        if (!store(branch0, terms, 5) || !store(pre, u, 64) || !store(post, z, 64)) return 2;
        ++slots;
    }
    if (!feof(core)) return 2;
    fclose(core); fclose(branch0); fclose(pre); fclose(post);
    fprintf(stderr, "SBR-55 Sauti QMF stages exported slots=%zu\n", slots);
    return slots ? 0 : 2;
}
