#ifndef SAUTI_AAC_SBR_TEMPORAL_ENERGY_UNITS_H
#define SAUTI_AAC_SBR_TEMPORAL_ENERGY_UNITS_H

#include "sauti_aac_sbr_payload.h"

/* SBR-60: explicit boundary between decoded SBR envelope-energy units and
 * the unit-float QMF analysis/synthesis path used by the temporal driver.
 *
 * The bitstream dequantizer preserves the conventional reference-QMF energy
 * values. The Sauti temporal QMF samples are normalized by 1/32768, so their
 * squared energies must use the same basis: E_unit = E_reference / 2^30.
 * Noise factors are dimensionless ratios and are deliberately unchanged.
 *
 * This helper is transactional and side-data only. It does NOT alter the
 * standard payload parser, its persistent state, or the public HE-AAC
 * decoder. A future production driver must explicitly select this boundary
 * and independently validate the complete HF/noise/sine path. */
#define SAUTI_AAC_SBR_QMF_REFERENCE_AMPLITUDE 32768.0
#define SAUTI_AAC_SBR_QMF_REFERENCE_ENERGY 1073741824.0

int sauti_aac_sbr_temporal_payload_normalize_qmf_energy(
    const SautiAacSbrPayloadFrame *input,
    SautiAacSbrPayloadFrame *output);

#endif
