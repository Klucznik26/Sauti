#ifndef SAUTI_AAC_SBR_HUFFMAN_H
#define SAUTI_AAC_SBR_HUFFMAN_H

#include <stddef.h>
#include <stdint.h>

/* MPEG-4 AAC SBR classic entropy codebooks. Table IDs follow these
 * explicitly named configurations; they are NOT AAC-LC Huffman tables.
 * NOISE in the frequency direction uses the ENV_3_0DB_F family,
 * and balanced noise uses ENV_BAL_3_0DB_F. */
typedef enum {
    SAUTI_SBR_HUFF_ENV_1_5DB_T = 0,
    SAUTI_SBR_HUFF_ENV_1_5DB_F,
    SAUTI_SBR_HUFF_ENV_BAL_1_5DB_T,
    SAUTI_SBR_HUFF_ENV_BAL_1_5DB_F,
    SAUTI_SBR_HUFF_ENV_3_0DB_T,
    SAUTI_SBR_HUFF_ENV_3_0DB_F,
    SAUTI_SBR_HUFF_ENV_BAL_3_0DB_T,
    SAUTI_SBR_HUFF_ENV_BAL_3_0DB_F,
    SAUTI_SBR_HUFF_NOISE_3_0DB_T,
    SAUTI_SBR_HUFF_NOISE_BAL_3_0DB_T,
    SAUTI_SBR_HUFF_TABLE_COUNT
} SautiAacSbrHuffmanBook;

/* Decode precisely one signed SBR Huffman delta using the Sauti NASM
 * bit reader. Works at any bit offset, no allocation, no FFmpeg.
 * Returns 1 on success. On invalid ID, arguments or truncation returns
 * 0 and leaves both *bit_offset and *out_delta unchanged. */
int sauti_aac_sbr_huffman_decode(const uint8_t *bytes, size_t size,
                                  size_t *bit_offset,
                                  SautiAacSbrHuffmanBook book,
                                  int32_t *out_delta);
#endif
