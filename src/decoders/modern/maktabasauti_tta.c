/* MaktabaSauti, modul maktabasauti_tta: The True Audio (TTA 1.x) lossless, plik `.tta`, napisany od zera w czystym C.
 *
 * Plik: opcjonalny znacznik ID3v2, naglowek 22 B (little endian: "TTA1", format 1 = zwykly / 2 = szyfrowany, kanaly, glebia, czestotliwosc, liczba probek, CRC32), tabela rozmiarow ramek
 * (jedno u32 na ramke + CRC32), potem ramki jedna za druga; kazda ramka konczy sie CRC32.  Ramka ma 256 * czestotliwosc / 245 probek na kanal (ostatnia krotsza).  Dane ramki (bity od
 * najmlodszego): na kazda probke i kanal kod Rice'a z dwoma adaptacyjnymi parametrami (jedynki prefiksu wybieraja k0 albo k1), odwzorowanie parzysty / nieparzysty na znak, filtr
 * adaptacyjny znak-znak (8 wspolczynnikow, przesuniecie zalezne od glebi), predyktor pierwszego rzedu, a przy wielu kanalach dekorelacja (ostatni kanal + polowa poprzedniego, reszta przez
 * roznice).  Ramki sa niezalezne (stan zerowany na poczatku kazdej), wiec seek jest dokladny.  Brak sprawdzania CRC (jak domyslnie w FFmpeg): uszkodzona ramka daje bledne probki, a nie
 * blad, o ile strumien bitow da sie odczytac.  Wyjscie f32 = probka / 2^(8 * bajty - 1), gdzie bajty = (glebia + 7) / 8 (8 bit: ze znakiem po odjeciu 128, 24 bit: wartosc 24 bitowa).
 * Wejscie moze byc nieprzewijalne (ramki leza kolejno); seek wymaga wejscia przewijalnego.
 * Nieobslugiwane (E_UNSUPPORTED_FEATURE): format 2 (szyfrowany), glebia powyzej 24 bit. */
#include "maktabasauti/maktabasauti_codec.h"
#include <stdint.h>
#include <string.h>

#define HDR 22u
#define CH_MAX 16u
#define FRAMES_MAX (1u << 22)
#define PCM_MAX ((size_t)8u << 20)                                                /* probek (ramka x kanaly) w buforze: 32 MiB */
#define FRAME_BYTES_MAX ((size_t)64 << 20)

typedef struct { uint32_t k0, k1, sum0, sum1; } rice_t;
typedef struct { uint32_t qm[8]; int32_t dx[8], dl[8], error, round; unsigned shift; } filt_t;
typedef struct { rice_t r; filt_t f; int32_t pred; } chan_t;

typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd;
    unsigned ch, bits, bps; uint32_t rate, data_len, frame_len, last_len, nframes; int seekable;
    uint32_t *sizes; uint64_t *offs; uint64_t io_pos;
    uint8_t *buf; size_t buf_cap; int32_t *pcm; chan_t chan[CH_MAX];
    uint32_t next_frame, pos, frame_samples; uint64_t skip;
    maktabasauti_result_t pend;
} tta_t;

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }

/* ---- czytnik bitow od najmlodszego ---- */
typedef struct { const uint8_t *p; size_t nbits, pos; int over; } br_t;
static uint32_t br_get(br_t *b, unsigned n) {
    size_t byte = b->pos >> 3; unsigned sh = (unsigned)(b->pos & 7u), i; uint64_t acc = 0; const size_t nbytes = b->nbits >> 3;
    if (n == 0) return 0;
    if (b->pos + n > b->nbits) { b->over = 1; b->pos = b->nbits; return 0; }
    for (i = 0; i < 5u; i++) if (byte + i < nbytes) acc |= (uint64_t)b->p[byte + i] << (8u * i);
    b->pos += n;
    return (uint32_t)((acc >> sh) & ((1ull << n) - 1u));
}
/* liczba jedynek do pierwszego zera (zero zuzyte); na koncu danych: over */
static uint32_t br_unary(br_t *b) {
    uint32_t n = 0;
    for (;;) {
        if (b->pos >= b->nbits) { b->over = 1; return n; }
        if (!((b->p[b->pos >> 3] >> (b->pos & 7u)) & 1u)) { b->pos++; return n; }
        b->pos++; n++;
    }
}

