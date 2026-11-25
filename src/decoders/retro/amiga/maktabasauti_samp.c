/* MaktabaSauti, modul maktabasauti_samp: STRUMIENIOWY dekoder Amiga IFF SAMP, napisany w czystym C.
 *
 * Opis Jima Fiore i Jeffa Glatta (dissidents), 3 grudnia 1989.  FORM SAMP, wymagany MHDR,
 * potem fale w BODY.  Kazda fala ma 80-bajtowy naglowek, potem opcjonalne koperty i dane.
 *
 * NumOfChans == 0: nie ma PlayMap.  Opis mowi, zeby grac fale na dowolnym kanale z ich Rate.
 * Robimy to jako jeden kanal, w kolejnosci pliku, kazda fala raz (zapisane bajty WaveSize).
 * NumOfChans != 0: PlayMap wymaga nuty MIDI, ktorej plik nie niesie.  Odmowa, bez zgadywania nuty.
 * VelStart 64 i 128 przesuwaja start o tablice predkosci MIDI.  Nuty nie ma: odmowa.
 * Inna wartosc VelStart niz 0, 64 i 128 jest sprzeczna z opisem.
 *
 * Format to liczba istotnych bitow, 8..28.  8: bajt ze znakiem (przyklad w opisie ma -90).
 * 9..16: slowo big-endian, bity dosuniete w lewo, dopelnienie do dwoch.
 * 17..28: dlugie slowo tak samo.  Przyklad 12 bitow: przesuniecie o 4 w lewo, znak w najstarszym bicie.
 * Wyjscie: bajt/128, slowo/32768, dlugie slowo/2147483648.  Sufit 384 kHz jak w 8SVX.
 * Kazda fala gra ze swoim Rate.  Gdy nastepne probki maja inny Rate, read zwraca
 * E_FORMAT_CHANGED i zero ramek: porcja sprzed granicy wyszla wczesniej, w starym Rate.
 * get_info podaje juz nowy Rate.  Kolejny read gra te probki.  Bez przeliczania.
 *
 * Petla (LoopStart/LoopEnd) trwa do puszczenia nuty.  Nuty nie ma, a nieskonczona petla nie jest
 * dlugoscia pliku: gramy zapisane probki raz.  ATAK/RLSE to glosnosc nuty, FATK/FRLS filtr,
 * ktorego Amiga nie ma, USER VOLMOD/PERMOD to modulacja ADKCON bez wzoru PCM: bajty pomijamy.
 * Bit 0 Flags to kolejna dyskietka.  Ten plik gra swoje fale.  Reszta bitow nie jest opisana.
 *
 * Plik urwany: cale probki, potem lepki E_TRUNCATED.  BODY przed MHDR bez przewijania: NOT_SEEKABLE. */
#include "maktabasauti/maktabasauti_codec.h"
#include <string.h>

#define STAGE_BYTES 4096u
#define PREFIX_BYTES 512u
#define MAX_CHUNKS 256u
#define MAX_WAVES 255u
#define WAVE_HDR 80u
#define RATE_MAX 384000u

