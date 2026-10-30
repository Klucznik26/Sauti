/* Portable adapter for the NASM AVR parser/PCM converter.
 * No codec logic lives here; state allocation and decoder ABI glue only. */
#include "sauti_decoder_avr.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t rate, channels, bits, is_signed;
    uint64_t frames;
    const uint8_t *audio;
} AvrMeta;

extern int sauti_avr_parse_asm(const uint8_t *, size_t, AvrMeta *);
extern void sauti_avr_pcm_to_f32_asm(const uint8_t *, float *, size_t,
                                     uint32_t, uint32_t);
typedef struct { AvrMeta meta; uint64_t position; } AvrState;

static int avr_probe(const uint8_t *p, size_t n) {
    return p && n >= 4 && memcmp(p, "2BIT", 4) == 0;
}
static void *avr_open(const uint8_t *p, size_t n, SautiPcmInfo *info) {
    if (!info || !avr_probe(p,n)) return NULL;
    AvrMeta m = {0};
    if (!sauti_avr_parse_asm(p,n,&m)) return NULL;
    AvrState *s = malloc(sizeof(*s));
    if (!s) return NULL;
    s->meta=m;
    s->position=0;
    info->sample_rate=m.rate;
    info->channels=m.channels;
    info->total_frames=m.frames;
    return s;
}
static size_t avr_read(void *ctx, float *out, size_t frames) {
    AvrState *s=ctx;
    if (!s || !out || !frames) return 0;
    uint64_t remaining=s->meta.frames-s->position;
    if ((uint64_t)frames>remaining) frames=(size_t)remaining;
    if (!frames) return 0;
    size_t width=s->meta.bits/8u;
    size_t count=frames*s->meta.channels;
    const uint8_t *start=s->meta.audio+(size_t)s->position*s->meta.channels*width;
    sauti_avr_pcm_to_f32_asm(start,out,count,s->meta.bits,s->meta.is_signed);
    s->position+=frames;
    return frames;
}
static int avr_seek(void *ctx,uint64_t frame) {
    AvrState *s=ctx;
    if (!s || frame>s->meta.frames) return 0;
    s->position=frame;
    return 1;
}
static void avr_close(void *ctx) { free(ctx); }
const SautiDecoderOps sauti_decoder_avr_ops={
    "atari-avr-pcm",avr_probe,avr_open,avr_read,avr_seek,avr_close
};
