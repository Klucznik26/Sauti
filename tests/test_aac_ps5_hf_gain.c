#include "sauti_aac_ps5_hf_gain.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void){
    SautiAacSbrTemporalHfTargets *targets=calloc(1u,sizeof(*targets));
    SautiAacSbrComplex *raw=calloc(32u*64u,sizeof(*raw));
    SautiAacPs5HfGainReport *report=malloc(sizeof(*report));
    assert(targets&&raw&&report);
    targets->previous_slots=2u;
    for(unsigned t=0;t<32u;t++){
        SautiAacSbrTemporalHfSlot *s=&targets->slot[t];
        s->source=t<2u?0u:1u;
        s->kx=20u;s->k2=22u;
        s->energy[20u]=4.0;
        s->energy[21u]=9.0;
        s->noise_factor[20u]=1.0;
        s->noise_factor[21u]=3.0;
        raw[t*64u+20u].re=1.0f;
        raw[t*64u+21u].im=3.0f;
    }
    memset(report,0x59,sizeof(*report));
    assert(sauti_aac_ps5_hf_gain_analyze(targets,raw,32u*64u,report));
    assert(report->inherited_slots==2u);
    assert(report->missing_source==0u);
    assert(report->gain[0][20]==2.0&&report->gain[1][21]==1.0);
    assert(report->gain[2][20]==2.0);
    assert(fabs(report->target_power-416.0)<1e-9);
    assert(fabs(report->predicted_signal_power-416.0)<1e-9);
    SautiAacPs5HfGainReport snapshot=*report;
    targets->slot[0].source=1u;
    assert(!sauti_aac_ps5_hf_gain_analyze(targets,raw,32u*64u,report));
    assert(!memcmp(report,&snapshot,sizeof(snapshot)));
    targets->slot[0].source=0u;
    targets->slot[7].energy[20u]=NAN;
    assert(!sauti_aac_ps5_hf_gain_analyze(targets,raw,32u*64u,report));
    assert(!memcmp(report,&snapshot,sizeof(snapshot)));
    targets->slot[7].energy[20u]=4.0;
    raw[3u*64u+20u].re=0.0f;
    assert(sauti_aac_ps5_hf_gain_analyze(targets,raw,32u*64u,report));
    assert(report->missing_source==1u&&report->gain[3u][20u]==0.0);
    assert(!sauti_aac_ps5_hf_gain_analyze(targets,raw,32u*64u-1u,report));
    free(report);free(raw);free(targets);
    puts("PASS PS-5c2 owned HF gain diagnostic, zero-source report and atomic invalid rollback");
    return 0;
}
