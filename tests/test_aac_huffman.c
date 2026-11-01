#include "sauti_aac_huffman.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(SautiAacHuffNode)==8,"NASM Huffman node layout changed");

static void valid_tree(void){
    /* Synthetic prefix-free table, NOT a normative AAC codebook:
     * 0 -> 0, 10 -> 1, 110 -> -2, 111 -> 3 */
    const SautiAacHuffEntry codes[]={
        {0,1,0},{2,2,1},{6,3,-2},{7,3,3}
    };
    SautiAacHuffNode tree[32];
    size_t count=sauti_aac_huffman_tree_build(codes,4,tree,32);
    assert(count>4 && count<=32);
    const uint8_t encoded[]={0x5b,0x80}; /* 0 10 110 111 0 */
    const int32_t expected[]={0,1,-2,3,0};
    size_t pos=0;
    for(unsigned i=0;i<5;i++){
        int32_t symbol=100;
        assert(sauti_aac_huffman_decode_asm(encoded,2,&pos,tree,count,&symbol));
        assert(symbol==expected[i]);
    }
    assert(pos==10);
    pos=16;
    int32_t sentinel=123;
    assert(!sauti_aac_huffman_decode_asm(encoded,2,&pos,tree,count,&sentinel));
    assert(pos==16 && sentinel==123);
    pos=7;
    assert(!sauti_aac_huffman_decode_asm(encoded,1,&pos,tree,count,&sentinel));
    assert(pos==7 && sentinel==123); /* 111 needs three bits */
    assert(!sauti_aac_huffman_decode_asm(NULL,2,&pos,tree,count,&sentinel));
    assert(!sauti_aac_huffman_decode_asm(encoded,2,NULL,tree,count,&sentinel));
    assert(!sauti_aac_huffman_decode_asm(encoded,2,&pos,NULL,count,&sentinel));
    assert(!sauti_aac_huffman_decode_asm(encoded,2,&pos,tree,count,NULL));
    assert(!sauti_aac_huffman_decode_asm(encoded,2,&pos,tree,0,&sentinel));
    assert(!sauti_aac_huffman_decode_asm(encoded,2,&pos,tree,4097,&sentinel));
}
static void invalid_trees(void){
    SautiAacHuffNode tree[100];
    const SautiAacHuffEntry duplicates[]={{0,1,3},{0,1,4}};
    const SautiAacHuffEntry prefix[]={{0,1,3},{1,2,4}};
    const SautiAacHuffEntry prefix_reverse[]={{1,2,4},{0,1,3}};
    const SautiAacHuffEntry too_long[]={{0,33,1}};
    const SautiAacHuffEntry zero_len[]={{0,0,1}};
    const SautiAacHuffEntry oversized[]={{4,2,1}};
    assert(!sauti_aac_huffman_tree_build(duplicates,2,tree,100));
    assert(!sauti_aac_huffman_tree_build(prefix,2,tree,100));
    assert(!sauti_aac_huffman_tree_build(prefix_reverse,2,tree,100));
    assert(!sauti_aac_huffman_tree_build(too_long,1,tree,100));
    assert(!sauti_aac_huffman_tree_build(zero_len,1,tree,100));
    assert(!sauti_aac_huffman_tree_build(oversized,1,tree,100));
    const SautiAacHuffEntry good[]={{0,1,0},{2,2,1},{6,3,2},{7,3,3}};
    assert(!sauti_aac_huffman_tree_build(good,4,tree,3));
    assert(!sauti_aac_huffman_tree_build(NULL,4,tree,100));
    assert(!sauti_aac_huffman_tree_build(good,4,NULL,100));
    assert(!sauti_aac_huffman_tree_build(good,0,tree,100));
    assert(!sauti_aac_huffman_tree_build(good,4,tree,4097));
}
static void thirty_two_bits(void){
    SautiAacHuffNode tree[80];
    const SautiAacHuffEntry entry[]={{UINT32_C(0xf0000000),32,-123}};
    const size_t count=sauti_aac_huffman_tree_build(entry,1,tree,80);
    assert(count==33);
    const uint8_t data[]={0xf0,0,0,0};
    int32_t symbol=0;
    size_t pos=0;
    assert(sauti_aac_huffman_decode_asm(data,sizeof(data),&pos,tree,count,&symbol));
    assert(pos==32 && symbol==-123);
    pos=31;symbol=44;
    assert(!sauti_aac_huffman_decode_asm(data,sizeof(data),&pos,tree,count,&symbol));
    assert(pos==31 && symbol==44);
}
int main(void){
    valid_tree();invalid_trees();thirty_two_bits();
    puts("PASS AAC Huffman ASM: prefix trie, variable lengths, EOF, invalid codebooks");
    return 0;
}
