#ifndef SAUTI_AAC_ELD_SAC_DEC_H
#define SAUTI_AAC_ELD_SAC_DEC_H
/* AAC-ELD v2: the signal path of the low delay MPEG Surround 2-1-2: the 64 band QMF grid of the mono downmix (the output of the low delay SBR) is NOT split into hybrid bands; a decorrelator (delay + all-pass in
 * three reverb bands, energy adjustment per band of the 23 band map), the upmix matrices interpolated slot by slot between the parameter sets, and two 64 band grids come out.  Without temporal shaping, residual
 * and phase coding (the low delay syntax has none of the last two). */
#include "sauti_aac_eld_sac.h"
#include "sauti_aac_sbr_payload.h"

typedef struct {
    SautiAacSbrComplex delay[64][8];       /* decorrelator: delayed input (up to 8 slots) */
    SautiAacSbrComplex ap_state[64][16];   /* decorrelator: all-pass state */
    float smooth_in[23], smooth_out[23];   /* energy adjustment: smoothed energies per processing band */
    float decor_prev[23][2], resid_prev[23][2];
    unsigned ready;
} SautiAacEldSacDec;

/* in / out0 / out1: slots x 64 QMF samples.  1 = ok, 0 = refused (temporal shaping is not implemented yet, malformed parameters). */
int sauti_aac_eld_sac_apply(SautiAacEldSacDec *d, const SautiAacEldSacConfig *cfg, const SautiUsacMpsFrame *f, unsigned slots, const SautiAacSbrComplex *in, SautiAacSbrComplex *out0, SautiAacSbrComplex *out1);
#endif
