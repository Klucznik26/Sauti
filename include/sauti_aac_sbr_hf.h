#ifndef SAUTI_AAC_SBR_HF_H
#define SAUTI_AAC_SBR_HF_H

#include "sauti_aac_sbr_patch.h"
#include <stddef.h>

/* SBR-12: copy/transposition BASE TERM of high-frequency SBR QMF data.
 * A complex QMF subband sample is not a PCM sample. Real HE-AAC
 * high-frequency synthesis also requires inverse-filter chirp,
 * LPC prediction, envelope adjustment and a 64-band QMF synthesis
 * filter bank. None of those operations is implied by this API. */
#define SAUTI_AAC_SBR_HF_MAX_SLOTS 40u
#define SAUTI_AAC_SBR_HF_LOW_BANDS 32u
#define SAUTI_AAC_SBR_HF_HIGH_BANDS 64u

typedef struct {
    float re;
    float im;
} SautiAacSbrComplex;

/* Validate the SBR-11 patch map against the full SBR-05 high/low/noise
 * geometry. This is shared by both the direct-transpose baseline and
 * the LPC/chirp generator. Returns 0 without modifying any inputs. */
int sauti_aac_sbr_hf_map_valid(const SautiAacSbrPatches *patches,
                               const SautiAacSbrTables *tables);

/* Input:  time-major low[slots][32], already ANALYZED complex QMF.
 * Output: time-major high[slots][64] containing the directly
 * transposed baseline from patches (no LPC/chirp/gain).
 * All unmapped high bins and bins outside [kx,k2) are zero.
 * low_count/high_count are entry counts, not byte counts; oversize
 * buffers beyond the required prefix are never overwritten.
 * 1 <= slots <= 40. A successful call overwrites the output prefix
 * atomically, even when low and high buffers alias. Any error leaves
 * output byte-for-byte unchanged, including NaN/Inf in used source
 * samples or inconsistent patch definitions.
 * No allocation and no external codec dependencies. */
int sauti_aac_sbr_hf_transpose(const SautiAacSbrPatches *patches,
                                const SautiAacSbrTables *tables,
                                const SautiAacSbrComplex *low,
                                size_t low_count,
                                unsigned slots,
                                SautiAacSbrComplex *high,
                                size_t high_count);

#endif
