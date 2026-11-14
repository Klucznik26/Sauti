#ifndef SAUTI_AAC_HE_ADTS_CPE_H
#define SAUTI_AAC_HE_ADTS_CPE_H

#include "sauti_aac_cpe.h"
#include "sauti_aac_sbr_payload.h"

/* Strict HE-AAC stereo core/SBR bridge. This first CPE stage accepts a
 * single ADTS AAC-LC CPE followed by one SBR CPE FIL (type 13 or a
 * verified type 14 CRC), returning 1024 interleaved core PCM frames plus
 * decoded per-channel SBR controls. It deliberately has no QMF output;
 * callers needing 2x PCM must use the separate QMF bridge. */
typedef struct {
    SautiAacLcSynthesis core[2];
    SautiAacSbrPayloadCpeState sbr;
    uint64_t frames;
} SautiAacHeAdtsCpeState;

int sauti_aac_he_adts_cpe_init(SautiAacHeAdtsCpeState *state);

/* 1 success, 0 malformed, -1 unsupported. All state, PCM and side-data are
 * unchanged on failure. A single-RDB ADTS CRC is verified for CPE; PS and
 * extra audio elements remain explicitly unsupported. */
int sauti_aac_he_adts_cpe_decode_ex(
    const uint8_t *frame,size_t frame_bytes,
    SautiAacHeAdtsCpeState *state,
    float pcm_interleaved[2048],SautiAacSbrPayloadCpeFrame *payload,
    SautiAacReason *reason);

#endif