/* ---- parametry Rice'a: tabela przesuniec 2^i do 2^31, potem 0x80000000, na koncu 0xFFFFFFFF ---- */
static uint32_t shift_1(unsigned i) { return i < 32u ? (1u << i) : (i < 40u ? 0x80000000u : 0xFFFFFFFFu); }
static uint32_t shift_16(unsigned i) { return shift_1(i + 4u); }
static void rice_init(rice_t *r) { r->k0 = r->k1 = 10u; r->sum0 = r->sum1 = shift_16(10u); }
static void rice_adapt(uint32_t *k, uint32_t *sum, uint32_t value) {
    *sum += value - (*sum >> 4);
    if (*k > 0 && *sum < shift_16(*k)) (*k)--;
    else if (*k < 36u && *sum > shift_16(*k + 1u)) (*k)++;
}

/* ---- filtr adaptacyjny znak-znak: 8 wspolczynnikow, uklad rejestrow dx / dl ---- */
static void filt_init(filt_t *f, unsigned shift) { memset(f, 0, sizeof *f); f->shift = shift; f->round = (int32_t)(1u << (shift - 1u)); }
static int32_t filt_run(filt_t *f, int32_t in) {
    uint32_t sum = (uint32_t)f->round; unsigned i;
    if (f->error < 0) for (i = 0; i < 8u; i++) f->qm[i] -= (uint32_t)f->dx[i];
    else if (f->error > 0) for (i = 0; i < 8u; i++) f->qm[i] += (uint32_t)f->dx[i];
    for (i = 0; i < 8u; i++) sum += (uint32_t)f->dl[i] * f->qm[i];
    for (i = 0; i < 4u; i++) { f->dx[i] = f->dx[i + 1u]; f->dl[i] = f->dl[i + 1u]; }
    f->dx[4] = (int32_t)(((uint32_t)(f->dl[4] >> 30)) | 1u);
    f->dx[5] = (int32_t)((((uint32_t)(f->dl[5] >> 30)) | 2u) & ~1u);
    f->dx[6] = (int32_t)((((uint32_t)(f->dl[6] >> 30)) | 2u) & ~1u);
    f->dx[7] = (int32_t)((((uint32_t)(f->dl[7] >> 30)) | 4u) & ~3u);
    f->error = in;
    in = (int32_t)((uint32_t)in + (uint32_t)((int32_t)sum >> f->shift));
    f->dl[4] = (int32_t)(0u - (uint32_t)f->dl[5]); f->dl[5] = (int32_t)(0u - (uint32_t)f->dl[6]);
    f->dl[6] = (int32_t)((uint32_t)in - (uint32_t)f->dl[7]); f->dl[7] = in;
    f->dl[5] = (int32_t)((uint32_t)f->dl[5] + (uint32_t)f->dl[6]); f->dl[4] = (int32_t)((uint32_t)f->dl[4] + (uint32_t)f->dl[5]);
    return in;
}

static int32_t pred_term(int32_t x, unsigned k) { return (int32_t)(uint32_t)((((uint64_t)(int64_t)x << k) - (uint64_t)(int64_t)x) >> k); }

