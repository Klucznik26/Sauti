#include "sauti_aac_adts_crc.h"

int sauti_aac_adts_crc16_update_bits(const uint8_t *bytes,size_t bytes_count,
                                     size_t bit_start,size_t bit_count,
                                     uint16_t *crc_state){
    if(!bytes || !crc_state || bytes_count>SIZE_MAX/8u ||
       bit_start>bytes_count*8u || bit_count>bytes_count*8u-bit_start)
        return 0;
    uint16_t value=*crc_state;
    for(size_t i=0u;i<bit_count;i++){
        const size_t at=bit_start+i;
        const unsigned input=(bytes[at/8u]>>(7u-(unsigned)(at&7u)))&1u;
        const unsigned top=(value>>15u)&1u;
        value=(uint16_t)(value<<1u);
        if(top^input)value^=0x8005u;
    }
    *crc_state=value;
    return 1;
}
int sauti_aac_adts_crc16_update_zero_bits(size_t bit_count,uint16_t *crc_state){
    if(!crc_state)return 0;
    uint16_t value=*crc_state;
    for(size_t i=0u;i<bit_count;i++){
        const unsigned top=(value>>15u)&1u;
        value=(uint16_t)(value<<1u);
        if(top)value^=0x8005u;
    }
    *crc_state=value;
    return 1;
}
int sauti_aac_adts_crc16_bits(const uint8_t *bytes,size_t bytes_count,
                              size_t bit_start,size_t bit_count,
                              uint16_t *crc){
    if(!crc)return 0;
    uint16_t value=0xffffu;
    if(!sauti_aac_adts_crc16_update_bits(bytes,bytes_count,bit_start,
                                         bit_count,&value))return 0;
    *crc=value;
    return 1;
}
