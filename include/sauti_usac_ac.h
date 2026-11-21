#ifndef SAUTI_USAC_AC_H
#define SAUTI_USAC_AC_H
#include "sauti_usac_bits.h"
/* Context-based arithmetic coder of the USAC spectral noiseless coding (ISO/IEC 23003-3, 7.4): two-tuples of quantised lines, the context built from the two neighbouring tuples of the
 * current window and the tuple at the same position of the previous window (`last`, resampled when the window length changes). */
typedef struct {
    uint8_t last[512 + 1];
    int last_len;
    uint8_t cur[4];
    uint32_t state_pre;
} SautiUsacAcState;
/* Decodes the `lg` lines (an even count) of one window of length `n` (1024 / 768 / 128 / 96) into signed integers q[0..n) (lines above lg are 0).  `reset` clears the context (first window
 * of an independent frame).  1 = ok, 0 = malformed (escape sequence too long).  The bit position ends where the arithmetic code ends (the 14 bits of look-ahead are given back). */
int sauti_usac_ac_decode(SautiUsacBits *b, SautiUsacAcState *state, int reset, unsigned lg, unsigned n, int32_t *q);
#endif
