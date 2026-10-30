#ifndef SAUTI_DECODER_YM_H
#define SAUTI_DECODER_YM_H
#include "sauti_decoder.h"
extern const SautiDecoderOps sauti_decoder_ym_ops;
/* Optional caller-owned unsigned PCM8 sample bank for YM2 Mad Max effects.
 * The bank and sample buffers must remain alive until decoder close.
 * Set before reading/seeking; NULL/0 disables the bank. */
typedef struct {const uint8_t *data;uint32_t size;} SautiYm2Sample;
int sauti_decoder_ym2_set_bank(SautiDecoder *decoder,
                              const SautiYm2Sample *bank,size_t count);
/* YM3b only: sample-frame loop target; callers opt into looping. */
int sauti_decoder_ym_loop_start(const SautiDecoder *decoder,uint64_t *frame);
#endif
