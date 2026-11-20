#ifndef SAUTI_AAC_CPE_H
#define SAUTI_AAC_CPE_H
#include "sauti_aac_lc_frame.h"
#include "sauti_aac_pred.h"

/* AAC-26/29C: one ID_CPE (channel pair) in a raw AAC-LC block.
 * Handles independent ICS and common_window with per-band Mid/Side,
 * intensity and correlated PNS when both bands are NOISE_HCB.
 * Non-conforming mixed MS/PNS, pulse and gain-control are rejected.
 * Returns 1 success, 0 malformed/truncated, -1 unsupported. */
typedef struct {
    SautiAacLcMonoData channel[2];
    unsigned common_window;
    unsigned ms_mask_present;
    uint8_t ms_used[8][64];
    /* Raw CPE bit position of second individual_channel_stream(), retained
     * for the ADTS CRC-16 protected-region walk. */
    size_t crc_second_ics_bit;
} SautiAacCpeData;

int sauti_aac_lc_cpe_parse(const uint8_t *raw,size_t length,
                           size_t *bit_offset,unsigned rate_index,
                           SautiAacCpeData *out,SautiAacReason *reason);

/* Synthesize an already parsed CPE into one interleaved 1024-frame stereo
 * AAC-LC block. This is the atomic core boundary used by both plain ADTS
 * AAC-LC and the HE-AAC SBR bridge: on error neither synthesis history nor
 * output is modified. */
int sauti_aac_lc_cpe_synthesize(const SautiAacCpeData *cpe,
                                unsigned rate_index,
                                SautiAacLcSynthesis state[2],
                                float pcm_interleaved[2048],
                                SautiAacReason *reason);

/* The two spectra of a parsed CPE after dequantisation, PNS, Mid/Side, correlated PNS and intensity stereo (shared by AAC-LC and the low-delay decoders). `rate` may carry
 * a layout (sauti_aac_layout.h).  `pns_stream` advances only on success.  1 success, 0 malformed, -1 unsupported. */
int sauti_aac_cpe_spectra(const SautiAacCpeData *cpe,unsigned rate,uint32_t *pns_stream,
                          float spectral[2][1024],SautiAacReason *reason);
int sauti_aac_cpe_spectra_pred(const SautiAacCpeData *parsed,unsigned rate,uint32_t *pns_stream,float spectral[2][1024],SautiAacPredState pred[2],SautiAacReason *reason);

/* Atomic stereo ADTS decode: interleaved LRLR, 1024 frames per channel.
 * On any failure neither channel state nor PCM is modified.
 * Correlated PNS requires both SFBs to carry NOISE_HCB plus ms_used. */
int sauti_aac_lc_adts_stereo_decode_ex(const uint8_t *frame,size_t length,
                                      SautiAacLcSynthesis state[2],
                                      float pcm_interleaved[2048],
                                      SautiAacReason *reason);
#endif