/* ---- ramka: dekoduje `samples` probek na kanal do s->pcm (przeplecione); 0 = ok, -1 = blad strumienia bitow ---- */
static int decode_frame(tta_t *s, const uint8_t *p, size_t n, uint32_t samples) {
    static const unsigned shifts[3] = { 10u, 9u, 10u };                           /* przesuniecie filtra dla 8 / 16 / 24 bit */
    br_t b; unsigned c; uint32_t i;
    if (n < 5u) return -1;
    b.p = p; b.nbits = (n - 4u) * 8u; b.pos = 0; b.over = 0;                      /* ostatnie 4 B to CRC32 ramki */
    for (c = 0; c < s->ch; c++) { rice_init(&s->chan[c].r); filt_init(&s->chan[c].f, shifts[s->bps - 1u]); s->chan[c].pred = 0; }
    for (i = 0; i < samples; i++) {
        int32_t *row = s->pcm + (size_t)i * s->ch;
        for (c = 0; c < s->ch; c++) {
            chan_t *ch = &s->chan[c]; uint32_t unary = br_unary(&b), k, value; int depth; int32_t v;
            if (b.over) return -1;
            if (unary == 0) { depth = 0; k = ch->r.k0; } else { depth = 1; k = ch->r.k1; unary--; }
            if (k > 31u || (k && unary > (0x7FFFFFFFu >> k))) return -1;
            value = k ? (unary << k) + br_get(&b, k) : unary;
            if (b.over) return -1;
            if (depth) { rice_adapt(&ch->r.k1, &ch->r.sum1, value); value += shift_1(ch->r.k0); }
            rice_adapt(&ch->r.k0, &ch->r.sum0, value);
            v = (int32_t)(1u + ((value >> 1) ^ ((value & 1u) - 1u)));               /* nieparzyste: (v + 1) / 2, parzyste: -(v / 2) */
            v = filt_run(&ch->f, v);
            if (s->bps != 4u) v = (int32_t)((uint32_t)v + (uint32_t)pred_term(ch->pred, s->bps == 1u ? 4u : 5u));          /* predyktor rzedu pierwszego: x * (2^k - 1) / 2^k */
            ch->pred = row[c] = v;
        }
        if (s->ch > 1u) {                                                           /* dekorelacja: ostatni kanal += polowa poprzedniego, reszta wstecz przez roznice */
            int r;
            row[s->ch - 1u] = (int32_t)((uint32_t)row[s->ch - 1u] + (uint32_t)(row[s->ch - 2u] / 2));
            for (r = (int)s->ch - 2; r >= 0; r--) row[r] = (int32_t)((uint32_t)row[r + 1] - (uint32_t)row[r]);
        }
    }
    return 0;
}

static void tta_free(tta_t *s) {
    const maktabasauti_host_t *h = s->host;
    if (s->sizes) h->free(h->user, s->sizes);
    if (s->offs) h->free(h->user, s->offs);
    if (s->buf) h->free(h->user, s->buf);
    if (s->pcm) h->free(h->user, s->pcm);
    h->free(h->user, s);
}

/* znacznik ID3v2 na poczatku: pelna dlugosc (naglowek 10 B + dane + opcjonalna stopka) albo 0 */
static size_t id3v2_len(const uint8_t *p, size_t n) {
    if (n < 10u || memcmp(p, "ID3", 3) != 0 || p[3] == 0xFFu || p[4] == 0xFFu || ((p[6] | p[7] | p[8] | p[9]) & 0x80u)) return 0;
    return 10u + (((size_t)p[6] << 21) | ((size_t)p[7] << 14) | ((size_t)p[8] << 7) | p[9]) + ((p[5] & 0x10u) ? 10u : 0u);
}

static int32_t MAKTABASAUTI_CALL tta_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    size_t t;
    (void)self; (void)hint;
    if (!p || n < 4u) return 0;
    if (memcmp(p, "TTA1", 4) == 0) return 95;
    t = id3v2_len(p, n);
    if (t && t + 4u <= n && memcmp(p + t, "TTA1", 4) == 0) return 95;
    return 0;
}

