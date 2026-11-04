#ifndef SAUTI_AAC_SBR_PAYLOAD_H
#define SAUTI_AAC_SBR_PAYLOAD_H
#include "sauti_aac_sbr_grid.h"
#include "sauti_aac_sbr_control.h"
#include "sauti_aac_sbr_quant.h"
#include "sauti_aac_sbr_energy.h"
#include "sauti_aac_sbr_limits.h"
#include "sauti_aac_sbr_master.h"
#include "sauti_aac_sbr_tables.h"
#include "sauti_aac_sbr_patch.h"
#include "sauti_aac_sbr_qmf_frame.h"
#include "sauti_aac_syntax.h"
#include <stddef.h>
#include <stdint.h>

/* SBR-29: decode a REAL, entropy-coded EXT_SBR_DATA FIL payload (SCE).
 * Input starts at the 4-bit extension_type=13 nibble at the bit offset
 * reported by sauti_aac_fill_parse(). The complete FIL extension length
 * in bits is count*8. The input can start at an UNALIGNED bit offset.
 *
 * First supported subset: AAC-LC 1024 PCM / 32 QMF core slots,
 * mono SCE, no CRC, no PS/extended_data, time borders within [0,16].
 * Standalone demux of the SCE/CPE followed by FIL inside ADTS and
 * HE-AAC PCM conformance validation are STILL outstanding.
 *
 * Each parsed frame provides real bs_header, grid, dtdf/invf, Huffman
 * envelope/noise, dequant energy, harmonic flags, freq/patch tables
 * and 32-slot synthesis control to the SBR-28 QMF frame driver.
 * Nothing here estimates QMF LPC, synthesizes PCM, or guesses SBR data.
 * All outputs and persistent entropy/header state commit atomically.
 */
#define SAUTI_AAC_SBR_PAYLOAD_MAX_BYTES 269u

typedef struct {
    uint32_t initialized;
    uint64_t frames;
    SautiAacSbrHeader header;
    SautiAacSbrQuantState quant_state;
} SautiAacSbrPayloadState;

typedef struct {
    SautiAacSbrHeader header;
    SautiAacSbrGrid grid;
    SautiAacSbrControl control;
    SautiAacSbrQuantFrame quant;
    SautiAacSbrEnergyFrame energy;
    SautiAacSbrMaster master;
    SautiAacSbrTables tables;
    SautiAacSbrPatches patches;
    uint8_t harmonic[SAUTI_AAC_SBR_MAX_MASTER_BANDS];
    uint8_t env_edges[SAUTI_AAC_SBR_MAX_ENV+1u];
    uint8_t noise_edges[3];
    uint8_t attack[SAUTI_AAC_SBR_MAX_ENV];
    uint32_t transient_env;
    uint32_t bs_smoothing_mode;
    uint32_t limiter_mode;
    uint32_t gain_mode;
    uint32_t payload_data_bits; /* bits consumed, excluding pad to byte */
} SautiAacSbrPayloadFrame;

/* output_rate: SBR output rate (twice the AAC-LC core sample rate).
 * bit_start: bit offset of 4-bit extension_type in caller buffer.
 * payload_bits: 8*FIL payload_bytes including the type nibble.
 * Reject CRC type 14, PS and other extensions explicitly.
 * In case of failure *out and *state remain byte-for-byte unchanged.
 * Returns 1 success, 0 reject/truncated/unsupported.
 */
/* Diagnostic stage identifies which strict SBR subset check rejected
 * a real payload. Only the stage output changes on failure. */
typedef enum {
    SAUTI_SBR_PAYLOAD_INPUT=0,
    SAUTI_SBR_PAYLOAD_TYPE,
    SAUTI_SBR_PAYLOAD_HEADER,
    SAUTI_SBR_PAYLOAD_TABLES,
    SAUTI_SBR_PAYLOAD_GRID,
    SAUTI_SBR_PAYLOAD_BORDERS,
    SAUTI_SBR_PAYLOAD_CONTROL,
    SAUTI_SBR_PAYLOAD_QUANT,
    SAUTI_SBR_PAYLOAD_ENERGY,
    SAUTI_SBR_PAYLOAD_HARMONICS,
    SAUTI_SBR_PAYLOAD_EXTENDED,
    SAUTI_SBR_PAYLOAD_PADDING,
    SAUTI_SBR_PAYLOAD_OK
} SautiAacSbrPayloadStage;

/* Parse complete SBR syntax with ISO-valid temporal borders extending
 * beyond the current AAC frame (e.g. FIXVAR 0..17).
 * This is a SIDE-DATA interface ONLY; it does not authorize QMF/PCM.
 * The strict original parse_ex still rejects out-of-frame time grids.
 * Caller must not advance audio playback without temporal buffering.
 * State and output are committed only when the entire FIL is valid. */
int sauti_aac_sbr_payload_temporal_parse_ex(
    const uint8_t *bytes,size_t bytes_count,
    size_t bit_start,size_t payload_bits,unsigned output_rate,
    SautiAacSbrPayloadState *state,SautiAacSbrPayloadFrame *out,
    SautiAacSbrPayloadStage *stage);

int sauti_aac_sbr_payload_sce_parse_ex(
    const uint8_t *bytes,size_t bytes_count,
    size_t bit_start,size_t payload_bits,unsigned output_rate,
    SautiAacSbrPayloadState *state,SautiAacSbrPayloadFrame *out,
    SautiAacSbrPayloadStage *stage);

int sauti_aac_sbr_payload_sce_parse(
    const uint8_t *bytes,size_t bytes_count,
    size_t bit_start,size_t payload_bits,unsigned output_rate,
    SautiAacSbrPayloadState *state,SautiAacSbrPayloadFrame *out);

/* Populate SBR-28 HF configuration with pointers into a valid frame.
 * Caller must keep the frame alive (no copying after this call).
 * Zero-copy view: no heap allocation. */
int sauti_aac_sbr_payload_make_hf(
    const SautiAacSbrPayloadFrame *frame,
    SautiAacSbrHfJointConfig *out);
#endif
