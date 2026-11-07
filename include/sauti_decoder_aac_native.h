#ifndef SAUTI_DECODER_AAC_NATIVE_H
#define SAUTI_DECODER_AAC_NATIVE_H
#include "sauti_decoder.h"

/* Native AAC-LC stereo ADTS (not MP4/M4A, SBR, PS or multichannel).
 * Decodes with Sauti's own C/NASM core; requires no FFmpeg libraries.
 * The decoder rejects streams using unsupported tools instead of silently
 * substituting external PCM or dropping frames. A single-RDB ADTS CRC is
 * validated for CPE before PCM is emitted. Seek rebuilds overlap state
 * by replaying preceding frames; reads use a one-frame PCM cache. */
extern const SautiDecoderOps sauti_decoder_aac_native_ops;

/* Strictly experimental native HE-AAC v1 / SBR ADTS bridge.
 *
 * This is deliberately a separate opt-in decoder: it accepts only a
 * single-channel, one-raw-block AAC-LC ADTS stream whose payload contains
 * explicit EXT_SBR_DATA (type 13) or CRC-verified EXT_SBR_DATA_CRC (type 14).
 * It produces 2048 mono frames per ADTS access unit at twice the core sample
 * rate. A single-RDB ADTS CRC is verified for the supported SCE syntax;
 * PS, CPE, multichannel and ordinary AAC-LC are rejected.
 * The underlying SBR/QMF
 * implementation is still experimental and must not be presented as a
 * conformance-quality HE-AAC decoder. Failed frames do not emit fallback
 * upsampled PCM. */
extern const SautiDecoderOps sauti_decoder_aac_he_native_ops;

/* Strictly experimental native HE-AAC v1 stereo CPE ADTS bridge.
 *
 * This opt-in decoder accepts only one CPE per non-CRC ADTS access unit
 * together with one SBR CPE type 13 or CRC-verified type 14 FIL. It
 * returns 2048 interleaved stereo frames at twice the core sample rate.
 * PS, multichannel and ordinary AAC-LC are rejected; a single-RDB ADTS CRC
 * is verified before output. Like the
 * mono HE adapter, this is not a conformance-quality
 * HE-AAC claim and never substitutes fallback PCM on a failed frame. */
extern const SautiDecoderOps sauti_decoder_aac_he_cpe_native_ops;
#endif
