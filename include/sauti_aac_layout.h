#ifndef SAUTI_AAC_LAYOUT_H
#define SAUTI_AAC_LAYOUT_H
/* Frame layout carried next to the sampling_frequency_index.  The scalefactor-band, spectral, dequantisation, TNS and PNS routines take one `unsigned` "rate index"; the low
 * 8 bits are the MPEG-4 sampling_frequency_index (0..12) and the next bits select the transform length, so the same routines serve AAC-LC (1024 lines, 128 short) and the low-delay
 * profiles (AAC-LD / AAC-ELD: 512 or 480 lines, long window only).  A plain 0..12 value means the classic 1024/128 layout, so every existing caller is unchanged. */
#define SAUTI_AAC_LAYOUT_1024 0u
#define SAUTI_AAC_LAYOUT_512 1u
#define SAUTI_AAC_LAYOUT_480 2u
#define SAUTI_AAC_LAYOUT_960 3u        /* AAC-LC family with frameLengthFlag = 1: 960 lines, eight short windows of 120 */
#define SAUTI_AAC_IDX(rate, layout) ((unsigned)(rate) | ((unsigned)(layout) << 8))
#define SAUTI_AAC_IDX_RATE(idx) ((unsigned)(idx) & 0xffu)
#define SAUTI_AAC_IDX_LAYOUT(idx) (((unsigned)(idx) >> 8) & 0xffu)
/* bit 16: the stream is AAC Main (ISO/IEC 14496-3 object type 1): ics_info carries predictor_data and the decoder runs the backward-adaptive predictors; a plain index is AAC-LC */
#define SAUTI_AAC_IDX_MAIN_FLAG 0x10000u
#define SAUTI_AAC_IDX_IS_MAIN(idx) (((unsigned)(idx) & SAUTI_AAC_IDX_MAIN_FLAG) != 0u)
/* bit 17: the stream is AAC LTP (object type 4): ics_info carries ltp_data (long-term prediction from the decoder's own output) instead of the Main predictor */
#define SAUTI_AAC_IDX_LTP_FLAG 0x20000u
#define SAUTI_AAC_IDX_IS_LTP(idx) (((unsigned)(idx) & SAUTI_AAC_IDX_LTP_FLAG) != 0u)
/* bit 18: error-resilient syntax without resilience tools (object types 17 ER AAC LC and 19 ER AAC LTP, epConfig 0): the raw_data_block has no element ids, no ID_END, no DSE / FIL, each element
 * starts with its 4-bit instance tag, and tns_data follows the gain_control_data_present flag */
#define SAUTI_AAC_IDX_ER_FLAG 0x40000u
#define SAUTI_AAC_IDX_IS_ER(idx) (((unsigned)(idx) & SAUTI_AAC_IDX_ER_FLAG) != 0u)
/* Length of one transform window of the layout in `idx`: the stride of the natural [window][line] spectrum layout of short windows and the number of lines of a long window. */
static inline unsigned sauti_aac_layout_window_len(unsigned idx, unsigned short_window) {
    switch (SAUTI_AAC_IDX_LAYOUT(idx)) {
    case SAUTI_AAC_LAYOUT_960: return short_window ? 120u : 960u;
    case SAUTI_AAC_LAYOUT_512: return 512u;
    case SAUTI_AAC_LAYOUT_480: return 480u;
    default: return short_window ? 128u : 1024u;
    }
}
#endif
