/* C odpowiednik sauti_aac_dequant.asm (patrz sauti_portable.h). */
#include "sauti_portable.h"
#include "sauti_aac_dequant.h"
int sauti_aac_dequant_scale_asm(const int32_t *q, const float *positive_pow43, const float *scale, float *dst, size_t count) {
    size_t i; float gain;
    if (!q || !positive_pow43 || !scale || !dst || !count || count > 1024u) return 0;
    gain = *scale;
    for (i = 0; i < count; i++) { float v = positive_pow43[i] * gain; dst[i] = q[i] < 0 ? -v : v; }   /* znak jak xorps z maska bitu znaku */
    return 1;
}
