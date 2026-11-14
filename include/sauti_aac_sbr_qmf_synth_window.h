#ifndef SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_H
#define SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_H

#include <stddef.h>
#include <stdint.h>

/* HE-AAC SBR-25: 64-channel QMF SYNTHESIS POLYPHASE WINDOW.
 *
 * This is the second half of the normative QMF synthesis bank:
 * it consumes already MODULATED 128 real-valued V samples per QMF
 * time slot and produces 64 real-valued output samples per slot,
 * applying the standard 640-tap synthesis prototype coefficients
 * with 10 historical 128-sample blocks.
 *
 * Crucially this routine does NOT turn SBR's 64 COMPLEX QMF bands
 * into 128 modulation samples. That modulation transform is a
 * separate stage; do not supply complex HF subbands to this API!
 * The output should not be treated as valid decoded PCM until the
 * modulation and frame integration are implemented and tested.
 *
 * SBR prototype table cross-checked against the public FFmpeg n6.1
 * aacsbrdata.h window (LGPL 2.1+). Redistribution obligations for
 * these numeric constants need review before public Sauti release.
 *
 * No external codec libraries, heap, hidden allocation or I/O.
 */
#define SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_SLOTS 40u
#define SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_INPUT 128u
#define SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_OUTPUT 64u
#define SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_TAPS 640u
#define SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_HISTORY 10u

typedef struct {
    uint32_t ready; /* zero-initialize the entire object */
    uint32_t head;  /* index of newest 128-sample block, 0..9 */
    float history[SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_HISTORY]
                 [SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_INPUT];
} SautiAacSbrQmfSynthWindowState;

/* Read one of the exact 640 window coefficients, without state update.
 * Invalid index/output pointer -> failure, *out unchanged. */
int sauti_aac_sbr_qmf_synth_window_coefficient(
    unsigned index,float *out);

/* Process 1..40 modulation blocks. 'modulation' contains slots*128
 * floats in chronological order. 'output' needs slots*64 floats.
 *
 * Per output n, use the 10 coefficient groups C[64*j+n]:
 *
 *   sum(j=0..9, C[64*j+n] * V[(128*j)+(64*(j&1))+n])
 *
 * where V[0..127] is the NEWEST input block and older V blocks
 * follow in chronological age. All 10 source blocks are retained
 * as a circular buffer between calls.
 *
 * Accepts in/out overlap only when the caller has distinct backing
 * buffers (they MUST NOT overlap); never partially commits on errors.
 * State and output stay bit-identical on invalid data, NaN, overflow,
 * corrupted history, insufficient capacity or invalid slot count.
 */
int sauti_aac_sbr_qmf_synth_window_64(
    const float *modulation,size_t input_count,
    unsigned slots,float *output,size_t output_count,
    SautiAacSbrQmfSynthWindowState *state);

#endif
