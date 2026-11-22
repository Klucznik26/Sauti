#ifndef SAUTI_USAC_PVC_H
#define SAUTI_USAC_PVC_H
/* Predictive vector coding (PVC) of eSBR, ISO/IEC 23003-3 7.5: in a PVC frame of a single channel element the envelope of the high band is not transmitted as scalefactors; for each of the 16 time slots
 * of the frame a 7-bit index (pvc_id) selects prediction coefficients that derive the high band envelope (8 groups, mode 1, or 6 groups, mode 2) from the energies of the three groups of low band
 * subbands just below the crossover.  This module holds the syntax result and the predictor (time smoothing, prediction); the adjustment of the HF signal is in sauti_aac_sbr_pvc_adjust.h. */
#include <stdint.h>

#define SAUTI_USAC_PVC_SLOTS 16u                 /* time slots of a frame (QMF slots: 16 * rate) */
#define SAUTI_USAC_PVC_ESC_SINE 31u              /* bs_sine_position: no onset inside the frame */

/* What one PVC frame carries besides the usual noise floor / inverse filtering data (those are in the payload frame). */
typedef struct {
    uint32_t active;                             /* the frame is a PVC frame */
    uint32_t mode;                               /* header pvc_mode: 1, 2 */
    uint32_t div_mode, ns_mode;
    uint32_t var_len;                            /* bs_var_len_hf: the frame's grid runs 0..3 time slots past the frame */
    uint16_t id[SAUTI_USAC_PVC_SLOTS];           /* pvc_id of every time slot */
    uint8_t border[3];                           /* the time slots the PVC adjustment works on: envelope borders, in time slots, inside [0, 16] */
    uint32_t first_slot;                         /* border[0]: the slots before it were already adjusted by the previous (classic) frame */
    uint32_t sine_position;                      /* bs_sine_position, ESC_SINE when none */
    uint32_t sin_start_cur, sin_len_cur;         /* a sine of the previous frame that reaches into the first slots of this one: start and length in time slots */
    uint32_t prev_mode;                          /* kind of the previous frame: 0 none, 1 classic, 2 PVC */
    uint32_t prev_freq_res;                      /* frequency resolution of the last envelope of the previous frame (selects the band table of a carried sine) */
} SautiUsacPvcFrame;

/* State of the predictor between frames: the low band energies (dB) of the last 15 time slots. */
typedef struct {
    uint32_t prev_flag;                          /* the previous frame was a PVC frame */
    uint32_t prev_rate;
    int32_t prev_first_band;
    float esg[2u*SAUTI_USAC_PVC_SLOTS-1u][3];
} SautiUsacPvcState;

/* low_energy: mean energy of QMF subbands 0..31 per time slot (16 slots, stride 32), in the units of the envelope; rate = QMF slots per time slot (2: 2:1, 4: 4:1); first_band = kx (start of the high band);
 * out: predicted envelope energy of every high band subband, linear, 16 x 64 (valid from first_band).  1 = ok, 0 = invalid parameters (the state is unchanged). */
int sauti_usac_pvc_predict(SautiUsacPvcState *st,const SautiUsacPvcFrame *f,unsigned rate,unsigned first_band,const float low_energy[16][32],float out[16][64]);
#endif
