#include "sauti.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void test_tone(unsigned n, unsigned bin) {
    float *input = calloc(n + 2, sizeof(float));
    float *re = calloc(n + 2, sizeof(float));
    float *im = calloc(n + 2, sizeof(float));
    float *mag = calloc(n / 2 + 3, sizeof(float));
    check(input && re && im && mag, "alloc FFT buffers");
    input[0] = 123.f; input[n + 1] = 321.f;
    re[0] = 456.f; re[n + 1] = 654.f;
    im[0] = 789.f; im[n + 1] = 987.f;
    mag[0] = 234.f; mag[n/2 + 2] = 432.f;
    float phase = 0.0f;
    sauti_sine_f32(input + 1, n, &phase, (float)(2.0 * 3.14159265358979323846 * bin / n));
    check(sauti_fft_real_f32(input+1, n, re+1, im+1) == 0, "fft accepted valid n");
    check(sauti_fft_magnitude_f32(re+1, im+1, n, mag+1) == 0, "magnitude accepted valid n");
    check(input[0] == 123.f && input[n+1] == 321.f, "input unchanged");
    check(re[0] == 456.f && re[n+1] == 654.f, "real guards");
    check(im[0] == 789.f && im[n+1] == 987.f, "imag guards");
    check(mag[0] == 234.f && mag[n/2+2] == 432.f, "mag guards");
    check(fabsf(mag[bin+1]-1.f) < 0.008f, "coherent Hann tone amplitude");
    check(fabsf(mag[bin]-0.5f) < 0.008f, "Hann left neighbor");
    check(fabsf(mag[bin+2]-0.5f) < 0.008f, "Hann right neighbor");
    check(mag[bin+5] < 0.006f, "far sidelobe remains small");
    check(im[bin+1] < -0.01f, "negative-exponent FFT sign");
    free(input); free(re); free(im); free(mag);
}

static void test_dc_and_impulse(void) {
    enum { N = 256 };
    float input[N] = {0}, re[N], im[N], mag[N / 2 + 1];
    for (unsigned i=0; i<N; i++) input[i] = 0.75f;
    check(sauti_fft_real_f32(input, N, re, im) == 0, "constant FFT");
    check(sauti_fft_magnitude_f32(re, im, N, mag) == 0, "constant magnitude");
    check(fabsf(mag[0] - 0.75f) < 0.002f, "Hann corrected DC amplitude");
    check(mag[4] < 0.002f, "constant higher bin leakage");
    memset(input, 0, sizeof(input));
    input[N / 2] = 1.0f; /* Hann window peaks at its center */
    check(sauti_fft_real_f32(input, N, re, im) == 0, "impulse FFT");
    check(sauti_fft_magnitude_f32(re, im, N, mag) == 0, "impulse magnitude");
    check(fabsf(mag[8] - (4.0f / N)) < 0.0001f, "impulse unnormalized fft and Hann scaling");
}

static void test_spectrum_renderer(void) {
    enum { W = 256, H = 128, BINS = 129 };
    uint32_t *pixels = calloc(W * H + 2, sizeof(uint32_t));
    float mags[BINS] = {0};
    check(pixels != NULL, "render allocation");
    pixels[0] = 0x11223344u;
    pixels[W * H + 1] = 0x55667788u;
    mags[11] = 0.99f;
    mags[12] = 0.65f;
    mags[25] = 0.2f;
    sauti_render_spectrum_argb32(pixels + 1, W, H, mags, BINS);
    check(pixels[0] == 0x11223344u && pixels[W * H + 1] == 0x55667788u,
          "spectrum renderer buffer boundaries");
    size_t painted = 0;
    for (unsigned i = 1; i <= W * H; ++i)
        painted += pixels[i] == 0xffd66b75u || pixels[i] == 0xff31d7b8u;
    check(painted > 100, "spectrum bars rendered");
    uint32_t saved = pixels[1];
    sauti_render_spectrum_argb32(pixels + 1, 0, H, mags, BINS);
    check(pixels[1] == saved, "invalid surface leaves buffer untouched");
    sauti_render_spectrum_argb32(NULL, W, H, mags, BINS);
    free(pixels);
}

int main(void) {
    test_tone(64, 8);
    test_tone(256, 13);
    test_tone(1024, 32);
    test_tone(4096, 95);
    float in[65] = {0}, re[65] = {0}, im[65] = {0}, mag[34] = {0};
    check(sauti_fft_real_f32(in, 63, re, im) == -1, "reject non-power-of-two");
    check(sauti_fft_real_f32(in, 8192, re, im) == -1, "reject oversized FFT");
    check(sauti_fft_real_f32(NULL, 64, re, im) == -1, "reject missing input");
    check(sauti_fft_magnitude_f32(re, im, 65, mag) == -1, "reject invalid magnitude n");
    check(sauti_fft_magnitude_f32(re, im, 64, NULL) == -1, "reject null output");
    check(sauti_fft_real_f32(in, 64, re, im) == 0, "FFT silence");
    check(sauti_fft_magnitude_f32(re, im, 64, mag) == 0, "magnitude silence");
    for (unsigned i=0;i<33;i++) check(mag[i]==0.0f, "zero spectrum");
    test_dc_and_impulse();
    test_spectrum_renderer();
    puts("PASS: spectrum renderer and boundary guards");
    puts("PASS: Hann FFT 64/256/1024/4096, bin peaks, conjugate sign, silence, guards");
    return 0;
}
