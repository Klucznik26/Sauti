#ifndef SAUTI_AAC_BANDS_H
#define SAUTI_AAC_BANDS_H
#include <stdint.h>
/* MPEG-4 AAC sampling_frequency_index 0..12; 1024 long / 128 short.
 * Provides pointer to immutable offsets (count+1 entries).
 * Caller must validate max_sfb <= count. */
int sauti_aac_band_offsets(unsigned sample_rate_index,unsigned short_window,
                           const uint16_t **offsets,unsigned *count);
#endif
