#ifndef SAUTI_USAC_H
#define SAUTI_USAC_H
/* USAC (xHE-AAC, MPEG-4 audio object type 42, ISO/IEC 23003-3): UsacConfig parser and the frequency-domain (FD) core decoder.
 * Implemented: eSBR 2:1 in its classic form (sauti_usac_sbr.c), UsacConfig (incl. SBR / MPEG Surround / extension element configuration), single channel and channel pair elements and LFE with arithmetic spectral coding, scale factors,
 * noise filling, TNS, M/S stereo, long / start / stop / eight-short windows of 1024 or 768 lines.  Not yet: LPD (ACELP / TCX) frames, complex stereo prediction, eSBR, MPS212, time-warped MDCT -
 * frames that use them are reported as unsupported (-1) and change nothing. */
#include "sauti_aac_lc_frame.h"
#include "sauti_usac_ac.h"
#include "sauti_aac_sbr_channel.h"
#include <stddef.h>
#include <stdint.h>

#define SAUTI_USAC_MAX_ELEMENTS 16u
#define SAUTI_USAC_MAX_CHANNELS 8u                        /* channel configurations 1..7 (up to 7.1); the state of a decoder instance (about 250 KiB) lives on the heap of the engine and is copied deeply at checkpoints */

enum { SAUTI_USAC_SCE = 0, SAUTI_USAC_CPE = 1, SAUTI_USAC_LFE = 2, SAUTI_USAC_EXT = 3 };

typedef struct {
    unsigned type;                       /* SAUTI_USAC_SCE .. EXT */
    unsigned tw_mdct, noise_fill;
    unsigned stereo_config_index;        /* CPE: 0 = plain stereo, 1..3 = MPEG Surround 212 */
    unsigned sbr_harmonic, sbr_intertes, sbr_pvc;
    unsigned sbr_start_freq, sbr_stop_freq, sbr_freq_scale, sbr_alter_scale, sbr_noise_bands, sbr_limiter_bands, sbr_limiter_gains, sbr_interpol_freq, sbr_smoothing_mode;
    unsigned mps_freq_res, mps_fixed_gain, mps_temp_shape, mps_decorr, mps_high_rate, mps_phase_coding, mps_otts_bands_phase, mps_residual_coding, mps_residual_bands, mps_pseudo_lr, mps_env_quant;
    unsigned ext_type, ext_default_len, ext_payload_frag;
} SautiUsacElement;

typedef struct {
    unsigned sampling_index;             /* usacSamplingFrequencyIndex, 0x1f = explicit rate */
    unsigned sample_rate;                /* output rate (after SBR) in Hz */
    unsigned core_rate;                  /* rate of the core coder */
    unsigned core_sbr_frame_len_idx;     /* 0: 768, 1: 1024, 2: 768 + SBR 8:3, 3: 1024 + SBR 2:1, 4: 1024 + SBR 4:1 */
    unsigned core_frame_len;             /* 768 or 1024 */
    unsigned sbr_mode;                   /* 0 none, 1 = 2:1, 2 = 8:3, 3 = 4:1 */
    unsigned out_frame_len;              /* samples per channel in one frame */
    unsigned aac_rate_index;             /* sampling_frequency_index of the core rate for the band tables */
    unsigned channel_config;             /* channelConfigurationIndex, 0 = explicit */
    unsigned out_channels;               /* channels of the output */
    unsigned num_elements;
    SautiUsacElement e[SAUTI_USAC_MAX_ELEMENTS];
    size_t bits_used;
} SautiUsacConfig;

/* Parses UsacConfig() starting at bit `bit_offset`.  1 = ok, 0 = malformed / not representable, -1 = valid but beyond what this decoder supports (e.g. explicit channel positions). */
int sauti_usac_config_parse(const uint8_t *data, size_t size, size_t bit_offset, SautiUsacConfig *out);

/* The whole AudioSpecificConfig of an MP4 / LOAS stream: object type 42 (escape 31 + 6 bits), samplingFrequencyIndex (4 bits, 15 = explicit 24-bit rate), channelConfiguration (4 bits), then UsacConfig().
 * Result as sauti_usac_config_parse; 0 for any other object type. */
int sauti_usac_asc_parse(const uint8_t *asc, size_t size, SautiUsacConfig *out);

typedef struct {
    SautiUsacAcState ac;
    float overlap[1024];
    unsigned prev_shape;
    uint32_t noise_seed;
    float prev_coef[1024];               /* CPE: the final spectrum of the previous frame (what went into the IMDCT): the downmix of the previous frame for complex prediction */
    /* complex stereo prediction state of a channel pair, kept in its first channel */
    int16_t alpha_prev_re[8][64], alpha_prev_im[8][64];
    unsigned prev_ngroups, last_seq, last_shape;     /* window groups, window sequence and shape of the previous frame's first channel */
} SautiUsacChannel;

typedef struct {
    SautiAacSbrHeader header;                    /* persists across frames: amp_res / xover from SbrInfo, the rest from the header */
    uint32_t have_header;
    SautiAacSbrQuantState quant[2];
} SautiUsacSbrState;

typedef struct {
    SautiUsacChannel ch[SAUTI_USAC_MAX_CHANNELS];
    SautiUsacSbrState sbr[SAUTI_USAC_MAX_ELEMENTS];       /* eSBR: header and entropy-coding history of every element */
    SautiAacSbrChannel sbr_ch[SAUTI_USAC_MAX_CHANNELS];   /* eSBR: QMF, HF generation and adjustment state of every channel */
    unsigned channels;
} SautiUsacState;

void sauti_usac_state_init(SautiUsacState *state, const SautiUsacConfig *cfg);

/* One access unit (usac_raw_data_block).  pcm receives cfg->out_frame_len x cfg->out_channels interleaved floats (1024 or, with 2:1 eSBR, 2048 samples per channel).  Atomic: 1 = decoded, 0 = malformed / truncated (state and pcm untouched),
 * -1 = valid but unsupported (LPD frame, complex prediction, eSBR, MPS212, timewarp).  `reason` may be NULL. */
int sauti_usac_decode(const SautiUsacConfig *cfg, SautiUsacState *state, const uint8_t *au, size_t length, float *pcm, SautiAacReason *reason);
#endif
