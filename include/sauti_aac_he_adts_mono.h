#ifndef SAUTI_AAC_HE_ADTS_MONO_H
#define SAUTI_AAC_HE_ADTS_MONO_H

#include "sauti_aac_lc_frame.h"
#include "sauti_aac_sbr_payload.h"

/* SBR-30: decode the AAC-LC mono CORE and SBR control payload from
 * one COMPLETE ADTS frame containing SCE + FIL/EXT_SBR_DATA + ID_END.
 *
 * This is the first true ADTS -> 1024 native core PCM + entropy-decoded
 * SBR control bridge. It does NOT yet produce upsampled HE-AAC PCM.
 * Feed core_pcm and the zero-copy SBR-29 HF configuration into SBR-28
 * in the next stage, after adding normative delay/frame handling.
 *
 * Supported: AAC-LC 1024-sample single-channel-element mono ADTS with
 * one raw_data_block, no ADTS CRC, one SBR extension type 13 in FIL.
 * SBR-29 restricts SBR grid to [0,16] and rejects extensions/PS/CRC.
 * Leading/trailing harmless EXT_FILL=0 FIL may be present. Multiple
 * SBR payloads, CPE, unknown extensions and extra channel elements
 * are explicitly rejected (never quietly discarded).
 *
 * Core state and SBR differential entropy/header histories persist
 * across calls. Initialize once per independent stream, reset after
 * seek/discontinuity. All output/state writes are atomic on errors.
 */
typedef struct {
    SautiAacLcSynthesis core;
    SautiAacSbrPayloadState sbr;
    uint64_t frames;
} SautiAacHeAdtsMonoState;

/* Resets a whole independent mono stream. 0/1 success/failure. */
int sauti_aac_he_adts_mono_init(SautiAacHeAdtsMonoState *state);

/* 1 = success, 0 = malformed/truncated/invalid, -1 = unsupported.
 * frame_size is available bytes; it may exceed one frame, in which
 * case only ADTS.frame_bytes is consumed.
 * PCM output is 1024 f32 AAC-LC samples at the CORE sample rate.
 * params contains the actual SBR syntax and dequantized HF energies.
 * On failure state, pcm, params remain unchanged; reason is set.
 * Once SBR-QMF synthesis is wired, 2048 HE-AAC samples will be produced
 * by a separate PCM-producing entry point. */
int sauti_aac_he_adts_mono_decode_ex(
    const uint8_t *frame,size_t frame_size,
    SautiAacHeAdtsMonoState *state,
    float core_pcm[1024],
    SautiAacSbrPayloadFrame *params,
    SautiAacReason *reason);

#endif
