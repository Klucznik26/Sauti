/* SBR-54m: isolated bridge to legacy delayed SBR-53e gain applicator.
 * No changes to playback; use separate SBR-53e state in the probe. */
#include "sauti_aac_sbr_temporal_gain_bridge.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void run(unsigned source){
    SautiAacSbrTemporalReferenceGainReport input={0};
    input.completed_count=1u;
    SautiAacSbrTemporalReferenceGainEnvelope *e=&input.completed[0];
    e->source=(uint8_t)source;e->envelope=0u;e->slots=34u;
    e->kx=16u;e->k2=32u;e->limiter_count=2u;
    e->limited.groups=2u;
    e->limited.group_cap[0]=0.75;
    e->limited.group_boost[0]=1.1;
    e->limited.group_cap[1]=2.25;
    e->limited.group_boost[1]=0.9;
    for(unsigned k=e->kx;k<e->k2;k++){
        e->limited.band[k].hf_gain=0.01*(double)(k+1u);
        e->limited.band[k].noise_amplitude=1.0;
        e->limited.band[k].sine_amplitude=0.0;
    }
    SautiAacSbrTemporalGainReport out={0};
    assert(sauti_aac_sbr_temporal_gain_bridge(&input,&out));
    assert(out.completed_count==1u);
    assert(out.completed[0].source==source);
    assert(out.completed[0].slots==34u);
    assert(out.completed[0].limiter_count==2u);
    assert(fabs(out.completed[0].applied_gain[16]-0.17)<1e-13);
    assert(out.completed[0].applied_gain[0]==0.0);
    assert(out.completed[0].applied_gain[32]==0.0);
    assert(out.completed[0].limiter_max[1]==2.25);
    assert(out.completed[0].limiter_boost[1]==0.9);
    const SautiAacSbrTemporalGainReport previous=out;
    e->limited.band[31u].hf_gain=NAN;
    assert(!sauti_aac_sbr_temporal_gain_bridge(&input,&out));
    assert(memcmp(&out,&previous,sizeof(out))==0);
    e->limited.band[31u].hf_gain=0.32;
    e->limited.band[0u].sine_amplitude=1.0;
    assert(!sauti_aac_sbr_temporal_gain_bridge(&input,&out));
    assert(memcmp(&out,&previous,sizeof(out))==0);
    e->limited.band[0u].sine_amplitude=0.0;
    e->limited.groups=3u;
    assert(!sauti_aac_sbr_temporal_gain_bridge(&input,&out));
    assert(memcmp(&out,&previous,sizeof(out))==0);
    printf("SBR-54m: owner=%u, real 34-slot envelope mapping and rollback PASS\n",source);
}

int main(void){
    run(0u);run(1u);
    puts("PASS SBR-54m isolated reference-G bridge (HF-only, no PCM)");
    return 0;
}
