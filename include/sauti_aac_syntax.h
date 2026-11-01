#ifndef SAUTI_AAC_SYNTAX_H
#define SAUTI_AAC_SYNTAX_H
#include <stddef.h>
#include <stdint.h>

/* Parse complete AAC-LC ics_info() grammar up to predictor_data_present.
 * Unsupported predictor syntax fails closed. SWB limit is validated later,
 * when sample rate and the scale-factor band table are both available.
 * Input bit offset is arbitrary; advanced only on complete success. */
typedef struct {
    uint32_t window_sequence;    /* 0 only-long, 1 long-start, 2 eight-short, 3 long-stop */
    uint32_t window_shape;       /* 0 sine, 1 KBD */
    uint32_t max_sfb;
    uint32_t window_count;       /* 1 or 8 */
    uint32_t group_count;
    uint8_t group_length[8];     /* group sizes, indexed 0..group_count-1 */
} SautiAacIcsInfo;

int sauti_aac_ics_info_parse(const uint8_t *bytes,size_t size,
                             size_t *bit_offset,SautiAacIcsInfo *info);

/* Parses FIL byte-count and first 4-bit extension type; skips the remainder
 * without claiming the SBR envelope was decoded or PS was found.
 * extension_type=0xffffffff when FIL count is zero. */
typedef struct {
    uint32_t payload_bytes;
    uint32_t extension_type;
    uint32_t sbr_candidate;      /* 13 EXT_SBR_DATA, 14 EXT_SBR_DATA_CRC */
    uint32_t sbr_crc_flag;
    size_t extension_start_bit;
    size_t extension_bits;       /* includes 4-bit type nibble */
} SautiAacFillInfo;

int sauti_aac_fill_parse(const uint8_t *bytes,size_t size,
                         size_t *bit_offset,SautiAacFillInfo *info);

/* Limited raw_data_block prefix walker. Stops at an undecodable syntactic
 * element (SCE/CPE/LFE/etc.) rather than assuming an audio-element length.
 * Only FIL and END are currently consumable.
 * 0 invalid/truncated, 1 reached ID_END, 2 needs channel-element decoder. */
typedef struct {
    uint32_t fill_count;
    uint32_t has_sbr_candidate;
    uint32_t first_audio_id;
    size_t stop_bit;
} SautiAacRawPrefix;

int sauti_aac_raw_prefix_parse(const uint8_t *bytes,size_t size,
                               size_t *bit_offset,SautiAacRawPrefix *info);

/* Header grammar only, NOT the SBR envelope/QMF spectral reconstruction.
 * Starts just after SBR bs_header_flag (no CRC or type nibble). */
typedef struct {
    uint32_t amp_res,start_freq,stop_freq,xover_band;
    uint32_t freq_scale,alter_scale,noise_bands;
    uint32_t limiter_bands,limiter_gains,interpol_freq,smoothing_mode;
    uint32_t extra_1,extra_2;
} SautiAacSbrHeader;
int sauti_aac_sbr_header_parse(const uint8_t *bytes,size_t size,
                               size_t *bit_offset,SautiAacSbrHeader *header);

/* ps_data header only, NOT IID/ICC Huffman envelopes or stereo synthesis.
 * has_header=0 means retain previously decoded PS header; other fields
 * are not yet valid. Caller manages previous state. */
typedef struct {
    uint32_t has_header,enable_iid,iid_mode;
    uint32_t enable_icc,icc_mode,enable_ext;
} SautiAacPsHeader;
int sauti_aac_ps_header_parse(const uint8_t *bytes,size_t size,
                              size_t *bit_offset,SautiAacPsHeader *header);

/* NASM floating-point overlap-add primitive for one windowed 128 or 1024
 * sample block. Precomputed correct 2*N AAC window and IMDCT time samples
 * must be provided by caller. All arrays must be nonoverlapping.
 * out[i] = overlap[i] + imdct[i] * window[i];
 * overlap[i] = imdct[N+i] * window[N+i];
 * No memory allocation. Returns 1 on success, 0 for invalid arguments. */
int sauti_aac_window_overlap_asm(const float *imdct,const float *window,
                                float *overlap,float *out,size_t n);
#endif
