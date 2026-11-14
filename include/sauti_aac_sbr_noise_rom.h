#ifndef SAUTI_AAC_SBR_NOISE_ROM_H
#define SAUTI_AAC_SBR_NOISE_ROM_H

#include "sauti_aac_sbr_hf.h"
#include "sauti_aac_sbr_tables.h"
#include <stddef.h>
#include <stdint.h>

/* HE-AAC SBR-23: MPEG-4 SBR's fixed 512-entry complex noise ROM.
 * Unlike the temporary 8-phase SBR-18 PRNG, the original ROM vectors
 * are NON-UNIT complex samples. They must not be renormalized.
 *
 * MPEG-4 SBR increments the running noise index before using a ROM
 * entry: given initial index=0, the FIRST emitted vector is ROM[1].
 * After ROM[511], the following vector is ROM[0]. Indexing is modulo 512.
 *
 * This module is a new optional standalone path, so earlier SBR-18,
 * SBR-20 and SBR-22 tests retain their existing deterministic behavior.
 * No runtime codec dependency, dynamic allocation or PCM synthesis.
 */
#define SAUTI_AAC_SBR_NOISE_ROM_SIZE 512u
#define SAUTI_AAC_SBR_NOISE_ROM_MAX_SLOTS 40u
#define SAUTI_AAC_SBR_NOISE_ROM_BANDS 64u

typedef struct {
    uint32_t initialized;  /* zero-initialize for index=0 first frame */
    uint32_t index;        /* most recently emitted noise index [0,511] */
    uint64_t emitted;      /* number of emitted complex noise vectors */
} SautiAacSbrNoiseRomState;

/* Unconditional indexed lookup, without changing any state.
 * On invalid index or NULL out, returns 0 and leaves *out unchanged. */
int sauti_aac_sbr_noise_rom_lookup(
    unsigned index, SautiAacSbrComplex *out);

/* State-consuming standalone lookup. *state is initialized to zero,
 * or may explicitly begin at {1,index,0} to reproduce reference cases.
 * On error neither *state nor *out changes. */
int sauti_aac_sbr_noise_rom_next(
    SautiAacSbrNoiseRomState *state, SautiAacSbrComplex *out);

/* A sequence of 'samples' consecutive vectors, equivalent to
 * repeated calls to *_next. Bounded by 40*64; output_count in entries.
 * Output and state stay unchanged on any invalid argument. */
int sauti_aac_sbr_noise_rom_sequence(
    SautiAacSbrNoiseRomState *state,
    unsigned samples,
    SautiAacSbrComplex *out,
    size_t output_count);

/* Apply *already-smoothed* gain and noise amplitudes to complex QMF:
 * Y[t,k] = X[t,k]*gain[t,k] + ROM[next_index]*noise[t,k].
 * Every active HF subband advances the ROM index, even with noise=0.
 * X/Y are time-major [slots][64], gain/noise are [40][64].
 * Samples outside [kx,kx+m) are copied unchanged.
 * Output and input may alias. On any error, output and ROM state
 * are left bitwise unchanged. No heap and no external codec.
 *
 * Full HE-AAC decoder integration remains a separate stage. */
int sauti_aac_sbr_noise_rom_apply_levels(
    const SautiAacSbrTables *tables,
    unsigned slots,
    const double (*gain)[SAUTI_AAC_SBR_NOISE_ROM_BANDS],
    const double (*noise)[SAUTI_AAC_SBR_NOISE_ROM_BANDS],
    const SautiAacSbrComplex *input,
    size_t input_count,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrNoiseRomState *state);

#endif
