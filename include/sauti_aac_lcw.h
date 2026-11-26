#ifndef SAUTI_AAC_LCW_H
#define SAUTI_AAC_LCW_H
/* AAC-LC / AAC Main with any program_config_element layout: up to 64 channels, elements matched to the PCE by (type, element_instance_tag), and coupling channel elements (CCE, ISO/IEC 14496-3
 * 4.6.8) - independently switched (time domain) and dependently switched (spectral domain, before or after TNS).  The standard layouts and the small PCE layouts of sauti_aac_lc_multi.h stay
 * with that decoder; this one is the general one (more than 8 channels, layouts without a standard position, coupling).  All state lives on the heap of the caller (malloc): the state of 48
 * channels is large and only streams that need it pay for it.
 *
 * Output order: the positions of the PCE as in FFmpeg (FL FR FC LFE BL BR FLC FRC BC SL SR) when the layout is made of those positions only; any other layout keeps the order of the PCE list
 * (front elements, side elements, back elements, LFE elements).  Plain LC with 1024 or 960 lines, and Main.  Fail-closed (-1): LTP / SSR / ER, SBR in a fill element, ADTS CRC. */
#include "sauti_aac_lcn.h"
#include <stddef.h>
#include <stdint.h>

#define SAUTI_AAC_LCW_MAX_CHANNELS 64u
#define SAUTI_AAC_LCW_MAX_CCE 16u
typedef struct {
    unsigned channels;                                  /* 1 .. SAUTI_AAC_LCW_MAX_CHANNELS */
    unsigned n_elements;                                /* channel elements of the PCE: type (0 SCE, 1 CPE, 3 LFE), tag, first channel slot; 0 = no usable tags: elements take the slots in stream order */
    unsigned n_cce;                                     /* num_valid_cc_elements of the PCE */
    uint8_t elem_type[SAUTI_AAC_LCW_MAX_CHANNELS],elem_tag[SAUTI_AAC_LCW_MAX_CHANNELS],elem_slot[SAUTI_AAC_LCW_MAX_CHANNELS];
    uint8_t map[SAUTI_AAC_LCW_MAX_CHANNELS];            /* map[output channel] = slot */
} SautiAacLcwLayout;
/* program_config_element at bit *pos (after the 3-bit element id in a raw_data_block, at the start of the PCE position in an AudioSpecificConfig); *pos moves past the comment field.
 * 1 ok, 0 malformed / truncated, -1 not supported (more than SAUTI_AAC_LCW_MAX_CHANNELS channels or elements). */
int sauti_aac_lcw_pce_parse(const uint8_t *data,size_t length,size_t *pos,SautiAacLcwLayout *layout);

int sauti_aac_lcw_scan_pce(const uint8_t *raw,size_t length,SautiAacLcwLayout *layout);   /* the PCE at the start of a raw_data_block (after DSE / FIL elements): 1 / 0 / -1 as above */

typedef struct {
    unsigned rate_index;                                /* 0..12 (0..11 for Main) */
    unsigned frame960;                                  /* plain LC with frameLengthFlag = 1 (Main: see sauti_aac_lcn.h) */
    unsigned main;                                      /* AAC Main (backward-adaptive prediction) */
    SautiAacLcwLayout layout;
} SautiAacLcwConfig;
typedef struct SautiAacLcw SautiAacLcw;
SautiAacLcw *sauti_aac_lcw_new(const SautiAacLcwConfig *config);        /* NULL: unsupported configuration or no memory */
SautiAacLcw *sauti_aac_lcw_dup(const SautiAacLcw *state);               /* deep copy (checkpoints) */
void sauti_aac_lcw_free(SautiAacLcw *state);
unsigned sauti_aac_lcw_frame_samples(const SautiAacLcw *state);         /* 1024 or 960 */
unsigned sauti_aac_lcw_channels(const SautiAacLcw *state);
/* One access unit (raw_data_block) -> interleaved float PCM, frame_samples x channels, output order.  Atomic: on failure the state and `pcm` are unchanged.  1 decoded, 0 malformed, -1 valid
 * but not supported (reason). */
int sauti_aac_lcw_decode(SautiAacLcw *state,const uint8_t *au,size_t length,float *pcm,SautiAacReason *reason);
#endif
