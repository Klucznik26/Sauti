#include "sauti_record_ring.h"
#include <string.h>
void sauti_record_reset(SautiRecordRing *r){
    if(!r)return;
    atomic_store(&r->write_pos,0);
    atomic_store(&r->read_pos,0);
    atomic_store(&r->overflow,0);
    atomic_store(&r->enabled,0);
}
void sauti_record_enable(SautiRecordRing *r,int on){
    if(r)atomic_store_explicit(&r->enabled,!!on,memory_order_release);
}
void sauti_record_push(SautiRecordRing *r,const float *src,uint32_t n){
    if(!r||!src||!n)return;
    if(!atomic_load_explicit(&r->enabled,memory_order_acquire))return;
    if(atomic_load_explicit(&r->overflow,memory_order_relaxed))return;
    unsigned w=atomic_load_explicit(&r->write_pos,memory_order_relaxed);
    unsigned rd=atomic_load_explicit(&r->read_pos,memory_order_acquire);
    if(n>SAUTI_RECORD_CAPACITY || w-rd>SAUTI_RECORD_CAPACITY-n){
        atomic_store_explicit(&r->overflow,1,memory_order_release);
        return;
    }
    for(uint32_t i=0;i<n;++i){
        uint32_t dst=((w+i)&(SAUTI_RECORD_CAPACITY-1u))*2u;
        r->frames[dst]=src[i*2u];
        r->frames[dst+1]=src[i*2u+1];
    }
    atomic_store_explicit(&r->write_pos,w+n,memory_order_release);
}
uint32_t sauti_record_pop(SautiRecordRing *r,float *out,uint32_t cap){
    if(!r||!out||!cap)return 0;
    unsigned rd=atomic_load_explicit(&r->read_pos,memory_order_relaxed);
    unsigned w=atomic_load_explicit(&r->write_pos,memory_order_acquire);
    uint32_t n=w-rd;
    if(n>cap)n=cap;
    for(uint32_t i=0;i<n;++i){
        uint32_t src=((rd+i)&(SAUTI_RECORD_CAPACITY-1u))*2u;
        out[i*2u]=r->frames[src];
        out[i*2u+1]=r->frames[src+1];
    }
    atomic_store_explicit(&r->read_pos,rd+n,memory_order_release);
    return n;
}
int sauti_record_overflow(const SautiRecordRing *r){
    return r?atomic_load_explicit(&r->overflow,memory_order_acquire):0;
}
