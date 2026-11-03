#ifndef SAUTI_AAC_SBR_MASTER_H
#define SAUTI_AAC_SBR_MASTER_H

#include <stdint.h>

/* Frequency-band boundaries for classic HE-AAC SBR (64 QMF subbands).
 * Stage SBR-02 handles bs_freq_scale=0 (linear spacing), and SBR-03
 * handles logarithmic bs_freq_scale=1..3.
 * k0/k2 must already have been determined from the SBR header and
 * validated against the actual output sampling rate by a future stage.
 * No QMF or audio reconstruction. */
#define SAUTI_AAC_SBR_QMF_BANDS 64u

typedef struct {
    uint32_t k0, k2;
    uint32_t count;                 /* Number of master bands (>= 2). */
    uint32_t crossover_band;        /* bs_xover_band, < count */
    uint32_t kx;                    /* First high-frequency QMF subband. */
    uint8_t edges[SAUTI_AAC_SBR_QMF_BANDS + 1u];
} SautiAacSbrMaster;

/* Build the spec's linear master table for bs_freq_scale=0.
 * Only bs_alter_scale=0/1 is valid. Invalid input returns 0 without
 * touching *out. For all other bs_freq_scale modes call must fail
 * explicitly rather than silently treating them as linear. */
int sauti_aac_sbr_master_linear(unsigned k0, unsigned k2,
                                unsigned alter_scale, unsigned xover_band,
                                SautiAacSbrMaster *out);

/* SBR-03: standard logarithmic master boundaries for bs_freq_scale=1..3.
 * Supports both bs_alter_scale values, up to 48 master bands, with
 * one or two frequency regions. k0/k2 still come from the caller.
 * Failure never changes *out; no SBR synthesis takes place here. */
int sauti_aac_sbr_master_log(unsigned k0, unsigned k2,
                             unsigned freq_scale, unsigned alter_scale,
                             unsigned xover_band, SautiAacSbrMaster *out);
#endif
