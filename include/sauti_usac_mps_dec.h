#ifndef SAUTI_USAC_MPS_DEC_H
#define SAUTI_USAC_MPS_DEC_H
/* USAC MPEG Surround 212, signal path (stage M3 of docs/USAC_MPS212_PLAN.md) for stereoConfigIndex 1 and 2 without residual bands, without phase coding and without temporal shaping: the QMF samples of the mono
 * downmix (after eSBR) go through the hybrid analysis (QMF bands 0..2 split into 6 + 2 + 2 bands, the others pass), the decorrelator (delay + all-pass per hybrid band, energy adjustment per parameter band), the
 * upmix matrices (interpolated slot by slot between the parameter sets) and the hybrid synthesis; two QMF grids come out.  Written for Sauti after studying libxaac's ixheaacd_mps_*.c (Apache 2.0). */
#include "sauti_aac_sbr_payload.h"
#include "sauti_aac_sbr_qmf_analysis.h"
#include "sauti_usac_mps.h"

#define SAUTI_USAC_MPS_HYBRID 71u
#define SAUTI_USAC_MPS_DELAY_MAX 11u

typedef struct {
    SautiAacSbrComplex lf[3][12], lf_res[3][12];                                       /* the last 12 slots of QMF bands 0..2 of the downmix and of the residual */                                                      /* the last 12 slots of QMF bands 0..2 (the 13 tap hybrid filters) */
    SautiAacSbrComplex delay[SAUTI_USAC_MPS_HYBRID][SAUTI_USAC_MPS_DELAY_MAX];         /* decorrelator: delayed input */
    SautiAacSbrComplex ap_state[SAUTI_USAC_MPS_HYBRID][SAUTI_USAC_MPS_DELAY_MAX];      /* decorrelator: all-pass state */
    float smooth_in[SAUTI_USAC_MPS_BANDS], smooth_out[SAUTI_USAC_MPS_BANDS];            /* decorrelator: smoothed energies per parameter band of the 28 band map */
    float resid_prev[SAUTI_USAC_MPS_BANDS][2], decor_prev[SAUTI_USAC_MPS_BANDS][2], rsd_prev[SAUTI_USAC_MPS_BANDS][2], im_prev[SAUTI_USAC_MPS_BANDS][2];    /* the upmix gains [band][output channel] of the last set of the previous frame */
    SautiAacSbrQmfAnalysisState res_analysis;                                          /* the 32 band QMF analysis of the residual channel (the second core channel) */
    SautiAacSbrComplex res_tail[6][32];                                                /* the last 6 slots of QMF bands 3..31 of the residual analysis: the reference delays them against bands 0..2 */
    float phase_prev[2][SAUTI_USAC_MPS_BANDS];                                         /* phase of the last set of the previous frame (left, right output) */
    int32_t smooth_phase[2][SAUTI_USAC_MPS_BANDS];                                     /* smoothed overall phase difference, Q27 */
    float stp_nrg_dir, stp_nrg_dir_prev, stp_nrg_diff[2], stp_nrg_diff_prev[2], stp_scale_last[2];   /* STP: smoothed energies and the last scale */
    unsigned stp_init, stp_update;
    float ges_pb[3][20], ges_avg[3], ges_frame[3];                                    /* GES: smoothed band energies, average energy and frame energy of the two outputs and the downmix */
    unsigned ges_init;
    unsigned ready;
} SautiUsacMpsDec;

/* The residual of the reference arrives with QMF bands 0..2 six slots AHEAD of the others (the same alignment the downmix has, see mps_lf_ahead): low = the 32 band analysis of this frame's residual
 * ([slot][32], `slots` slots), grid = [slot][64] for sauti_usac_mps212_apply (bands 32..63 zero). */
void sauti_usac_mps212_residual_grid(SautiUsacMpsDec *d, const SautiAacSbrComplex *low, unsigned slots, SautiAacSbrComplex *grid);

/* res_in: the QMF grid ([slot][64], bands 0 .. res_band_count - 1) of the residual channel, needed when cfg->residual_bands > 0 (it replaces the decorrelated signal in the parameter bands below cfg->residual_bands), else NULL.
 * cfg->residual (stereoConfigIndex >= 2) selects the prediction upmix, otherwise the rotation upmix; band_count = QMF bands that carry signal (sub_band_end of the eSBR header); in / out0 / out1: slots x 64 QMF samples.
 * 1 = ok, 0 = refused (unsupported configuration or malformed). */
int sauti_usac_mps212_apply(SautiUsacMpsDec *d, const SautiUsacMpsConfig *cfg, const SautiUsacMpsFrame *f, unsigned band_count, unsigned slots,
                            const SautiAacSbrComplex *in, const SautiAacSbrComplex *res_in, unsigned res_band_count, SautiAacSbrComplex *out0, SautiAacSbrComplex *out1);
#endif
