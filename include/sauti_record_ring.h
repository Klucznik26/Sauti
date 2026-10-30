#ifndef SAUTI_RECORD_RING_H
#define SAUTI_RECORD_RING_H
#include <stdatomic.h>
#include <stdint.h>
#define SAUTI_RECORD_CAPACITY 65536u
typedef struct {
    atomic_uint write_pos;
    atomic_uint read_pos;
    atomic_int overflow;
    atomic_int enabled;
    float frames[SAUTI_RECORD_CAPACITY*2u];
} SautiRecordRing;
void sauti_record_reset(SautiRecordRing *ring);
void sauti_record_enable(SautiRecordRing *ring,int enabled);
void sauti_record_push(SautiRecordRing *ring,const float *interleaved,uint32_t frames);
uint32_t sauti_record_pop(SautiRecordRing *ring,float *interleaved,uint32_t capacity);
int sauti_record_overflow(const SautiRecordRing *ring);
#endif
