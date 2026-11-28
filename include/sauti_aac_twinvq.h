#ifndef SAUTI_AAC_TWINVQ_H
#define SAUTI_AAC_TWINVQ_H
/* TwinVQ (MPEG-4 Audio object type 7, ISO/IEC 14496-3 subpart 4), base layer: interleaved weighted vector quantisation of the MDCT spectrum with a Bark-scale envelope, an LPC
 * (LSP) spectral envelope, a harmonic (pitch) component, per-frame gain and band limiting; long term prediction (the version 1 LTP, long and short windows) and TNS work on the result and the
 * filter bank is the one of AAC.  One mode (the parameters do not depend on the bit rate): 1 or 2 channels, 8..96 kHz, 1024 or 960 lines.  The frame carries no mode header: the number of
 * bits per frame (= the length of the access unit) and the sampling frequency select the bandwidth.  Not covered: the scalable enhancement layers (object type 7 with several
 * elementary streams) and ER TwinVQ (object type 21).  The state lives on the heap of the caller (malloc).
 *
 * Output: interleaved float PCM (full scale +-1), `block` samples per channel and access unit. */
#include "sauti_aac_lc_frame.h"
#include <stddef.h>
#include <stdint.h>

typedef struct SautiAacTwinVq SautiAacTwinVq;

/* rate_index: ADTS sampling frequency index of the ASC; block: 1024 or 960; first_au_bytes: the length of the first access unit (selects the bandwidth for the whole stream, as the
 * reference decoder does).  NULL: unsupported parameters or no memory. */
SautiAacTwinVq *sauti_aac_twinvq_new(unsigned rate_index,unsigned channels,unsigned block,size_t first_au_bytes);
SautiAacTwinVq *sauti_aac_twinvq_dup(const SautiAacTwinVq *state);
void sauti_aac_twinvq_free(SautiAacTwinVq *state);
unsigned sauti_aac_twinvq_block(const SautiAacTwinVq *state);
unsigned sauti_aac_twinvq_channels(const SautiAacTwinVq *state);

/* One access unit -> PCM.  1 decoded, 0 malformed.  Atomic: on failure the state and `pcm` are unchanged. */
int sauti_aac_twinvq_decode(SautiAacTwinVq *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason);

/* The number of scalefactor bands of the coded range of a frame of the given window sequence (what the side information of the frame is sized by); for fixture generators. */
unsigned sauti_aac_twinvq_max_sfb(const SautiAacTwinVq *state,unsigned window_sequence);
/* Test hook: the spectra of the last decoded frame before long term prediction and TNS (doubles, `block` per channel), and its window sequence / shape. */
const double *sauti_aac_twinvq_last_spectrum(const SautiAacTwinVq *state,unsigned channel,unsigned *window_sequence,unsigned *window_shape);
#endif
