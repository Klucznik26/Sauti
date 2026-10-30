#include "sauti.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(offsetof(SautiFrameScratch, tone) == 16384, "ASM scratch tone offset");
_Static_assert(offsetof(SautiFrameScratch, fft_real) == 32768, "ASM real offset");
_Static_assert(offsetof(SautiFrameScratch, fft_imag) == 49152, "ASM imag offset");
_Static_assert(offsetof(SautiFrameScratch, magnitude) == 65536, "ASM magnitude offset");
_Static_assert(offsetof(SautiFrameScratch, phase_a) == 81920, "ASM phase A offset");
_Static_assert(offsetof(SautiFrameScratch, phase_b) == 81924, "ASM phase B offset");
_Static_assert(offsetof(SautiFrameScratch, db_bins) == 81928, "ASM db offset");
_Static_assert(offsetof(SautiFrameScratch, log_bands) == 98312, "ASM log offset");
_Static_assert(offsetof(SautiFrameScratch, smooth_bands) == 99336, "ASM smooth offset");
_Static_assert(sizeof(SautiFrameScratch) == 100360, "ASM scratch size");

static void require(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static void one_size(unsigned w, unsigned h) {
    size_t count = (size_t)w * h;
    uint32_t *buf = calloc(count + 2, sizeof(*buf));
    SautiFrameScratch *scratch = calloc(1, sizeof(*scratch));
    require(buf && scratch, "allocate frame test");
    buf[0] = 0x11112222u;
    buf[count + 1] = 0x33334444u;
    require(sauti_render_demo_frame(buf + 1, w, h, scratch) == 0, "render frame");
    require(buf[0] == 0x11112222u && buf[count + 1] == 0x33334444u,
            "render does not overrun pixels");
    size_t trace = 0, bars = 0;
    for (size_t i=1; i<=count; ++i) {
        trace += buf[i] == 0xff32e6c2u;
        bars += buf[i] == 0xffd66b75u || buf[i] == 0xff31d7b8u;
    }
    require(trace > 0 && bars > 0, "wave and FFT panels have nonempty content");
    float phase_before = scratch->phase_a;
    require(sauti_render_demo_frame(buf + 1, w, h, scratch) == 0, "render again");
    require(scratch->phase_a != phase_before, "phase advances between frames");
    require(buf[0] == 0x11112222u && buf[count + 1] == 0x33334444u,
            "second frame bounds");
    free(scratch);
    free(buf);
}
int main(void) {
    uint32_t fake[3] = {0xabcdef01u,0x12345678u,0x98765432u};
    SautiFrameScratch scratch = {0};
    require(sauti_render_demo_frame(fake+1, 127, 128, &scratch) == -1,
            "reject too small window");
    require(sauti_render_demo_frame(fake+1, 128, 4097, &scratch) == -1,
            "reject oversized window");
    require(sauti_render_demo_frame(fake+1, 128, 128, NULL) == -1,
            "reject NULL scratch");
    require(sauti_render_demo_frame(NULL, 128, 128, &scratch) == -1,
            "reject NULL pixel buffer");
    require(fake[0] == 0xabcdef01u && fake[1] == 0x12345678u &&
            fake[2] == 0x98765432u, "invalid frames do not write memory");
    one_size(128,128);
    one_size(641,361);
    one_size(1024,640);
    puts("PASS: portable ASM frame / resize / phase continuity / canaries");
    return 0;
}
