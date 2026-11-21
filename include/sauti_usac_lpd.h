#ifndef SAUTI_USAC_LPD_H
#define SAUTI_USAC_LPD_H
/* USAC linear-prediction-domain (LPD) core, ISO/IEC 23003-3 (ACELP / TCX / FAC).  This header is the syntax layer (stage L0 of docs/USAC_LPD_PLAN.md): lpd_channel_stream() and fac_data() parsed into raw
 * indices; nothing is dequantised here.  The arithmetic coded TCX spectra are decoded while parsing (their length in bits is only known by decoding them), through the same context coder as the FD core. */
#include "sauti_usac_ac.h"
#include "sauti_usac_bits.h"
#include <stdint.h>

#define SAUTI_USAC_LPD_MAX_SUBFR 4u                     /* 64-sample ACELP subframes per 256-sample block (3 for a 768-line core) */
#define SAUTI_USAC_LPD_FAC_MAX 192u                      /* lines of one FAC block: ccfl / 8 */

/* One block of 8 lines of the algebraic vector quantiser (multi-rate Gosset lattice RE8): the codebook number qn, the base codebook index (4 * n bits) and the eight Voronoi extension indices (nk bits each). */
typedef struct { uint16_t qn; uint32_t index; uint16_t kv[8]; } SautiUsacLpdAvq;

typedef struct {
    unsigned present;
    unsigned gain;                                       /* fac_gain (7 bits), only in the closing FAC data of a frame */
    unsigned length;                                     /* lines */
    unsigned count;                                      /* blocks of 8 lines */
    SautiUsacLpdAvq blk[SAUTI_USAC_LPD_FAC_MAX / 8u];
} SautiUsacLpdFac;

/* The LSF quantiser data of one set of line spectral frequencies: LPC0 (start of the frame, first LPD frame only), LPC1 / LPC2 / LPC3 (quarter / half / three quarters) and LPC4 (end of the frame). */
typedef struct {
    unsigned present, mode_lpc, nk_mode;
    unsigned first_stage;                                /* 8-bit index of the first stage */
    unsigned has_first_stage;
    uint16_t qn[2];
    uint32_t index[2];
    uint16_t kv[2][8];
} SautiUsacLpdLsf;

typedef struct {
    unsigned mean_energy;
    unsigned acb_index[SAUTI_USAC_LPD_MAX_SUBFR], ltp_filtering[SAUTI_USAC_LPD_MAX_SUBFR], gain[SAUTI_USAC_LPD_MAX_SUBFR];
    uint32_t icb_index[SAUTI_USAC_LPD_MAX_SUBFR][8];
} SautiUsacLpdAcelp;

typedef struct {
    unsigned acelp_core_mode, lpd_mode;
    unsigned mod[4];                                     /* 0 ACELP, 1 TCX 256, 2 TCX 512, 3 TCX 1024 (per quarter of the frame; a longer TCX frame repeats it) */
    unsigned bpf_control_info, core_mode_last, fac_data_present, first_lpd_flag;
    unsigned short_fac_flag;
    SautiUsacLpdFac fac[4];                              /* FAC data in front of quarter k (transitions inside the frame or from the previous LPD frame) */
    SautiUsacLpdFac fac_end;                             /* closing FAC data of a frame that follows an FD frame (carries the gain) */
    SautiUsacLpdAcelp acelp[4];
    unsigned noise_factor[4], global_gain[4], tcx_len[4];
    unsigned arith_reset;
    int32_t quant[1024];                                 /* the TCX spectra, one after the other (tcx_len[k] lines at the offset of the quarters before) */
    SautiUsacLpdLsf lsf[5];                              /* [0] LPC0 ... [4] LPC4 */
} SautiUsacLpdFrame;

/* fac_data(): `length` lines, optional gain (7 bits) when `with_gain`.  1 = ok, 0 = malformed. */
int sauti_usac_lpd_fac_parse(SautiUsacBits *b, unsigned length, int with_gain, SautiUsacLpdFac *fac);

/* lpd_channel_stream() of one channel.  ccfl = core frame length (1024 / 768).  `ac` is the arithmetic coder context of the channel (shared with the FD windows of the same channel), `indep` the independency flag of the
 * access unit.  1 = ok, 0 = malformed. */
int sauti_usac_lpd_parse(SautiUsacBits *b, unsigned ccfl, int indep, SautiUsacAcState *ac, SautiUsacLpdFrame *f);
#endif
