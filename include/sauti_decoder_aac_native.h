#ifndef SAUTI_DECODER_AAC_NATIVE_H
#define SAUTI_DECODER_AAC_NATIVE_H
#include "sauti_decoder.h"

/* Native AAC-LC stereo ADTS (not MP4/M4A, SBR, PS or multichannel).
 * Decodes with Sauti's own C/NASM core; requires no FFmpeg libraries.
 * The decoder rejects streams using unsupported tools instead of silently
 * substituting external PCM or dropping frames. Seek rebuilds overlap state
 * by replaying preceding frames; reads use a one-frame PCM cache. */
extern const SautiDecoderOps sauti_decoder_aac_native_ops;
#endif
