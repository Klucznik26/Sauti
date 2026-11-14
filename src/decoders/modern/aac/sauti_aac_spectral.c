/* AAC spectral codeword -> quantized coefficients.
 * This thin C driver binds original NASM Huffman, bitreader, and radix-tuple
 * primitives. No FFmpeg dependency; normative AAC Huffman tables to follow. */
#include "sauti_aac_spectral.h"
#include "sauti_aac_headers.h"
#include <stdint.h>
#include <string.h>

enum { MAX_AAC_QUANT=8191 };
static int bget(const uint8_t *bytes,size_t length,size_t *pos,
                uint32_t *value){
    return sauti_aac_bits_read_asm(bytes,length,pos,1,value);
}
static int esc(const uint8_t *bytes,size_t length,size_t *pos,
               uint32_t *magnitude){
    unsigned n=0;
    uint32_t b=1;
    /* ISO escape: N ones, a zero separator, followed by N+4 bits.
     * Values >=8192 exceed AAC's permitted maximum magnitude.
     * Stop on the 9th '1' to avoid invalid shifts and unbounded loops. */
    for(;;){
        if(!bget(bytes,length,pos,&b))return 0;
        if(b==0)break;
        if(++n>8u)return 0;
    }
    uint32_t suffix;
    if(!sauti_aac_bits_read_asm(bytes,length,pos,n+4u,&suffix))return 0;
    uint32_t value=(UINT32_C(1) << (n+4u)) | suffix;
    if(value>MAX_AAC_QUANT)return 0;
    *magnitude=value;
    return 1;
}
int sauti_aac_spectral_finish(uint32_t codebook,uint32_t index,
                              const uint8_t *bytes,size_t length,
                              size_t *bit_offset,int32_t output[4]){
    if(!bytes||!bit_offset||!output ||
       length>SIZE_MAX/8u || *bit_offset>length*8u)return 0;
    int16_t digits[4];
    if(!sauti_aac_spectral_index_asm(codebook,index,digits))return 0;
    int32_t next[4]={digits[0],digits[1],digits[2],digits[3]};
    size_t pos=*bit_offset;
    const unsigned count=codebook<=4u?4u:2u;
    const int unsigned_book=(codebook>=3u && codebook<=4u) ||
                            (codebook>=7u && codebook<=11u);
    if(unsigned_book) {
        for(unsigned i=0;i<count;i++){
            if(next[i]){
                uint32_t sign=0;
                if(!bget(bytes,length,&pos,&sign))return 0;
                if(sign)next[i]=-next[i];
            }
        }
    }
    if(codebook==11u){
        /* Signs precede ALL ESC payloads; each magnitude=16 has one ESC. */
        for(unsigned i=0;i<2;i++){
            if(next[i]==16 || next[i]==-16){
                const int negative=next[i]<0;
                uint32_t v=0;
                if(!esc(bytes,length,&pos,&v))return 0;
                next[i]=negative?-(int32_t)v:(int32_t)v;
            }
        }
    }
    memcpy(output,next,sizeof(next));
    *bit_offset=pos;
    return 1;
}
int sauti_aac_spectral_decode(uint32_t codebook,
                              const uint8_t *bytes,size_t length,
                              size_t *bit_offset,
                              const SautiAacHuffNode *nodes,size_t node_count,
                              int32_t output[4]){
    if(!bytes||!bit_offset||!nodes||!output)return 0;
    size_t pos=*bit_offset;
    int32_t index;
    if(!sauti_aac_huffman_decode_asm(bytes,length,&pos,
                                     nodes,node_count,&index) ||
       index<0)return 0;
    int32_t values[4];
    if(!sauti_aac_spectral_finish(codebook,(uint32_t)index,
                                  bytes,length,&pos,values))return 0;
    memcpy(output,values,sizeof(values));
    *bit_offset=pos;
    return 1;
}
