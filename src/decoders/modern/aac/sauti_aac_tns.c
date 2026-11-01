#include "sauti_aac_tns.h"
#include "sauti_aac_headers.h"
#include <string.h>
#include <stdint.h>

static int read(const uint8_t *p,size_t n,size_t *pos,unsigned count,
                uint32_t *value){
    return sauti_aac_bits_read_asm(p,n,pos,count,value);
}
int sauti_aac_tns_parse(const uint8_t *data,size_t size,size_t *bit_offset,
                        const SautiAacIcsInfo *ics,SautiAacTnsData *out){
    if(!data||!bit_offset||!ics||!out||size>SIZE_MAX/8u ||
       *bit_offset>size*8u || ics->window_sequence>3u)return 0;
    const unsigned shortwin=ics->window_sequence==2u;
    if(ics->window_count!=(shortwin?8u:1u))return 0;
    const unsigned countbits=shortwin?1u:2u;
    const unsigned lengthbits=shortwin?4u:6u;
    const unsigned orderbits=shortwin?3u:5u;
    const unsigned ordermax=shortwin?7u:20u;
    size_t pos=*bit_offset;
    SautiAacTnsData parsed;memset(&parsed,0,sizeof(parsed));
    for(unsigned w=0;w<ics->window_count;w++){
        uint32_t count=0;
        if(!read(data,size,&pos,countbits,&count) ||
           count>SAUTI_AAC_TNS_MAX_FILTERS)return 0;
        parsed.filter_count[w]=(uint8_t)count;
        uint32_t resolution=0;
        if(count && !read(data,size,&pos,1,&resolution))return 0;
        for(unsigned f=0;f<count;f++){
            SautiAacTnsFilter *filter=&parsed.filter[w][f];
            uint32_t length=0,order=0;
            if(!read(data,size,&pos,lengthbits,&length) ||
               !read(data,size,&pos,orderbits,&order) ||
               order>ordermax)return 0;
            filter->length=(uint8_t)length;
            filter->order=(uint8_t)order;
            filter->resolution=(uint8_t)(resolution?4u:3u);
            if(order==0)continue;
            uint32_t direction=0,compress=0;
            if(!read(data,size,&pos,1,&direction) ||
               !read(data,size,&pos,1,&compress))return 0;
            filter->direction=(uint8_t)direction;
            filter->compress=(uint8_t)compress;
            const unsigned width=filter->resolution-(unsigned)compress;
            const int32_t sign=1<<(width-1u);
            for(unsigned i=0;i<order;i++){
                uint32_t bits=0;
                if(!read(data,size,&pos,width,&bits))return 0;
                filter->coefficients[i]=(int8_t)((int32_t)(bits^sign)-sign);
            }
        }
    }
    *out=parsed;
    *bit_offset=pos;
    return 1;
}
