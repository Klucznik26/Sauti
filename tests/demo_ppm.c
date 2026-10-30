#include "sauti.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "sauti-demo.ppm";
    enum { W = 1024, H = 576, N = 4096 };
    float *samples = calloc(N, sizeof(float));
    float *second = calloc(N, sizeof(float));
    float *re = calloc(N, sizeof(float));
    float *im = calloc(N, sizeof(float));
    float *mag = calloc(N / 2 + 1, sizeof(float));
    uint32_t *pixels = calloc(W * H, sizeof(uint32_t));
    if (!samples || !second || !re || !im || !mag || !pixels) return 1;
    float phase = 0.f;
    sauti_sine_f32(samples, N, &phase, (float)(2.0 * 3.14159265358979323846 * 35 / N));
    phase = 0.f;
    sauti_sine_f32(second, N, &phase, (float)(2.0 * 3.14159265358979323846 * 117 / N));
    for (unsigned i = 0; i < N; i++) samples[i] = 0.62f * samples[i] + 0.32f * second[i];
    if (sauti_fft_real_f32(samples, N, re, im) != 0) return 2;
    if (sauti_fft_magnitude_f32(re, im, N, mag) != 0) return 3;
    sauti_render_wave_argb32(pixels, W, H / 2, samples, N);
    sauti_render_spectrum_argb32(pixels + W * (H / 2), W, H / 2, mag, N / 2 + 1);
    FILE *f = fopen(path, "wb");
    if (!f) return 4;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (size_t i = 0; i < (size_t)W * H; i++) {
        uint32_t color = pixels[i];
        unsigned char rgb[3] = {(unsigned char)(color >> 16),
                                (unsigned char)(color >> 8), (unsigned char)color};
        if (fwrite(rgb, 1, 3, f) != 3) return 5;
    }
    if (fclose(f) != 0) return 6;
    free(pixels); free(mag); free(im); free(re); free(second); free(samples);
    printf("Sayari Sauti: %s (%dx%d, windowed FFT)\n", path, W, H);
    return 0;
}
