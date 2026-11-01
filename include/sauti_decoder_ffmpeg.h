#ifndef SAUTI_DECODER_FFMPEG_H
#define SAUTI_DECODER_FFMPEG_H
#include "sauti_decoder.h"
/* Optional in-process libavformat/libavcodec decoders. */
extern const SautiDecoderOps sauti_decoder_aac_ops;
extern const SautiDecoderOps sauti_decoder_alac_ops;
extern const SautiDecoderOps sauti_decoder_webm_ops;
#endif
