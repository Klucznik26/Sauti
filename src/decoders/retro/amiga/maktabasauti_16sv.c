/* MaktabaSauti, modul maktabasauti_16sv: STRUMIENIOWY dekoder Amiga IFF 16SV, napisany w czystym C.
 *
 * Publiczny opis (16sv.datatype, SoundFX, SoundBox): FORM 16SV ma te same chunki co 8SVX, a BODY
 * niesie probki 16-bitowe ze znakiem, big-endian, zamiast bajtow.  CHAN 6 to stereo (caly lewy,
 * potem caly prawy), 2 i 4 to jeden kanal.  Inne chunki (NAME, ANNO, (c), ATAK, RLSE) sa pomijane.
 * VHDR.volume to glosnosc urzadzenia (0x10000 = pelna), nie skala probek, wiec nie zmienia PCM.
 * oneShotHiSamples + repeatHiSamples przy ctOctave <= 1 jest metadanymi petli: wydajemy cale BODY
 * jeden raz.  Przy ctOctave > 1 (EA, 7 lutego 1985, ten sam VHDR co 8SVX) ta suma jest dlugoscia
 * najwyzszej oktawy i ona jest pierwsza.  Kazda nizsza jest dwa razy dluzsza.  Jednorazowe granie
 * bierze najnizsza oktawa: jej one-shot i repeat, jeden raz.  Suma 0 albo oktawa poza calym BODY:
 * odmawiamy.  Krotsze cale BODY daje tyle probek tej oktawy, ile jest, i EOF, nie lepkie urwanie.
 *
 * sCompression inne niz 0: odmawiamy.  SoundFX nazywa 1 = FDPCM16_6 (Fibonacci 8:3) i 2 = EDPCM16_5
 * (wykladnicza 16:5), ale nie podaje tablicy delt ani ukladu bitow.  CHAN spoza 2, 4 i 6 tez: SoundFX
 * wymienia quadro, a opublikowany CHAN nie ma wartosci ani ukladu czterech kanalow.
 * SEQN nie jest stosowany: rejestracja Petera Normana opisuje offsety bajtowe 8-bitowego 8SVX.
 *
 * Wyjscie f32 = probka / 32768.  Mono dziala bez przewijania.  Stereo wymaga dwoch pozycji odczytu.
 * Plik urwany: cale probki sprzed urwania, potem lepki E_TRUNCATED. */
#include "maktabasauti/maktabasauti_codec.h"
#include <string.h>

#define STAGE_BYTES 4096u
#define PREFIX_BYTES 512u
#define MAX_CHUNKS 256u

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
static size_t st_avail(const stage_t *s) { return s->have - s->at; }
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

typedef struct {
    uint64_t base, len, nxt;
    stage_t st;
} chn_t;
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd;
    uint32_t channels;
    chn_t ch[2];
    uint64_t total, pos;
    int total_known, seekable, tail;
    maktabasauti_result_t pend;
} sv16_t;

