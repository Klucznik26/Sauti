#ifndef SAUTI_AAC_SBR_HF_ADJUST_H
#define SAUTI_AAC_SBR_HF_ADJUST_H

#include "sauti_aac_sbr_energy.h"
#include "sauti_aac_sbr_hf.h"
#include <stddef.h>
#include <stdint.h>

/* SBR-15: envelope-matched COMPLEX high-band QMF amplitude adjustment.
 *
 * This is an independently testable subset of HE-AAC SBR envelope
 * adjustment, NOT the complete normative HF adjustment. It does not yet
 * inject noise or sine harmonics, limit/boost gains or smooth boundaries.
 *
 * Slot borders are already expanded into QMF time-slot coordinates by
 * the caller. They must strictly partition [0,slots] into env_count
 * envelopes. Geometry and frequency-resolution arrays come from SBR-09.
 * interpolation=0: one measured power per envelope FREQUENCY BAND.
 * interpolation=1: one measured power per individual QMF subband.
 *
 * For each applicable group, mean(|X|^2) is measured across slots and
 * bins and its complex amplitude is scaled by sqrt(target/mean).
 * Target energy uses units of squared QMF samples supplied by caller;
 * integrating with a QMF analysis bank MUST define its normalization.
 * Zero source with positive target cannot be corrected by gain alone
 * and returns 0 (future HF-noise generation handles that case).
 *
 * Successful result overwrites exactly slots*64 complex samples, with
 * unmodified bins outside the SBR high-frequency interval; output may
 * alias input. On failure the whole output remains byte-for-byte intact. */
int sauti_aac_sbr_hf_adjust(const SautiAacSbrTables *tables,
                            const SautiAacSbrEnergyFrame *energy,
                            const uint8_t *slot_borders,
                            unsigned slots,
                            unsigned interpolation,
                            const SautiAacSbrComplex *input,
                            size_t input_count,
                            SautiAacSbrComplex *output,
                            size_t output_count);

#endif
