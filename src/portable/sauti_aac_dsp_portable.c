/* C odpowiednik sauti_aac_dsp.asm (patrz sauti_portable.h). */
#include "sauti_portable.h"
#include "sauti_aac_syntax.h"
int sauti_aac_window_overlap_asm(const float *imdct, const float *window, float *overlap, float *out, size_t n) {
    size_t i;
    if (!imdct || !window || !overlap || !out || (n != 128u && n != 1024u && n != 120u && n != 960u)) return 0;
    for (i = 0; i < n; i++) {
        const float o = imdct[i] * window[i] + overlap[i];
        out[i] = o;
        overlap[i] = imdct[n + i] * window[n + i];
    }
    return 1;
}
