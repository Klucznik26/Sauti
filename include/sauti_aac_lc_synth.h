#ifndef SAUTI_AAC_LC_SYNTH_H
#define SAUTI_AAC_LC_SYNTH_H
#include "sauti_aac_dequant.h"
#include "sauti_aac_tns.h"
#include "sauti_aac_ssr.h"
#include <stdint.h>

/* Per-channel synthesis state. Reset at each new independent stream/seek.
 * This is not a complete AAC bitstream decoder: receives already parsed
 * sections/scalefactors and 1024 quantized spectral coefficients.
 * TNS, PNS and intensity stereo are not implemented and are rejected. */
typedef struct {
    float overlap[1024];
    unsigned previous_shape;
    unsigned previous_sequence;
    unsigned initialized;
    uint32_t pns_seed;
    /* Window-sequence transitions the standard forbids (e.g. ONLY_LONG straight to EIGHT_SHORT, which fdkaac emits at stream
     * start).  FFmpeg decodes them from the current sequence and the saved overlap alone, and so do we; the count lets a caller
     * report it.  Counted, never fatal. */
    uint32_t transition_violations;
} SautiAacLcSynthesis;

int sauti_aac_lc_synthesis_init(SautiAacLcSynthesis *state,
                               unsigned previous_shape);

/* Complete a channel's dequant -> IMDCT -> windows -> overlap-add.
 * Output PCM and the persistent state are committed only on success.
 * A frame with unsupported PNS / intensity codebooks is rejected instead
 * of producing deceptively silent output. No heap / FFmpeg dependency.
 * Caller must ensure TNS and other unsupported tools are absent. */
/* Synthesize already dequantized spectra (after stereo MS and PNS).
 * Applies inverse TNS, IMDCT and window overlap, atomically.
 * Used by the CPE path: stereo processing MUST precede inverse TNS. */
int sauti_aac_lc_synthesis_spectral(SautiAacLcSynthesis *state,
                                    const SautiAacIcsInfo *ics,
                                    const SautiAacTnsData *tns,
                                    unsigned sample_rate_index,
                                    const float spectral[1024],
                                    float pcm[1024]);
/* AAC SSR: the same as sauti_aac_lc_synthesis_spectral (inverse TNS first) with the gain control filter bank (sauti_aac_ssr.h) instead of the plain IMDCT + window + overlap-add; `ssr` is the
 * per-channel state of that filter bank, `gain` the gain_control_data() of the frame (NULL = absent).  The window shape / sequence history stays in `state`.  1 ok, 0 invalid arguments. */
int sauti_aac_lc_synthesis_spectral_ssr(SautiAacLcSynthesis *state,
                                        SautiAacSsrState *ssr,
                                        const SautiAacIcsInfo *ics,
                                        const SautiAacTnsData *tns,
                                        unsigned sample_rate_index,
                                        const float spectral[1024],
                                        const SautiAacSsrGain *gain,
                                        float pcm[1024]);

/* Native AAC-LC mono synthesis with both PNS and decoded TNS. */
int sauti_aac_lc_synthesis_frame_tns(SautiAacLcSynthesis *state,
                                unsigned sample_rate_index,
                                const SautiAacIcsInfo *ics,
                                const SautiAacSections *sections,
                                const SautiAacScaleFactors *factors,
                                const int32_t quant[1024],
                                const SautiAacTnsData *tns,
                                float pcm[1024]);

/* PNS-capable synthesis. The original API remains strict for callers
 * which have not opted into the new noise tool. */
int sauti_aac_lc_synthesis_frame_pns(SautiAacLcSynthesis *state,
                                unsigned sample_rate_index,
                                const SautiAacIcsInfo *ics,
                                const SautiAacSections *sections,
                                const SautiAacScaleFactors *factors,
                                const int32_t quant[1024],
                                float pcm[1024]);
int sauti_aac_lc_synthesis_frame(SautiAacLcSynthesis *state,
                                unsigned sample_rate_index,
                                const SautiAacIcsInfo *ics,
                                const SautiAacSections *sections,
                                const SautiAacScaleFactors *factors,
                                const int32_t quant[1024],
                                float pcm[1024]);
#endif
