#include "sauti_aac_sbr_low_source_audit.h"
#include <math.h>
#include <string.h>
#include <stdint.h>
#define N (32u*32u)
static int valid_state(const SautiAacSbrLowSourceAuditState *s){
    if(!s || s->frames==UINT64_MAX)return 0;
    for(unsigned b=0;b<4u;b++){
        if(s->count[b]>s->frames*32u ||
           s->adjacent_count[b]>s->count[b] ||
           !isfinite(s->energy[b]) || !isfinite(s->real_energy[b]) ||
           !isfinite(s->imag_energy[b]) || !isfinite(s->adjacent_real[b]) ||
           !isfinite(s->adjacent_imag[b]) ||
           !isfinite(s->adjacent_norm_prev[b]) ||
           !isfinite(s->adjacent_norm_next[b]) ||
           !isfinite(s->max_magnitude[b]) ||
           s->energy[b]<0.0 || s->real_energy[b]<0.0 ||
           s->imag_energy[b]<0.0 || s->max_magnitude[b]<0.0)
            return 0;
    }
    return 1;
}
int sauti_aac_sbr_low_source_audit_add(
    const SautiAacSbrComplex *low,size_t n,
    const SautiAacSbrTemporalSource6Report *r,
    SautiAacSbrLowSourceAuditState *state){
    if(!low || !r || !valid_state(state) || n<N ||
       r->frame_index!=state->frames ||
       r->current_source_slots!=26u ||
       r->carried_from_previous!=(state->frames?6u:0u) ||
       r->missing_preroll!=(state->frames?0u:6u) ||
       r->previous_owner_slots>6u)return 0;
    for(unsigned i=0;i<N;i++)
        if(!isfinite(low[i].re) || !isfinite(low[i].im))return 0;
    SautiAacSbrLowSourceAuditState next=*state;
    for(unsigned t=r->missing_preroll;t<32u;t++)
        for(unsigned b=0;b<4u;b++){
            const unsigned k=b+SAUTI_AAC_SBR_LOW_AUDIT_FIRST;
            const SautiAacSbrComplex z=low[t*32u+k];
            const double re=z.re,im=z.im;
            const double er=re*re,ei=im*im,e=er+ei;
            next.real_energy[b]+=er;
            next.imag_energy[b]+=ei;
            next.energy[b]+=e;
            next.count[b]++;
            if(sqrt(e)>next.max_magnitude[b]){
                next.max_magnitude[b]=sqrt(e);
                next.peak_frame[b]=state->frames;
                next.peak_slot[b]=(uint8_t)t;
            }
            if(t>r->missing_preroll){
                const SautiAacSbrComplex p=low[(t-1u)*32u+k];
                const double pr=p.re,pi=p.im;
                next.adjacent_real[b]+=pr*re+pi*im;
                next.adjacent_imag[b]+=pr*im-pi*re;
                next.adjacent_norm_prev[b]+=pr*pr+pi*pi;
                next.adjacent_norm_next[b]+=e;
                next.adjacent_count[b]++;
            }
        }
    for(unsigned b=0;b<4u;b++)
        if(!isfinite(next.energy[b]) || !isfinite(next.real_energy[b]) ||
           !isfinite(next.imag_energy[b]) ||
           !isfinite(next.adjacent_real[b]) ||
           !isfinite(next.adjacent_imag[b]) ||
           !isfinite(next.adjacent_norm_prev[b]) ||
           !isfinite(next.adjacent_norm_next[b]) ||
           !isfinite(next.max_magnitude[b]))return 0;
    next.frames++;
    *state=next;
    return 1;
}
