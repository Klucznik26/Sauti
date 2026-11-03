#ifndef SAUTI_AAC_SBR_SMOOTH_H
#define SAUTI_AAC_SBR_SMOOTH_H

#include "sauti_aac_sbr_tables.h"
#include <stdint.h>

/* HE-AAC SBR-21: per-QMF-subband five-tap smoothing of the gain and
 * noise-amplitude COEFFICIENTS, before complex HF signal assembly.
 * The fixed taps are the MPEG-4 SBR h_smooth weights:
 * 1/3, 0.30150283239582, 0.21816949906249,
 * 0.11516383427084, 0.03183050093751 (newest to oldest).
 *
 * This module does NOT smooth complex QMF samples or substitute for
 * the still-unintegrated normative 512-entry noise sequence.
 * It is deliberately independent of the SBR-20 joint assembler.
 */
#define SAUTI_AAC_SBR_SMOOTH_SLOTS 40u
#define SAUTI_AAC_SBR_SMOOTH_BANDS 64u
#define SAUTI_AAC_SBR_SMOOTH_HISTORY 4u

typedef struct {
    uint32_t ready;  /* initialize whole object to zero */
    uint32_t kx, m;  /* identical frequency geometry across frames */
    double gain[SAUTI_AAC_SBR_SMOOTH_HISTORY]
               [SAUTI_AAC_SBR_SMOOTH_BANDS];
    double noise[SAUTI_AAC_SBR_SMOOTH_HISTORY]
                [SAUTI_AAC_SBR_SMOOTH_BANDS];
} SautiAacSbrSmoothState;

typedef struct {
    double gain[SAUTI_AAC_SBR_SMOOTH_SLOTS]
               [SAUTI_AAC_SBR_SMOOTH_BANDS];
    double noise[SAUTI_AAC_SBR_SMOOTH_SLOTS]
                [SAUTI_AAC_SBR_SMOOTH_BANDS];
} SautiAacSbrSmoothFrame;

/* Envelope-local target arrays (gain and noise amplitudes, not powers):
 * target_gain[env_count][64], target_noise[env_count][64].
 * env_edges[env_count+1] partitions [0,slots], in QMF slots.
 * attack[env_count] contains 0/1 and bypasses smoothing in marked
 * envelopes (transients), but still advances the time-slot history.
 *
 * bs_smoothing_mode=0 -> 5-tap smoothing (unless attack);
 * bs_smoothing_mode=1 -> no smoothing, still advance history.
 * Startup repeats the first envelope's levels for all four historical
 * slots. The output is zero outside [kx,kx+m).
 *
 * Malformed geometry, changed geometry without reset, invalid targets,
 * attack flags or state leave BOTH *out and *state unchanged.
 * No heap allocation, FFmpeg, external codec or PCM conversion.
 */
int sauti_aac_sbr_smooth_coefficients(
    const SautiAacSbrTables *tables,
    unsigned env_count,
    const uint8_t *env_edges,
    unsigned slots,
    const double (*target_gain)[SAUTI_AAC_SBR_SMOOTH_BANDS],
    const double (*target_noise)[SAUTI_AAC_SBR_SMOOTH_BANDS],
    const uint8_t *attack,
    unsigned bs_smoothing_mode,
    SautiAacSbrSmoothState *state,
    SautiAacSbrSmoothFrame *out);

#endif
