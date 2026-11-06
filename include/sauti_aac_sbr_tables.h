#ifndef SAUTI_AAC_SBR_TABLES_H
#define SAUTI_AAC_SBR_TABLES_H

#include "sauti_aac_sbr_master.h"
#include <stdint.h>

/* SBR-05: frequency resolution boundaries of the classic MPEG-4
 * HE-AAC spectral band replication (64-channel QMF) tool.
 * Exactly 0..48 high, 0..24 low and 1..5 noise bands are representable.
 * This is metadata only; it does not synthesize audio or decode SBR. */
#define SAUTI_AAC_SBR_MAX_MASTER_BANDS 48u
#define SAUTI_AAC_SBR_MAX_NOISE_BANDS 5u

typedef struct {
    uint32_t kx;                  /* First high-frequency QMF subband. */
    uint32_t m;                   /* Number of reconstructed QMF bands. */
    uint32_t n_high, n_low, n_noise;
    uint8_t high[SAUTI_AAC_SBR_MAX_MASTER_BANDS + 1u];
    uint8_t low[SAUTI_AAC_SBR_MAX_MASTER_BANDS / 2u + 1u];
    uint8_t noise[SAUTI_AAC_SBR_MAX_NOISE_BANDS + 1u];
} SautiAacSbrTables;

/* bs_noise_bands is 0..3. Master input is validated independently,
 * including all QMF boundaries. In case of any invalid value, *out
 * remains byte-for-byte unchanged (fail-closed). No allocations. */
int sauti_aac_sbr_tables_build(const SautiAacSbrMaster *master,
                                unsigned noise_bands, SautiAacSbrTables *out);
#endif
