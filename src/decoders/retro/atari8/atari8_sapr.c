#include "atari8_sapr.h"
#include <string.h>

static bool equal(const uint8_t *p, size_t n, const char *s)
{
    return n == strlen(s) && memcmp(p, s, n) == 0;
}

static bool number(const uint8_t *p, size_t n, uint32_t *out)
{
    uint32_t v = 0;
    if (!n)
        return false;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9')
            return false;
        unsigned digit = p[i] - '0';
        if (v > (32767u - digit) / 10u)
            return false;
        v = v * 10u + digit;
    }
    *out = v;
    return true;
}

Atari8SaprResult atari8_sapr_parse(const uint8_t *data, size_t size,
                                   Atari8SaprInfo *info)
{
    if (!data || !info || size < 5 || memcmp(data, "SAP\r\n", 5))
        return ATARI8_SAPR_BAD_FORMAT;
    Atari8SaprInfo x = {0};
    bool type = false, end = false, fast = false, time_seen = false;
    uint32_t scanlines = 0;
    size_t pos = 5;
    /* Bound header scanning independently of the binary payload. */
    while (pos < size && pos < 16384u) {
        size_t start = pos;
        while (pos < size && pos < 16384u && data[pos] != '\r') {
            if (data[pos] < 32 || data[pos] > 126)
                return ATARI8_SAPR_BAD_FORMAT;
            ++pos;
        }
        if (pos >= 16384u || size - pos < 2 || data[pos + 1] != '\n')
            return ATARI8_SAPR_BAD_FORMAT;
        size_t len = pos - start;
        const uint8_t *line = data + start;
        pos += 2;
        if (!len) {
            end = true;
            break;
        }
        size_t key = 0;
        while (key < len && line[key] != ' ')
            ++key;
        size_t arg = key < len ? key + 1 : key;
        const uint8_t *a = line + arg;
        size_t n = len - arg;
        if (equal(line, key, "TYPE")) {
            if (type || n != 1)
                return ATARI8_SAPR_BAD_FORMAT;
            if (*a != 'R')
                return ATARI8_SAPR_UNSUPPORTED;
            type = true;
        } else if (equal(line, key, "FASTPLAY")) {
            if (fast || !number(a, n, &scanlines) || !scanlines)
                return ATARI8_SAPR_BAD_FORMAT;
            fast = true;
        } else if (equal(line, key, "STEREO")) {
            if (n || x.stereo)
                return ATARI8_SAPR_BAD_FORMAT;
            x.stereo = true;
        } else if (equal(line, key, "NTSC")) {
            if (n || x.ntsc)
                return ATARI8_SAPR_BAD_FORMAT;
            x.ntsc = true;
        } else if (equal(line, key, "SONGS") || equal(line, key, "DEFSONG")) {
            uint32_t v;
            if (!number(a, n, &v))
                return ATARI8_SAPR_BAD_FORMAT;
            if (v != (equal(line, key, "SONGS") ? 1u : 0u))
                return ATARI8_SAPR_UNSUPPORTED;
        } else if (equal(line, key, "AUTHOR") || equal(line, key, "NAME")
                   || equal(line, key, "DATE")) {
            Atari8SapText *field=equal(line,key,"NAME") ? &x.metadata.name :
                (equal(line,key,"AUTHOR") ? &x.metadata.author : &x.metadata.date);
            if (!atari8_sap_text_parse(field,a,n))
                return ATARI8_SAPR_BAD_FORMAT;
        } else if (equal(line, key, "TIME")) {
            if (time_seen) {
                x.time = (Atari8SapTime){0, ATARI8_SAP_TIME_INVALID, false};
            } else {
                x.time = atari8_sap_time_parse(a,n);
                time_seen = true;
            }
        } else {
            return ATARI8_SAPR_UNSUPPORTED;
        }
    }
    if (!type || !end)
        return ATARI8_SAPR_BAD_FORMAT;
    size_t stride = x.stereo ? 18u : 9u;
    if (size == pos || (size - pos) % stride)
        return ATARI8_SAPR_BAD_FORMAT;
    x.data = data + pos;
    x.frame_count = (size - pos) / stride;
    x.cycles_per_frame = (fast ? scanlines : (x.ntsc ? 262u : 312u)) * 114u;
    *info = x;
    return ATARI8_SAPR_OK;
}

Atari8SaprResult atari8_sapr_open(Atari8Sapr *s, const uint8_t *data,
                                  size_t size, uint32_t rate)
{
    Atari8SaprInfo info;
    if (!s)
        return ATARI8_SAPR_BAD_FORMAT;
    Atari8SaprResult r = atari8_sapr_parse(data, size, &info);
    if (r != ATARI8_SAPR_OK)
        return r;
    memset(s, 0, sizeof(*s));
    s->info = info;
    s->nonlinear_mix = true;
    atari8_core_reset(&s->core, info.ntsc, info.stereo);
    if (!atari8_pcm_init(&s->pcm, s->core.pokey[0].clock_hz, rate,
                         info.stereo ? 2u : 1u, true)) {
        s->terminal = ATARI8_SAPR_UNSUPPORTED;
        return s->terminal;
    }
    return ATARI8_SAPR_OK;
}

/* Frame writes are atomic with respect to playback; no artificial STIMER. */
static bool apply(Atari8Core *core, const uint8_t *data)
{
    Atari8Core next = *core;
    for (unsigned ch = 0; ch < atari8_core_channels(core); ++ch) {
        uint16_t base = (uint16_t)(0xd200u + ch * 16u);
        const uint8_t *p = data + ch * 9u;
        for (unsigned i = 0; i < 8; i += 2) {
            if (!atari8_core_write(&next, (uint16_t)(base + i), p[i]))
                return false;
        }
        if (!atari8_core_write(&next, (uint16_t)(base + 8), p[8]))
            return false;
        for (unsigned i = 1; i < 8; i += 2)
            if (!atari8_core_write(&next, (uint16_t)(base + i), p[i]))
                return false;
    }
    *core = next;
    return true;
}

Atari8SaprResult atari8_sapr_next(Atari8Sapr *s, int16_t frame[2])
{
    if (!s || !frame)
        return ATARI8_SAPR_BAD_FORMAT;
    if (s->terminal != ATARI8_SAPR_OK)
        return s->terminal;
    for (;;) {
        if (!s->remaining) {
            if (s->next_frame == s->info.frame_count) {
                s->terminal = ATARI8_SAPR_EOF;
                return s->terminal;
            }
            size_t stride = s->info.stereo ? 18u : 9u;
            if (!apply(&s->core, s->info.data + s->next_frame * stride)) {
                s->terminal = ATARI8_SAPR_UNSUPPORTED;
                return s->terminal;
            }
            ++s->next_frame;
            s->remaining = s->info.cycles_per_frame;
        }
        uint8_t level[2];
        atari8_core_levels(&s->core, level);
        bool ready;
        if (s->nonlinear_mix) {
            uint16_t analog[2]={atari8_pokey_curve[level[0]],
                                atari8_pokey_curve[level[1]]};
            ready=atari8_pcm_feed_u16(&s->pcm,analog,frame);
        } else {
            ready=atari8_pcm_feed(&s->pcm,level,frame);
        }
        atari8_core_step(&s->core, 1);
        --s->remaining;
        if (ready)
            return ATARI8_SAPR_OK;
    }
}
