/* AIFF / AIFF-C uncompressed PCM (NONE, sowt).
 * Memory-only, no OS dependency. The caller owns the input bytes.
 * Reject unknown compression, corrupt chunks and inconsistent frame sizes. */
#include "sauti_decoder_aiff.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *pcm;
    uint32_t rate, channels, bits;
    uint64_t frames, position;
    int little_endian;
} AiffState;
static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static int is_tag(const uint8_t *p, const char *tag) {
    return memcmp(p, tag, 4) == 0;
}
static int aiff_probe(const uint8_t *p, size_t n) {
    return p && n >= 12 && is_tag(p, "FORM") &&
           (is_tag(p + 8, "AIFF") || is_tag(p + 8, "AIFC"));
}
static uint32_t parse_rate(const uint8_t *p) {
    const unsigned exp = be16(p);
    if (exp == 0 || exp >= 0x7fffu || (exp & 0x8000u)) return 0;
    const uint64_t mantissa = ((uint64_t)be32(p+2) << 32) | be32(p+6);
    if (!(mantissa & UINT64_C(0x8000000000000000))) return 0;
    const double rate = ldexp((double)mantissa, (int)exp - 16383 - 63);
    if (!isfinite(rate) || rate < 1000.0 || rate > 384000.0) return 0;
    uint32_t rounded = (uint32_t)(rate + 0.5);
    return fabs(rate - (double)rounded) < 0.01 ? rounded : 0;
}
static void *aiff_open(const uint8_t *p, size_t n, SautiPcmInfo *info) {
    if (!info || !aiff_probe(p,n)) return NULL;
    size_t form_size = (size_t)be32(p+4);
    if (form_size < 4u || form_size > n - 8u) return NULL;
    size_t end = 8u + form_size, off = 12u;
    const int aifc = is_tag(p+8,"AIFC");
    uint32_t frames = 0, rate = 0, channels = 0, bits = 0;
    const uint8_t *pcm = NULL;
    size_t pcm_bytes = 0;
    int saw_comm = 0, saw_ssnd = 0, little_endian = 0;
    while (off + 8u <= end) {
        const uint8_t *chunk = p + off;
        size_t size = (size_t)be32(chunk+4);
        off += 8u;
        if (size > end - off) return NULL;
        const uint8_t *body = p+off;
        if (is_tag(chunk,"COMM")) {
            if (saw_comm++ || size < (aifc ? 22u : 18u)) return NULL;
            channels = be16(body);
            frames = be32(body+2);
            bits = be16(body+6);
            rate = parse_rate(body+8);
            if (aifc) {
                if (is_tag(body+18,"sowt")) little_endian=1;
                else if (!is_tag(body+18,"NONE")) return NULL;
            }
        } else if (is_tag(chunk,"SSND")) {
            if (saw_ssnd++ || size < 8u) return NULL;
            size_t offset = (size_t)be32(body);
            if (offset > size - 8u) return NULL;
            pcm = body + 8u + offset;
            pcm_bytes = size - 8u - offset;
        }
        off += size;
        if (size & 1u) {
            if (off >= end) return NULL;
            ++off;
        }
    }
    if (off != end || !saw_comm || !saw_ssnd || !frames || !rate ||
        channels < 1u || channels > 2u ||
        (bits != 8u && bits != 16u && bits != 24u && bits != 32u)) return NULL;
    size_t sample_width = bits / 8u;
    size_t bytes_per_frame = sample_width * channels;
    if ((uint64_t)frames > (uint64_t)(pcm_bytes / bytes_per_frame)) return NULL;
    AiffState *s = calloc(1,sizeof(*s));
    if (!s) return NULL;
    s->pcm=pcm; s->rate=rate; s->channels=channels;
    s->bits=bits; s->frames=frames; s->little_endian=little_endian;
    info->sample_rate=rate; info->channels=channels; info->total_frames=frames;
    return s;
}
static size_t aiff_read(void *ctx, float *out, size_t frames) {
    AiffState *s=ctx;
    if (!s || !out || !frames || s->position >= s->frames) return 0;
    if ((uint64_t)frames > s->frames - s->position)
        frames=(size_t)(s->frames-s->position);
    unsigned width=s->bits/8u;
    size_t count=frames*s->channels;
    const uint8_t *p=s->pcm + (size_t)s->position*s->channels*width;
    uint64_t sign=UINT64_C(1) << (s->bits-1u);
    for (size_t i=0;i<count;i++) {
        uint32_t raw=0;
        for (unsigned j=0;j<width;j++) {
            if (s->little_endian) raw|=(uint32_t)p[j] << (8u*j);
            else raw=(raw<<8)|p[j];
        }
        int64_t value=(raw & sign) ? (int64_t)raw - (int64_t)(sign*2u) : (int64_t)raw;
        out[i]=(float)((double)value/(double)sign);
        p+=width;
    }
    s->position+=frames;
    return frames;
}
static int aiff_seek(void *ctx,uint64_t frame) {
    AiffState *s=ctx;
    if (!s || frame>s->frames) return 0;
    s->position=frame;return 1;
}
static void aiff_close(void *ctx){free(ctx);}
const SautiDecoderOps sauti_decoder_aiff_ops={
    "aiff-pcm",aiff_probe,aiff_open,aiff_read,aiff_seek,aiff_close
};
