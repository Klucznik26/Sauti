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
    /* AAC Main predictor_data (only with sauti_aac_ics_info_parse_idx and the Main flag of the index; otherwise all zero) */
    uint8_t predictor_present;   /* predictor_data_present */
    uint8_t predictor_reset;     /* predictor_reset */
    uint8_t predictor_reset_group; /* predictor_reset_group_number, 1..30 (0: none) */
    uint8_t prediction_used[41]; /* per scalefactor band below min(max_sfb, PRED_SFB_MAX) */
    /* AAC LTP ltp_data (only with the LTP flag of the index, long windows) */
    uint8_t ltp_present;         /* ltp_data_present */
    uint16_t ltp_lag;            /* 0..2047 samples */
    uint8_t ltp_coef;            /* index 0..7 into the LTP gain table */
    uint8_t ltp_used[40];        /* ltp_long_used per scalefactor band below min(max_sfb, 40) */
} SautiAacIcsInfo;

int sauti_aac_ics_info_parse(const uint8_t *bytes,size_t size,
                             size_t *bit_offset,SautiAacIcsInfo *info);
/* The same for a packed index (sauti_aac_layout.h): with SAUTI_AAC_IDX_MAIN_FLAG the predictor_data of AAC Main (predictor_reset, reset group, prediction_used per band) is parsed;
 * without it predictor_data_present = 1 fails as before (AAC-LC).  The band limit PRED_SFB_MAX comes from the sampling_frequency_index; 7350 Hz has none (fails). */
int sauti_aac_ics_info_parse_idx(const uint8_t *bytes,size_t size,size_t *bit_offset,unsigned idx,SautiAacIcsInfo *info);
/* ltp_data() of AAC LTP: lag (11), coefficient index (3) and one used flag per band below min(max_sfb, 40); fills the ltp_* fields of `ics` (max_sfb must be set).  Atomic. */
int sauti_aac_ltp_data_parse(const uint8_t *bytes,size_t size,size_t *bit_offset,SautiAacIcsInfo *ics);
/* Number of scalefactor bands the Main predictor covers (ISO/IEC 14496-3 4.6.7, Table 4.? PRED_SFB_MAX), 0 for an unsupported rate. */
unsigned sauti_aac_pred_sfb_max(unsigned sampling_frequency_index);

/* pulse_data() (ISO/IEC 14496-3 4.4.2.7, tylko okna dlugie): count = number_pulse + 1 (1..4), pierwsza pozycja = swb_offset[start_sfb] + offset[0], kolejne dodaja offset[i].
 * Parser sprawdza tylko skladnie i to, ze okno nie jest EIGHT_SHORT; zakres start_sfb/pozycji sprawdza sauti_aac_lc_pulse_apply (zna tabele pasm dla czestotliwosci). */
typedef struct {
    uint32_t count;
    uint32_t start_sfb;
    uint8_t offset[4];
    uint8_t amp[4];
} SautiAacPulseData;
int sauti_aac_pulse_parse(const uint8_t *bytes,size_t size,size_t *bit_offset,
                          const SautiAacIcsInfo *ics,SautiAacPulseData *out);

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

/* FIL dla rdzenia AAC-LC (bez SBR): konsumuje jeden element FIL (identyfikator ID_FIL juz odczytany) i POMIJA jego ladunek dla kazdego typu rozszerzenia poza SBR (13/14) --
 * EXT_FILL, EXT_FILL_DATA, EXT_DATA_ELEMENT, DYNAMIC_RANGE, SAC i typy zarezerwowane (ISO/IEC 14496-3 4.4.2.7: dekoder pomija nieznane).  SBR zmienia wyjscie (HE-AAC), wiec rdzen LC go
 * nie pomija: zwraca -1 i NIE przesuwa *bit_offset.  1 = pominieto (info, gdy niepuste, opisuje element), 0 = niepoprawny/ucieta ramka. */
int sauti_aac_fill_skip_non_sbr(const uint8_t *bytes,size_t size,
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
    uint32_t pvc_mode;                   /* eSBR: bs_pvc_mode of the header (0 none, 1, 2), read only when the configuration enables PVC */
    uint32_t slot_scale;                 /* QMF slots per SBR time slot: 0 / 2 = 2:1 (and 8:3), 4 = eSBR 4:1; set by the USAC decoder, not part of any bitstream */
} SautiAacSbrHeader;
int sauti_aac_sbr_header_parse(const uint8_t *bytes,size_t size,
                               size_t *bit_offset,SautiAacSbrHeader *header);

/* ps_data header. has_header=0 means retain the previously decoded
 * configuration. Stereo synthesis remains a separate PS/QMF stage. */
typedef struct {
    uint32_t has_header,enable_iid,iid_mode;
    uint32_t enable_icc,icc_mode,enable_ext;
} SautiAacPsHeader;
int sauti_aac_ps_header_parse(const uint8_t *bytes,size_t size,
                              size_t *bit_offset,SautiAacPsHeader *header);

