/* MaktabaSauti, modul maktabasauti_voc: STRUMIENIOWY dekoder Creative Voice File (VOC), napisany w czystym C.
 *
 * Plik NIE jest wczytywany do pamieci.  Dla dobrych plikow wynik jest IDENTYCZNY bit w bit z dawnym adapterem (sauti_decoder_voc.c): bloki 1/2 (PCM 8 bit, takze z blokiem 8 = stereo / rozszerzona
 * czestotliwosc), 3 (cisza), 4/5 (znaczniki, pomijane), 9 (PCM 8/16 bit), te same wzory (8 bit: (v-128)/128, 16 bit: v/32768).  Bloki powtorzen (6/7), ADPCM i nieznane sa odrzucane, nie odtwarzane blednie.
 *
 * Dwa tryby:
 *   - wejscie PRZEWIJALNE: przy otwarciu przechodzi po naglowkach blokow (seek), buduje indeks czesci (do 16384, jak dawniej) i zna dlugosc; seek dokladny, jak w dawnym adapterze;
 *   - wejscie NIEPRZEWIJALNE: dlugosc nieznana, bloki sa dekodowane po kolei (nic nie jest zapamietywane poza bufor 4 KiB); blad w bloku za wydanymi juz ramkami wychodzi po tych ramkach
 *     (dawny adapter odrzucal wtedy caly plik); zmiana czestotliwosci / liczby kanalow w srodku = E_UNSUPPORTED_FEATURE (adapter tez nie mial resamplera).
 * Plik ucięty: ramki sprzed ucięcia sa wydawane, potem E_TRUNCATED (dawny adapter odrzucal caly plik; to samo dotyczy bloku 8 bez bloku danych na koncu: dawny adapter odrzucal plik, modul go przyjmuje).  Naglowek (czesc z poczatku, blok danych) jest czytany z prefiksu okna czytnika (peek), wiec
 * E_WOULD_BLOCK przy otwarciu jest wznawialny; wejscie nieprzewijalne tez, dopoki pierwszy blok audio miesci sie w prefiksie (inaczej: odczyt z odrzuceniem, nie wznawialny). */
#include "maktabasauti/maktabasauti_codec.h"
#include <string.h>

#define STAGE_BYTES 4096u
#define PREFIX_BYTES 512u
#define MAX_PARTS 16384u
#define MAX_BLOCKS (1u << 20)

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

static uint16_t le16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t default_mask(uint32_t ch) { return ch == 1u ? MAKTABASAUTI_LAYOUT_MONO : (ch == 2u ? MAKTABASAUTI_LAYOUT_STEREO : 0u); }

