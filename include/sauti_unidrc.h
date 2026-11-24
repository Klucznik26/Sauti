#ifndef SAUTI_UNIDRC_H
#define SAUTI_UNIDRC_H
/* MPEG-D DRC (uniDRC, ISO/IEC 23003-4) for USAC: the bitstream parsers, stage U1 of docs/UNIDRC_PLAN.md.  UniDrcConfig() (also its version 1 extension), LoudnessInfoSet() and UniDrcGain() are parsed into plain
 * structures; selection, gain interpolation and application come later.  Written for Sauti after studying decoder/drc_src of libxaac (Apache 2.0); the code tables come from there (sauti_unidrc_tables.c). */
#include "sauti_usac_bits.h"
#include <stdint.h>

#define SAUTI_UNIDRC_MAX_COEFFS 8u
#define SAUTI_UNIDRC_MAX_GAINSETS 24u
#define SAUTI_UNIDRC_MAX_SEQ 24u
#define SAUTI_UNIDRC_MAX_INSTR 36u
#define SAUTI_UNIDRC_MAX_CH 8u
#define SAUTI_UNIDRC_MAX_BANDS 8u
#define SAUTI_UNIDRC_MAX_DMX 16u
#define SAUTI_UNIDRC_MAX_LOUD 36u
#define SAUTI_UNIDRC_MAX_NODES 256u
#define SAUTI_UNIDRC_EFFECT_DUCK_OTHER 0x0400u
#define SAUTI_UNIDRC_EFFECT_DUCK_SELF 0x0800u

typedef struct {
    unsigned profile;                  /* gainCodingProfile: 0 regular, 1 fading, 2 clipping, 3 constant */
    unsigned interpolation;            /* 0 spline, 1 linear */
    unsigned full_frame, time_alignment;
    unsigned time_delta_min;           /* 0 = the default of the sample rate */
    unsigned band_count;
    unsigned seq_idx[SAUTI_UNIDRC_MAX_BANDS];
} SautiUniDrcGainSet;

typedef struct {
    unsigned version, location, frame_size;        /* frame_size 0 = the codec frame */
    unsigned gain_set_count, gain_seq_count;
    SautiUniDrcGainSet gain_set[SAUTI_UNIDRC_MAX_GAINSETS];
    int gain_set_of_seq[SAUTI_UNIDRC_MAX_SEQ];     /* -1 = no gain set (a constant sequence) */
} SautiUniDrcCoeffs;

typedef struct {
    unsigned id, target_channels, target_layout, coefficients_present;
} SautiUniDrcDownmix;

typedef struct {
    unsigned set_id, location, effect;
    unsigned dmx_id[8], dmx_id_count, apply_to_dwnmix;
    int limiter_present; float limiter_peak;
    int target_loudness_present, target_loudness_upper, target_loudness_lower;
    int depends_on_present; unsigned depends_on, no_independent_use;
    unsigned channels;                             /* channels the instruction is given for */
    int gain_set_index[SAUTI_UNIDRC_MAX_CH];       /* per channel, -1 = none */
    int group_of_ch[SAUTI_UNIDRC_MAX_CH];          /* channel group of a channel, -1 = none */
    unsigned groups;
    int gain_set_of_group[SAUTI_UNIDRC_MAX_CH];
    float ducking_scaling[SAUTI_UNIDRC_MAX_CH];    /* per channel group (ducking sets) */
    struct { unsigned scaling, offset_flag; float attn, ampl, offset; } mod[SAUTI_UNIDRC_MAX_CH][SAUTI_UNIDRC_MAX_BANDS];   /* gain modifiers per channel group and band */
} SautiUniDrcInstr;

typedef struct {
    unsigned sample_rate;                          /* 0 = not given */
    unsigned base_channels;
    unsigned dmx_count; SautiUniDrcDownmix dmx[SAUTI_UNIDRC_MAX_DMX];
    unsigned coeff_count; SautiUniDrcCoeffs coeff[SAUTI_UNIDRC_MAX_COEFFS];
    unsigned instr_count; SautiUniDrcInstr instr[SAUTI_UNIDRC_MAX_INSTR];
    unsigned unsupported;                          /* bit 0: parametric DRC, bit 1: EQ / loudness EQ present in the extension (parsed over, not used) */
} SautiUniDrcConfig;

typedef struct { unsigned method; float value; unsigned system, reliability; } SautiUniDrcMeasure;
typedef struct {
    unsigned set_id, downmix_id;
    int sample_peak_present, true_peak_present; float sample_peak, true_peak;
    unsigned measures; SautiUniDrcMeasure measure[16];
} SautiUniDrcLoudness;
typedef struct { unsigned count; SautiUniDrcLoudness info[SAUTI_UNIDRC_MAX_LOUD]; unsigned album_count; SautiUniDrcLoudness album[SAUTI_UNIDRC_MAX_LOUD]; } SautiUniDrcLoudnessSet;

typedef struct { unsigned count; int time[SAUTI_UNIDRC_MAX_NODES]; float gain_db[SAUTI_UNIDRC_MAX_NODES], slope[SAUTI_UNIDRC_MAX_NODES]; unsigned coding_mode; } SautiUniDrcNodes;
typedef struct { SautiUniDrcNodes seq[SAUTI_UNIDRC_MAX_SEQ]; unsigned seq_count; } SautiUniDrcGain;

/* 1 = parsed, 0 = malformed, -1 = a feature that is not supported (the structure is not valid then) */
int sauti_unidrc_config_parse(SautiUsacBits *b, SautiUniDrcConfig *out);
int sauti_unidrc_loudness_parse(SautiUsacBits *b, SautiUniDrcLoudnessSet *out);
/* UniDrcGain() of one frame: `frame_size` = samples per DRC frame (the codec frame), `rate` the sample rate (for the default time resolution) */
int sauti_unidrc_gain_parse(const SautiUniDrcConfig *cfg, unsigned frame_size, unsigned rate, SautiUsacBits *b, SautiUniDrcGain *out);
unsigned sauti_unidrc_delta_tmin(unsigned rate);

/* Gain curve of one gain sequence (stage U3).  The gain of a frame is final only when the NEXT frame's nodes are known (a frame whose last node is before its end continues with the first segment of the next one,
 * which starts at that node), so the output of a call is the gain of the PREVIOUS frame: the PCM that the access unit N produces is multiplied by the gains of access unit N - 1 completed by access unit N (measured against
 * libxaac: the gain buffer of its DRC processing, 87 dB).  The state holds the gains of the frame still open. */
typedef struct { int have; int time; float gain_db, slope; float open[4096]; } SautiUniDrcSeqState;
typedef struct {
    float boost, compress;                  /* user factors on the positive / negative gains (1.0 = off) */
    unsigned scaling_flag; float attn, ampl;/* gain modifiers of the instruction (band 0) */
    unsigned offset_flag; float offset_db;
    unsigned ducking_flag; float ducking_scaling;
    int limiter_present; float limiter_peak, norm_gain_db;   /* limiter peak target and the loudness normalisation gain (clipping prevention) */
} SautiUniDrcCurveParams;
/* lin[0 .. frame_size - 1] = the LINEAR gain of every sample of the previous frame (1.0 in the first call); the state is advanced.  1 = ok, 0 = malformed */
int sauti_unidrc_curve(const SautiUniDrcGainSet *gs, const SautiUniDrcNodes *n, unsigned frame_size, unsigned rate, const SautiUniDrcCurveParams *p, SautiUniDrcSeqState *st, float *lin);
#endif