static maktabasauti_result_t read_exact(const maktabasauti_host_t *h, maktabasauti_reader_t *rd, void *dst, size_t n) {
    size_t got = 0;
    while (got < n) {
        const int64_t m = h->read(h->user, rd, (uint8_t *)dst + got, n - got);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_TRUNCATED;
        got += (size_t)m;
    }
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL tta_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd,
                                                       maktabasauti_stream_info_t *info, void **state) {
    uint8_t hd[HDR], tmp[256]; tta_t *s; maktabasauti_result_t r; uint64_t pos = 0, total; size_t tag; unsigned fmt; uint32_t i; uint64_t rate64, fl; int64_t fsize;
    (void)self; *state = NULL;
    if (!h || !info || !state || !h->alloc || !h->free || !h->read || !h->peek) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    { const int64_t m = h->peek(h->user, rd, tmp, 16u); if (m < 0) return (maktabasauti_result_t)m; tag = id3v2_len(tmp, (size_t)m); }
    if (h->peek(h->user, rd, hd, 4u) < 4) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (!tag && memcmp(hd, "TTA1", 4) != 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    while (pos < tag) {                                                             /* przeskocz znacznik ID3v2 */
        size_t want = (size_t)(tag - pos); int64_t m;
        if (want > sizeof tmp) want = sizeof tmp;
        m = h->read(h->user, rd, tmp, want);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        pos += (uint64_t)m;
    }
    r = read_exact(h, rd, hd, HDR);
    if (r != MAKTABASAUTI_OK) return r == MAKTABASAUTI_E_TRUNCATED ? MAKTABASAUTI_E_UNSUPPORTED_FORMAT : r;
    pos += HDR;
    if (memcmp(hd, "TTA1", 4) != 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    fmt = le16(hd + 4);
    if (fmt != 1u && fmt != 2u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (fmt == 2u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;                       /* szyfrowany */
    rate64 = le32(hd + 10);
    if (le16(hd + 6) < 1u || le16(hd + 6) > CH_MAX || le16(hd + 8) < 1u || rate64 == 0 || rate64 > 1000000u || le32(hd + 14) == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (le16(hd + 8) > 24u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    fl = 256u * rate64 / 245u;
    if (fl == 0 || fl * le16(hd + 6) > PCM_MAX) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    s = (tta_t *)h->alloc(h->user, sizeof *s);
    if (!s) return MAKTABASAUTI_E_NO_MEMORY;
    memset(s, 0, sizeof *s);
    s->host = h; s->rd = rd; s->ch = le16(hd + 6); s->bits = le16(hd + 8); s->bps = (s->bits + 7u) / 8u; s->rate = (uint32_t)rate64; s->data_len = le32(hd + 14);
    s->frame_len = (uint32_t)fl; s->last_len = s->data_len % s->frame_len;
    total = (uint64_t)s->data_len / s->frame_len + (s->last_len ? 1u : 0u);
    if (total == 0 || total > FRAMES_MAX) { tta_free(s); return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
    s->nframes = (uint32_t)total;
    fsize = h->size ? h->size(h->user, rd) : -1;
    if (fsize >= 0 && (uint64_t)fsize < pos + 4u * total + 4u) { tta_free(s); return MAKTABASAUTI_E_TRUNCATED; }
    s->sizes = (uint32_t *)h->alloc(h->user, (size_t)total * sizeof(uint32_t));
    s->offs = (uint64_t *)h->alloc(h->user, (size_t)total * sizeof(uint64_t));
    if (!s->sizes || !s->offs) { tta_free(s); return MAKTABASAUTI_E_NO_MEMORY; }
    for (i = 0; i < s->nframes; i++) {
        uint8_t w[4];
        r = read_exact(h, rd, w, 4u);
        if (r != MAKTABASAUTI_OK) { tta_free(s); return r; }
        s->sizes[i] = le32(w);
    }
    { uint8_t w[4]; r = read_exact(h, rd, w, 4u); if (r != MAKTABASAUTI_OK) { tta_free(s); return r; } }                 /* CRC32 tabeli */
    pos += 4u * total + 4u;
    {
        size_t maxsz = 0;
        for (i = 0; i < s->nframes; i++) {
            s->offs[i] = pos; pos += s->sizes[i];
            if (s->sizes[i] > maxsz) maxsz = s->sizes[i];
        }
        s->io_pos = s->offs[0];
        if (maxsz > FRAME_BYTES_MAX || maxsz > (size_t)s->frame_len * s->ch * 8u + 4096u) { tta_free(s); return MAKTABASAUTI_E_CORRUPT_DATA; }
        s->buf_cap = maxsz ? maxsz : 1u;
    }
    s->buf = (uint8_t *)h->alloc(h->user, s->buf_cap);
    s->pcm = (int32_t *)h->alloc(h->user, (size_t)s->frame_len * s->ch * sizeof(int32_t));
    if (!s->buf || !s->pcm) { tta_free(s); return MAKTABASAUTI_E_NO_MEMORY; }
    s->seekable = h->input_flags ? (h->input_flags(h->user, rd) & MAKTABASAUTI_INPUT_SEEKABLE) != 0 : 0;
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = s->rate; info->channels = s->ch;
    info->channel_mask = s->ch == 1u ? MAKTABASAUTI_LAYOUT_MONO : (s->ch == 2u ? MAKTABASAUTI_LAYOUT_STEREO : 0u);
    info->native_format = MAKTABASAUTI_FORMAT_F32; info->bits_per_sample = s->bits;
    info->total_frames = s->data_len;
    info->flags = (s->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u) | MAKTABASAUTI_STREAM_LOSSLESS;
    strncpy(info->codec_name, "tta", sizeof info->codec_name - 1u);
    strncpy(info->container_name, "tta", sizeof info->container_name - 1u);
    *state = s;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t load_frame(tta_t *s, uint32_t f) {
    const maktabasauti_host_t *h = s->host; maktabasauti_result_t r; const uint32_t samples = f + 1u == s->nframes && s->last_len ? s->last_len : s->frame_len;
    if (s->sizes[f] < 5u) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (s->io_pos != s->offs[f]) {
        if (!s->seekable || h->seek(h->user, s->rd, (int64_t)s->offs[f], 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
        s->io_pos = s->offs[f];
    }
    r = read_exact(h, s->rd, s->buf, s->sizes[f]);
    if (r != MAKTABASAUTI_OK) return r;
    s->io_pos += s->sizes[f];
    if (decode_frame(s, s->buf, s->sizes[f], samples) != 0) return MAKTABASAUTI_E_CORRUPT_DATA;
    s->frame_samples = samples;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL tta_read(void *state, void *dst, size_t n, size_t *frames) {
    tta_t *s = (tta_t *)state; float *o = (float *)dst; const unsigned ch = s->ch;
    size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (s->pend != MAKTABASAUTI_OK) return s->pend;
    while (done < n) {
        size_t take, i;
        if (s->pos >= s->frame_samples) {
            uint32_t f;
            if (s->next_frame >= s->nframes) { r = MAKTABASAUTI_EOF; break; }
            f = s->next_frame++; s->pos = 0; s->frame_samples = 0;
            r = load_frame(s, f);
            if (r != MAKTABASAUTI_OK) break;
            if (s->skip) { s->pos = s->skip < s->frame_samples ? (uint32_t)s->skip : s->frame_samples; s->skip = 0; }
            continue;
        }
        take = s->frame_samples - s->pos;
        if (take > n - done) take = n - done;
        for (i = 0; i < take * ch; i++) {
            const int32_t v = s->pcm[(size_t)s->pos * ch + i];                       /* jak w FFmpeg: 8 bit = u8 - 128, 16 bit = s16, 24 bit = wartosc * 256 w s32 */
            o[done * ch + i] = s->bps == 1u ? (float)(int8_t)(uint8_t)v * (1.0f / 128.0f) : s->bps == 2u ? (float)(int16_t)(uint16_t)v * (1.0f / 32768.0f) : (float)(int32_t)((uint32_t)v << 8) * (1.0f / 2147483648.0f);
        }
        s->pos += (uint32_t)take; done += take;
    }
    *frames = done;
    if (r != MAKTABASAUTI_OK && r != MAKTABASAUTI_EOF) s->pend = r;
    if (done) return MAKTABASAUTI_OK;
    return r;
}

static maktabasauti_result_t MAKTABASAUTI_CALL tta_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    tta_t *s = (tta_t *)state; (void)flags;
    if (!s->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (frame > s->data_len) return MAKTABASAUTI_E_OUT_OF_RANGE;
    if (frame == s->data_len) { s->next_frame = s->nframes; s->skip = 0; }
    else { s->next_frame = (uint32_t)(frame / s->frame_len); s->skip = frame % s->frame_len; }
    s->pos = s->frame_samples = 0; s->pend = MAKTABASAUTI_OK; *actual = frame;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL tta_reset(void *state) { uint64_t a; return tta_seek(state, 0, 0, &a); }
static maktabasauti_result_t MAKTABASAUTI_CALL tta_flush(void *state) { ((tta_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static void MAKTABASAUTI_CALL tta_close(void *state) { if (state) tta_free((tta_t *)state); }

const maktabasauti_codec_t maktabasauti_tta_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "tta", "The True Audio (TTA 1.x) lossless", "maktabasauti_tta",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0, NULL,
    tta_probe, tta_open, tta_read, tta_seek, tta_reset, tta_flush, NULL, NULL, NULL, tta_close, { NULL }, { NULL }
};
