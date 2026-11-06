#ifndef SAUTI_AAC_SBR_QMF_ANALYSIS_H
#define SAUTI_AAC_SBR_QMF_ANALYSIS_H

#include "sauti_aac_sbr_hf.h"
#include <stddef.h>
#include <stdint.h>

/* HE-AAC SBR-27: 32-band complex QMF ANALYSIS from the AAC-LC core.
 * Per QMF time slot, consume 32 chronological real PCM samples and
 * produce 32 complex low-frequency subbands at twice the core's
 * spectral resolution. This stage feeds SBR-12/13 HF reconstruction.
 *
 * MPEG-4 SBR Figure 4.42:
 *   x[31-n] = input[n], n=0..31; older x are shifted by 32;
 *   u[n] = SUM_{j=0..4} x[n+64*j] * C[2*(n+64*j)], n=0..63;
 *   X[k] = 2 * SUM_{n=0..63} u[n] *
 *                exp(i*pi/64*(k+0.5)*(2*n-0.5)), k=0..31.
 *
 * C is the SAME 640-element MPEG-4 prototype as SBR-25. This
 * module reuses it (even indices only) and does not copy tables.
 * O(32*64) direct modulation is intentionally unoptimized; FFT/DCT
 * and later ASM may replace the kernel after conformance testing.
 * No external codec library, heap, file I/O or process-global state.
 */
#define SAUTI_AAC_SBR_QMF_ANALYSIS_SLOTS 40u
#define SAUTI_AAC_SBR_QMF_ANALYSIS_INPUT 32u
#define SAUTI_AAC_SBR_QMF_ANALYSIS_BANDS 32u
#define SAUTI_AAC_SBR_QMF_ANALYSIS_HISTORY 320u

typedef struct {
    uint32_t ready;             /* must be 0/1; zero-initialize */
    uint64_t samples_consumed;  /* increments by slots*32 on success */
    float history[SAUTI_AAC_SBR_QMF_ANALYSIS_HISTORY];
} SautiAacSbrQmfAnalysisState;

/* Input PCM: [slots][32] contiguous chronological floats.
 * Output: [slots][32] time-major SautiAacSbrComplex values.
 * active_bands: 1..32. Bins >= active_bands are exactly zero,
 * but the analysis history still consumes all 32 PCM samples.
 *
 * Input and output may overlap if the allocation has enough bytes.
 * State must have a distinct backing allocation from both arrays.
 * All requested output elements and state are staged; on error
 * (invalid counts, NaN/Inf, state corruption, overflow) BOTH remain
 * byte-for-byte unchanged, including on errors in later slots.
 *
 * State is independent per audio channel and continues between frames.
 * Does not parse HE-AAC or combine high QMF bands or synthesize PCM.
 */
int sauti_aac_sbr_qmf_analyze_32(
    const float *pcm,
    size_t pcm_count,
    unsigned slots,
    unsigned active_bands,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrQmfAnalysisState *state);

#endif
