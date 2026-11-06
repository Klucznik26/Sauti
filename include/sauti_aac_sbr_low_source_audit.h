#ifndef SAUTI_AAC_SBR_LOW_SOURCE_AUDIT_H
#define SAUTI_AAC_SBR_LOW_SOURCE_AUDIT_H
#include "sauti_aac_sbr_temporal_source6.h"
/* SBR-54v: read-only complex LOW W[i-6] source audit, bands 2..5.
 * Measurements are SAUTI-only: no independent FFmpeg QMF reference,
 * no HF synthesis, and no normative PCM claim. */
#define SAUTI_AAC_SBR_LOW_AUDIT_FIRST 2u
#define SAUTI_AAC_SBR_LOW_AUDIT_BANDS 4u
typedef struct {
    uint64_t frames, count[4], adjacent_count[4];
    double energy[4], real_energy[4], imag_energy[4];
    double adjacent_real[4], adjacent_imag[4];
    double adjacent_norm_prev[4], adjacent_norm_next[4];
    double max_magnitude[4];
    uint64_t peak_frame[4];
    uint8_t peak_slot[4];
} SautiAacSbrLowSourceAuditState;
/* aligned_low is the exact 32x32 output of 54s.
 * Preroll is excluded; all cells including skipped ones are validated.
 * Adjacent phase uses slots t-1,t only when BOTH are valid in
 * this frame; continuity across frame edges is not inferred.
 * Failure leaves the entire state unchanged. */
int sauti_aac_sbr_low_source_audit_add(
    const SautiAacSbrComplex *aligned_low,size_t count,
    const SautiAacSbrTemporalSource6Report *source6,
    SautiAacSbrLowSourceAuditState *state);
#endif
