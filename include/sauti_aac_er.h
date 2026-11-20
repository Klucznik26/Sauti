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
    unsigned bits;             /* number of bits the configuration occupies (a LATM StreamMuxConfig continues right after it) */
} SautiAacErAsc;
int sauti_aac_er_asc_parse(const uint8_t *asc,size_t length,SautiAacErAsc *out);

/* AAC-LD / ELD access units cannot be written in ADTS (the profile field has 2 bits), and the engine passes frames in ADTS form, so containers wrap them in a 7-byte INTERNAL header that
 * is not valid ADTS: the `layer` field is 01 (always 00 in real ADTS), the profile field is 0 for AAC-LD (23), 1 for AAC-ELD (39) and 2 for AAC-LC with 960-line frames (aot 2, sauti_aac_lcn.h), the private bit is the 480-line (LD / ELD) frame flag; sampling
 * frequency index, channel configuration and frame length are as in ADTS.  The header never leaves the engine.  Returns the frame length (7 + au_length). */
static inline size_t sauti_aac_er_wrap_header(uint8_t out[7],unsigned aot,unsigned rate_index,unsigned channel_config,unsigned frame480,uint32_t au_length){
    const size_t l=7u+au_length;
    out[0]=0xff;out[1]=0xf3;
    out[2]=(uint8_t)(((aot==2u?2u:(aot==SAUTI_AAC_AOT_ER_ELD?1u:0u))<<6)|(rate_index<<2)|((frame480&1u)<<1)|((channel_config>>2)&1u));
    out[3]=(uint8_t)(((channel_config&3u)<<6)|((l>>11)&3u));
    out[4]=(uint8_t)(l>>3);
    out[5]=(uint8_t)(((l&7u)<<5)|0x1fu);
    out[6]=0xfc;
    return l;
}

/* Number of samples per channel in one frame of this configuration (512 or 480); 0 for an unsupported configuration. */
unsigned sauti_aac_er_frame_samples(const SautiAacErConfig *config);
void sauti_aac_er_state_init(SautiAacErState *state);

/* Decode one access unit to interleaved float PCM (channels x frame_samples).  Atomic: on failure the state and `pcm` are unchanged.
 * 1 decoded, 0 malformed / truncated, -1 valid but unsupported tool (gain control, LTP, ...). */
int sauti_aac_er_decode(const SautiAacErConfig *config,SautiAacErState *state,
                        const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason);
#endif
