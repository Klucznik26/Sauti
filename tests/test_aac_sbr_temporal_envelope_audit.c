/* SBR-54f: 32-slot original envelope plus a 2-slot FIXVAR tail.
 * Its averaged ratio must differ from single-slot peak ratios.
 * No decoder coefficient is adjusted. */
#include "sauti_aac_sbr_temporal_envelope_audit.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NBIN (32u*64u)
#define NCAP (3u*SAUTI_AAC_SBR_MAX_ENV)

static void make_frame(unsigned frame,
                       SautiAacSbrTemporalHfTargets *targets,
                       SautiAacSbrComplex high[NBIN],
                       SautiAacSbrTemporalNoiseFrame *noise){
    memset(targets,0,sizeof(*targets));
    memset(high,0,NBIN*sizeof(*high));
    memset(noise,0,sizeof(*noise));
    for(unsigned t=0u;t<32u;t++){
        const unsigned inherited=frame==1u && t<2u;
        const double e=(frame==0u || inherited)?4.0:9.0;
        const double hp=inherited?1.0:e;
        const double np=(frame==0u || inherited)?0.8:1.0;
        SautiAacSbrTemporalHfSlot *slot=&targets->slot[t];
        slot->source=(uint8_t)(inherited?0u:1u);
        slot->envelope=0u;
        slot->kx=16u;
        slot->k2=17u;
        slot->energy[16u]=e;
        const unsigned ix=t*64u+16u;
        noise->active[ix]=1u;
        noise->raw_amplitude[ix]=sqrt(np);
        high[ix].re=(float)sqrt(hp);
    }
}
static void push(unsigned index,
                 SautiAacSbrTemporalEnvelopeAuditState *s,
                 SautiAacSbrTemporalHfTargets *target,
                 SautiAacSbrComplex high[NBIN],
                 SautiAacSbrTemporalNoiseFrame *noise){
    SautiAacSbrTemporalApplyReport ar={0};
    SautiAacSbrTemporalNoiseReport nr={0};
    ar.emitted=1u;ar.frame_index=index;
    nr.emitted=1u;nr.frame_index=index;nr.vectors=32u;
    assert(sauti_aac_sbr_temporal_envelope_audit_push(
        target,high,NBIN,noise,&ar,&nr,s));
}
int main(void){
    SautiAacSbrTemporalEnvelopeAuditGroup groups[NCAP]={0};
    SautiAacSbrTemporalEnvelopeAuditState s={
        .groups=groups,.max_frames=3u
    };
    SautiAacSbrTemporalHfTargets targets;
    SautiAacSbrComplex high[NBIN];
    SautiAacSbrTemporalNoiseFrame noise;
    make_frame(0u,&targets,high,&noise);
    push(0u,&s,&targets,high,&noise);
    assert(groups[0].count[16u]==32u);
    assert(s.next_emitted_frame==1u);

    /* The 2 delayed slots of the previous envelope belong
     * to original frame 0, and must NOT form a new envelope. */
    make_frame(1u,&targets,high,&noise);
    SautiAacSbrTemporalApplyReport ar={0};
    SautiAacSbrTemporalNoiseReport nr={0};
    ar.emitted=1u;ar.frame_index=1u;
    nr.emitted=1u;nr.frame_index=1u;nr.vectors=32u;
    const size_t size=sizeof(groups);
    SautiAacSbrTemporalEnvelopeAuditGroup before[NCAP];
    memcpy(before,groups,size);
    high[31u*64u+16u].re=NAN;
    assert(!sauti_aac_sbr_temporal_envelope_audit_push(
        &targets,high,NBIN,&noise,&ar,&nr,&s));
    assert(memcmp(before,groups,size)==0 && s.next_emitted_frame==1u);
    high[31u*64u+16u].re=3.0f;
    targets.slot[0].energy[16u]=5.0; /* conflicts with frame 0 */
    assert(!sauti_aac_sbr_temporal_envelope_audit_push(
        &targets,high,NBIN,&noise,&ar,&nr,&s));
    assert(memcmp(before,groups,size)==0 && s.next_emitted_frame==1u);
    targets.slot[0].energy[16u]=4.0;
    push(1u,&s,&targets,high,&noise);

    assert(groups[0].count[16u]==34u);
    assert(groups[SAUTI_AAC_SBR_MAX_ENV].count[16u]==30u);
    SautiAacSbrTemporalEnvelopeAuditReport report;
    assert(sauti_aac_sbr_temporal_envelope_audit_report(&s,&report));
    assert(report.completed_frames==2u && report.envelopes==2u &&
           report.active_envelope_bins==2u &&
           report.total_slot_bins==64u &&
           report.above_target==2u && report.above_twice==0u);
    const double expected=(32.0*4.0+2.0*1.0+34.0*0.8)/
                          (34.0*4.0);
    assert(fabs(report.max_ratio-expected)<1e-7);
    assert(report.worst_frame==0u && report.worst_envelope==0u &&
           report.worst_bin==16u && report.worst_slots==34u);

    /* Frame indexes are strictly monotonic. An invalid third
     * frame cannot alter any previous original-envelope totals. */
    memcpy(before,groups,size);
    make_frame(2u,&targets,high,&noise);
    nr.frame_index=7u;ar.frame_index=7u;
    assert(!sauti_aac_sbr_temporal_envelope_audit_push(
        &targets,high,NBIN,&noise,&ar,&nr,&s));
    assert(memcmp(before,groups,size)==0 && s.next_emitted_frame==2u);
    puts("SBR-54f: 32+2 FIXVAR slots grouped into original envelope PASS");
    puts("SBR-54f: true averaged per-bin target ratio and rollback PASS");
    puts("PASS SBR-54f temporal envelope-mean energy audit (no PCM)");
    return 0;
}
