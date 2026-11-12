#include "sauti_aac_ps5_cross.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void){
    SautiAacSbrTemporalHistory *h=calloc(1u,sizeof(*h));
    SautiAacSbrTemporalHfTargets *targets=calloc(1u,sizeof(*targets));
    SautiAacSbrComplex *prev=calloc(32u*64u,sizeof(*prev));
    SautiAacSbrComplex *now=calloc(32u*64u,sizeof(*now));
    SautiAacPs5CrossReport *r=calloc(1u,sizeof(*r));
    assert(h&&targets&&prev&&now&&r);
    h->ready=1u;h->has_previous=1u;
    h->previous.grid.env_count=1u;
    h->previous.env_edges[0]=0u;
    h->previous.env_edges[1]=34u;
    h->previous.tables.kx=20u;
    h->previous.tables.m=1u;
    h->previous_plan.deferred_slots=2u;
    targets->previous_slots=2u;
    for(unsigned t=0;t<2u;t++){
        SautiAacSbrTemporalHfSlot *s=&targets->slot[t];
        s->source=0u;s->envelope=0u;s->kx=20u;s->k2=21u;
        s->energy[20u]=8.0;s->noise_factor[20u]=1.0;
        now[t*64u+20u].re=2.0f;
    }
    for(unsigned t=0;t<32u;t++)prev[t*64u+20u].re=1.0f;
    assert(sauti_aac_ps5_cross_envelope_analyze(
        h,targets,prev,2048u,now,2048u,r));
    assert(r->total_slots_in_envelope==34u&&r->deferred_slots==2u);
    assert(fabs(r->mean_source_power[20u]-40.0/34.0)<1e-12);
    assert(fabs(r->provisional_gain[20u]-sqrt(4.0/(40.0/34.0)))<1e-12);
    assert(fabs(r->signal_budget-136.0)<1e-12);
    SautiAacPs5CrossReport snapshot=*r;
    targets->slot[1].energy[20u]=9.0;
    assert(!sauti_aac_ps5_cross_envelope_analyze(
        h,targets,prev,2048u,now,2048u,r));
    assert(memcmp(&snapshot,r,sizeof(*r))==0);
    targets->slot[1].energy[20u]=8.0;
    now[1u*64u+20u].im=NAN;
    assert(!sauti_aac_ps5_cross_envelope_analyze(
        h,targets,prev,2048u,now,2048u,r));
    assert(memcmp(&snapshot,r,sizeof(*r))==0);
    now[1u*64u+20u].im=0.0f;
    assert(!sauti_aac_ps5_cross_envelope_analyze(
        h,targets,prev,2048u,now,2047u,r));
    assert(memcmp(&snapshot,r,sizeof(*r))==0);
    free(r);free(now);free(prev);free(targets);free(h);
    puts("PASS PS-5c4a full FIXVAR envelope spans two QMF frames and rejects partial data");
    return 0;
}