/* Streaming PS state. A ps_header_flag=0 frame inherits the previous header;
 * a first such frame is invalid. The final IID/ICC envelope is retained for
 * time-delta coding in the next frame. This is syntax state only, not PS/QMF
 * stereo synthesis. */
#define SAUTI_AAC_PS_MAX_PARAMETERS 34u
#define SAUTI_AAC_PS_MAX_PHASE_PARAMETERS 17u
typedef struct {
    uint32_t initialized;
    uint64_t frames;
    SautiAacPsHeader header;
    int8_t iid_last[SAUTI_AAC_PS_MAX_PARAMETERS];
    int8_t icc_last[SAUTI_AAC_PS_MAX_PARAMETERS];
    int8_t ipd_last[SAUTI_AAC_PS_MAX_PHASE_PARAMETERS];
    int8_t opd_last[SAUTI_AAC_PS_MAX_PHASE_PARAMETERS];
} SautiAacPsHeaderState;
int sauti_aac_ps_header_state_init(SautiAacPsHeaderState *state);
int sauti_aac_ps_header_state_parse(const uint8_t *bytes,size_t size,
                                    size_t *bit_offset,
                                    SautiAacPsHeaderState *state,
                                    SautiAacPsHeader *out);

/* Structural ps_data() frame parser. It retains the active header and final
 * IID/ICC envelope across frames, reads frame_class/num_env_idx, variable
 * envelope borders, all baseline IID/ICC Huffman parameters, and the
 * standardized IPD/OPD extension (when present). PS/QMF stereo synthesis is
 * a separate future stage. num_env==0 is legal and retains previous
 * parameters. State, output and cursor commit together. */
#define SAUTI_AAC_PS_MAX_ENVELOPES 5u
typedef struct {
    SautiAacPsHeader header;    /* active (possibly inherited) configuration */
    uint32_t header_present;    /* ps_header_flag in this frame */
    uint32_t frame_class;
    uint32_t num_env_idx;
    uint32_t num_env;           /* signaled envelopes: 0, 1, 2, 3 or 4 */
    uint32_t resolved_num_env;  /* includes a synthetic tail, at most 5 */
    uint8_t border_position[SAUTI_AAC_PS_MAX_ENVELOPES];
    uint8_t iid_count;
    uint8_t icc_count;
    uint8_t ipdopd_count;
    uint8_t enable_ipdopd;
    int8_t iid[SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PARAMETERS];
    int8_t icc[SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PARAMETERS];
    int8_t ipd[SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PHASE_PARAMETERS];
    int8_t opd[SAUTI_AAC_PS_MAX_ENVELOPES][SAUTI_AAC_PS_MAX_PHASE_PARAMETERS];
    uint32_t extension_bytes;   /* length of the bounded ps_extension payload */
} SautiAacPsFrame;
int sauti_aac_ps_frame_parse(const uint8_t *bytes,size_t size,
                             size_t *bit_offset,
                             SautiAacPsHeaderState *state,
                             SautiAacPsFrame *frame);

/* Stable identifiers for the baseline IID/ICC and IPD/OPD VLCs. These accessors
 * support deterministic conformance tests; production frame parsing uses the
 * same tables and bounded bit reader. */
typedef enum {
    SAUTI_AAC_PS_IID_DF0=0, SAUTI_AAC_PS_IID_DF1,
    SAUTI_AAC_PS_IID_DT0,   SAUTI_AAC_PS_IID_DT1,
    SAUTI_AAC_PS_ICC_DF,    SAUTI_AAC_PS_ICC_DT,
    SAUTI_AAC_PS_IPD_DF,    SAUTI_AAC_PS_IPD_DT,
    SAUTI_AAC_PS_OPD_DF,    SAUTI_AAC_PS_OPD_DT,
    SAUTI_AAC_PS_HUFFMAN_TABLES
} SautiAacPsHuffmanTable;
int sauti_aac_ps_huffman_codeword(SautiAacPsHuffmanTable table,
                                  unsigned symbol,uint32_t *code,
                                  uint8_t *bits);
int sauti_aac_ps_huffman_symbol_decode(
    SautiAacPsHuffmanTable table,const uint8_t *bytes,size_t size,
    size_t *bit_offset,int *delta);

/* NASM floating-point overlap-add primitive for one windowed 128 or 1024
 * sample block. Precomputed correct 2*N AAC window and IMDCT time samples
 * must be provided by caller. All arrays must be nonoverlapping.
 * out[i] = overlap[i] + imdct[i] * window[i];
 * overlap[i] = imdct[N+i] * window[N+i];
 * No memory allocation. Returns 1 on success, 0 for invalid arguments. */
int sauti_aac_window_overlap_asm(const float *imdct,const float *window,
                                float *overlap,float *out,size_t n);
#endif