static maktabasauti_result_t ch_byte(sv16_t *d, unsigned c, uint64_t idx, uint8_t *b) {
    chn_t *k = &d->ch[c];
    const int cold = k->nxt != idx;
    maktabasauti_result_t r;
    if (idx >= k->len) return MAKTABASAUTI_EOF;
    if (cold) {
        if (d->host->seek(d->host->user, d->rd, (int64_t)(k->base + idx), 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
        st_clear(&k->st);
        k->nxt = idx;
    }
    if (st_avail(&k->st) == 0) {
        if (d->channels == 2u && d->host->seek(d->host->user, d->rd, (int64_t)(k->base + idx), 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
        r = st_need(d->host, d->rd, &k->st, 1u);
        if (r != MAKTABASAUTI_OK) return r;
    }
    *b = k->st.buf[k->st.at++];
    k->nxt = idx + 1u;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t sv16_frame(sv16_t *d, float *out) {
    unsigned c;
    for (c = 0; c < d->channels; c++) {
        uint8_t hi, lo;
        int16_t s;
        maktabasauti_result_t r = ch_byte(d, c, d->pos * 2u, &hi);
        if (r == MAKTABASAUTI_OK) r = ch_byte(d, c, d->pos * 2u + 1u, &lo);
        if (r != MAKTABASAUTI_OK) return r == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r;
        s = (int16_t)(((unsigned)hi << 8) | lo);
        out[c] = (float)s * (1.0f / 32768.0f);
    }
    d->pos++;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL sv16_read(void *state, void *dst, size_t n, size_t *frames) {
    sv16_t *d = (sv16_t *)state; float *o = (float *)dst; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (d->pend != MAKTABASAUTI_OK) return d->pend;
    while (done < n) {
        if (d->pos >= d->total) { r = d->tail ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF; break; }
        r = sv16_frame(d, o + done * d->channels);
        if (r != MAKTABASAUTI_OK) break;
        done++;
    }
    *frames = done;
    if (r == MAKTABASAUTI_E_TRUNCATED) d->pend = r;
    if (done) return MAKTABASAUTI_OK;
    return r == MAKTABASAUTI_OK ? MAKTABASAUTI_EOF : r;
}
static maktabasauti_result_t sv16_goto(sv16_t *d, uint64_t frame) {
    unsigned c;
    for (c = 0; c < d->channels; c++) {
        chn_t *k = &d->ch[c];
        st_clear(&k->st);
        if (d->host->seek(d->host->user, d->rd, (int64_t)(k->base + frame * 2u), 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
        k->nxt = frame * 2u;
    }
    d->pos = frame;
    d->pend = MAKTABASAUTI_OK;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL sv16_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    sv16_t *d = (sv16_t *)state; maktabasauti_result_t r; (void)flags;
    if (!d->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (frame > d->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    r = sv16_goto(d, frame);
    if (r == MAKTABASAUTI_OK) *actual = frame;
    return r;
}
static maktabasauti_result_t MAKTABASAUTI_CALL sv16_reset(void *state) { sv16_t *d = (sv16_t *)state; return d->seekable ? sv16_goto(d, 0) : MAKTABASAUTI_E_NOT_SEEKABLE; }
static maktabasauti_result_t MAKTABASAUTI_CALL sv16_flush(void *state) { ((sv16_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL sv16_last_error(const void *state) { (void)state; return NULL; }
static void MAKTABASAUTI_CALL sv16_close(void *state) {
    sv16_t *d = (sv16_t *)state;
    if (!d) return;
    d->host->free(d->host->user, d);
}

static int32_t MAKTABASAUTI_CALL sv16_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 12u && is_tag(p, "FORM") && is_tag(p + 8, "16SV")) ? 60 : 0;
}
static maktabasauti_result_t MAKTABASAUTI_CALL sv16_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    scan_t s; uint8_t ck[8], vh[20]; maktabasauti_result_t r; uint64_t end, off = 12, body_off = 0, body_len = 0;
    uint32_t form_size, rate = 0, comp = 0, channels = 1, chan_val = 0, one_shot = 0, repeat = 0, octave = 0;
    int saw_vhdr = 0, saw_body = 0, saw_chan = 0; unsigned chunks = 0; sv16_t *d; int64_t size; uint64_t hi;
    (void)self; *state = NULL;
    memset(&s, 0, sizeof s);
    {   int64_t n; const uint32_t fl = h->input_flags ? h->input_flags(h->user, rd) : 0u;
        s.h = h; s.rd = rd; s.pos = 0; s.seekable = (fl & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
        n = h->peek(h->user, rd, s.pre, sizeof s.pre);
        if (n < 0) return (maktabasauti_result_t)n;
        s.pre_n = (size_t)n; }
    if (s.pre_n < 12u || !is_tag(s.pre, "FORM") || !is_tag(s.pre + 8, "16SV")) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    form_size = be32(s.pre + 4);
    if (form_size < 4u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    end = 8u + (uint64_t)form_size;
    size = h->size ? h->size(h->user, rd) : -1;
    while (off + 8u <= end && chunks++ < MAX_CHUNKS) {
        uint32_t csize; uint64_t at;
        r = sc_fetch(&s, off, ck, sizeof ck);
        if (r != MAKTABASAUTI_OK) { if (saw_body && saw_vhdr) break; return r; }
        csize = be32(ck + 4); off += 8u; at = off;
        if (is_tag(ck, "VHDR")) {
            if (saw_vhdr++ || csize < 20u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = sc_fetch(&s, at, vh, sizeof vh);
            if (r != MAKTABASAUTI_OK) return r;
            one_shot = be32(vh); repeat = be32(vh + 4); rate = be16(vh + 12); octave = vh[14]; comp = vh[15];
        } else if (is_tag(ck, "CHAN")) {
            uint8_t cv[4];
            if (saw_chan++ || csize < 4u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = sc_fetch(&s, at, cv, 4u);
            if (r != MAKTABASAUTI_OK) { if (saw_vhdr && saw_body) { saw_chan = 0; break; } return r; }
            chan_val = be32(cv);
        } else if (is_tag(ck, "BODY")) {
            if (saw_body++) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            body_off = at; body_len = csize;
        }
        if (saw_vhdr && saw_body && (saw_chan || !s.seekable || off + csize >= end)) break;
        if (saw_body && !saw_vhdr && !s.seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
        if ((uint64_t)csize > UINT64_MAX - off) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        off += csize; if (csize & 1u) ++off;
        if (saw_vhdr && saw_body && saw_chan) break;
    }
    if (!saw_vhdr || !saw_body) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (comp != 0u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    if (!rate || rate > 384000u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (saw_chan) { if (chan_val == 6u) channels = 2u; else if (chan_val == 2u || chan_val == 4u) channels = 1u; else return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
    hi = (uint64_t)one_shot + repeat;
    if (octave > 1u && hi == 0u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    if (channels == 2u && !s.seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    d = (sv16_t *)h->alloc(h->user, sizeof *d);
    if (!d) return MAKTABASAUTI_E_NO_MEMORY;
    memset(d, 0, sizeof *d);
    d->host = h; d->rd = rd; d->channels = channels; d->seekable = s.seekable && size >= 0;
    d->ch[0].nxt = UINT64_MAX; d->ch[1].nxt = UINT64_MAX;
    {   const uint64_t per = body_len / channels; unsigned c; uint64_t frames, avail_end, deficit = 0;
        avail_end = size >= 0 ? (uint64_t)size : body_off + body_len;
        for (c = 0; c < channels; c++) {
            uint64_t len = per, base = body_off + (uint64_t)c * per, have;
            have = avail_end > base ? avail_end - base : 0;
            if (have < len) { deficit = 1; len = have; }
            d->ch[c].base = base; d->ch[c].len = len;
        }
        frames = d->ch[0].len / 2u;
        if (channels == 2u && d->ch[1].len / 2u < frames) frames = d->ch[1].len / 2u;
        if (!deficit) d->total_known = 1;
        if (octave > 1u) {
            uint64_t span, origin, low_len, out;
            if (octave - 1u >= 64u || hi > UINT64_MAX / (1ull << (octave - 1u))) { h->free(h->user, d); return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
            span = 1ull << (octave - 1u);
            low_len = hi * span;
            if (span > 1ull && hi > UINT64_MAX / (span - 1ull)) { h->free(h->user, d); return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
            origin = hi * (span - 1ull);
            if (origin >= frames) { h->free(h->user, d); return deficit ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
            out = low_len;
            if (out > frames - origin) out = frames - origin;
            /* Urwane jest tylko to, co obcina wybrana oktawa.  Krotsze cale BODY konczy sie EOF. */
            d->tail = deficit && origin + low_len > frames;
            for (c = 0; c < channels; c++) {
                uint64_t off = origin * 2u, nbytes = out * 2u;
                if (off > d->ch[c].len) off = d->ch[c].len;
                if (nbytes > d->ch[c].len - off) nbytes = d->ch[c].len - off;
                d->ch[c].base += off; d->ch[c].len = nbytes;
            }
            frames = out;
        } else d->tail = deficit != 0;
        d->total = frames;
        if (per < 2u || !frames) { h->free(h->user, d); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
    }
    r = sc_move(&s, d->ch[0].base);
    d->ch[0].nxt = 0;
    if (r != MAKTABASAUTI_OK) { h->free(h->user, d); return r; }
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info; info->sample_rate = rate; info->channels = channels;
    info->channel_mask = channels == 1u ? MAKTABASAUTI_LAYOUT_MONO : MAKTABASAUTI_LAYOUT_STEREO;
    info->native_format = MAKTABASAUTI_FORMAT_F32; info->bits_per_sample = 16;
    info->total_frames = d->total_known || d->seekable ? d->total : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = (d->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u) | MAKTABASAUTI_STREAM_LOSSLESS;
    strncpy(info->codec_name, "pcm", sizeof info->codec_name - 1u);
    strncpy(info->container_name, "iff", sizeof info->container_name - 1u);
    *state = d;
    return MAKTABASAUTI_OK;
}

const maktabasauti_codec_t maktabasauti_16sv_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "amiga-16sv", "Amiga IFF 16SV (signed 16-bit PCM, planar; streaming)", "maktabasauti_16sv",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    sv16_probe, sv16_open, sv16_read, sv16_seek, sv16_reset, sv16_flush, NULL, sv16_last_error, NULL, sv16_close, { NULL }, { NULL }
};
