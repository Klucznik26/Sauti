#include "sauti_aac_tns.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_bands.h"
#include <math.h>
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

/* AAC-LC tns_max_bands for 1024/128 transforms, by sample-rate index.
 * Values are tabulated in the ISO AAC specification. */
static const uint8_t max_long[13]={
    31,31,34,40,42,51,46,46,42,42,42,39,39
};
static const uint8_t max_short[13]={
    9,9,10,14,14,14,14,14,14,14,14,14,14
};
int sauti_aac_tns_apply(const SautiAacTnsData *tns,
                        const SautiAacIcsInfo *ics,unsigned rate,
                        float spectral[1024]){
    if(!tns||!ics||!spectral||rate>=13u||
       ics->window_sequence>3u)return 0;
    const unsigned shortwin=ics->window_sequence==2u;
    const unsigned windows=shortwin?8u:1u;
    if(ics->window_count!=windows)return 0;
    const uint16_t *offsets=NULL;unsigned bands=0;
    if(!sauti_aac_band_offsets(rate,shortwin,&offsets,&bands))return 0;
    const unsigned mmm=shortwin?max_short[rate]:max_long[rate];
    const unsigned cap=mmm<ics->max_sfb?mmm:ics->max_sfb;
    float next[1024];
    for(unsigned i=0;i<1024;i++){
        if(!isfinite(spectral[i]))return 0;
        next[i]=spectral[i];
    }
    const double pi=3.14159265358979323846264338327950288;
    for(unsigned w=0;w<windows;w++){
        unsigned bottom=bands;
        if(tns->filter_count[w]>(shortwin?1u:3u))return 0;
        for(unsigned j=0;j<tns->filter_count[w];j++){
            const SautiAacTnsFilter *f=&tns->filter[w][j];
            if(f->length>bottom || f->order>(shortwin?7u:20u) ||
               (f->resolution!=3u && f->resolution!=4u) ||
               f->compress>1u || f->direction>1u)return 0;
            const unsigned top=bottom;
            bottom=top-f->length;
            if(!f->order)continue;
            const unsigned width=f->resolution-f->compress;
            const int half=1<<(width-1u);
            const double pos_scale=(double)(half*2-1);
            /* Quantization normalization uses uncompressed resolution.
             * Signed coefficients are bounded by compressed bit width. */
            const double iqpos=((1<<(f->resolution-1))-0.5)/(pi/2.0);
            const double iqneg=((1<<(f->resolution-1))+0.5)/(pi/2.0);
            double lpc[SAUTI_AAC_TNS_MAX_ORDER]={0};
            double temp[SAUTI_AAC_TNS_MAX_ORDER]={0};
            (void)pos_scale;
            for(unsigned k=0;k<f->order;k++){
                const int q=f->coefficients[k];
                if(q< -half || q>=half)return 0;
                const double reflection=sin((double)q/(q>=0?iqpos:iqneg));
                for(unsigned i=0;i<k;i++)
                    temp[i]=lpc[i]+reflection*lpc[k-1u-i];
                temp[k]=reflection;
                for(unsigned i=0;i<=k;i++)lpc[i]=temp[i];
            }
            const unsigned low=bottom<cap?bottom:cap;
            const unsigned high=top<cap?top:cap;
            unsigned start=offsets[low],stop=offsets[high];
            if(start>=stop)continue;
            const unsigned size=shortwin?128u:1024u;
            if(stop>size)return 0;
            const unsigned base=w*size;
            const int direction=f->direction?-1:1;
            int index=(int)(base+(direction<0?stop-1u:start));
            for(unsigned m=0;m<stop-start;m++,index+=direction){
                double value=next[index];
                const unsigned past=m<f->order?m:f->order;
                for(unsigned k=1;k<=past;k++)
                    value-=lpc[k-1u]*next[index-(int)k*direction];
                if(!isfinite(value) || fabs(value)>3.402823466e38)
                    return 0;
                next[index]=(float)value;
            }
        }
    }
    memcpy(spectral,next,sizeof(next));
    return 1;
}
