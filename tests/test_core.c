#include "sauti.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(void) {
    enum { N = 4800, WIDTH = 640, HEIGHT = 360 };
    float *samples = calloc(N + 2, sizeof(*samples));
    uint32_t *pixels = calloc(WIDTH * HEIGHT + 2, sizeof(*pixels));
    check(samples != NULL && pixels != NULL, "allocations");
    samples[0] = 1234.0f;
    samples[N + 1] = -1234.0f;
    pixels[0] = 0x12345678u;
    pixels[WIDTH * HEIGHT + 1] = 0x87654321u;
    float phase = 0.0f;
    const float step = (float)(2.0 * 3.14159265358979323846 * 440.0 / 48000.0);
    sauti_sine_f32(samples + 1, N, &phase, step);
    check(samples[0] == 1234.0f && samples[N + 1] == -1234.0f, "sample canaries");
    check(fabsf(samples[1]) < 0.002f, "zero phase");
    check(fabsf(samples[1 + 25] - 0.991445f) < 0.02f, "440 Hz sin sample");
    check(phase >= 0.0f && phase < 6.284f, "phase wraps");

    SautiMetrics m = {-1.0f, -1.0f};
    sauti_metrics_f32(samples + 1, N, &m);
    check(m.peak > 0.99f && m.peak <= 1.01f, "peak");
    check(fabsf(m.rms - 0.70710678f) < 0.01f, "rms");
    sauti_metrics_f32(NULL, 0, &m);
    check(m.peak == 0.0f && m.rms == 0.0f, "zero input stats");

    sauti_render_wave_argb32(pixels + 1, WIDTH, HEIGHT, samples + 1, N);
    check(pixels[0] == 0x12345678u && pixels[WIDTH * HEIGHT + 1] == 0x87654321u,
          "render canaries");
    unsigned long trace_count = 0;
    for (unsigned i = 1; i <= WIDTH * HEIGHT; ++i)
        trace_count += pixels[i] == 0xff32e6c2u;
    check(trace_count > WIDTH, "nonempty waveform trace");
    sauti_render_wave_argb32(NULL, WIDTH, HEIGHT, samples + 1, N);
    puts("PASS: sine / phase / RMS / peak / ARGB scope / canary guards");
    free(samples);
    free(pixels);
    return 0;
}
