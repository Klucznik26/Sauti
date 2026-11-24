/* MaktabaSauti, modul maktabasauti_svx: STRUMIENIOWY dekoder Amiga IFF 8SVX (8-bit PCM ze znakiem i kompresja delta Fibonacciego / wykladnicza), napisany w czystym C.
 *
 * Format (Commodore IFF 8SVX): FORM <rozmiar> 8SVX, chunki VHDR (oneShotHiSamples, repeatHiSamples, samplesPerHiCycle, samplesPerSec u16, ctOctave, sCompression, volume), opcjonalnie CHAN (6 = stereo,
 * 2 / 4 = jeden kanal), BODY.  sCompression 0: bajty ze znakiem; 1: delta Fibonacciego (BODY kanalu = bajt wypelniajacy, wartosc poczatkowa ze znakiem, potem bajty z dwoma kodami 4-bitowymi,
 * MLODSZY polbajt pierwszy, tablica {-34,-21,-13,-8,-5,-3,-2,-1,0,1,2,3,5,8,13,21}); 2: delta wykladnicza (rozszerzenie spoza specyfikacji, ten sam uklad, tablica {-128,-64,-32,-16,-8,-4,-2,-1,
 * 0,1,2,4,8,16,32,64}), obslugiwana tak jak w FFmpeg.  Wartosc jest obcinana do -128..127 (jak FFmpeg; wzorzec do porownania: dekodery 8svx_fib / 8svx_exp).  Stereo: BODY = caly kanal lewy, potem caly
 * prawy (skompresowane: kazdy z wlasnym naglowkiem 2 bajty); wymaga wejscia PRZEWIJALNEGO (dwie pozycje odczytu).  Wyjscie f32 = wartosc / 128.  VHDR.volume i petle (repeatHiSamples) nie sa
 * stosowane (jak w FFmpeg).
 *
 * Plik NIE jest wczytywany do pamieci (bufor 4 KiB na kanal); mono dziala tez na wejsciu nieprzewijalnym.  Przewijanie: PCM z arytmetyki pozycji, delta przez punkty kontrolne stanu.  Plik ucięty:
 * ramki sprzed ucięcia sa wydawane, potem E_TRUNCATED (lepki, czyszczony przez seek / reset / flush). */
#include "maktabasauti/maktabasauti_codec.h"
#include <string.h>

#define STAGE_BYTES 4096u
#define PREFIX_BYTES 512u
#define MAX_CHUNKS 256u
#define CP_MAX 1024u

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

/* ---- skaner naglowka (jak w maktabasauti_pcm.c): odczyt na bezwzglednych przesunieciach, z prefiksu albo z wejscia ---- */
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

static const int8_t fib_table[16] = { -34, -21, -13, -8, -5, -3, -2, -1, 0, 1, 2, 3, 5, 8, 13, 21 };
static const int8_t exp_table[16] = { -128, -64, -32, -16, -8, -4, -2, -1, 0, 1, 2, 4, 8, 16, 32, 64 };

typedef struct { uint64_t frame; int32_t v[2]; } cp_t;
typedef struct {
    uint64_t base, len, nxt;                                  /* blok kanalu w pliku: poczatek, dlugosc (po przycieciu do rozmiaru pliku), nastepny bajt do pobrania */
    stage_t st;
    int32_t v, v0;                                            /* biezaca wartosc (delta) i wartosc poczatkowa z bloku */
    uint8_t cur;                                              /* bajt z kodami, z ktorego bierzemy kolejne polbajty */
} chn_t;
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd;
    uint32_t channels, comp;
    const int8_t *table;
    chn_t ch[2];
    uint64_t total, pos;                                      /* ramek do wydania (znane, gdy rozmiar wejscia znany albo nie) i nastepna ramka */
    int total_known, seekable, tail;                          /* tail: dane urwane przed deklarowanym koncem */
    cp_t *cps; size_t ncp; uint64_t stride;
    maktabasauti_result_t pend;
} svx_t;

