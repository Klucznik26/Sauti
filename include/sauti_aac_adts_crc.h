#ifndef SAUTI_AAC_ADTS_CRC_H
#define SAUTI_AAC_ADTS_CRC_H

#include <stddef.h>
#include <stdint.h>

/* ISO/IEC ADTS error-protection primitive: CRC-16 MSB-first, polynomial
 * x^16+x^15+x^2+1 (0x8005), initial register 0xffff and no final xor.
 * The caller supplies precisely the standard-defined protected bit range;
 * this helper deliberately does not pretend an entire ADTS frame is one
 * universally valid CRC region. */
int sauti_aac_adts_crc16_bits(const uint8_t *bytes,size_t bytes_count,
                              size_t bit_start,size_t bit_count,
                              uint16_t *crc);

/* Continue an existing ADTS CRC register with another MSB-first bit range.
 * This is for the non-contiguous protected syntax regions of raw_data_block.
 * On invalid bounds the previous register is left unchanged. */
int sauti_aac_adts_crc16_update_bits(const uint8_t *bytes,size_t bytes_count,
                                     size_t bit_start,size_t bit_count,
                                     uint16_t *crc_state);
int sauti_aac_adts_crc16_update_zero_bits(size_t bit_count,uint16_t *crc_state);

#endif
