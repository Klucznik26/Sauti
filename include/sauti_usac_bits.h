#ifndef SAUTI_USAC_BITS_H
#define SAUTI_USAC_BITS_H
#include <stddef.h>
#include <stdint.h>
/* MSB-first bit reader of the USAC decoder.  Reads past the end return zero bits but still advance `pos` (the arithmetic decoder looks ahead of the data it really consumes); the caller
 * compares `pos` with `nbits` at the end of an element / frame to detect an overrun. */
typedef struct { const uint8_t *data; size_t nbits; size_t pos; } SautiUsacBits;
static inline uint32_t sauti_usac_get(SautiUsacBits *b, unsigned n) {
    uint32_t v = 0;
    while (n--) {
        const size_t p = b->pos++;
        v <<= 1;
        if (p < b->nbits) v |= (uint32_t)((b->data[p >> 3] >> (7u - (p & 7u))) & 1u);
    }
    return v;
}
/* escapedValue(nBits1, nBits2, nBits3) of ISO/IEC 23003-3 */
static inline uint32_t sauti_usac_escaped(SautiUsacBits *b, unsigned n1, unsigned n2, unsigned n3) {
    uint32_t v = sauti_usac_get(b, n1), v2;
    if (v < (1u << n1) - 1u) return v;
    v += v2 = sauti_usac_get(b, n2);
    if (n3 && v2 == (1u << n2) - 1u) v += sauti_usac_get(b, n3);
    return v;
}
#endif