static maktabasauti_result_t ch_byte(svx_t *d, unsigned c, uint64_t idx, uint8_t *b) {
    chn_t *k = &d->ch[c];
    if (idx >= k->len) return MAKTABASAUTI_EOF;
    if (st_avail(&k->st) == 0) {
        maktabasauti_result_t r;
        if (d->channels == 2u && d->host->seek(d->host->user, d->rd, (int64_t)(k->base + idx), 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;     /* stereo: dwa miejsca odczytu */
        r = st_need(d->host, d->rd, &k->st, 1u);
        if (r != MAKTABASAUTI_OK) return r;
    }
    *b = k->st.buf[k->st.at++]; k->nxt = idx + 1u;
    return MAKTABASAUTI_OK;
}
static void cp_note(svx_t *d) {                                /* punkt kontrolny co `stride` ramek; przy pelnej puli co drugi wypada, a krok sie podwaja */
    size_t i;
    if (!d->cps || d->comp == 0u || d->pos == 0 || d->pos % d->stride != 0) return;
    if (d->ncp && d->cps[d->ncp - 1u].frame >= d->pos) return;
    if (d->ncp == CP_MAX) {
        for (i = 0; i < CP_MAX / 2u; i++) d->cps[i] = d->cps[2u * i];
        d->ncp = CP_MAX / 2u; d->stride *= 2u;
        if (d->pos % d->stride != 0) return;
    }
    d->cps[d->ncp].frame = d->pos; d->cps[d->ncp].v[0] = d->ch[0].v; d->cps[d->ncp].v[1] = d->ch[1].v; d->ncp++;
}
static maktabasauti_result_t svx_frame(svx_t *d, float *out) {
    unsigned c; uint8_t b[2]; int32_t nv[2]; maktabasauti_result_t r;
    cp_note(d);
    for (c = 0; c < d->channels; c++) {
        if (d->comp == 0u) { r = ch_byte(d, c, d->pos, &b[c]); nv[c] = (int8_t)b[c]; }
        else if ((d->pos & 1u) == 0) {
            r = ch_byte(d, c, 2u + d->pos / 2u, &b[c]);
            if (r == MAKTABASAUTI_OK) { d->ch[c].cur = b[c]; nv[c] = d->ch[c].v + d->table[b[c] & 15u]; }
        } else { r = MAKTABASAUTI_OK; b[c] = d->ch[c].cur; nv[c] = d->ch[c].v + d->table[b[c] >> 4]; }
        if (r != MAKTABASAUTI_OK) return r == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r;
        if (d->comp != 0u) nv[c] = nv[c] > 127 ? 127 : nv[c] < -128 ? -128 : nv[c];
    }
    for (c = 0; c < d->channels; c++) { d->ch[c].v = nv[c]; out[c] = (float)nv[c] * 0.0078125f; }
    d->pos++;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL svx_read(void *state, void *dst, size_t n, size_t *frames) {
    svx_t *d = (svx_t *)state; float *o = (float *)dst; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (d->pend != MAKTABASAUTI_OK) return d->pend;
    while (done < n) {
        if (d->pos >= d->total) { r = d->tail ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF; break; }
        r = svx_frame(d, o + done * d->channels);
        if (r != MAKTABASAUTI_OK) break;
        done++;
    }
    *frames = done;
    if (r == MAKTABASAUTI_E_TRUNCATED) d->pend = r;
    if (done) return MAKTABASAUTI_OK;
    return r == MAKTABASAUTI_OK ? MAKTABASAUTI_EOF : r;
}
static maktabasauti_result_t svx_goto(svx_t *d, uint64_t frame) {
    uint64_t from = 0; int32_t v0 = 0, v1 = 0; size_t i; unsigned c; float tmp[2];
    if (d->comp != 0u && frame != 0) {
        for (i = d->ncp; i > 0; i--) if (d->cps[i - 1u].frame <= frame) { from = d->cps[i - 1u].frame; v0 = d->cps[i - 1u].v[0]; v1 = d->cps[i - 1u].v[1]; break; }
    } else from = frame;
    for (c = 0; c < d->channels; c++) {
        chn_t *k = &d->ch[c];
        const uint64_t idx = d->comp == 0u ? from : 2u + from / 2u;
        st_clear(&k->st); k->nxt = idx;
        if (d->host->seek(d->host->user, d->rd, (int64_t)(k->base + idx), 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
        k->v = from ? (c ? v1 : v0) : k->v0;
        if (d->comp != 0u && (from & 1u)) {                                                    /* w srodku bajtu: potrzebny bajt z kodami */
            uint8_t b;
            if (d->host->seek(d->host->user, d->rd, (int64_t)(k->base + 2u + from / 2u), 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
            st_clear(&k->st);
            if (ch_byte(d, c, 2u + from / 2u, &b) != MAKTABASAUTI_OK) return MAKTABASAUTI_E_TRUNCATED;
            k->cur = b;
        }
    }
    d->pos = from; d->pend = MAKTABASAUTI_OK;
    while (d->pos < frame) {
        const maktabasauti_result_t r = svx_frame(d, tmp);
        if (r != MAKTABASAUTI_OK) return r == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL svx_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    svx_t *d = (svx_t *)state; maktabasauti_result_t r; (void)flags;
    if (!d->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (frame > d->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    r = svx_goto(d, frame);
    if (r == MAKTABASAUTI_OK) *actual = frame;
    return r;
}
static maktabasauti_result_t MAKTABASAUTI_CALL svx_reset(void *state) { svx_t *d = (svx_t *)state; return d->seekable ? svx_goto(d, 0) : MAKTABASAUTI_E_NOT_SEEKABLE; }
static maktabasauti_result_t MAKTABASAUTI_CALL svx_flush(void *state) { ((svx_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL svx_last_error(const void *state) { (void)state; return NULL; }
static void MAKTABASAUTI_CALL svx_close(void *state) {
    svx_t *d = (svx_t *)state;
    if (!d) return;
    if (d->cps) d->host->free(d->host->user, d->cps);
    d->host->free(d->host->user, d);
}

static int32_t MAKTABASAUTI_CALL svx_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 12u && is_tag(p, "FORM") && is_tag(p + 8, "8SVX")) ? 60 : 0;
}
static maktabasauti_result_t MAKTABASAUTI_CALL svx_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    scan_t s; uint8_t ck[8], vh[20]; maktabasauti_result_t r; uint64_t end, off = 12, body_off = 0, body_len = 0, avail_end = 0; unsigned chunks = 0;
    uint32_t form_size, rate = 0, comp = 0, channels = 1, chan_val = 0; int saw_vhdr = 0, saw_body = 0, saw_chan = 0; svx_t *d; int64_t size;
    (void)self; *state = NULL;
    memset(&s, 0, sizeof s);
    {   int64_t n; const uint32_t fl = h->input_flags ? h->input_flags(h->user, rd) : 0u;
        s.h = h; s.rd = rd; s.pos = 0; s.seekable = (fl & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
        n = h->peek(h->user, rd, s.pre, sizeof s.pre);
        if (n < 0) return (maktabasauti_result_t)n;
        s.pre_n = (size_t)n; }
    if (s.pre_n < 12u || !is_tag(s.pre, "FORM") || !is_tag(s.pre + 8, "8SVX")) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    form_size = be32(s.pre + 4);
    if (form_size < 4u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    end = 8u + (uint64_t)form_size;
    size = h->size ? h->size(h->user, rd) : -1;
    while (off + 8u <= end && chunks++ < MAX_CHUNKS) {
        uint32_t csize; uint64_t at;
        r = sc_fetch(&s, off, ck, sizeof ck);
        if (r != MAKTABASAUTI_OK) { if (saw_body && saw_vhdr) break; return r; }                     /* nagłówek chunku urwany: dane juz sa */
        csize = be32(ck + 4); off += 8u; at = off;
        if (is_tag(ck, "VHDR")) {
            if (saw_vhdr++ || csize < 20u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = sc_fetch(&s, at, vh, sizeof vh);
            if (r != MAKTABASAUTI_OK) return r;
            rate = be16(vh + 12); comp = vh[15];
        } else if (is_tag(ck, "CHAN")) {
            uint8_t cv[4];
            if (saw_chan++ || csize < 4u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = sc_fetch(&s, at, cv, 4u);
            if (r != MAKTABASAUTI_OK) { if (saw_vhdr && saw_body) { saw_chan = 0; break; } return r; }            /* CHAN urwany za danymi: bez niego (jak FFmpeg) */
            chan_val = be32(cv);
        } else if (is_tag(ck, "BODY")) {
            if (saw_body++) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            body_off = at; body_len = csize;
        }
        if (saw_vhdr && saw_body && (saw_chan || !s.seekable || off + csize >= end)) break;            /* CHAN moze byc jeszcze za danymi: sprawdz (tylko wejscie przewijalne) */
        if (saw_body && !saw_vhdr && !s.seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;               /* VHDR za danymi: trzeba wrocic */
        off += csize; if (csize & 1u) ++off;
        if (saw_vhdr && saw_body && saw_chan) break;
    }
    if (!saw_vhdr || !saw_body || !rate || rate > 384000u || comp > 2u) return comp > 2u && saw_vhdr ? MAKTABASAUTI_E_UNSUPPORTED_FEATURE : MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (saw_chan) { if (chan_val == 6u) channels = 2u; else if (chan_val == 2u || chan_val == 4u) channels = 1u; else return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
    if (channels == 2u && !s.seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;                           /* stereo: dwie pozycje odczytu */
    d = (svx_t *)h->alloc(h->user, sizeof *d);
    if (!d) return MAKTABASAUTI_E_NO_MEMORY;
    memset(d, 0, sizeof *d);
    d->host = h; d->rd = rd; d->channels = channels; d->comp = comp; d->table = comp == 2u ? exp_table : fib_table; d->seekable = s.seekable && size >= 0; d->stride = 4096u;
    {   const uint64_t per = body_len / channels; unsigned c; uint64_t frames = 0, deficit = 0;
        avail_end = size >= 0 ? (uint64_t)size : body_off + body_len;
        for (c = 0; c < channels; c++) {
            uint64_t len = per, base = body_off + (uint64_t)c * per, have;
            have = avail_end > base ? avail_end - base : 0;
            if (have < len) { deficit = 1; len = have; }
            d->ch[c].base = base; d->ch[c].len = len;
        }
        if (comp == 0u) { frames = d->ch[0].len; if (channels == 2u && d->ch[1].len < frames) frames = d->ch[1].len; }
        else { uint64_t m = d->ch[0].len; if (channels == 2u && d->ch[1].len < m) m = d->ch[1].len; frames = m >= 2u ? 2u * (m - 2u) : 0; }
        if (!deficit) d->total_known = 1;
        d->tail = deficit != 0;
        d->total = frames;
        if (per == 0 || (comp != 0u && per < 3u) || !frames) { h->free(h->user, d); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
        if (comp != 0u) {                                                                                  /* wartosc poczatkowa: drugi bajt bloku kanalu (pierwszy to wypelnienie) */
            for (c = 0; c < channels; c++) {
                uint8_t hd[2];
                r = sc_fetch(&s, d->ch[c].base, hd, 2u);
                if (r != MAKTABASAUTI_OK) { h->free(h->user, d); return r; }
                d->ch[c].v = d->ch[c].v0 = (int8_t)hd[1];
            }
        }
    }
    if (d->seekable && comp != 0u) {
        d->cps = (cp_t *)h->alloc(h->user, CP_MAX * sizeof *d->cps);
        if (!d->cps) { h->free(h->user, d); return MAKTABASAUTI_E_NO_MEMORY; }
    }
    /* pozycja wejscia: poczatek danych mono (stereo przewija sie przed kazdym odczytem); nieprzewijalne: odrzucenie naglowka */
    r = sc_move(&s, d->ch[0].base + (comp != 0u ? 2u : 0u));                                          /* skompresowane: za bajtem wypelniajacym i wartoscia poczatkowa */
    d->ch[0].nxt = comp != 0u ? 2u : 0u;
    if (r != MAKTABASAUTI_OK) { if (d->cps) h->free(h->user, d->cps); h->free(h->user, d); return r; }
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info; info->sample_rate = rate; info->channels = channels;
    info->channel_mask = channels == 1u ? MAKTABASAUTI_LAYOUT_MONO : MAKTABASAUTI_LAYOUT_STEREO;
    info->native_format = MAKTABASAUTI_FORMAT_F32; info->bits_per_sample = 8;
    info->total_frames = d->total_known || d->seekable ? d->total : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = (d->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u) | (comp == 0u ? MAKTABASAUTI_STREAM_LOSSLESS : 0u);
    strncpy(info->codec_name, comp == 0u ? "pcm" : comp == 1u ? "8svx-fib" : "8svx-exp", sizeof info->codec_name - 1u);
    strncpy(info->container_name, "iff", sizeof info->container_name - 1u);
    *state = d;
    return MAKTABASAUTI_OK;
}

const maktabasauti_codec_t maktabasauti_svx_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "amiga-8svx", "Amiga IFF 8SVX (PCM, Fibonacci / exponential delta; streaming)", "maktabasauti_svx",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    svx_probe, svx_open, svx_read, svx_seek, svx_reset, svx_flush, NULL, svx_last_error, NULL, svx_close, { NULL }, { NULL }
};
