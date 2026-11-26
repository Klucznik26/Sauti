#ifndef SAUTI_AAC_HE_MULTI_H
#define SAUTI_AAC_HE_MULTI_H
/* HE-AAC (SBR) with any channel layout the wide LC decoder takes (sauti_aac_lcw.h): the standard channel configurations 3..7 and program_config_element layouts.  Every SCE / CPE has its own SBR
 * state (header, envelope history, time grid, QMF) as ISO/IEC 14496-3 4.5 asks: the SBR extension in the fill element after a channel element belongs to it; an LFE and an element whose access
 * unit carries no SBR go through the QMF banks as the core signal alone (no_sbr, high band silent).  Parametric stereo needs a single SCE and stays with the mono / stereo HE paths.
 * State lives on the heap of the caller (malloc).  Output: interleaved float PCM in the output order of sauti_aac_lcw, 2048 samples per channel (1024 when the SBR output has the core rate). */
#include "sauti_aac_lcw.h"
#include <stddef.h>

typedef struct SautiAacHeMulti SautiAacHeMulti;
/* config: channel configuration 3..7 (cfg->layout is ignored), or 0 = cfg->layout (a PCE).  core_rate in Hz.  downsampled: the SBR output has the core rate (32-band synthesis). NULL: not supported. */
SautiAacHeMulti *sauti_aac_he_multi_new(const SautiAacLcwConfig *cfg,unsigned config,unsigned core_rate,unsigned downsampled);
SautiAacHeMulti *sauti_aac_he_multi_dup(const SautiAacHeMulti *state);
void sauti_aac_he_multi_free(SautiAacHeMulti *state);
unsigned sauti_aac_he_multi_channels(const SautiAacHeMulti *state);
unsigned sauti_aac_he_multi_frame_samples(const SautiAacHeMulti *state);     /* output samples per channel and access unit */
/* 1 decoded, 0 malformed, -1 valid but not supported.  On failure the state may have advanced (the caller drops the frame). */
int sauti_aac_he_multi_decode(SautiAacHeMulti *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason);
#endif
