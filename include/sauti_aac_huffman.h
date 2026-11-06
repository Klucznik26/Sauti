#ifndef SAUTI_AAC_HUFFMAN_H
#define SAUTI_AAC_HUFFMAN_H
#include <stddef.h>
#include <stdint.h>

/* A prefix-decoder primitive for the future AAC spectral/scalefactor tables.
 * This is NOT yet populated with normative AAC codebooks.
 * Trees are validated/built once in C; the hot bit traversal is in NASM.
 * No allocations or runtime FFmpeg dependency.
 */
#define SAUTI_AAC_HUFF_MAX_NODES 4096u
#define SAUTI_AAC_HUFF_MISSING UINT16_MAX

typedef struct {
    uint32_t code;      /* MSB-first codeword, right-aligned */
    uint8_t bits;       /* 1..32 */
    int16_t symbol;     /* caller-specific decoded value */
} SautiAacHuffEntry;

typedef struct {
    uint16_t child_zero;  /* missing = UINT16_MAX */
    uint16_t child_one;   /* missing = UINT16_MAX */
    int16_t symbol;
    uint16_t is_terminal; /* 0 or 1 */
} SautiAacHuffNode;

/* Returns populated node count or 0 on any invalid/ambiguous tree.
 * On failure, scratch nodes may be modified and MUST NOT be used.
 * Caller supplies sufficient space for all trie nodes, max 4096. */
size_t sauti_aac_huffman_tree_build(const SautiAacHuffEntry *entries,
                                    size_t entry_count,
                                    SautiAacHuffNode *nodes,
                                    size_t capacity);

/* 1 if a symbol was decoded. On failure, both *offset and *out unchanged.
 * Input nodes must be produced by tree_build; arbitrary trees are unsupported.
 * Uses SysV x86-64 calling convention. */
int sauti_aac_huffman_decode_asm(const uint8_t *data,size_t length,
                                 size_t *bit_offset,
                                 const SautiAacHuffNode *nodes,
                                 size_t node_count,int32_t *out);
#endif
