#ifndef SAUTI_AAC_LC_SYNTH_H
#define SAUTI_AAC_LC_SYNTH_H
#include "sauti_aac_dequant.h"
#include "sauti_aac_tns.h"
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
} SautiAacLcSynthesis;

int sauti_aac_lc_synthesis_init(SautiAacLcSynthesis *state,
                               unsigned previous_shape);

/* Complete a channel's dequant -> IMDCT -> windows -> overlap-add.
 * Output PCM and the persistent state are committed only on success.
 * A frame with unsupported PNS / intensity codebooks is rejected instead
 * of producing deceptively silent output. No heap / FFmpeg dependency.
 * Caller must ensure TNS and other unsupported tools are absent. */
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
