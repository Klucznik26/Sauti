#ifndef SAUTI_AAC_SBR_TEMPORAL_SMOOTH_H
#define SAUTI_AAC_SBR_TEMPORAL_SMOOTH_H

#include "sauti_aac_sbr_temporal_noise.h"

/* HE-AAC SBR-53h: apply the SBR-21 five-tap coefficient-domain
 * smoother to the RAW noise amplitudes of the one-frame-delayed
 * SBR-53f ROM frame. Keep one 4-slot history per absolute QMF bin.
 *
 * h = { 1/3, 0.30150283239582, 0.21816949906249,
 *       0.11516383427084, 0.03183050093751 };
 * bs_smoothing_mode=1 or original envelope attack bypasses FIR for
 * that slot, but raw target values still enter the running history.
 * An inactive band resets its own noise history; on the first active
 * slot, the four historical entries are seeded from that first
 * amplitude, exactly as in the SBR-21 startup implementation.
 *
 * The side-data is tied to the original SBR frame, even across FIXVAR.
 * This is a DIAGNOSTIC NOISE COEFFICIENT stage only: no harmonic gate,
 * sinusoid, gain smoothing, mixing, QMF synthesis or playable PCM.
 */
typedef struct {
    uint32_t ready;
    uint64_t frames;
    uint8_t seen[64];
    double recent[4][64]; /* oldest to newest */
} SautiAacSbrTemporalSmoothState;

typedef struct {
    double amplitude[32u*64u];
} SautiAacSbrTemporalSmoothFrame;

typedef struct {
    uint64_t frame_index;
    uint32_t active_bins,nonzero_amplitudes,bypassed_slots;
} SautiAacSbrTemporalSmoothReport;

/* One call for each consecutive EMITTED frame from SBR-53f,
 * including the final _flush output. On error output/report/state
 * are bitwise unchanged. Frame buffers must not alias. */
int sauti_aac_sbr_temporal_noise_smooth(
    const SautiAacSbrTemporalNoiseFrame *noise,
    const SautiAacSbrTemporalNoiseReport *noise_report,
    SautiAacSbrTemporalSmoothState *state,
    SautiAacSbrTemporalSmoothFrame *out,
    SautiAacSbrTemporalSmoothReport *report);

#endif
