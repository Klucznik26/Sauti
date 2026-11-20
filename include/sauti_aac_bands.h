#ifndef SAUTI_AAC_BANDS_H
#define SAUTI_AAC_BANDS_H
#include <stdint.h>
/* MPEG-4 AAC sampling_frequency_index 0..12 (optionally with a low-delay layout in the upper bits, see sauti_aac_layout.h); 1024 long / 128 short (512 / 480 long for LD/ELD).
 * Provides pointer to immutable offsets (count+1 entries).
 * Caller must validate max_sfb <= count. */
int sauti_aac_band_offsets(unsigned sample_rate_index,unsigned short_window,
                           const uint16_t **offsets,unsigned *count);
/* tns_max_bands for the layout in `sample_rate_index` (see sauti_aac_layout.h); 0 = not defined for that layout / rate. */
unsigned sauti_aac_tns_max_bands(unsigned sample_rate_index,unsigned short_window);
#endif
