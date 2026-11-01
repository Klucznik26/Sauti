#ifndef SAUTI_AAC_HEADERS_H
#define SAUTI_AAC_HEADERS_H
#include <stddef.h>
#include <stdint.h>
/* ADTS and MPEG-4 AudioSpecificConfig metadata, not audio decoding. */
typedef struct {
    uint32_t core_sample_rate;
    uint32_t core_channels;
    uint32_t core_aot;        /* 2 = AAC-LC. */
    uint32_t frame_bytes;     /* Entire ADTS frame including header/CRC. */
    uint32_t header_bytes;    /* 7 or 9. */
    uint32_t raw_blocks;      /* 1..4 per ADTS frame. */
    uint32_t mpeg2;           /* ADTS ID bit. */
    uint32_t crc_present;
} SautiAacAdts;

typedef struct {
    uint32_t core_sample_rate;
    uint32_t output_sample_rate;
    uint32_t core_channels;
    uint32_t output_channels;
    uint32_t core_aot;        /* Currently AAC-LC only (2). */
    uint32_t sbr_present;     /* Explicit AOT=5 or AOT=29. */
    uint32_t ps_present;      /* Explicit AOT=29. Requires SBR. */
    uint32_t explicit_extension; /* 0 means extensions MAY be implicit. */
} SautiAacConfig;

/* Linux x86-64 SysV ABI, hand-written NASM. Returns 1 on success, 0 on
 * truncation/unsupported data. Failure never modifies *out or *bit_offset. */
int sauti_aac_adts_parse_asm(const uint8_t *data, size_t size,
                             SautiAacAdts *out);
int sauti_aac_bits_read_asm(const uint8_t *data, size_t size,
                            size_t *bit_offset, unsigned count,
                            uint32_t *out);
/* Parses explicit LC, HE-AAC (SBR), HE-AAC v2 (SBR+PS) ASC headers.
 * Does not claim to detect implicit SBR/PS in the AAC raw-data payload.
 * Does not decode compressed audio. */
int sauti_aac_asc_parse(const uint8_t *data, size_t bytes,
                         SautiAacConfig *out);
#endif
