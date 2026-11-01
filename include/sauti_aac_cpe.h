#ifndef SAUTI_AAC_CPE_H
#define SAUTI_AAC_CPE_H
#include "sauti_aac_lc_frame.h"

/* AAC-26: one ID_CPE (channel pair) in a raw AAC-LC block.
 * Supports independent ICS and common_window including per-band Mid/Side.
 * Intensity, pulse, gain-control and unsupported MS/PNS combinations
 * remain explicitly rejected.
 * Returns 1 success, 0 malformed/truncated, -1 unsupported. */
typedef struct {
    SautiAacLcMonoData channel[2];
    unsigned common_window;
    unsigned ms_mask_present;
    uint8_t ms_used[8][64];
} SautiAacCpeData;

int sauti_aac_lc_cpe_parse(const uint8_t *raw,size_t length,
                           size_t *bit_offset,unsigned rate_index,
                           SautiAacCpeData *out,SautiAacReason *reason);

/* Atomic stereo ADTS decode: interleaved LRLR, 1024 frames per channel.
 * On any failure neither channel state nor PCM is modified.
 * Stereo MS/intensity tools are deliberately rejected until implemented. */
int sauti_aac_lc_adts_stereo_decode_ex(const uint8_t *frame,size_t length,
                                      SautiAacLcSynthesis state[2],
                                      float pcm_interleaved[2048],
                                      SautiAacReason *reason);
#endif
