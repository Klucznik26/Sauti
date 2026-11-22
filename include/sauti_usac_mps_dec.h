#ifndef SAUTI_USAC_MPS_DEC_H
#define SAUTI_USAC_MPS_DEC_H
/* USAC MPEG Surround 212, signal path (stage M3 of docs/USAC_MPS212_PLAN.md) for stereoConfigIndex 1 and 2 without residual bands, without phase coding and without temporal shaping: the QMF samples of the mono
 * downmix (after eSBR) go through the hybrid analysis (QMF bands 0..2 split into 6 + 2 + 2 bands, the others pass), the decorrelator (delay + all-pass per hybrid band, energy adjustment per parameter band), the
 * upmix matrices (interpolated slot by slot between the parameter sets) and the hybrid synthesis; two QMF grids come out.  Written for Sauti after studying libxaac's ixheaacd_mps_*.c (Apache 2.0). */
#include "sauti_aac_sbr_payload.h"
#include "sauti_usac_mps.h"

#define SAUTI_USAC_MPS_HYBRID 71u
#define SAUTI_USAC_MPS_DELAY_MAX 11u

typedef struct {
    SautiAacSbrComplex lf[3][12];                                                      /* the last 12 slots of QMF bands 0..2 (the 13 tap hybrid filters) */
    SautiAacSbrComplex delay[SAUTI_USAC_MPS_HYBRID][SAUTI_USAC_MPS_DELAY_MAX];         /* decorrelator: delayed input */
    SautiAacSbrComplex ap_state[SAUTI_USAC_MPS_HYBRID][SAUTI_USAC_MPS_DELAY_MAX];      /* decorrelator: all-pass state */
    float smooth_in[SAUTI_USAC_MPS_BANDS], smooth_out[SAUTI_USAC_MPS_BANDS];            /* decorrelator: smoothed energies per parameter band of the 28 band map */
    float resid_prev[SAUTI_USAC_MPS_BANDS][2], decor_prev[SAUTI_USAC_MPS_BANDS][2];    /* the upmix gains [band][output channel] of the last set of the previous frame */
    unsigned ready;
} SautiUsacMpsDec;

/* cfg->residual (stereoConfigIndex >= 2) selects the prediction upmix, otherwise the rotation upmix; band_count = QMF bands that carry signal (sub_band_end of the eSBR header); in / out0 / out1: slots x 64 QMF samples.
 * 1 = ok, 0 = refused (unsupported configuration or malformed). */
int sauti_usac_mps212_apply(SautiUsacMpsDec *d, const SautiUsacMpsConfig *cfg, const SautiUsacMpsFrame *f, unsigned band_count, unsigned slots,
                            const SautiAacSbrComplex *in, SautiAacSbrComplex *out0, SautiAacSbrComplex *out1);
#endif
