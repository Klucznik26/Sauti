#ifndef SAUTI_AAC_SBR_CRC_H
#define SAUTI_AAC_SBR_CRC_H

#include <stddef.h>
#include <stdint.h>

/* ISO/IEC 14496-3 SBR extension CRC: G10(x) =
 * x^10 + x^9 + x^5 + x^4 + x + 1, initial value zero. Bits are
 * consumed MSB-first in coded order. This helper does not add a zero
 * flush or invert the result. It is intentionally a bounded bit-range
 * primitive so an unaligned AAC FIL payload can be checked exactly up to
 * (but never including) bs_fill_bits. */
int sauti_aac_sbr_crc10_bits(const uint8_t *bytes,size_t bytes_count,
                             size_t bit_start,size_t bit_count,
                             uint16_t *crc);

#endif
