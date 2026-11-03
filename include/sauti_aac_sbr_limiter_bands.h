#ifndef SAUTI_AAC_SBR_LIMITER_BANDS_H
#define SAUTI_AAC_SBR_LIMITER_BANDS_H

#include "sauti_aac_sbr_hf_limiter.h"
#include "sauti_aac_sbr_patch.h"

/* SBR-17: limiter frequency-band generation from bs_limiter_bands
 * (0=one wide limiter band, 1=1.2, 2=2, 3=3 bands per octave).
 * This is the frequency partition required by SBR-16; it does not
 * perform envelope adjustment or generate PCM.
 *
 * Boundary candidates come from the LOW frequency-resolution table
 * and internal patch junctions. Sub-octave thinning is controlled by
 * the MPEG-4 SBR 0.49-band nominal threshold. Junctions are preserved
 * even if close to a neighboring boundary. The outer [kx,k2] edges
 * must always survive, including an omitted <3-band HF patch tail.
 */
typedef struct {
    uint32_t count; /* 1..64 */
    uint8_t edges[SAUTI_AAC_SBR_MAX_LIMITER_BANDS + 1u];
} SautiAacSbrLimiterBands;

/* Validates full SBR-05/11 geometry before constructing the table.
 * On invalid mode, corrupt table/map, non-progress, or capacity failure,
 * leaves *out bitwise unchanged. No heap, external decoder or audio. */
int sauti_aac_sbr_limiter_bands_build(
    const SautiAacSbrTables *tables,
    const SautiAacSbrPatches *patches,
    unsigned bs_limiter_bands,
    SautiAacSbrLimiterBands *out);

/* Convenience path: computes the frequency partition automatically and
 * passes it to SBR-16. Neither output nor report changes on failure.
 * Uses PRE-adjusted QMF (NOT SBR-15 output) to prevent double scaling. */
int sauti_aac_sbr_hf_limit_auto(
    const SautiAacSbrTables *tables,
    const SautiAacSbrPatches *patches,
    unsigned bs_limiter_bands,
    const SautiAacSbrEnergyFrame *energy,
    const uint8_t *slot_borders,
    unsigned slots,
    unsigned gain_mode,
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrLimiterReport *report);

#endif
