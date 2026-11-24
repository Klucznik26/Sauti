/* AAC-ELD v2: configuration and frame location of the low delay MPEG Surround 212 (ELDEXT_LDSAC).  Written after studying SpatialDecParseSpecificConfig(), SpatialDecParseFrameData() and
 * mpegSurroundDecoder_Parse() of the FDK AAC decoder (read, not copied; the syntax is ISO/IEC 14496-3 4.5.2.1 / ISO/IEC 23003-1 5.2). */
#include "sauti_aac_eld_sac.h"
#include <string.h>

static const unsigned sac_rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };

int sauti_aac_eld_sac_config_parse(const uint8_t *bytes, size_t length, SautiAacEldSacConfig *out) {
    SautiUsacBits b;
    SautiAacEldSacConfig c;
    unsigned sfi, tree, ext_bits;
    if (!bytes || !out || length == 0u || length > 64u) return 0;
    memset(&c, 0, sizeof c);
    b.data = bytes; b.nbits = length * 8u; b.pos = 0;
    sfi = sauti_usac_get(&b, 4);
    if (sfi == 15u) c.sample_rate = sauti_usac_get(&b, 24);
    else if (sfi < 13u) c.sample_rate = sac_rates[sfi];
    else return 0;
    c.time_slots = sauti_usac_get(&b, 5) + 1u;
    c.mps.ld = 1u;
    c.mps.freq_res = sauti_usac_get(&b, 3);
    tree = sauti_usac_get(&b, 4);
    if (tree != 7u) return -1;                                                  /* only the 2-1-2 tree (TREE_212) */
    c.quant_mode = sauti_usac_get(&b, 2);
    c.mps.arbitrary_downmix = sauti_usac_get(&b, 1);
    c.fixed_gain_dmx = sauti_usac_get(&b, 3);
    c.mps.temp_shape = sauti_usac_get(&b, 2);
    if (c.mps.temp_shape > 2u) return -1;
    c.mps.decorr = sauti_usac_get(&b, 2);
    if (c.mps.decorr > 2u) return -1;
    if (c.mps.temp_shape == 2u) c.mps.env_quant = sauti_usac_get(&b, 1);
    b.pos = (b.pos + 7u) & ~(size_t)7u;                                          /* ByteAlign() */
    if (c.mps.freq_res == 0u) return -1;
    ext_bits = (unsigned)(b.nbits - b.pos);
    while (ext_bits >= 8u) {                                                    /* SpatialExtensionConfig(): type (4), length (4 / 12 / 28), skipped */
        unsigned len;
        (void)sauti_usac_get(&b, 4);
        len = sauti_usac_get(&b, 4);
        if (len == 15u) { len += sauti_usac_get(&b, 8); if (len == 15u + 255u) len += sauti_usac_get(&b, 16); }
        ext_bits = (unsigned)(b.nbits - b.pos);
        if (len * 8u > ext_bits) return 0;
        b.pos += (size_t)len * 8u;
        ext_bits = (unsigned)(b.nbits - b.pos);
    }
    if (b.pos > b.nbits) return 0;
    *out = c;
    return 1;
}

int sauti_aac_eld_sac_frame_parse(const SautiAacEldSacConfig *cfg, const uint8_t *au, size_t bit_pos, size_t bit_end, SautiUsacMpsState *st, SautiUsacMpsFrame *frame, size_t *end) {
    SautiUsacBits b;
    unsigned type, anc_type, anc_start_stop;
    int r;
    if (!cfg || !au || !st || !frame || bit_pos + 8u > bit_end) return 0;
    b.data = au; b.nbits = bit_end; b.pos = bit_pos;
    type = sauti_usac_get(&b, 4);
    anc_type = sauti_usac_get(&b, 2);
    anc_start_stop = sauti_usac_get(&b, 2);
    if (type != 9u || anc_type != 0u || anc_start_stop != 3u) return 0;         /* EXT_LDSAC_DATA, MPEGS_ANCTYPE_FRAME (the header is in the ASC), MPEGS_START_STOP: the whole frame in this access unit */
    r = sauti_usac_mps212_parse(&cfg->mps, cfg->time_slots, &b, 0, st, frame);
    if (r != 1) return 0;
    if (end) *end = b.pos;
    return 1;
}