/* ---- skaner naglowka (jak w maktabasauti_pcm.c): odczyt na bezwzglednych przesunieciach z prefiksu albo z wejscia ---- */
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
        if (r < 0) return r == MAKTABASAUTI_E_OUT_OF_RANGE ? MAKTABASAUTI_EOF : (maktabasauti_result_t)r;      /* poza koncem pliku: koniec blokow (plik ucięty) */
        s->pos = off;
        return MAKTABASAUTI_OK;
    }
    if (off < s->pos) return MAKTABASAUTI_E_NOT_SEEKABLE;
    while (s->pos < off) {
        uint8_t tmp[256]; const uint64_t left = off - s->pos; const size_t want = left < sizeof tmp ? (size_t)left : sizeof tmp;
        const int64_t m = s->h->read(s->h->user, s->rd, tmp, want);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_EOF;
        s->pos += (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}
/* OK albo EOF (za koncem wejscia) albo blad wejscia */
static maktabasauti_result_t sc_fetch(scan_t *s, uint64_t off, void *dst, size_t len) {
    uint8_t *o = (uint8_t *)dst; maktabasauti_result_t r;
    if (off + len <= s->pre_n) { memcpy(dst, s->pre + off, len); return MAKTABASAUTI_OK; }
    r = sc_move(s, off);
    if (r != MAKTABASAUTI_OK) return r;
    while (len) {
        const int64_t m = s->h->read(s->h->user, s->rd, o, len);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_EOF;
        o += m; len -= (size_t)m; s->pos += (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}

/* ---- skladnia blokow (czysta funkcja, bez wejscia) ---- */
typedef struct { uint32_t rate, channels, last_width, last_channels, ext_rate, ext_channels; int pending_ext; } vfmt_t;
typedef struct { int kind; unsigned width; uint64_t frames; uint32_t hdr; } vseg_t;      /* kind: 0 znacznik / inne (pomijane), 1 PCM, 2 cisza; hdr: ile bajtow tresci zuzyl naglowek bloku */
static uint32_t legacy_rate(unsigned tc) { return 1000000u / (256u - tc); }
static uint32_t hdr_need(unsigned type) { switch (type) { case 1: return 2; case 3: return 3; case 4: return 2; case 8: return 4; case 9: return 12; default: return 0; } }
static int fmt_set(vfmt_t *f, uint32_t rate, uint32_t channels) {
    if (rate < 1000u || rate > 192000u || channels < 1u || channels > 2u) return 0;
    if (f->rate && (f->rate != rate || f->channels != channels)) return 0;
    f->rate = rate; f->channels = channels;
    return 1;
}
/* b: pierwsze hdr_need(type) bajtow tresci (gdy length >= hdr_need) */
static maktabasauti_result_t parse_block(vfmt_t *f, unsigned type, uint32_t length, const uint8_t *b, vseg_t *g) {
    memset(g, 0, sizeof *g);
    switch (type) {
    case 1: {
        uint32_t rate, ch;
        if (length < 3u || b[1] != 0) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;                       /* tylko kodek 0 (PCM 8 bit) */
        rate = f->pending_ext ? f->ext_rate : legacy_rate(b[0]); ch = f->pending_ext ? f->ext_channels : 1u; f->pending_ext = 0;
        if (!fmt_set(f, rate, ch) || (length - 2u) % ch != 0u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        g->kind = 1; g->width = 1; g->frames = (length - 2u) / ch; g->hdr = 2; f->last_width = 1; f->last_channels = ch;
        return MAKTABASAUTI_OK;
    }
    case 2:
        if (!f->last_width || !f->last_channels || length % (f->last_width * f->last_channels) != 0u) return MAKTABASAUTI_E_CORRUPT_DATA;
        g->kind = 1; g->width = f->last_width; g->frames = length / (f->last_width * f->last_channels); g->hdr = 0;
        return MAKTABASAUTI_OK;
    case 3:
        if (length != 3u || !fmt_set(f, legacy_rate(b[2]), 1u)) return MAKTABASAUTI_E_CORRUPT_DATA;
        g->kind = 2; g->frames = (uint64_t)le16(b) + 1u; g->hdr = 3;
        return MAKTABASAUTI_OK;
    case 4: case 5:
        if (type == 4 && length != 2u) return MAKTABASAUTI_E_CORRUPT_DATA;
        g->kind = 0; g->hdr = type == 4 ? 2u : 0u;
        return MAKTABASAUTI_OK;
    case 8:
        if (length != 4u || b[2] != 0 || b[3] > 1u) return MAKTABASAUTI_E_CORRUPT_DATA;
        f->ext_channels = (uint32_t)b[3] + 1u; f->ext_rate = 256000000u / (65536u - (uint32_t)le16(b)) / f->ext_channels; f->pending_ext = 1;
        g->kind = 0; g->hdr = 4;
        return MAKTABASAUTI_OK;
    case 9: {
        uint32_t rate, ch; unsigned width;
        if (length < 13u) return MAKTABASAUTI_E_CORRUPT_DATA;
        rate = le32(b); ch = b[5];
        width = b[4] == 8u && le16(b + 6) == 0u ? 1u : b[4] == 16u && le16(b + 6) == 4u ? 2u : 0u;
        if (!width) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        if (!fmt_set(f, rate, ch) || (length - 12u) % (width * ch) != 0u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        g->kind = 1; g->width = width; g->frames = (length - 12u) / (width * ch); g->hdr = 12; f->last_width = width; f->last_channels = ch; f->pending_ext = 0;
        return MAKTABASAUTI_OK;
    }
    default: return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;                                                   /* powtorzenia (6/7), ADPCM, nieznane */
    }
}

typedef struct { uint64_t data_off, first, frames; unsigned width; int silence; } part_t;
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd; stage_t st;
    uint32_t channels; int seekable; int total_known; uint64_t total, pos;
    maktabasauti_result_t pend;
    /* tryb z indeksem (wejscie przewijalne) */
    part_t *parts; size_t nparts, cur; long staged;
    /* tryb strumieniowy (wejscie nieprzewijalne) */
    vfmt_t fmt; vseg_t seg; uint64_t seg_left; uint64_t skip_left; int ended;
} voc_t;

static int32_t MAKTABASAUTI_CALL voc_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 26u && memcmp(p, "Creative Voice File\x1a", 20) == 0 && le16(p + 20) >= 26u &&
            le16(p + 24) == (uint16_t)((~(unsigned)le16(p + 22) + 0x1234u) & 0xffffu)) ? 50 : 0;
}

static float conv(unsigned width, const uint8_t *v) {
    if (width == 1u) return ((float)v[0] - 128.0f) / 128.0f;
    return (float)(int16_t)le16(v) / 32768.0f;
}
static void emit(voc_t *v, float *o, const uint8_t *src, unsigned width, size_t frames) {
    size_t k, i;
    for (k = 0; k < frames; k++) for (i = 0; i < v->channels; i++) o[k * v->channels + i] = conv(width, src + (k * v->channels + i) * width);
}

/* ---- tryb strumieniowy: nastepny blok z bufora posredniego; OK = nowy segment w v->seg, EOF = koniec plikow (czysty), inny kod = blad (wejscie bez zmian po E_WOULD_BLOCK) ---- */
static maktabasauti_result_t stream_next(voc_t *v) {
    for (;;) {
        maktabasauti_result_t r; unsigned type; uint32_t length, need; vfmt_t f2; vseg_t g;
        if (v->skip_left) {                                                                          /* reszta poprzedniego bloku (znaczniki, nadmiar) */
            r = st_need(v->host, v->rd, &v->st, 1);
            if (r == MAKTABASAUTI_EOF) return MAKTABASAUTI_E_TRUNCATED;
            if (r != MAKTABASAUTI_OK) return r;
            { const size_t a = st_avail(&v->st); const size_t t = v->skip_left < a ? (size_t)v->skip_left : a; v->st.at += t; v->skip_left -= t; }
            continue;
        }
        if (v->ended) return MAKTABASAUTI_EOF;
        r = st_need(v->host, v->rd, &v->st, 1);
        if (r == MAKTABASAUTI_EOF) { v->ended = 1; return MAKTABASAUTI_EOF; }                       /* koniec pliku bez bloku konca: jak w dawnym adapterze */
        if (r != MAKTABASAUTI_OK) return r;
        type = v->st.buf[v->st.at];
        if (type == 0u) { v->ended = 1; return MAKTABASAUTI_EOF; }
        r = st_need(v->host, v->rd, &v->st, 4);
        if (r == MAKTABASAUTI_EOF) return MAKTABASAUTI_E_TRUNCATED;
        if (r != MAKTABASAUTI_OK) return r;
        length = (uint32_t)v->st.buf[v->st.at + 1] | ((uint32_t)v->st.buf[v->st.at + 2] << 8) | ((uint32_t)v->st.buf[v->st.at + 3] << 16);
        need = hdr_need(type); if (length < need) need = 0;
        if (need) {
            r = st_need(v->host, v->rd, &v->st, 4u + need);
            if (r == MAKTABASAUTI_EOF) return MAKTABASAUTI_E_TRUNCATED;
            if (r != MAKTABASAUTI_OK) return r;
        }
        f2 = v->fmt;
        r = parse_block(&f2, type, length, v->st.buf + v->st.at + 4, &g);
        if (r != MAKTABASAUTI_OK) return r;
        if (v->channels && f2.channels && f2.channels != v->channels) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        v->fmt = f2; v->st.at += 4u + need;
        v->seg = g; v->seg_left = g.frames;
        v->skip_left = g.kind == 0 ? (uint64_t)length - g.hdr : 0u;
        if (g.kind != 0) return MAKTABASAUTI_OK;
    }
}

static maktabasauti_result_t MAKTABASAUTI_CALL voc_read(void *state, void *dst, size_t n, size_t *frames) {
    voc_t *v = (voc_t *)state; float *o = (float *)dst; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (v->pend != MAKTABASAUTI_OK) return v->pend;
    while (done < n) {
        size_t want = n - done;
        if (v->seekable) {
            part_t *pt;
            if (v->pos >= v->total) { r = MAKTABASAUTI_EOF; break; }
            while (v->cur < v->nparts && v->pos >= v->parts[v->cur].first + v->parts[v->cur].frames) v->cur++;
            pt = &v->parts[v->cur];
            if ((uint64_t)want > pt->first + pt->frames - v->pos) want = (size_t)(pt->first + pt->frames - v->pos);
            if (pt->silence) { memset(o + done * v->channels, 0, want * v->channels * sizeof(float)); v->pos += want; done += want; continue; }
            if (v->staged != (long)v->cur) {                                                           /* wejscie nie stoi na danych tej czesci */
                const uint64_t off = pt->data_off + (v->pos - pt->first) * v->channels * pt->width;
                if (v->host->seek(v->host->user, v->rd, (int64_t)off, 0) < 0) { r = MAKTABASAUTI_E_NOT_SEEKABLE; break; }
                st_clear(&v->st); v->staged = (long)v->cur;
            }
            {   const size_t fb = (size_t)v->channels * pt->width; size_t avail_f;
                if (want > STAGE_BYTES / fb) want = STAGE_BYTES / fb;
                r = st_need(v->host, v->rd, &v->st, fb);
                if (r == MAKTABASAUTI_EOF) { r = MAKTABASAUTI_E_TRUNCATED; break; }
                if (r != MAKTABASAUTI_OK) break;
                avail_f = st_avail(&v->st) / fb; if (want > avail_f) want = avail_f;
                emit(v, o + done * v->channels, v->st.buf + v->st.at, pt->width, want);
                v->st.at += want * fb; v->pos += want; done += want; r = MAKTABASAUTI_OK;
            }
        } else {
            if (v->seg_left == 0) {
                r = stream_next(v);
                if (r != MAKTABASAUTI_OK) break;
                continue;
            }
            if ((uint64_t)want > v->seg_left) want = (size_t)v->seg_left;
            if (v->seg.kind == 2) { memset(o + done * v->channels, 0, want * v->channels * sizeof(float)); v->seg_left -= want; v->pos += want; done += want; continue; }
            {   const size_t fb = (size_t)v->channels * v->seg.width; size_t avail_f;
                if (want > STAGE_BYTES / fb) want = STAGE_BYTES / fb;
                r = st_need(v->host, v->rd, &v->st, fb);
                if (r == MAKTABASAUTI_EOF) { r = MAKTABASAUTI_E_TRUNCATED; break; }
                if (r != MAKTABASAUTI_OK) break;
                avail_f = st_avail(&v->st) / fb; if (want > avail_f) want = avail_f;
                emit(v, o + done * v->channels, v->st.buf + v->st.at, v->seg.width, want);
                v->st.at += want * fb; v->seg_left -= want; v->pos += want; done += want; r = MAKTABASAUTI_OK;
            }
        }
    }
    *frames = done;
    if (r != MAKTABASAUTI_OK && r != MAKTABASAUTI_EOF && r != MAKTABASAUTI_E_WOULD_BLOCK) {                 /* blad lepki; ramki sprzed bledu wydajemy najpierw */
        if (done) { v->pend = r; return MAKTABASAUTI_OK; }
        v->pend = r; return r;
    }
    if (done) return MAKTABASAUTI_OK;
    return r;
}
static maktabasauti_result_t MAKTABASAUTI_CALL voc_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    voc_t *v = (voc_t *)state; (void)flags;
    if (!v->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (frame > v->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    v->pos = frame; v->cur = 0; v->staged = -1; st_clear(&v->st); v->pend = MAKTABASAUTI_OK;
    *actual = frame;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL voc_reset(void *state) { uint64_t a; return voc_seek(state, 0, 0, &a); }
static maktabasauti_result_t MAKTABASAUTI_CALL voc_flush(void *state) { ((voc_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL voc_last_error(const void *state) { (void)state; return NULL; }
static void MAKTABASAUTI_CALL voc_close(void *state) {
    voc_t *v = (voc_t *)state;
    if (!v) return;
    if (v->parts) v->host->free(v->host->user, v->parts);
    v->host->free(v->host->user, v);
}

static maktabasauti_result_t add_part(const maktabasauti_host_t *h, voc_t *v, size_t *cap, const part_t *p) {
    if (v->nparts >= MAX_PARTS) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;                               /* jak w dawnym adapterze: najwyzej 16384 czesci */
    if (v->nparts == *cap) {
        const size_t next = *cap ? *cap * 2u : 8u; part_t *np = (part_t *)h->alloc(h->user, next * sizeof *np);
        if (!np) return MAKTABASAUTI_E_NO_MEMORY;
        if (v->nparts) memcpy(np, v->parts, v->nparts * sizeof *np);
        if (v->parts) h->free(h->user, v->parts);
        v->parts = np; *cap = next;
    }
    v->parts[v->nparts++] = *p;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL voc_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    scan_t s; uint8_t hd[26], bh[16] = { 0 }; voc_t *v = NULL; maktabasauti_result_t r; vfmt_t fmt; int have_first = 0; uint64_t cursor, first_next = 0, frames_total = 0; size_t cap = 0; unsigned blocks = 0;
    int64_t n; uint32_t fl;
    (void)self; *state = NULL;
    memset(&fmt, 0, sizeof fmt);
    fl = h->input_flags ? h->input_flags(h->user, rd) : 0u;
    memset(s.pre, 0, sizeof s.pre);
    s.h = h; s.rd = rd; s.pos = 0; s.seekable = (fl & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
    n = h->peek(h->user, rd, s.pre, sizeof s.pre);
    if (n < 0) return (maktabasauti_result_t)n;
    s.pre_n = (size_t)n;
    if (s.pre_n < sizeof hd) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    memcpy(hd, s.pre, sizeof hd);
    if (!voc_probe(NULL, hd, sizeof hd, NULL)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    cursor = le16(hd + 20);
    v = (voc_t *)h->alloc(h->user, sizeof *v);
    if (!v) return MAKTABASAUTI_E_NO_MEMORY;
    memset(v, 0, sizeof *v);
    v->host = h; v->rd = rd; v->seekable = s.seekable; v->staged = -1;
    for (;;) {
        uint8_t type; uint32_t length, need; vseg_t g; uint64_t body, next;
        if (blocks++ >= MAX_BLOCKS) { r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto fail; }
        r = sc_fetch(&s, cursor, bh, 1);
        if (r == MAKTABASAUTI_EOF) break;                                                            /* koniec wejscia bez bloku konca */
        if (r != MAKTABASAUTI_OK) goto fail;
        type = bh[0];
        if (type == 0u) break;
        r = sc_fetch(&s, cursor + 1u, bh + 1, 3);
        if (r == MAKTABASAUTI_EOF) break;                                                            /* urwany naglowek bloku: koniec (plik ucięty) */
        if (r != MAKTABASAUTI_OK) goto fail;
        length = (uint32_t)bh[1] | ((uint32_t)bh[2] << 8) | ((uint32_t)bh[3] << 16);
        body = cursor + 4u; next = body + length;
        need = hdr_need(type); if (length < need) need = 0;
        if (need) {
            r = sc_fetch(&s, body, bh + 4, need);
            if (r == MAKTABASAUTI_EOF) break;
            if (r != MAKTABASAUTI_OK) goto fail;
        }
        r = parse_block(&fmt, type, length, bh + 4, &g);
        if (r != MAKTABASAUTI_OK) goto fail;
        if (g.kind != 0) {
            if (s.seekable) {
                part_t p; p.data_off = body + g.hdr; p.first = frames_total; p.frames = g.frames; p.width = g.width; p.silence = g.kind == 2;
                if (g.frames) {
                    if (frames_total > UINT64_MAX - g.frames) { r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto fail; }
                    r = add_part(h, v, &cap, &p);
                    if (r != MAKTABASAUTI_OK) goto fail;
                    frames_total += g.frames;
                }
            } else if (!have_first) {
                have_first = 1; first_next = body + g.hdr;
                v->fmt = fmt; v->seg = g; v->seg_left = g.frames; v->skip_left = 0;
                break;
            }
        }
        cursor = next;
    }
    if (s.seekable ? frames_total == 0u : !have_first) { r = MAKTABASAUTI_E_UNSUPPORTED_FORMAT; goto fail; }
    if (!fmt.rate) { r = MAKTABASAUTI_E_UNSUPPORTED_FORMAT; goto fail; }
    v->channels = fmt.channels;
    if (s.seekable) { v->total = frames_total; v->total_known = 1; }
    else { r = sc_move(&s, first_next); if (r != MAKTABASAUTI_OK) { if (r == MAKTABASAUTI_EOF) r = MAKTABASAUTI_E_UNSUPPORTED_FORMAT; goto fail; } }
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = fmt.rate; info->channels = fmt.channels; info->channel_mask = default_mask(fmt.channels);
    info->native_format = MAKTABASAUTI_FORMAT_F32; info->bits_per_sample = 0;
    info->total_frames = v->total_known ? v->total : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = (s.seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u) | MAKTABASAUTI_STREAM_LOSSLESS;
    strncpy(info->codec_name, "pcm", sizeof info->codec_name - 1u);
    strncpy(info->container_name, "voc", sizeof info->container_name - 1u);
    *state = v;
    return MAKTABASAUTI_OK;
fail:
    if (v) { if (v->parts) h->free(h->user, v->parts); h->free(h->user, v); }
    return r == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_UNSUPPORTED_FORMAT : r;
}

const maktabasauti_codec_t maktabasauti_voc_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "creative-voc-pcm", "Creative VOC PCM (streaming)", "maktabasauti_voc",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    voc_probe, voc_open, voc_read, voc_seek, voc_reset, voc_flush, NULL, voc_last_error, NULL, voc_close, { NULL }, { NULL }
};