typedef struct { uint8_t buf[STAGE_BYTES]; size_t at, have; } stage_t;
static maktabasauti_result_t st_need(const maktabasauti_host_t *h, maktabasauti_reader_t *rd, stage_t *s, size_t n) {
    if (s->have - s->at >= n) return MAKTABASAUTI_OK;
    if (s->at) { memmove(s->buf, s->buf + s->at, s->have - s->at); s->have -= s->at; s->at = 0; }
    while (s->have < n) {
        const int64_t m = h->read(h->user, rd, s->buf + s->have, STAGE_BYTES - s->have);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_EOF;
        s->have += (size_t)m;
    }
    return MAKTABASAUTI_OK;
}
static void st_clear(stage_t *s) { s->at = s->have = 0; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(((unsigned)p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int is_tag(const uint8_t *p, const char *tag) { return memcmp(p, tag, 4) == 0; }

typedef struct {
    const maktabasauti_host_t *h; maktabasauti_reader_t *rd;
    uint8_t pre[PREFIX_BYTES]; size_t pre_n;
    uint64_t pos;
    int seekable;
} scan_t;
static maktabasauti_result_t sc_move(scan_t *s, uint64_t off) {
    if (s->pos == off) return MAKTABASAUTI_OK;
    if (s->seekable) {
        const int64_t r = s->h->seek(s->h->user, s->rd, (int64_t)off, 0);
        if (r < 0) return r == MAKTABASAUTI_E_OUT_OF_RANGE ? MAKTABASAUTI_E_UNSUPPORTED_FORMAT : (maktabasauti_result_t)r;
        s->pos = off;
        return MAKTABASAUTI_OK;
    }
    if (off < s->pos) return MAKTABASAUTI_E_NOT_SEEKABLE;
    while (s->pos < off) {
        uint8_t tmp[256]; const uint64_t left = off - s->pos; const size_t want = left < sizeof tmp ? (size_t)left : sizeof tmp;
        const int64_t m = s->h->read(s->h->user, s->rd, tmp, want);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        s->pos += (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t sc_fetch(scan_t *s, uint64_t off, void *dst, size_t len) {
    uint8_t *o = (uint8_t *)dst; maktabasauti_result_t r;
    if (off + len <= s->pre_n) { memcpy(dst, s->pre + off, len); return MAKTABASAUTI_OK; }
    r = sc_move(s, off);
    if (r != MAKTABASAUTI_OK) return r;
    while (len) {
        const int64_t m = s->h->read(s->h->user, s->rd, o, len);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        o += m; len -= (size_t)m; s->pos += (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}

typedef struct { uint32_t wave_size, rate, skip; } wh_t;
static maktabasauti_result_t parse_wh(const uint8_t *p, uint32_t width, wh_t *o) {
    uint32_t wave, rate, atak, rlse, fatk, frls, user; uint8_t vel; uint64_t skip;
    wave = be32(p); rate = be32(p + 12); vel = p[25];
    atak = be32(p + 58); rlse = be32(p + 62); fatk = be32(p + 66); frls = be32(p + 70); user = be32(p + 74);
    if (vel != 0u && vel != 64u && vel != 128u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (vel != 0u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    if (rate == 0u || rate > RATE_MAX) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if ((wave & 1u) || wave % width != 0u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if ((atak | rlse | fatk | frls | user) & 1u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    skip = (uint64_t)atak + rlse + fatk + frls + user;
    if (skip > UINT32_MAX) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    o->wave_size = wave; o->rate = rate; o->skip = (uint32_t)skip;
    return MAKTABASAUTI_OK;
}
static uint32_t width_of(uint32_t format) {
    if (format == 8u) return 1u;
    if (format <= 16u) return 2u;
    return 4u;
}

typedef struct { uint64_t off, frames, first; uint32_t follow, rate; } wave_t;
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd; stage_t st;
    wave_t w[MAX_WAVES];
    uint32_t nw, width, format, rate, wave_rate, more;
    uint64_t total, pos, left;
    int seekable, tail, announce, unknown_len;
    maktabasauti_result_t pend;
} samp_t;

static maktabasauti_result_t take(samp_t *d, size_t n, uint8_t *o) {
    const maktabasauti_result_t r = st_need(d->host, d->rd, &d->st, n);
    if (r == MAKTABASAUTI_EOF) return MAKTABASAUTI_E_TRUNCATED;
    if (r != MAKTABASAUTI_OK) return r;
    memcpy(o, d->st.buf + d->st.at, n); d->st.at += n;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t discard(samp_t *d, uint32_t n) {
    uint8_t tmp[256];
    while (n) {
        const size_t k = n < sizeof tmp ? n : sizeof tmp; const maktabasauti_result_t r = take(d, k, tmp);
        if (r != MAKTABASAUTI_OK) return r;
        n -= (uint32_t)k;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t load_wave(samp_t *d) {
    uint8_t hd[WAVE_HDR]; wh_t wh; maktabasauti_result_t r;
    d->more--;
    r = take(d, sizeof hd, hd);
    if (r != MAKTABASAUTI_OK) return r;
    r = parse_wh(hd, d->width, &wh);
    if (r != MAKTABASAUTI_OK) return r;
    r = discard(d, wh.skip);
    if (r != MAKTABASAUTI_OK) return r;
    if (wh.wave_size == 0u) { d->left = 0; return MAKTABASAUTI_OK; }
    d->left = wh.wave_size / d->width;
    d->wave_rate = wh.rate;
    d->announce = wh.rate != d->rate; /* probki czekaja; sygnal przed pierwsza ramka nowego Rate */
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t one_sample(samp_t *d, float *o) {
    uint8_t b[4]; const maktabasauti_result_t r = take(d, d->width, b);
    if (r != MAKTABASAUTI_OK) return r;
    if (d->width == 1u) *o = (float)(int8_t)b[0] * (1.0f / 128.0f);
    else if (d->width == 2u) *o = (float)(int16_t)be16(b) * (1.0f / 32768.0f);
    else *o = (float)((double)(int32_t)be32(b) * (1.0 / 2147483648.0));
    d->left--; d->pos++;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL samp_read(void *state, void *dst, size_t n, size_t *frames) {
    samp_t *d = (samp_t *)state; float *o = (float *)dst; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (d->pend != MAKTABASAUTI_OK) return d->pend;
    while (done < n) {
        if (d->announce) {
            if (done) { r = MAKTABASAUTI_OK; break; } /* ta porcja jest jeszcze w starym Rate */
            d->rate = d->wave_rate; d->announce = 0; r = MAKTABASAUTI_E_FORMAT_CHANGED; break;
        }
        if (d->left == 0) {
            if (d->more == 0) { r = d->tail ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF; break; }
            r = load_wave(d);
            if (r != MAKTABASAUTI_OK) break;
            continue;
        }
        r = one_sample(d, o + done);
        if (r != MAKTABASAUTI_OK) break;
        done++;
    }
    *frames = done;
    if (r != MAKTABASAUTI_OK && r != MAKTABASAUTI_EOF && r != MAKTABASAUTI_E_FORMAT_CHANGED) {
        d->pend = r; d->left = 0; d->more = 0;
        if (r == MAKTABASAUTI_E_TRUNCATED) d->tail = 1;
    }
    if (done) return MAKTABASAUTI_OK;
    return r == MAKTABASAUTI_OK ? MAKTABASAUTI_EOF : r;
}
static maktabasauti_result_t goto_frame(samp_t *d, uint64_t frame) {
    uint32_t i; uint64_t into, off;
    if (frame == d->total) { d->pos = frame; d->left = 0; d->more = 0; d->pend = MAKTABASAUTI_OK; d->announce = 0; return MAKTABASAUTI_OK; }
    for (i = 0; i < d->nw; i++) if (frame < d->w[i].first + d->w[i].frames) break;
    if (i >= d->nw) return MAKTABASAUTI_E_CORRUPT_DATA;
    into = frame - d->w[i].first; off = d->w[i].off + into * d->width;
    if (d->host->seek(d->host->user, d->rd, (int64_t)off, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    st_clear(&d->st);
    d->left = d->w[i].frames - into; d->more = d->w[i].follow; d->pos = frame; d->pend = MAKTABASAUTI_OK;
    d->wave_rate = d->w[i].rate; d->announce = d->wave_rate != d->rate; /* seek nie odswieza opisu: nastepny read zglasza zmiane */
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL samp_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    samp_t *d = (samp_t *)state; (void)flags;
    if (!d->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (frame > d->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    {
        const maktabasauti_result_t r = goto_frame(d, frame);
        if (r == MAKTABASAUTI_OK) *actual = frame;
        return r;
    }
}
static maktabasauti_result_t MAKTABASAUTI_CALL samp_reset(void *state) {
    samp_t *d = (samp_t *)state;
    return d->seekable ? goto_frame(d, 0) : MAKTABASAUTI_E_NOT_SEEKABLE;
}
static maktabasauti_result_t MAKTABASAUTI_CALL samp_flush(void *state) { ((samp_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL samp_last_error(const void *state) { (void)state; return NULL; }
static void MAKTABASAUTI_CALL samp_close(void *state) {
    samp_t *d = (samp_t *)state;
    if (!d) return;
    d->host->free(d->host->user, d);
}

static int32_t MAKTABASAUTI_CALL samp_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 12u && is_tag(p, "FORM") && is_tag(p + 8, "SAMP")) ? 60 : 0;
}

static maktabasauti_result_t index_waves(samp_t *d, scan_t *s, uint64_t body_off, uint64_t avail, int deficit, uint32_t num_waves) {
    uint64_t cursor = body_off, limit = body_off + avail, frame = 0; uint32_t seen = 0; int tail = 0;
    while (seen < num_waves && !tail) {
        uint8_t hd[WAVE_HDR]; wh_t wh; maktabasauti_result_t r; uint64_t sample_at, bytes, frames;
        if (cursor > limit || limit - cursor < WAVE_HDR) {
            if (!deficit) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            tail = 1; break;
        }
        r = sc_fetch(s, cursor, hd, sizeof hd);
        if (r != MAKTABASAUTI_OK) return r;
        r = parse_wh(hd, d->width, &wh);
        if (r != MAKTABASAUTI_OK) return r;
        if ((uint64_t)wh.skip > UINT64_MAX - cursor - WAVE_HDR) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        sample_at = cursor + WAVE_HDR + wh.skip; seen++;
        if (sample_at > limit) {
            if (!deficit) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            tail = 1; break;
        }
        bytes = wh.wave_size;
        if (limit - sample_at < bytes) {
            if (!deficit) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            tail = 1; bytes = limit - sample_at; bytes -= bytes % d->width;
        }
        frames = bytes / d->width;
        if (frames) {
            if (d->nw >= MAX_WAVES) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
            if (d->rate == 0u) d->rate = wh.rate;
            d->w[d->nw].off = sample_at; d->w[d->nw].frames = frames; d->w[d->nw].first = frame;
            d->w[d->nw].follow = tail ? 0u : num_waves - seen; d->w[d->nw].rate = wh.rate;
            frame += frames; d->nw++;
        }
        if (tail) break;
        cursor = sample_at + wh.wave_size;
    }
    if (seen < num_waves && !deficit && !tail) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (!frame) return (deficit || tail) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    d->total = frame; d->tail = tail || deficit; d->left = d->w[0].frames; d->more = d->w[0].follow; d->pos = 0;
    d->wave_rate = d->w[0].rate; d->announce = 0;
    return sc_move(s, d->w[0].off);
}

static maktabasauti_result_t prime_forward(samp_t *d, scan_t *s, uint64_t cursor, uint64_t limit, int deficit, uint32_t num_waves) {
    uint32_t seen = 0;
    while (seen < num_waves) {
        uint8_t hd[WAVE_HDR]; wh_t wh; maktabasauti_result_t r; uint64_t sample_at, bytes, frames = 0;
        if (cursor > limit || limit - cursor < WAVE_HDR) {
            if (!deficit) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            if (!d->total) return MAKTABASAUTI_E_TRUNCATED;
            d->tail = 1; d->more = 0; return MAKTABASAUTI_OK;
        }
        r = sc_fetch(s, cursor, hd, sizeof hd);
        if (r != MAKTABASAUTI_OK) return r;
        r = parse_wh(hd, d->width, &wh);
        if (r != MAKTABASAUTI_OK) return r;
        if ((uint64_t)wh.skip > UINT64_MAX - cursor - WAVE_HDR) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        sample_at = cursor + WAVE_HDR + wh.skip; seen++;
        bytes = wh.wave_size;
        if (sample_at > limit || limit - sample_at < bytes) {
            if (!deficit) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            if (sample_at > limit) return d->total ? MAKTABASAUTI_OK : MAKTABASAUTI_E_TRUNCATED;
            bytes = limit - sample_at; bytes -= bytes % d->width; d->tail = 1;
        }
        frames = bytes / d->width;
        if (frames) {
            d->rate = wh.rate; d->wave_rate = wh.rate; d->announce = 0;
            d->total = frames; d->left = frames; d->more = d->tail ? 0u : num_waves - seen; d->pos = 0;
            return sc_move(s, sample_at);
        }
        if (d->tail) return MAKTABASAUTI_E_TRUNCATED;
        cursor = sample_at + wh.wave_size;
    }
    return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
}

static void fill_info(const samp_t *d, maktabasauti_stream_info_t *info) {
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info; info->sample_rate = d->rate; info->channels = 1u;
    info->channel_mask = MAKTABASAUTI_LAYOUT_MONO;
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->bits_per_sample = d->format;
    info->total_frames = d->unknown_len ? MAKTABASAUTI_UNKNOWN_LENGTH : d->total;
    info->flags = (d->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u) | MAKTABASAUTI_STREAM_LOSSLESS;
    strncpy(info->codec_name, "pcm", sizeof info->codec_name - 1u);
    strncpy(info->container_name, "iff", sizeof info->container_name - 1u);
}

static maktabasauti_result_t MAKTABASAUTI_CALL samp_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    scan_t s; uint8_t ck[8], mh[6]; maktabasauti_result_t r;
    uint64_t end, off = 12, body_off = 0, body_len = 0, avail;
    uint32_t form_size, format = 0, num_waves = 0, num_chans = 0;
    int saw_mhdr = 0, saw_body = 0, deficit = 0; unsigned chunks = 0; samp_t *d; int64_t size;
    (void)self; *state = NULL;
    memset(&s, 0, sizeof s);
    {   int64_t n; const uint32_t fl = h->input_flags ? h->input_flags(h->user, rd) : 0u;
        s.h = h; s.rd = rd; s.pos = 0; s.seekable = (fl & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
        n = h->peek(h->user, rd, s.pre, sizeof s.pre);
        if (n < 0) return (maktabasauti_result_t)n;
        s.pre_n = (size_t)n; }
    if (s.pre_n < 12u || !is_tag(s.pre, "FORM") || !is_tag(s.pre + 8, "SAMP")) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    form_size = be32(s.pre + 4);
    if (form_size < 4u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    end = 8u + (uint64_t)form_size;
    size = h->size ? h->size(h->user, rd) : -1;
    while (off + 8u <= end && chunks++ < MAX_CHUNKS) {
        uint32_t csize; uint64_t at;
        r = sc_fetch(&s, off, ck, sizeof ck);
        if (r != MAKTABASAUTI_OK) { if (saw_mhdr && saw_body) break; return r; }
        csize = be32(ck + 4); off += 8u; at = off;
        if (is_tag(ck, "MHDR")) {
            if (saw_mhdr++) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            if (csize < 6u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = sc_fetch(&s, at, mh, sizeof mh);
            if (r != MAKTABASAUTI_OK) return r;
            num_waves = mh[0]; format = mh[1]; num_chans = mh[4];
            if (csize != 6u + num_chans * 128u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            (void)mh[2]; (void)mh[3]; (void)mh[5]; /* Flags, PlayMode, Pad: PlayMode dotyczy tylko PlayMap */
        } else if (is_tag(ck, "BODY")) {
            if (saw_body++) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            body_off = at; body_len = csize;
        }
        if (!s.seekable && saw_mhdr && saw_body) break;
        if (saw_body && !saw_mhdr && !s.seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
        if ((uint64_t)csize > UINT64_MAX - off) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        off += csize; if (csize & 1u) ++off;
    }
    if (!saw_mhdr || !saw_body) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (num_chans != 0u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    if (num_waves == 0u || format < 8u || format > 28u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    d = (samp_t *)h->alloc(h->user, sizeof *d);
    if (!d) return MAKTABASAUTI_E_NO_MEMORY;
    memset(d, 0, sizeof *d);
    d->host = h; d->rd = rd; d->seekable = s.seekable; d->format = format; d->width = width_of(format);
    avail = body_len;
    if (size >= 0) {
        if ((uint64_t)size <= body_off) avail = 0;
        else if ((uint64_t)size - body_off < body_len) { avail = (uint64_t)size - body_off; deficit = 1; }
    }
    r = s.seekable ? index_waves(d, &s, body_off, avail, deficit, num_waves)
                   : prime_forward(d, &s, body_off, body_off + avail, deficit, num_waves);
    if (r != MAKTABASAUTI_OK) { h->free(h->user, d); return r; }
    if (!d->left && !d->more) { h->free(h->user, d); return d->tail ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
    d->unknown_len = !d->seekable && d->more != 0u;
    fill_info(d, info);
    *state = d;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL samp_get_info(const void *state, maktabasauti_stream_info_t *info) {
    if (!state || !info) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    fill_info((const samp_t *)state, info);
    return MAKTABASAUTI_OK;
}

const maktabasauti_codec_t maktabasauti_samp_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "amiga-samp",
    "Amiga IFF SAMP (linear PCM, no MIDI map; streaming)", "maktabasauti_samp",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    samp_probe, samp_open, samp_read, samp_seek, samp_reset, samp_flush, NULL, samp_last_error, samp_get_info, samp_close, { NULL }, { NULL }
};
