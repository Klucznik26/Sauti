#include "sauti_aac_ps_qmf_bank.h"
#include <stdint.h>
#include <string.h>
#include <math.h>

static int distinct(const void *a,size_t n,const void *b,size_t m){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=n:x-y>=m;
}
int sauti_aac_ps_qmf_bank_analyze(const SautiAacPsHybridSchedule *schedule,
                                  const SautiAacSbrComplex *qmf,size_t qmf_count,
                                  SautiAacPsQmfLowState *state,
                                  SautiAacSbrComplex *out,size_t out_count){
    if(!schedule||!qmf||!state||!out||
       qmf_count<38u*64u)return 0;
    const unsigned bands=schedule->parameter_bands==20u&&schedule->hybrid_bands==71u?71u:
       schedule->parameter_bands==34u&&schedule->hybrid_bands==91u?91u:0u;
    if(!bands||out_count<(size_t)32u*bands)return 0;
    const size_t in_size=(size_t)38u*64u*sizeof(*qmf);
    const size_t out_size=(size_t)32u*bands*sizeof(*out);
    if(!distinct(qmf,in_size,out,out_size)||
       !distinct(qmf,in_size,state,sizeof(*state))||
       !distinct(out,out_size,state,sizeof(*state)))return 0;

    const unsigned low_bands=bands==71u?10u:32u;
    const unsigned high_bands=bands-low_bands;
    SautiAacSbrComplex low[32u*32u], high[32u*61u], next[32u*91u];
    SautiAacPsQmfLowState next_state=*state;
    if(!sauti_aac_ps_qmf_low_analyze(schedule,qmf,qmf_count,
                                     &next_state,low,(size_t)32u*low_bands) ||
       !sauti_aac_ps_qmf_high_extract(schedule,qmf,qmf_count,
                                     high,(size_t)32u*high_bands))return 0;
    for(unsigned slot=0;slot<32u;slot++){
        memcpy(next+(size_t)slot*bands,low+(size_t)slot*low_bands,
               low_bands*sizeof(*next));
        memcpy(next+(size_t)slot*bands+low_bands,
               high+(size_t)slot*high_bands,high_bands*sizeof(*next));
    }
    memcpy(out,next,out_size);
    *state=next_state;
    return 1;
}
