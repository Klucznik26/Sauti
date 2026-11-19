#include "sauti_aac_tables.h"
#include "sauti_aac_spectral.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void write_bits(uint8_t data[8],size_t *offset,uint32_t code,unsigned width){
    assert(width<=32 && *offset+width<=64);
    for(unsigned i=width;i>0;i--){
        unsigned bit=(code>>(i-1u))&1u;
        if(bit)data[*offset/8u]|=(uint8_t)(1u<<(7u-(*offset%8u)));
        ++*offset;
    }
}
int main(void){
    SautiAacHuffNode nodes[SAUTI_AAC_HUFF_MAX_NODES];
    unsigned verified=0;
    for(unsigned book=0;book<=11;book++){
        size_t node_count=sauti_aac_standard_tree_build(book,nodes,
                                                        SAUTI_AAC_HUFF_MAX_NODES);
        assert(node_count>1 && node_count<=SAUTI_AAC_HUFF_MAX_NODES);
        unsigned count=book==0 ? 121u : book<=6 ? 81u :
                       book<=8 ? 64u : book<=10 ? 169u : 289u;
        for(unsigned symbol=0;symbol<count;symbol++){
            uint32_t code=0;uint8_t bits=0;
            assert(sauti_aac_standard_codeword(book,symbol,&code,&bits));
            assert(bits>0 && bits<=32);
            uint8_t data[8]={0};size_t size=0;
            write_bits(data,&size,code,bits);
            size_t offset=0;
            int32_t value=-777;
            if(book==0){
                assert(sauti_aac_scalefactor_symbol_decode(data,sizeof(data),
                        &offset,nodes,node_count,&value));
                assert(value==(int32_t)symbol-60);
            }else{
                assert(sauti_aac_huffman_decode_asm(data,sizeof(data),&offset,
                        nodes,node_count,&value));
                assert(value==(int32_t)symbol);
            }
            assert(offset==size);
            /* Incomplete code must not publish a partial symbol. For a
             * non-byte-aligned prefix, start with too few remaining bits. */
            if(bits>1){
                size_t partial=7;int32_t untouched=12345;
                uint8_t cut[1]={0xff};
                assert(!sauti_aac_huffman_decode_asm(cut,0,&partial,
                        nodes,node_count,&untouched));
                assert(partial==7 && untouched==12345);
            }
            verified++;
        }
    }
    size_t n=sauti_aac_standard_tree_build(11,nodes,SAUTI_AAC_HUFF_MAX_NODES);
    uint32_t code=0;uint8_t bits=0;
    assert(sauti_aac_standard_codeword(11,288,&code,&bits));
    uint8_t data[8]={0};size_t pos=0;
    write_bits(data,&pos,code,bits);
    write_bits(data,&pos,2,2); /* sign: first negative */
    write_bits(data,&pos,0,1);write_bits(data,&pos,2,4); /* 18 */
    write_bits(data,&pos,2,2);write_bits(data,&pos,3,5); /* 35 */
    size_t offset=0;int32_t tuple[4]={0};
    assert(sauti_aac_spectral_decode(11,data,sizeof(data),&offset,
                                    nodes,n,tuple));
    assert(offset==pos && tuple[0]==-18 && tuple[1]==35);
    assert(!sauti_aac_standard_codeword(12,0,&code,&bits));
    assert(!sauti_aac_standard_codeword(0,121,&code,&bits));
    assert(!sauti_aac_standard_tree_build(12,nodes,SAUTI_AAC_HUFF_MAX_NODES));
    /* The cached tries equal a fresh build from the codewords, for every book, and small capacities are refused as before. */
    for(unsigned book=0;book<12u;book++){
        SautiAacHuffNode fresh[SAUTI_AAC_HUFF_MAX_NODES],cached[SAUTI_AAC_HUFF_MAX_NODES];
        SautiAacHuffEntry entries[289];size_t cnt=0;uint32_t c;uint8_t b;
        while(sauti_aac_standard_codeword(book,(unsigned)cnt,&c,&b)){entries[cnt].code=c;entries[cnt].bits=b;entries[cnt].symbol=(int16_t)cnt;cnt++;}
        const size_t want=sauti_aac_huffman_tree_build(entries,cnt,fresh,SAUTI_AAC_HUFF_MAX_NODES);
        assert(want>1u);
        for(int round=0;round<2;round++){
            assert(sauti_aac_standard_tree_build(book,cached,SAUTI_AAC_HUFF_MAX_NODES)==want);
            assert(memcmp(fresh,cached,want*sizeof(fresh[0]))==0);
        }
        assert(!sauti_aac_standard_tree_build(book,cached,want-1u));
        assert(sauti_aac_standard_tree_build(book,cached,want)==want);
    }
    puts("PASS AAC standard Huffman: all codewords, scalefactors, spectral ESC");
    printf("Verified %u normative codewords over 12 tables\n",verified);
    return 0;
}
