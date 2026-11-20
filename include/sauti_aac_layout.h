#ifndef SAUTI_AAC_LAYOUT_H
#define SAUTI_AAC_LAYOUT_H
/* Frame layout carried next to the sampling_frequency_index.  The scalefactor-band, spectral, dequantisation, TNS and PNS routines take one `unsigned` "rate index"; the low
 * 8 bits are the MPEG-4 sampling_frequency_index (0..12) and the next bits select the transform length, so the same routines serve AAC-LC (1024 lines, 128 short) and the low-delay
 * profiles (AAC-LD / AAC-ELD: 512 or 480 lines, long window only).  A plain 0..12 value means the classic 1024/128 layout, so every existing caller is unchanged. */
#define SAUTI_AAC_LAYOUT_1024 0u
#define SAUTI_AAC_LAYOUT_512 1u
#define SAUTI_AAC_LAYOUT_480 2u
#define SAUTI_AAC_IDX(rate, layout) ((unsigned)(rate) | ((unsigned)(layout) << 8))
#define SAUTI_AAC_IDX_RATE(idx) ((unsigned)(idx) & 0xffu)
#define SAUTI_AAC_IDX_LAYOUT(idx) ((unsigned)(idx) >> 8)
#endif
