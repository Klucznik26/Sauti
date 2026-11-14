#ifndef SAUTI_AAC_SBR_GAIN_LIMIT_PARTITION_H
#define SAUTI_AAC_SBR_GAIN_LIMIT_PARTITION_H

#include "sauti_aac_sbr_gain_partition.h"
#include <stdint.h>

/* SBR-54k: reference-based limiter for the SBR-54j HF/noise/sine
 * pre-limiter amplitudes. Input E, E_curr and Q refer to a complete
 * original envelope (including possible FIXVAR handoff).
 *
 * Frequency groups are supplied by the already-existing SBR-17
 * limiter-band builder; edges are absolute QMF indices in [1,64].
 * One common (attack) delta flag applies to this envelope.
 *
 * Matches the mathematical sequence from FFmpeg aacsbr.c gain_calc:
 *  - sum energy and unadjusted HF power per limiter group
 *  - gain_max = limgain[mode]*sqrt((eps+sum(E))/(eps+sum(Ecurr)))
 *  - cap HF gain and cap noise amplitude relative to HF gain
 *  - after = sum(Ecurr*G^2 + S^2 + delta*(S==0)*Qm^2)
 *  - boost = min(1.584893192, sqrt((eps+sum(E))/(eps+after)))
 *  - multiply G, Qm, Sm by the same boost.
 *
 * Isolated numeric model only. No PCM or temporal decoder state
 * is modified; no claim of conformant synthesis or sample scaling.
 * For any invalid input, caller output remains BITWISE unchanged.
 */
typedef struct {
    SautiAacSbrGainPartition band[64];
    double group_cap[64];
    double group_boost[64];
    uint32_t gain_capped_bins;
    uint32_t noise_capped_bins;
    uint32_t sine_bins;
    unsigned groups;
} SautiAacSbrGainLimitedFrame;

int sauti_aac_sbr_gain_limit_partition(
    const double envelope_energy[64],
    const double current_hf_power[64],
    const double noise_factor[64],
    const uint8_t sine_active[64],
    const uint8_t edges[65],
    unsigned group_count,
    unsigned gain_mode,
    unsigned delta,
    SautiAacSbrGainLimitedFrame *out);

/* sine_index is FFmpeg s_indexmapped (midpoint only). sine_mapped
 * is 1 across the envelope-resolution band that contains a sine.
 * The single-map function passes the same array for both. */
int sauti_aac_sbr_gain_limit_partition_flags(
    const double envelope_energy[64],
    const double current_hf_power[64],
    const double noise_factor[64],
    const uint8_t sine_index[64],
    const uint8_t sine_mapped[64],
    const uint8_t edges[65],
    unsigned group_count,
    unsigned gain_mode,
    unsigned delta,
    SautiAacSbrGainLimitedFrame *out);

#endif
