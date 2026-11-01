#ifndef SAUTI_AAC_LC_FRAME_H
#define SAUTI_AAC_LC_FRAME_H
#include "sauti_aac_lc_synth.h"
#include "sauti_aac_headers.h"
#include <stddef.h>
#include <stdint.h>

/* AAC-13/14: restricted mono, one-raw-block ADTS -> 1024 float samples.
 * Return 1 decoded, 0 malformed/truncated, -1 valid but unsupported tool.
 * Only raw AAC-LC SCE with pulse=tns=gain_control=0 is supported.
 * CPE/stereo, PNS, intensity, SBR, pulse and TNS are NOT approximated.
 * On any failure PCM and synthesis state stay unchanged. */
int sauti_aac_lc_adts_mono_decode(const uint8_t *frame,size_t length,
                                 SautiAacLcSynthesis *state,
                                 float pcm[1024]);

/* Isolated parsed SCE from a raw_data_block. Produces the quantized spectrum
 * and scale factor fields. On failure leaves output and offset unchanged.
 * -1 = unsupported AAC element/tool; 0 = malformed or truncated. */
typedef struct {
    SautiAacIcsInfo ics;
    SautiAacSections sections;
    SautiAacScaleFactors factors;
    int32_t quant[1024];
    uint32_t global_gain;
} SautiAacLcMonoData;
int sauti_aac_lc_mono_parse(const uint8_t *raw,size_t length,
                           size_t *bit_offset,unsigned sample_rate_index,
                           SautiAacLcMonoData *out);
#endif
