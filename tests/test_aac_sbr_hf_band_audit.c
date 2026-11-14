/* SBR-54t: per-band complex-QMF discrepancy, phase-sensitive
 * cross-energy and atomic rollback. No native audio is modified. */
#include "sauti_aac_sbr_hf_band_audit.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define N (32u*64u)

static void fill(SautiAacSbrComplex a[N],SautiAacSbrComplex b[N]){
    memset(a,0,N*sizeof(*a));
    memset(b,0,N*sizeof(*b));
    for(unsigned t=0u;t<32u;t++){
        unsigned i=t*64u+20u;
        a[i].re=2.0f;b[i].re=1.0f;
        i=t*64u+21u;
        a[i].re=1.0f;a[i].im=1.0f;
        b[i].re=1.0f;b[i].im=-1.0f;
    }
}

int main(void){
    SautiAacSbrComplex a[N],b[N];
    fill(a,b);
    SautiAacSbrHfBandAuditState stats={0};
    const SautiAacSbrTemporalSource6Report first={
        .carried_from_previous=0u,
        .current_source_slots=26u,.missing_preroll=6u,
        .previous_owner_slots=0u,.frame_index=0u
    };
    assert(sauti_aac_sbr_hf_band_audit_add(
        a,N,b,N,&first,&stats));
    assert(stats.frames==1u && stats.active_total==52u &&
           stats.changed_total==52u);
    assert(stats.active[20u]==26u &&
           stats.changed[21u]==26u);
    assert(stats.old_energy[20u]==104.0);
    assert(stats.new_energy[20u]==26.0);
    assert(stats.cross[20u]==52.0);
    assert(stats.difference[20u]==26.0);
    assert(stats.old_energy[21u]==52.0);
    assert(stats.new_energy[21u]==52.0);
    assert(stats.cross[21u]==0.0);
    assert(stats.difference[21u]==104.0);
    assert(stats.peak_difference==2.0 &&
           stats.peak_frame==0u &&
           stats.peak_slot==6u &&
           stats.peak_band==21u);
    puts("SBR-54t: first-frame six-slot preroll excluded PASS");

    const SautiAacSbrTemporalSource6Report second={
        .carried_from_previous=6u,
        .current_source_slots=26u,.missing_preroll=0u,
        .previous_owner_slots=2u,.frame_index=1u
    };
    assert(sauti_aac_sbr_hf_band_audit_add(
        a,N,b,N,&second,&stats));
    assert(stats.frames==2u &&
           stats.active_total==116u &&
           stats.changed_total==116u);
    assert(stats.active[20u]==58u &&
           stats.changed[21u]==58u);
    assert(stats.old_energy[20u]==232.0);
    assert(stats.new_energy[20u]==58.0);
    assert(stats.cross[20u]==116.0);
    assert(stats.difference[20u]==58.0);
    assert(stats.difference[21u]==232.0);
    puts("SBR-54t: two-frame energy and band correlation budgets PASS");

    /* Malformed current input, even outside active HF or ignored
     * preroll, must not publish an incomplete stats report. */
    const SautiAacSbrHfBandAuditState saved=stats;
    a[0u].im=NAN;
    SautiAacSbrTemporalSource6Report third=second;
    third.frame_index=2u;
    third.previous_owner_slots=6u;
    assert(!sauti_aac_sbr_hf_band_audit_add(
        a,N,b,N,&third,&stats));
    assert(memcmp(&saved,&stats,sizeof(stats))==0);
    a[0u].im=0.0f;
    third.frame_index=1u; /* Replay */
    assert(!sauti_aac_sbr_hf_band_audit_add(
        a,N,b,N,&third,&stats));
    assert(memcmp(&saved,&stats,sizeof(stats))==0);
    third.frame_index=2u;
    third.missing_preroll=6u; /* Invalid 2nd-frame preroll */
    assert(!sauti_aac_sbr_hf_band_audit_add(
        a,N,b,N,&third,&stats));
    assert(memcmp(&saved,&stats,sizeof(stats))==0);
    third.missing_preroll=0u;
    assert(!sauti_aac_sbr_hf_band_audit_add(
        a,N-1u,b,N,&third,&stats));
    assert(memcmp(&saved,&stats,sizeof(stats))==0);

    /* Identical HF arrays increase active but not changed counts. */
    assert(sauti_aac_sbr_hf_band_audit_add(
        a,N,a,N,&third,&stats));
    assert(stats.frames==3u && stats.active_total==180u &&
           stats.changed_total==116u);
    assert(stats.difference[21u]==232.0);
    puts("SBR-54t: late NaN, replay, geometry, count and rollback PASS");
    puts("PASS SBR-54t per-band complex HF source audit (NOT PCM)");
    return 0;
}
