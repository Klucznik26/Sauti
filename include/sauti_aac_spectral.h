#ifndef SAUTI_AAC_SPECTRAL_H
#define SAUTI_AAC_SPECTRAL_H
#include <stddef.h>
#include <stdint.h>
#include "sauti_aac_huffman.h"

/* ISO AAC spectral codebook index -> tuple (up to 4 int16 values).
 * Books: 1..4 signed/unsigned quads, 5..11 signed/unsigned pairs.
 * Books 0, 12..15 (ZERO/PNS/intensity/reserved) are not spectral tuples.
 * No Huffman codewords are supplied by this function.
 * Implemented in original x86-64 NASM. Failure does not modify output. */
int sauti_aac_spectral_index_asm(uint32_t codebook,uint32_t index,
                                 int16_t output[4]);

/* Complete an already decoded spectral Huffman index by reading sign bits,
 * and for ESC_HCB 11, escape magnitudes (up to AAC maximum 8191).
 * The bitstream is positioned just AFTER its Huffman codeword.
 * Codebook tables are not yet included. All outputs remain unchanged on
 * failure; zero padding in unused channels of pair codebooks.
 * Orchestration C calls the NASM tuple mapper and NASM bitreader. */
int sauti_aac_spectral_finish(uint32_t codebook,uint32_t index,
                              const uint8_t *bytes,size_t length,
                              size_t *bit_offset,int32_t output[4]);

/* Decode one Huffman symbol with validated tree, then map its AAC spectral
 * codebook index, and finally apply signs and escapes. This is an actual
 * bitstream path, but only interoperable with AAC when normative Huffman
 * codeword tables are supplied by the caller (not yet bundled).
 * Atomic on errors: output and bit_offset unchanged. */
int sauti_aac_spectral_decode(uint32_t codebook,
                              const uint8_t *bytes,size_t length,
                              size_t *bit_offset,
                              const SautiAacHuffNode *nodes,size_t node_count,
                              int32_t output[4]);
#endif
