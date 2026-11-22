#ifndef SAUTI_USAC_MPS_H
#define SAUTI_USAC_MPS_H
/* USAC MPEG Surround 212 (stereoConfigIndex 1..3): the PARAMETER decoder, stage M1 + M2a of docs/USAC_MPS212_PLAN.md.  Mps212Data() of a frame (parameter sets, EcData with Huffman / PCM coding of CLD, ICC and IPD, smoothing
 * data) is parsed and turned into dequantised CLD (dB), ICC and IPD (radians) per parameter set and parameter band: indices mapped from the coded frequency groups to the parameter bands, interpolated between the
 * sets, clamped; the state that links frames (previous indices, quantiser of the previous set, smoothing) lives in SautiUsacMpsState.  The signal path (hybrid filter bank, decorrelator, matrices) is NOT here yet. */
#include "sauti_usac_bits.h"
#include <stdint.h>

#define SAUTI_USAC_MPS_BANDS 28u
#define SAUTI_USAC_MPS_SETS 9u                           /* numParamSets (at most 8) plus the set that closes a frame whose last set is not in its last slot */

/* the fields of Mps212Config() the frame parser needs */
typedef struct { unsigned freq_res, high_rate, phase_coding, bands_phase, temp_shape, env_quant, decorr, fixed_gain, residual, residual_bands; } SautiUsacMpsConfig;   /* residual: stereoConfigIndex >= 2 */

typedef struct {
    unsigned num_sets;                                   /* including the closing set when ext_frame */
    unsigned ext_frame;                                  /* the last transmitted set is not in the last time slot: a copy of it is appended */
    unsigned param_slot[SAUTI_USAC_MPS_SETS];
    unsigned independency;
    unsigned phase_mode, opd_smoothing;
    unsigned param_bands;
    int cld_idx[SAUTI_USAC_MPS_SETS][SAUTI_USAC_MPS_BANDS], icc_idx[SAUTI_USAC_MPS_SETS][SAUTI_USAC_MPS_BANDS], ipd_idx[SAUTI_USAC_MPS_SETS][SAUTI_USAC_MPS_BANDS];
    float cld[SAUTI_USAC_MPS_SETS][SAUTI_USAC_MPS_BANDS];          /* dB */
    float icc[SAUTI_USAC_MPS_SETS][SAUTI_USAC_MPS_BANDS];
    float ipd[SAUTI_USAC_MPS_SETS][SAUTI_USAC_MPS_BANDS];          /* radians */
    unsigned smooth_time[SAUTI_USAC_MPS_SETS];                      /* 64 .. 512, per set */
    uint8_t smooth_data[SAUTI_USAC_MPS_SETS][SAUTI_USAC_MPS_BANDS]; /* band is smoothed */
} SautiUsacMpsFrame;

typedef struct {
    int cmp_cld_prev[SAUTI_USAC_MPS_BANDS], cmp_icc_prev[SAUTI_USAC_MPS_BANDS], cmp_ipd_prev[SAUTI_USAC_MPS_BANDS];   /* last coded (grouped) indices, EcData history */
    int cld_pre[SAUTI_USAC_MPS_BANDS], icc_pre[SAUTI_USAC_MPS_BANDS], ipd_pre[SAUTI_USAC_MPS_BANDS];                    /* last mapped indices, "keep / interpolate" history */
    unsigned quant_coarse_prev[3];                       /* CLD, ICC, IPD */
    unsigned smooth_time_prev;
    uint8_t smooth_data_prev[SAUTI_USAC_MPS_BANDS];
} SautiUsacMpsState;

/* Parses Mps212Data(indep) of the element configured by `e` at the position of `b` for a frame of `time_slots` QMF slots (32 at the 2:1 and 8:3 output, 64 at 4:1) and decodes the parameters.  1 = ok, 0 = malformed,
 * -1 = a tool that is not implemented (temporal shaping enabled in the frame).  `st` and `f` are changed only on success. */
int sauti_usac_mps212_parse(const SautiUsacMpsConfig *e, unsigned time_slots, SautiUsacBits *b, int indep, SautiUsacMpsState *st, SautiUsacMpsFrame *f);
#endif
