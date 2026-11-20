/* C odpowiednik sauti_aac_spectral.asm (patrz sauti_portable.h). */
#include "sauti_portable.h"
#include "sauti_aac_spectral.h"
int sauti_aac_spectral_index_asm(uint32_t codebook, uint32_t index, int16_t output[4]) {
    uint32_t radix, offset, count, limit, v; int16_t t[4] = { 0, 0, 0, 0 };
    if (!output) return 0;
    if (codebook >= 1u && codebook <= 2u) { radix = 3; offset = 1; count = 4; limit = 81; }
    else if (codebook >= 3u && codebook <= 4u) { radix = 3; offset = 0; count = 4; limit = 81; }
    else if (codebook >= 5u && codebook <= 6u) { radix = 9; offset = 4; count = 2; limit = 81; }
    else if (codebook >= 7u && codebook <= 8u) { radix = 8; offset = 0; count = 2; limit = 64; }
    else if (codebook >= 9u && codebook <= 10u) { radix = 13; offset = 0; count = 2; limit = 169; }
    else if (codebook == 11u) { radix = 17; offset = 0; count = 2; limit = 289; }
    else return 0;
    if (index >= limit) return 0;
    v = index;
    while (count) { count--; t[count] = (int16_t)(uint16_t)((v % radix) - offset); v /= radix; }   /* cyfry od najmlodszej do ostatniego slotu, jak w asemblerze */
    memcpy(output, t, sizeof t);
    return 1;
}
