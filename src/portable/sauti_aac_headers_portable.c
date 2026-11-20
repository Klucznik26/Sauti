/* C odpowiednik sauti_aac_headers.asm (patrz sauti_portable.h). */
#include "sauti_portable.h"
#include "sauti_aac_headers.h"
static const uint32_t rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
int sauti_aac_adts_parse_asm(const uint8_t *data, size_t size, SautiAacAdts *out) {
    unsigned b1, b2, sfi, ch, hdr, len;
    if (!data || !out || size < 7u || data[0] != 0xffu) return 0;
    b1 = data[1];
    if ((b1 & 0xf6u) != 0xf0u) return 0;                                                   /* synchronizacja, layer 00, bit ID ignorowany */
    hdr = (b1 & 1u) ? 7u : 9u;
    if (size < hdr) return 0;
    b2 = data[2];
    if ((b2 >> 6) != 1u) return 0;                                                         /* tylko AAC-LC */
    sfi = (b2 >> 2) & 15u;
    if (sfi > 12u) return 0;
    ch = ((b2 & 1u) << 2) | ((unsigned)data[3] >> 6);
    if (ch > 7u) return 0;
    len = (((unsigned)data[3] & 3u) << 11) | ((unsigned)data[4] << 3) | ((unsigned)data[5] >> 5);
    if (len < hdr || len > size) return 0;
    out->core_sample_rate = rates[sfi]; out->core_channels = ch; out->core_aot = 2u; out->frame_bytes = len; out->header_bytes = hdr;
    out->raw_blocks = (data[6] & 3u) + 1u; out->mpeg2 = (b1 >> 3) & 1u; out->crc_present = (~b1) & 1u;
    return 1;
}
int sauti_aac_bits_read_asm(const uint8_t *data, size_t bytes, size_t *bit_offset, unsigned count, uint32_t *out) {
    size_t total, pos; uint32_t v = 0; unsigned i;
    if (!data || !bit_offset || !out || count == 0u || count > 32u || bytes > 0x1fffffffffffffffull) return 0;
    total = bytes * 8u; pos = *bit_offset;
    if (pos > total || total - pos < count) return 0;
    for (i = 0; i < count; i++, pos++) v = (v << 1) | (uint32_t)((data[pos >> 3] >> (7u - (unsigned)(pos & 7u))) & 1u);
    *out = v; *bit_offset = pos;
    return 1;
}
