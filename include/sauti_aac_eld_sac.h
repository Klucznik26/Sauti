#ifndef SAUTI_AAC_ELD_SAC_H
#define SAUTI_AAC_ELD_SAC_H
/* AAC-ELD v2: the low delay MPEG Surround (LD-SAC, MPS 212 "2-1-2" tree) of ELDEXT_LDSAC: its SpatialSpecificConfig() and the SpatialFrame() of every access unit.  The parameter decoder is that of
 * the USAC MPS212 (sauti_usac_mps.c) in its LD form; this file only reads the configuration and finds the frame in the access unit. */
#include "sauti_usac_mps.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned sample_rate;          /* of the SAC (the output rate of the ELD) */
    unsigned time_slots;           /* QMF slots per frame (bsFrameLength + 1) */
    unsigned quant_mode;           /* bsQuantMode */
    unsigned fixed_gain_dmx;
    SautiUsacMpsConfig mps;        /* ld = 1, freq_res, temp_shape, env_quant, decorr, arbitrary_downmix */
} SautiAacEldSacConfig;

/* 1 = parsed, 0 = malformed, -1 = a valid configuration this decoder does not have (a tree other than 212, reserved values) */
int sauti_aac_eld_sac_config_parse(const uint8_t *bytes, size_t length, SautiAacEldSacConfig *out);

/* The extension payload that follows the SBR data of an access unit: extension_type (4 bits, EXT_LDSAC_DATA = 9), ancType (2), ancStartStop (2) and SpatialFrame().  `bit_pos` is the end of the SBR data,
 * `bit_end` the end of the access unit; `*end` receives the position after the frame.  1 = parsed, 0 = malformed. */
int sauti_aac_eld_sac_frame_parse(const SautiAacEldSacConfig *cfg, const uint8_t *au, size_t bit_pos, size_t bit_end, SautiUsacMpsState *st, SautiUsacMpsFrame *frame, size_t *end);
#endif
