#include "sauti_aac_spectral.h"
#include "sauti_aac_huffman.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {uint8_t data[16];size_t bit;} Bits;
static void put(Bits *s,unsigned width,uint32_t bits){
    assert(width<=32 && s->bit+width<=sizeof(s->data)*8u);
    for(unsigned k=width;k>0;k--){
        if((bits>>(k-1u))&1u)
            s->data[s->bit/8u]|=(uint8_t)(1u << (7u-(s->bit%8u)));
        ++s->bit;
    }
}
static void check_tuple(unsigned cb,unsigned index,
                        int16_t a,int16_t b,int16_t c,int16_t d){
    int16_t out[4]={77,77,77,77};
    assert(sauti_aac_spectral_index_asm(cb,index,out));
    assert(out[0]==a && out[1]==b && out[2]==c && out[3]==d);
}
static void index_mapping(void){
    check_tuple(1,0,-1,-1,-1,-1);
    check_tuple(2,40,0,0,0,0);
    check_tuple(1,80,1,1,1,1);
    check_tuple(3,0,0,0,0,0);
    check_tuple(4,80,2,2,2,2);
    check_tuple(5,0,-4,-4,0,0);
    check_tuple(6,40,0,0,0,0);
    check_tuple(5,80,4,4,0,0);
    check_tuple(7,63,7,7,0,0);
    check_tuple(8,9,1,1,0,0);
    check_tuple(9,168,12,12,0,0);
    check_tuple(10,0,0,0,0,0);
    check_tuple(11,288,16,16,0,0);
    int16_t unchanged[4]={31,32,33,34};
    assert(!sauti_aac_spectral_index_asm(0,0,unchanged));
    assert(!sauti_aac_spectral_index_asm(12,0,unchanged));
    assert(!sauti_aac_spectral_index_asm(1,81,unchanged));
    assert(!sauti_aac_spectral_index_asm(7,64,unchanged));
    assert(!sauti_aac_spectral_index_asm(9,169,unchanged));
    assert(!sauti_aac_spectral_index_asm(11,289,unchanged));
    assert(!sauti_aac_spectral_index_asm(1,0,NULL));
    assert(unchanged[0]==31 && unchanged[1]==32 && unchanged[2]==33 &&
           unchanged[3]==34);
}
static void signs_and_escaping(void){
    Bits b={0};
    put(&b,3,5);                  /* intentionally unaligned prefix */
    put(&b,4,0xau);               /* signs - + - + for four 2s */
    int32_t values[4]={0};
    size_t pos=3;
    assert(sauti_aac_spectral_finish(3,80,b.data,sizeof(b.data),&pos,values));
    assert(pos==7 && values[0]==-2 && values[1]==2 &&
           values[2]==-2 && values[3]==2);
    b=(Bits){0};put(&b,2,2);    /* negative first, positive second */
    put(&b,1,0);put(&b,4,2);    /* ESC first: 16+2=18 */
    put(&b,2,2);put(&b,5,3);    /* ESC second: N=1, 32+3=35 */
    pos=0;
    assert(sauti_aac_spectral_finish(11,288,b.data,sizeof(b.data),&pos,values));
    assert(pos==14 && values[0]==-18 && values[1]==35 &&
           values[2]==0 && values[3]==0);
    b=(Bits){0};
    put(&b,2,0);                 /* both positive */
    for(unsigned i=0;i<8;i++)put(&b,1,1);
    put(&b,1,0);put(&b,12,4095);/* maximum 8191 */
    put(&b,1,0);put(&b,4,0);    /* other value 16 */
    pos=0;
    assert(sauti_aac_spectral_finish(11,288,b.data,sizeof(b.data),&pos,values));
    assert(values[0]==8191 && values[1]==16 && pos==28);
    b=(Bits){0};
    put(&b,2,0);
    for(unsigned i=0;i<9;i++)put(&b,1,1); /* escapes beyond AAC max */
    pos=0;
    int32_t before[4]={9,8,7,6};
    memcpy(values,before,sizeof(values));
    assert(!sauti_aac_spectral_finish(11,288,b.data,sizeof(b.data),&pos,values));
    assert(pos==0 && memcmp(values,before,sizeof(values))==0);
    b=(Bits){0};
    put(&b,1,1); /* only one sign, four nonzero entries */
    /* One byte includes padding bits. A truncated read starting at bit
     * seven must roll back after consuming the one remaining sign bit. */
    pos=7;
    memcpy(values,before,sizeof(values));
    assert(!sauti_aac_spectral_finish(3,80,b.data,1,&pos,values));
    assert(pos==7 && memcmp(values,before,sizeof(values))==0);
    b=(Bits){0};
    put(&b,1,0);put(&b,1,1); /* signs for codebook 8 pair */
    pos=0;
    assert(sauti_aac_spectral_finish(8,9,b.data,1,&pos,values));
    assert(pos==2 && values[0]==1 && values[1]==-1 &&
           values[2]==0 && values[3]==0);
}
static void complete_pipeline(void){
    /* Synthetic canonical mapping, NOT standardized Huffman codewords:
     * 0 -> index 288, 10 -> index 0. This verifies the integrated path. */
    const SautiAacHuffEntry entries[]={{0,1,288},{2,2,0}};
    SautiAacHuffNode tree[32];
    size_t n=sauti_aac_huffman_tree_build(entries,2,tree,32);
    assert(n==4);
    Bits b={0};
    put(&b,1,0);       /* Huffman code -> tuple [16,16] */
    put(&b,2,2);       /* signs 1,0 */
    put(&b,1,0);put(&b,4,2);
    put(&b,2,2);put(&b,5,3);
    size_t pos=0;
    int32_t values[4]={0};
    assert(sauti_aac_spectral_decode(11,b.data,sizeof(b.data),&pos,
                                    tree,n,values));
    assert(pos==15 && values[0]==-18 && values[1]==35);
    b=(Bits){0};put(&b,2,2); /* Huffman index 0 -> zeros */
    pos=0;
    assert(sauti_aac_spectral_decode(11,b.data,sizeof(b.data),&pos,
                                    tree,n,values));
    assert(pos==2 && values[0]==0 && values[1]==0 &&
           values[2]==0 && values[3]==0);
    pos=7;
    int32_t before[4]={9,8,7,6};
    memcpy(values,before,sizeof(values));
    /* 10 -> zero tuple needs 2 bits, but only one remains. */
    b=(Bits){0};put(&b,8,1);
    assert(!sauti_aac_spectral_decode(11,b.data,1,&pos,tree,n,values));
    assert(pos==7 && memcmp(values,before,sizeof(values))==0);
}
int main(void){
    index_mapping();signs_and_escaping();complete_pipeline();
    puts("PASS AAC spectral ASM: codebook tuples, signs, escape 8191, bitstream atomicity");
    return 0;
}
