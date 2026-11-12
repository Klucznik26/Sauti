#include "sauti_aac_ps5_hf_envelope.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
    SautiAacSbrTemporalHfTargets *own=calloc(1u,sizeof(*own));
    SautiAacSbrComplex *raw=calloc(32u*64u,sizeof(*raw));
    SautiAacPs5HfEnvelopeReport *r=calloc(1u,sizeof(*r));
    assert(own&&raw&&r);
    own->previous_slots=2u;
    for(unsigned t=0;t<32u;t++){
        SautiAacSbrTemporalHfSlot *s=&own->slot[t];
        s->source=t<2u?0u:1u;
        s->envelope=t<2u?3u:0u;
        s->kx=20u;s->k2=21u;
        s->energy[20]=8.0;
        s->noise_factor[20]=1.0;
        raw[t*64u+20u].re=t<2u?2.0f:1.0f;
    }
    assert(sauti_aac_ps5_hf_envelope_analyze(own,raw,32u*64u,r));
    assert(r->groups==2u&&r->inherited_slots==2u);
    assert(fabs(r->signal_budget-128.0)<1e-9);
    assert(fabs(r->noise_budget-128.0)<1e-9);
    assert(fabs(r->raw_power-38.0)<1e-9);
    assert(fabs(r->provisional_gain[0][20]-1.0)<1e-9);
    assert(fabs(r->provisional_gain[2][20]-2.0)<1e-9);
    /* Same envelope with changing raw power: mean, NOT per-slot power. */
    raw[3u*64u+20u].re=0.0f;
    assert(sauti_aac_ps5_hf_envelope_analyze(own,raw,32u*64u,r));
    assert(r->provisional_gain[3][20]>2.0);
    SautiAacPs5HfEnvelopeReport previous=*r;
    own->slot[0].source=1u;
    assert(!sauti_aac_ps5_hf_envelope_analyze(own,raw,32u*64u,r));
    assert(!memcmp(&previous,r,sizeof(*r)));
    own->slot[0].source=0u;
    own->slot[15].noise_factor[20]=NAN;
    assert(!sauti_aac_ps5_hf_envelope_analyze(own,raw,32u*64u,r));
    assert(!memcmp(&previous,r,sizeof(*r)));
    own->slot[15].noise_factor[20]=1.0;
    for(unsigned t=0;t<2u;t++)raw[t*64u+20].re=0.0f;
    assert(sauti_aac_ps5_hf_envelope_analyze(own,raw,32u*64u,r));
    assert(r->missing_source==2u);
    assert(!sauti_aac_ps5_hf_envelope_analyze(own,raw,2047u,r));
    free(r);free(raw);free(own);
    puts("PASS PS-5c3 owned envelope mean-power, signal/noise budgets, deferred slots and rollback");
    return 0;
}
