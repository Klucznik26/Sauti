#ifndef SAUTI_AAC_HE_ADTS_QMF_H
#define SAUTI_AAC_HE_ADTS_QMF_H

#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_sbr_qmf_frame.h"

/* HE-AAC SBR-31: first whole-frame bridge from ADTS mono AAC-LC
 * + EXT_SBR_DATA, through SBR-29 entropy decoding and SBR-28 QMF
 * analysis/HF reconstruction/synthesis, into 2048 real samples.
 *
 * This is a STRICT EXPERIMENTAL signal path: it does not guarantee
 * conformance-quality HE-AAC because normative QMF delay, predictor
 * estimation and some zero-source SBR envelopes remain incomplete.
 * An unsupported frame fails atomically; there is no silent fallback
 * to naive PCM upsampling or fake high-frequency coefficients.
 *
 * First supported format remains ADTS one-block 1024-sample AAC-LC
 * single-channel with SCE+FIL SBR type 13 or validated type 14 CRC,
 * no ADTS CRC/CPE. A parsed PS extension is deliberately rejected here until
 * the separate stereo PS/QMF reconstruction stage exists.
 * Output sample rate is twice the ADTS core rate (e.g. 24k -> 48k).
 */
typedef struct {
    SautiAacHeAdtsMonoState adts;
    SautiAacSbrQmfFrameState qmf;
    uint64_t frames;
} SautiAacHeAdtsQmfState;

/* Initialize for a new independent stream or after a seek. */
int sauti_aac_he_adts_qmf_init(SautiAacHeAdtsQmfState *state);

/* Returns 1 on complete 2048-sample mono QMF synthesis,
 * 0 malformed/invalid, -1 unsupported.
 * The AAC+SBR headers, Huffman histories, AAC overlap, QMF analysis,
 * ROM noise phase, sine phase, QMF synthesis, PCM and diagnostics
 * commit together ONLY on success. All other outputs stay unchanged.
 * Reason is always set if non-NULL.
 *
 * The input ADTS frame must have an explicitly signaled SBR FIL;
 * an ordinary AAC-LC frame is never mislabeled HE-AAC output. */
int sauti_aac_he_adts_qmf_decode_ex(
    const uint8_t *frame,size_t frame_bytes,
    SautiAacHeAdtsQmfState *state,
    float pcm_2x[2048],
    SautiAacSbrLimiterReport *report,
    SautiAacReason *reason);

#endif
