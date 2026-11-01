/* Build an AAC-family Huffman prefix trie without allocating memory.
 * Native Sauti tables will be supplied in separate modules. Do not confuse
 * the synthetic codebooks in unit tests with normative MPEG-4 AAC tables. */
#include "sauti_aac_huffman.h"
#include <stdint.h>

_Static_assert(sizeof(SautiAacHuffNode)==8,"NASM Huffman node ABI must be 8 bytes");

static void init_node(SautiAacHuffNode *n){
    n->child_zero=SAUTI_AAC_HUFF_MISSING;
    n->child_one=SAUTI_AAC_HUFF_MISSING;
    n->symbol=0;
    n->is_terminal=0;
}
size_t sauti_aac_huffman_tree_build(const SautiAacHuffEntry *entries,
                                    size_t entry_count,
                                    SautiAacHuffNode *nodes,
                                    size_t capacity){
    if(!entries||!nodes||!entry_count || entry_count>512 ||
       capacity<2 || capacity>SAUTI_AAC_HUFF_MAX_NODES)return 0;
    size_t used=1;
    init_node(&nodes[0]);
    for(size_t i=0;i<entry_count;i++){
        const SautiAacHuffEntry *e=&entries[i];
        if(!e->bits || e->bits>32u ||
           (e->bits<32u && (e->code >> e->bits)!=0u))return 0;
        uint16_t at=0;
        for(unsigned left=e->bits;left>0;left--){
            SautiAacHuffNode *current=&nodes[at];
            if(current->is_terminal)return 0; /* existing word is prefix */
            const unsigned bit=(e->code>>(left-1u))&1u;
            uint16_t *link=bit ? &current->child_one : &current->child_zero;
            if(*link==SAUTI_AAC_HUFF_MISSING){
                if(used>=capacity)return 0;
                *link=(uint16_t)used;
                init_node(&nodes[used]);
                used++;
            }
            at=*link;
        }
        SautiAacHuffNode *leaf=&nodes[at];
        if(leaf->is_terminal ||
           leaf->child_zero!=SAUTI_AAC_HUFF_MISSING ||
           leaf->child_one!=SAUTI_AAC_HUFF_MISSING)return 0;
        leaf->symbol=e->symbol;
        leaf->is_terminal=1;
    }
    return used;
}
