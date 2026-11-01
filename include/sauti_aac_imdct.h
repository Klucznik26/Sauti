#ifndef SAUTI_AAC_IMDCT_H
#define SAUTI_AAC_IMDCT_H
#include <stddef.h>

/* Inverse MDCT primitive: n spectral floats -> 2*n time samples.
 * Supported n: 128 and 1024. The normalization convention is 2/n:
 * y[t] = 2/n * sum_k x[k] cos(pi/n * (t+0.5+n/2)*(k+0.5)).
 *
 * This is an unwindowed mathematical transform, NOT finished AAC PCM.
 * No FFmpeg, allocations or persistent state. Output is untouched on error.
 * Source and destination may alias (input is only read before output commit).
 */
int sauti_aac_imdct(const float *spectral,size_t n,float *time_samples);

/* Own NASM SSE2 dot-product kernel, exposed for native regression tests. */
double sauti_aac_imdct_dot_asm(const float *spectral,
                              const float *basis,size_t n);
#endif
