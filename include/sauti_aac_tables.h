#ifndef SAUTI_AAC_TABLES_H
#define SAUTI_AAC_TABLES_H
#include "sauti_aac_huffman.h"
/* book=0: scalefactor (121 symbols), books 1..11: spectral.
 * Caller supplies validated node storage; no heap allocation. */
int sauti_aac_standard_codeword(unsigned book,unsigned symbol,
                               uint32_t *code,uint8_t *bits);
size_t sauti_aac_standard_tree_build(unsigned book,SautiAacHuffNode *nodes,
                                     size_t capacity);
/* Returns signed scalefactor delta, index 60 maps to zero.
 * Offset and output are unchanged if decoding fails. */
int sauti_aac_scalefactor_symbol_decode(const uint8_t *data,size_t length,
                                        size_t *bit_offset,
                                        const SautiAacHuffNode *nodes,size_t count,
                                        int32_t *difference);
#endif
