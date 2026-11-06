#ifndef SAUTI_AAC_SBR_TEMPORAL_ENERGY_AUDIT_H
#define SAUTI_AAC_SBR_TEMPORAL_ENERGY_AUDIT_H

#include "sauti_aac_sbr_temporal_mix.h"
#include "sauti_aac_sbr_payload.h"

/* SBR-54e: read-only, pre-synthesis HF energy allocation audit.
 *
 * Compares the decoded target energy per active QMF slot/bin with
 * (a) adjusted high-band |X|^2 from SBR-53e and (b) noise
 *     raw_amplitude^2 from SBR-53f.
 *
 * Noise nominal power is raw_amplitude^2, not literal ROM magnitude
 * or a statistically independent cross term. The sum of (a)+(b)
 * is a PREVIEW budget, not an ISO normative SBR energy check.
 * It is diagnostic evidence of possible target double-allocation.
 * Harmonics, chirp, coupled stereo and normalized QMF scaling are
 * not accounted for. Does not change decoder or DSP state.
 */
typedef struct {
    uint64_t frame_index;
    uint32_t active_bins;
    uint32_t double_alloc_bins; /* actual HF power + nominal noise > target */
    uint32_t harmonic_bins;
    double decoded_target_power;
    double adjusted_hf_power;
    double nominal_noise_power;
    double mixed_hf_power;
    double max_budget_ratio;
} SautiAacSbrTemporalEnergyAudit;

int sauti_aac_sbr_temporal_energy_audit(
    const SautiAacSbrTemporalHfTargets *emitted_targets,
    const SautiAacSbrComplex *adjusted_hf,size_t adjusted_count,
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrComplex *mixed_hf,size_t mixed_count,
    const SautiAacSbrTemporalApplyReport *apply,
    const SautiAacSbrTemporalNoiseReport *noise_report,
    SautiAacSbrTemporalEnergyAudit *out);

#endif
