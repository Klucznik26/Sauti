#ifndef SAUTI_USAC_HBE_H
#define SAUTI_USAC_HBE_H
/* USAC eSBR harmonic transposer (HBE), ISO/IEC 23003-3 7.5.3, QMF based variant (the DFT based "high quality" transposer is not implemented).  The low band QMF slots of a frame go in, the transposed
 * subband signals of the high band come out: pv[slot][band] for bands [start_band, end_band).  The output of a frame belongs to the PREVIOUS frame's low band (the transposer works on a window of 13 analysis
 * columns, the second half of its output buffer waits for the next frame): the caller delays the low band by `no_bins` slots.  Algorithm per docs/USAC_HBE_PLAN.md, tables from libxaac (data only). */
#include <stdint.h>

#define SAUTI_USAC_HBE_MAX_SLOTS 64u                     /* QMF slots per frame: 32 (2:1, 8:3), 64 (4:1) */
#define SAUTI_USAC_HBE_BANDS 64u
#define SAUTI_USAC_HBE_MAX_SYNTH 20u                     /* synth_size = 4 * ((start_band + 4) / 8 + 1) */
#define SAUTI_USAC_HBE_PATCHES 6u
#define SAUTI_USAC_HBE_CONTEXT 12u                       /* columns of the operating window kept from frame to frame */
#define SAUTI_USAC_HBE_CARRY 10u                         /* output slots a frame leaves for the next one (rows no_bins .. no_bins + 9) */

typedef struct { double re, im; } SautiUsacHbeCx;

/* The state that lives from frame to frame (about 15 KiB: part of the state of every channel, which is copied at checkpoints).  Everything else of a frame is scratch on the stack. */
typedef struct {
    unsigned ready;
    unsigned no_bins;                                    /* slots per frame */
    unsigned start_band, end_band, k_start, synth_size, max_stretch, upsamp4;
    unsigned x_over[SAUTI_USAC_HBE_PATCHES];             /* upper QMF band of every patch (0 = unused) */
    float time_tail[SAUTI_USAC_HBE_MAX_SYNTH];           /* the last synth_size time samples of the previous frame */
    float synth_buf[20u * SAUTI_USAC_HBE_MAX_SYNTH];     /* history of the synthesis filter bank */
    float analy_buf[640];                                /* history of the analysis filter bank */
    float context[SAUTI_USAC_HBE_CONTEXT][SAUTI_USAC_HBE_BANDS][2];   /* the 12 columns of the operating window before the frame's own */
    float carry[SAUTI_USAC_HBE_CARRY][SAUTI_USAC_HBE_BANDS][2];       /* products already accumulated for the slots after the frame */
} SautiUsacHbe;

/* The tables of the SBR header changed (or the first frame): start_band / end_band from the low-resolution band table, k_start, synth_size, patch borders.  `ccfl768` limits k_start for a 768 core, `upsamp4` for
 * eSBR 4:1.  The synthesis / analysis histories are cleared.  1 = ok, 0 = refused. */
int sauti_usac_hbe_reinit(SautiUsacHbe *h, unsigned no_bins, const uint8_t *low, unsigned n_low, const uint8_t *high, unsigned n_high, unsigned upsamp4, unsigned ccfl768);

/* eSBR 4:1: the patches beyond the transposed orders repeat the last transposed patch (done by sauti_usac_hbe_process; public for the unit test).  pv = no_bins * 64 complex samples. */
void sauti_usac_hbe_replicate(const SautiUsacHbe *h, SautiUsacHbeCx *pv);

/* One frame (for eSBR 4:1 the output also gets the spectral replication of the patches beyond the fourth order): q = no_bins slots of low band QMF samples (bands 0..31, q[slot * 64 + band]); pitch = sbrPitchInBins; pv = no_bins * 64 complex samples (zero outside [start_band, end_band)).
 * 1 = ok, 0 = refused (the state is then undefined, reinit). */
int sauti_usac_hbe_process(SautiUsacHbe *h, const SautiUsacHbeCx *q, unsigned pitch_in_bins, SautiUsacHbeCx *pv);
#endif
