#ifndef SAUTI_AAC_SBR_QMF_SYNTH_MOD_H
#define SAUTI_AAC_SBR_QMF_SYNTH_MOD_H

#include "sauti_aac_sbr_hf.h"
#include "sauti_aac_sbr_qmf_synth_window.h"

/* HE-AAC SBR-26: reference, O(128*64) direct complex synthesis-QMF
 * modulation. Converts 64 complex subband coefficients per time slot
 * into the 128 REAL modulation V samples consumed by SBR-25.
 *
 * For QMF bin k and modulation sample n:
 * phase = pi*(k+0.5)*(2*n-63)/128,
 * V[n] = (1/64) * Re(sum(k=0..63, X[k]*exp(+i*phase))).
 *
 * This is the straightforward reference form of the complex-modulated
 * 64-band SBR synthesis transform (without optimized DCT/MDCT).
 * 1/64 matches 64-band synthesis scaling. This transform does not
 * synthesize PCM by itself and does not implement analysis QMF.
 * No external codec library, heap or mutable process-global tables.
 */
#define SAUTI_AAC_SBR_QMF_MOD_BANDS 64u
#define SAUTI_AAC_SBR_QMF_MOD_VALUES 128u
#define SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS 40u

/* Takes time-major complex QMF [slots][64]. Produces time-major REAL
 * modulation blocks [slots][128]. Counts are number of elements,
 * not bytes. Only required prefixes are accessed/overwritten.
 *
 * The implementation stages all results first, so even buffer overlap
 * is tolerated (input and output may be the same allocation).
 * On malformed sizes, slots, NaN, infinities or range overflow the
 * complete output stays byte-for-byte unchanged.
 */
int sauti_aac_sbr_qmf_synth_modulate_64(
    const SautiAacSbrComplex *bands,
    size_t bands_count,
    unsigned slots,
    float *modulation,
    size_t modulation_count);

/* Connect SBR-26 modulation and SBR-25 polyphase window atomically:
 * complex QMF [slots][64] -> 64 REAL samples per time slot.
 * Per-channel state must start zeroed and persists across calls.
 * Both samples and history stay unchanged if either stage fails.
 * Output samples are SYNTHESIS-QMF waveform candidates; validating
 * complete HE-AAC decode / bitstream alignment remains separate. */
int sauti_aac_sbr_qmf_synth_64(
    const SautiAacSbrComplex *bands,
    size_t bands_count,
    unsigned slots,
    float *output,
    size_t output_count,
    SautiAacSbrQmfSynthWindowState *state);

#endif
