#include "sauti_aac_sbr_crc.h"

int sauti_aac_sbr_crc10_bits(const uint8_t *bytes,size_t bytes_count,
                             size_t bit_start,size_t bit_count,
                             uint16_t *crc){
    if(!bytes || !crc || bytes_count>SIZE_MAX/8u ||
       bit_start>bytes_count*8u || bit_count>bytes_count*8u-bit_start)
        return 0;
    uint16_t state=0u;
    for(size_t i=0;i<bit_count;i++){
        const unsigned bit=(bytes[(bit_start+i)/8u] >>
                            (7u-(unsigned)((bit_start+i)&7u)))&1u;
        const unsigned feedback=bit^((state>>9u)&1u);
        state=(uint16_t)((state<<1u)&0x03ffu);
        if(feedback)state^=0x0233u;
    }
    *crc=state;
    return 1;
}
