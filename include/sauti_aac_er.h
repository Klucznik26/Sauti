#ifndef SAUTI_AAC_ER_H
#define SAUTI_AAC_ER_H
#include "sauti_aac_cpe.h"
#include "sauti_aac_layout.h"
#include <stddef.h>
#include <stdint.h>

/* Error-resilient AAC family without the resilience tools: AAC-LD (audio object type 23) and AAC-ELD (39) with epConfig 0 and no section/scalefactor/spectral resilience
 * (what fdkaac and FFmpeg produce and decode).  An access unit is one raw frame with NO ADTS header and NO element ids: the elements come in the order of the
 * channel_configuration (1 = one SCE, 2 = one CPE), each starting with a 4-bit element_instance_tag (AAC-LD only).  The frame holds 512 or 480 samples per channel.
 *   AAC-LD : ics_info as AAC-LC but only ONLY_LONG windows; window_shape 0 = sine window with full overlap, 1 = low-overlap sine window; pulse and gain-control bits present,
 *            TNS data after the gain-control bit.
 *   AAC-ELD: ics_info is only max_sfb; a CPE is always common_window; no pulse, no gain control; the filterbank is the low-delay MDCT with the ELD window (N = 512 or 480). */
#define SAUTI_AAC_AOT_ER_LD 23u
#define SAUTI_AAC_AOT_ER_ELD 39u

typedef struct {
    unsigned aot;              /* 23 or 39 */
    unsigned rate_index;       /* MPEG-4 sampling_frequency_index of the core: 3..7 (48 .. 22.05 kHz) */
    unsigned frame480;         /* 0 = 512 lines per frame, 1 = 480 */
    unsigned channel_config;   /* 1 or 2 */
} SautiAacErConfig;

typedef struct {
    float overlap[1536];       /* AAC-LD: n floats; AAC-ELD: 3n floats (the window spans four frames) */
    unsigned initialized;
} SautiAacErChannel;

typedef struct {
    SautiAacErChannel channel[2];
    uint32_t pns_seed;
} SautiAacErState;

/* AudioSpecificConfig of AAC-LD (23) and AAC-ELD (39): the part of ISO/IEC 14496-3 1.6 that matters here.  1 parsed, 0 malformed, -1 a feature this decoder does not have:
 * a program_config_element instead of a channel_configuration, error protection (epConfig != 0), the error-resilience flags (section / scalefactor / spectral data resilience),
 * an explicit sampling frequency, dependsOnCoreCoder.  `ld_sbr` is set for AAC-ELD with the low-delay SBR tool (not decoded yet: the caller must refuse such streams). */
typedef struct {
    unsigned aot;              /* 23 or 39 */
    unsigned rate_index;       /* 0..12 */
    unsigned channel_config;   /* 0..7 as transmitted */
    unsigned frame480;
    unsigned ld_sbr;           /* ELD only: ldSbrPresentFlag */
    unsigned ld_sbr_downsampled; /* ELD only: ldSbrSamplingRate (1 = the SBR output has twice the core rate) */
    unsigned ld_sbr_crc;
} SautiAacErAsc;
int sauti_aac_er_asc_parse(const uint8_t *asc,size_t length,SautiAacErAsc *out);

/* Number of samples per channel in one frame of this configuration (512 or 480); 0 for an unsupported configuration. */
unsigned sauti_aac_er_frame_samples(const SautiAacErConfig *config);
void sauti_aac_er_state_init(SautiAacErState *state);

/* Decode one access unit to interleaved float PCM (channels x frame_samples).  Atomic: on failure the state and `pcm` are unchanged.
 * 1 decoded, 0 malformed / truncated, -1 valid but unsupported tool (gain control, LTP, ...). */
int sauti_aac_er_decode(const SautiAacErConfig *config,SautiAacErState *state,
                        const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason);
#endif
