/* C odpowiednik sauti_aac_huffman.asm (patrz sauti_portable.h). */
#include "sauti_portable.h"
#include "sauti_aac_huffman.h"
int sauti_aac_huffman_decode_asm(const uint8_t *data, size_t length, size_t *bit_offset, const SautiAacHuffNode *nodes, size_t node_count, int32_t *out) {
    size_t bits, pos, n = 0;
    if (!data || !bit_offset || !nodes || !out || node_count <= 1u || node_count > 4096u || length > 0x1fffffffffffffffull) return 0;
    bits = length * 8u; pos = *bit_offset;
    if (pos >= bits) return 0;
    for (;;) {
        unsigned bit, next;
        if (nodes[n].is_terminal & 1u) { *out = nodes[n].symbol; *bit_offset = pos; return 1; }
        if (pos >= bits) return 0;
        bit = (data[pos >> 3] >> (7u - (unsigned)(pos & 7u))) & 1u;
        next = bit ? nodes[n].child_one : nodes[n].child_zero;
        if (next == 0xffffu || next >= node_count) return 0;
        n = next; pos++;
    }
}
