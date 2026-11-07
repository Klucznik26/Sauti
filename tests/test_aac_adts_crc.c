#include "sauti_aac_adts_crc.h"
#include <assert.h>
#include <stdio.h>

int main(void){
    static const uint8_t check[]="123456789";
    uint16_t crc=0u;
    /* CRC-16/CMS: poly 0x8005, init ffff, normal bit order, xorout 0. */
    assert(sauti_aac_adts_crc16_bits(check,9u,0u,72u,&crc));
    assert(crc==0xaee7u);
    uint16_t split=0xffffu;
    assert(sauti_aac_adts_crc16_update_bits(check,9u,0u,23u,&split));
    assert(sauti_aac_adts_crc16_update_bits(check,9u,23u,49u,&split));
    assert(split==crc);
    /* Exercise an unaligned protected syntax range and reject invalid spans
     * without modifying the caller's previous checksum. */
    const uint8_t packed[]={0xa6u,0x53u,0xf0u};
    assert(sauti_aac_adts_crc16_bits(packed,3u,3u,17u,&crc));
    assert(crc==0x5e98u);
    const uint16_t saved=crc;
    assert(!sauti_aac_adts_crc16_bits(packed,3u,8u,17u,&crc));
    assert(crc==saved);
    assert(!sauti_aac_adts_crc16_update_bits(packed,3u,20u,5u,&split));
    assert(split==0xaee7u);
    puts("PASS AAC ADTS CRC-16: ISO polynomial, MSB ranges and bounds");
    return 0;
}
