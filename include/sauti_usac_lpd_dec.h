#ifndef SAUTI_USAC_LPD_DEC_H
#define SAUTI_USAC_LPD_DEC_H
/* USAC LPD decoding stages (docs/USAC_LPD_PLAN.md, L1 onwards).  Float implementation; the data tables come from sauti_usac_lpd_tables.h. */
#include "sauti_usac_lpd.h"

#define SAUTI_LPD_ORDER 16u

/* L1: algebraic vector quantiser (multi-rate lattice RE8) and the line spectral frequencies. */
/* One 8-line block: qn = codebook number, index = base codebook index, kv = Voronoi extension indices.  out[8] are the integer lattice coordinates.  1 = ok, 0 = index out of range. */
int sauti_usac_lpd_avq_decode(unsigned qn, uint32_t index, const uint16_t kv[8], int32_t out[8]);

/* Dequantises the LSF sets of a frame.  lsf[s][0..15] in Hz (0..6400), s = 0..4 as in SautiUsacLpdFrame.lsf; sets the frame does not carry are left as they come in `lsf` (the decoder keeps the end set of the
 * previous frame in lsf[0] when !first_lpd_flag: the caller fills lsf[0] from its own state in that case).  1 = ok, 0 = malformed. */
int sauti_usac_lpd_lsf_dequant(const SautiUsacLpdFrame *f, float lsf[5][SAUTI_LPD_ORDER]);

/* LSF (Hz) <-> LSP (cosine domain) and LSP -> prediction filter a[0..16] (a[0] = 1). */
void sauti_usac_lpd_lsf_to_lsp(const float *lsf, float *lsp);
void sauti_usac_lpd_lsp_to_lsf(const float *lsp, float *lsf);
void sauti_usac_lpd_lsp_to_lpc(const float *lsp, float *a);
/* nb_subfr interpolated filters between lsp_old and lsp_new followed by the filter of lsp_new itself: a[(nb_subfr + 1) * 17] */
void sauti_usac_lpd_lpc_interpolate(const float *lsp_old, const float *lsp_new, float *a, unsigned nb_subfr);
/* the three filters of a TCX frame: the middle one (interpolated between the old and new LSP), the old and the new one: a[3 * 17] */
void sauti_usac_lpd_lpc_tcx_set(const float *lsp_old, const float *lsp_new, float *a, unsigned nb_subfr);

/* L2 - L4: the LPD frame decoder.  One instance per channel; state is plain data (copyable). */
#define SAUTI_LPD_MAX_PITCH 411u                          /* TMAX + 6 * ((24000 * TMIN + 6400) / 12800 - TMIN): the longest pitch lag of the highest core rate */
#define SAUTI_LPD_SYNTH_DELAY_MAX 448u                    /* ((16 / 2) - 1) * 64 */
typedef struct {
    int mode_prev;                                        /* mode of the last quarter: 0 ACELP, 1..3 TCX, -1 start of decoding / after FD, -2 after eight short windows */
    float lsp_old[16], lsf_prev[16];
    float lp_prev[2 * 17];                                /* the two last filters of the previous frame */
    float exc_prev[1 + 2 * 128];                          /* the overlap part of the previous quarter (ACELP: de-emphasis memory + FAC zero input response, TCX: windowed excitation) */
    float xcit_prev[SAUTI_LPD_MAX_PITCH + 17];
    float synth_prev[SAUTI_LPD_MAX_PITCH + SAUTI_LPD_SYNTH_DELAY_MAX];
    float bpf_prev[12 + 64];
    int pitch_prev[7];
    float gain_prev[7];
    int bpf_active_prev;
    float gain_threshold, fac_gain, fac_fd[64];
    unsigned fscale;                                      /* core sampling rate */
    float overlap[1024];                                  /* output overlap (not yet used by a following frame) in the units of the PCM */
    int prev_lpd;                                         /* the previous frame was an LPD frame */
} SautiUsacLpdState;

void sauti_usac_lpd_state_init(SautiUsacLpdState *st, unsigned core_rate);
/* Decodes one LPD frame into out[ccfl] (PCM scale +-32768).  fd_tail = the unwindowed tail (second half of the IMDCT output, PCM scale +-32768) of the FD frame before it - used when the previous frame was not LPD (NULL = start of decoding, zeros) -
 * and fd_last_short = that frame had eight short windows.  noise_seed: the TCX noise generator of the channel.  1 = ok, 0 = malformed, -1 = valid but not supported yet (a frame that starts with ACELP after an FD frame). */
int sauti_usac_lpd_decode(SautiUsacLpdState *st, const SautiUsacLpdFrame *f, unsigned ccfl, unsigned *noise_seed, const float *fd_tail, int fd_last_short, float *out);

/* LPD -> FD: the output (PCM scale +-32768) of a long FD frame that follows an LPD frame (window sequence seq: 0 = ONLY_LONG: a full sine overlap, FAC ignored; 1 / 3 = LONG_START (STOP_START after LPD) / LONG_STOP: the slope of ccfl / 4 and the FAC).  x[0..ccfl) = the first half of the IMDCT output of the FD frame in PCM scale, fac = its fac_data (NULL / not
 * present: none).  Uses and consumes the state of the LPD core (synthesis look-ahead, filter memory); afterwards the state is "previous frame was FD".  1 = ok, 0 = malformed, -1 = unsupported (ccfl != 1024, rate). */
int sauti_usac_lpd_fd_frame(SautiUsacLpdState *st, unsigned ccfl, unsigned seq, const float *x, const SautiUsacLpdFac *fac, float *out);
/* The same for an FD frame with eight short windows (lfac = ccfl / 16): returns the overlap the LPD core leaves (ovl[ccfl], PCM scale) and the FAC signal (facsig[2 * 64]); the caller assembles the output
 * (the first window with the transition slope, + facsig at [ccfl / 2, ccfl / 2 + 2 * 64) ) and ends with lpd_fd_finish, which continues the long-term post-filter over the start of out[] and leaves the LPD state. */
int sauti_usac_lpd_fd_short_begin(const SautiUsacLpdState *st, unsigned ccfl, const SautiUsacLpdFac *fac, float *ovl, float *facsig);
int sauti_usac_lpd_fd_finish(SautiUsacLpdState *st, unsigned ccfl, int is_short, float *out);
#endif
