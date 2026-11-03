#ifndef SAUTI_AAC_SBR_HF_LIMITER_H
#define SAUTI_AAC_SBR_HF_LIMITER_H

#include "sauti_aac_sbr_hf_adjust.h"

/* HE-AAC SBR-16: HF gain limiting + bounded boost, with no stochastic
 * noise, sine harmonics or time-smoothing. This is a separate path from
 * SBR-15, consuming PRE-adjustment complex QMF to avoid double scaling.
 * It is an independently testable subset of the normative SBR envelope
 * adjustment, not a declaration of full standards conformance.
 *
 * Limiter band edges are supplied by the caller and MUST partition
 * [kx, kx+m] exactly. Generation of f_tablelim from bs_limiter_bands
 * / patch boundaries is a separate stage.
 *
 * Gain modes: 0=-3 dB, 1=0 dB, 2=+3 dB, 3=unrestricted.
 * Gain cap per limiter group:
 *     Gmax = min(100000, lim_gain * sqrt(sum(Etarget)/sum(Ecurrent))).
 * Each subband's ideal gain is min(sqrt(Etarget/Ecurrent), Gmax).
 * The energy-restoring per-group boost is capped at ~4 dB
 *     Gboost = min(1.584893192, sqrt(sum(Etarget)/sum(EafterCap))).
 * Real and imaginary components are scaled identically.
 * For zero source and positive target, fail closed: no noise synthesis.
 * All values must fit finite float after scaling.
 */
#define SAUTI_AAC_SBR_MAX_LIMITER_BANDS 64u

typedef struct {
    uint32_t env_count;
    uint32_t limiter_count;
    double limiter_max[SAUTI_AAC_SBR_MAX_ENV]
                      [SAUTI_AAC_SBR_MAX_LIMITER_BANDS];
    double limiter_boost[SAUTI_AAC_SBR_MAX_ENV]
                        [SAUTI_AAC_SBR_MAX_LIMITER_BANDS];
    double applied_gain[SAUTI_AAC_SBR_MAX_ENV]
                       [SAUTI_AAC_SBR_HF_HIGH_BANDS];
} SautiAacSbrLimiterReport;

/* Buffers are time-major [slots][64]; entry counts are not byte counts.
 * Each envelope [slot_borders[e], slot_borders[e+1]) is adjusted using
 * independently measured mean power per complex QMF subband, regardless
 * of the source envelope frequency resolution (high/low specifies how
 * Etarget is expanded over QMF subbands).
 *
 * output and input may alias. report and output must be separate valid
 * objects. Writes are atomic: on any error neither output nor report
 * changes. No allocation or external codec library. */
int sauti_aac_sbr_hf_limit(const SautiAacSbrTables *tables,
                            const SautiAacSbrEnergyFrame *energy,
                            const uint8_t *slot_borders,
                            unsigned slots,
                            const uint8_t *limiter_edges,
                            unsigned limiter_count,
                            unsigned gain_mode,
                            const SautiAacSbrComplex *input,
                            size_t input_count,
                            SautiAacSbrComplex *output,
                            size_t output_count,
                            SautiAacSbrLimiterReport *report);

#endif
