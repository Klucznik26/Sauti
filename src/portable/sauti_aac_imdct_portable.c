/* C odpowiednik sauti_aac_imdct.asm (patrz sauti_portable.h). */
#include "sauti_portable.h"
#include "sauti_aac_imdct.h"
double sauti_aac_imdct_dot_asm(const float *spectral, const float *basis, size_t n) {
    double acc = 0.0; size_t i;
    if (!spectral || !basis || !n) return 0.0;
    for (i = 0; i < n; i++) acc += (double)spectral[i] * (double)basis[i];                  /* konwersja do double przed mnozeniem, jak cvtss2sd + mulsd + addsd */
    return acc;
}
