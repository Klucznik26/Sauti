/* Creative Voice File (VOC): bounded, zero-copy PCM decoder.
 * Supported: legacy type 1/2, type 3 silence, type 8 extended PCM,
 * type 9 PCM U8/S16LE and metadata (4/5). Repeat blocks and compressed
 * codecs are rejected rather than silently producing incorrect audio.
 * Input bytes remain owned by the caller for the decoder's lifetime. */
#include "sauti_decoder_voc.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    uint64_t first, frames;
    uint8_t bytes_per_sample, silence;
} VocPart;
typedef struct {
    VocPart *parts;
    size_t count, capacity, index;
    uint32_t rate, channels;
    uint64_t frames, position;
} VocState;

static uint16_t le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int voc_probe(const uint8_t *p, size_t n) {
    return p && n >= 26 && memcmp(p, "Creative Voice File\x1a", 20) == 0 &&
           le16(p + 20) >= 26 && (size_t)le16(p + 20) <= n &&
           le16(p + 24) == (uint16_t)((~(unsigned)le16(p + 22) + 0x1234u) & 0xffffu);
}
static int voc_format(VocState *s, uint32_t rate, uint32_t channels) {
    if (rate < 1000u || rate > 192000u || channels < 1u || channels > 2u)
        return 0;
    if (s->rate && (s->rate != rate || s->channels != channels))
        return 0; /* No resampler/downmixer in the portable decoder. */
    s->rate = rate;
    s->channels = channels;
    return 1;
}
static int voc_append(VocState *s, const uint8_t *data, uint64_t frames,
                      unsigned width, int silence) {
    if (!frames) return 1;
    if (s->frames > UINT64_MAX - frames || s->count >= 16384u) return 0;
    if (s->count == s->capacity) {
        size_t next = s->capacity ? s->capacity * 2u : 8u;
        if (next > 16384u) next = 16384u;
        if (next > SIZE_MAX / sizeof(*s->parts)) return 0;
        VocPart *p = realloc(s->parts, next * sizeof(*s->parts));
        if (!p) return 0;
        s->parts = p;
        s->capacity = next;
    }
    s->parts[s->count++] = (VocPart){data, s->frames, frames, (uint8_t)width,
                                      (uint8_t)(silence != 0)};
    s->frames += frames;
    return 1;
}
static uint32_t legacy_rate(unsigned time_constant) {
    return 1000000u / (256u - time_constant);
}
static void *voc_open(const uint8_t *p, size_t n, SautiPcmInfo *info) {
    if (!info || !voc_probe(p, n)) return NULL;
    VocState *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    size_t cursor = le16(p + 20);
    uint32_t last_rate = 0, last_channels = 0;
    unsigned last_width = 0, pending_extended = 0;
    uint32_t extended_rate = 0, extended_channels = 0;
    int valid = 1;
    while (cursor < n && valid) {
        unsigned type = p[cursor++];
        if (type == 0) break; /* Terminator has no length field. */
        if (n - cursor < 3u) { valid = 0; break; }
        size_t length = (size_t)p[cursor] | ((size_t)p[cursor + 1] << 8) |
                        ((size_t)p[cursor + 2] << 16);
        cursor += 3;
        if (length > n - cursor) { valid = 0; break; }
        const uint8_t *b = p + cursor;
        if (type == 1) {
            if (length < 3u || b[1] != 0) { valid = 0; break; }
            uint32_t rate = pending_extended ? extended_rate : legacy_rate(b[0]);
            uint32_t channels = pending_extended ? extended_channels : 1u;
            pending_extended = 0;
            if (!voc_format(s, rate, channels) ||
                (length - 2u) % channels != 0u ||
                !voc_append(s, b + 2, (length - 2u) / channels, 1u, 0)) {
                valid = 0; break;
            }
            last_rate = rate; last_channels = channels; last_width = 1u;
        } else if (type == 2) {
            if (!last_width || !last_channels ||
                length % (last_width * last_channels) != 0u ||
                !voc_append(s, b, length / (last_width * last_channels),
                            last_width, 0)) {
                valid = 0; break;
            }
        } else if (type == 3) {
            if (length != 3u || !voc_format(s, legacy_rate(b[2]), 1u) ||
                !voc_append(s, NULL, (uint64_t)le16(b) + 1u, 0u, 1)) {
                valid = 0; break;
            }
        } else if (type == 4 || type == 5) {
            if (type == 4 && length != 2u) { valid = 0; break; }
            /* Metadata is not audio. */
        } else if (type == 8) {
            if (length != 4u || b[2] != 0 || b[3] > 1u) {
                valid = 0; break;
            }
            extended_channels = (uint32_t)b[3] + 1u;
            extended_rate = 256000000u /
                            (65536u - (uint32_t)le16(b)) / extended_channels;
            pending_extended = 1u;
        } else if (type == 9) {
            if (length < 13u) { valid = 0; break; }
            uint32_t rate = le32(b);
            uint32_t channels = b[5];
            unsigned width = b[4] == 8u && le16(b + 6) == 0u ? 1u :
                             b[4] == 16u && le16(b + 6) == 4u ? 2u : 0u;
            if (!width || !voc_format(s, rate, channels) ||
                (length - 12u) % (width * channels) != 0u ||
                !voc_append(s, b + 12, (length - 12u) / (width * channels),
                            width, 0)) {
                valid = 0; break;
            }
            last_rate = rate; last_channels = channels; last_width = width;
            pending_extended = 0;
        } else {
            /* Includes repeat blocks (6/7), ADPCM or unknown block types. */
            valid = 0; break;
        }
        cursor += length;
    }
    if (!valid || !s->frames || pending_extended) {
        free(s->parts);
        free(s);
        return NULL;
    }
    (void)last_rate;
    info->sample_rate = s->rate;
    info->channels = s->channels;
    info->total_frames = s->frames;
    return s;
}
static size_t voc_read(void *ctx, float *out, size_t frames) {
    VocState *s = ctx;
    if (!s || !out || !frames || s->position >= s->frames) return 0;
    if ((uint64_t)frames > s->frames - s->position)
        frames = (size_t)(s->frames - s->position);
    size_t done = 0;
    while (done < frames && s->index < s->count) {
        const VocPart *part = &s->parts[s->index];
        if (s->position >= part->first + part->frames) {
            s->index++;
            continue;
        }
        size_t inside = (size_t)(s->position - part->first);
        size_t take = frames - done;
        if ((uint64_t)take > part->frames - inside)
            take = (size_t)(part->frames - inside);
        size_t samples = take * s->channels;
        float *dst = out + done * s->channels;
        if (part->silence) {
            for (size_t i = 0; i < samples; i++) dst[i] = 0.0f;
        } else if (part->bytes_per_sample == 1u) {
            const uint8_t *src = part->data + inside * s->channels;
            for (size_t i = 0; i < samples; i++)
                dst[i] = ((float)src[i] - 128.0f) / 128.0f;
        } else {
            const uint8_t *src = part->data + inside * s->channels * 2u;
            for (size_t i = 0; i < samples; i++) {
                int16_t sample = (int16_t)le16(src + i * 2u);
                dst[i] = (float)sample / 32768.0f;
            }
        }
        done += take;
        s->position += take;
    }
    return done;
}
static int voc_seek(void *ctx, uint64_t frame) {
    VocState *s = ctx;
    if (!s || frame > s->frames) return 0;
    s->position = frame;
    s->index = 0;
    while (s->index < s->count &&
           frame >= s->parts[s->index].first + s->parts[s->index].frames)
        s->index++;
    return 1;
}
static void voc_close(void *ctx) {
    VocState *s = ctx;
    if (!s) return;
    free(s->parts);
    free(s);
}
const SautiDecoderOps sauti_decoder_voc_ops = {
    "creative-voc-pcm", voc_probe, voc_open, voc_read, voc_seek, voc_close
};
