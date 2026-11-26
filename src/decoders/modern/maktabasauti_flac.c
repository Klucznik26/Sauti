/* MaktabaSauti, modul maktabasauti_flac: STRUMIENIOWY dekoder FLAC oparty na dr_flac (third_party/dr_libs/dr_flac.h, domena publiczna / MIT-0, bez zmian), z wywolaniami zwrotnymi czytajacymi przez host.
 *
 * Plik NIE jest wczytywany do pamieci: dr_flac dostaje dane z bufora posredniego, wiec wejscie moze byc nieprzewijalne, bez znanego rozmiaru i dowolnie dlugie.  Dekodowanie robi ta sama biblioteka co dawny adapter
 * (drflac_read_pcm_frames_f32), wiec PCM jest IDENTYCZNY bit w bit.  Pamiec zalezy od NAGLOWKA (STREAMINFO), nie od dlugosci pliku: bufor posredni ma ~2x najwiekszy mozliwy rozmiar ramki + 20 KiB (zwykle 40-100 KiB,
 * w najgorszym przypadku ~2 MiB), a dr_flac trzyma bufory ramki (max_blocksize x kanaly x 4 B).
 *
 * E_WOULD_BLOCK: dr_flac czyta synchronicznie (wywolanie zwrotne nie moze przerwac dekodowania), wiec przed dekodowaniem KAZDEJ nowej ramki modul upewnia sie, ze bufor ma wystarczajaco bajtow na cala ramke (albo wejscie sie
 * skonczylo); inaczej zwraca E_WOULD_BLOCK, nic nie tracac.  Konsekwencja: opoznienie rzedu jednej ramki, a ramka wieksza niz limit z naglowka (niezgodna z STREAMINFO) = E_CORRUPT_DATA.
 * Otwarcie: gdy naglowek (STREAMINFO + wszystkie bloki metadanych + naglowek pierwszej ramki) miesci sie w oknie czytnika (16 KiB), jest czytany przez peek i modul nie zuzywa z wejscia nic do udanego otwarcia (blad lub
 * E_WOULD_BLOCK nie psuja wejscia; czy ponowienie dziala dla wejscia nieprzewijalnego, rozstrzyga host: wejscie przewijalne jest cofane, nieprzewijalne nie ma gwarancji); dluzsze metadane (np. okladka) sa czytane przez
 * wywolania zwrotne wprost z wejscia, wiec po bledzie wejscie nieprzewijalne jest przesuniete.
 *
 * Roznice wobec dawnego adaptera: dlugosc 0 w STREAMINFO (zapis strumieniowy) jest przyjmowana (dlugosc nieznana; seek wylaczony), plik ucięty daje ramki do ucięcia i E_TRUNCATED, ramka wieksza niz limit z naglowka
 * albo uszkodzona = E_CORRUPT_DATA po wydanych ramkach.  Liczba kanalow jak dawniej: 1 lub 2. */
#include "maktabasauti/maktabasauti_codec.h"
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define DRFLAC_API static                         /* prywatna kopia: modul nie zalezy od implementacji w innym pliku (dawny adapter ma wlasna) */
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "../../../third_party/dr_libs/dr_flac.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#define PEEK_BYTES 16384u
#define STAGE_EXTRA (PEEK_BYTES + 4096u)
#define NEED_MIN 4096u
#define NEED_MAX (8u << 20)

typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd;
    uint8_t *buf; size_t cap, at, have, need;
    uint64_t in_pos;                              /* przesuniecie wejscia tuz za ostatnim bajtem w buforze posrednim */
    int64_t size;
    int seekable, can_seek, eof, blocked, fast, broken;
    drflac *f;
    maktabasauti_stream_info_t info;                                /* opis strumienia (get_info: rdzen sprawdza flage SEEKABLE przed przewinieciem wejscia) */
    uint32_t channels;
    uint64_t total, pos; int total_known;
    maktabasauti_result_t pend;
} flac_t;

static uint32_t be24(const uint8_t *p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }
/* the FLAC channel assignments of the format specification (3: L R C; 4: FL FR BL BR; 5: FL FR FC BL BR; 6: 5.1; 7: FL FR FC LFE BC SL SR; 8: 7.1), the same order as the WAVE mask */
static uint64_t default_mask(uint32_t ch) {
    const uint64_t lr = MAKTABASAUTI_CH_FL | MAKTABASAUTI_CH_FR;
    switch (ch) {
    case 1u: return MAKTABASAUTI_LAYOUT_MONO;
    case 2u: return MAKTABASAUTI_LAYOUT_STEREO;
    case 3u: return lr | MAKTABASAUTI_CH_FC;
    case 4u: return lr | MAKTABASAUTI_CH_BL | MAKTABASAUTI_CH_BR;
    case 5u: return lr | MAKTABASAUTI_CH_FC | MAKTABASAUTI_CH_BL | MAKTABASAUTI_CH_BR;
    case 6u: return MAKTABASAUTI_LAYOUT_5_1;
    case 7u: return lr | MAKTABASAUTI_CH_FC | MAKTABASAUTI_CH_LFE | MAKTABASAUTI_CH_BC | MAKTABASAUTI_CH_SL | MAKTABASAUTI_CH_SR;
    case 8u: return MAKTABASAUTI_LAYOUT_7_1;
    default: return 0u;
    }
}

/* ---- wywolania zwrotne dr_flac ---- */
static int64_t logical_pos(const flac_t *v) { return (int64_t)v->in_pos - (int64_t)(v->have - v->at); }
static int refill(flac_t *v) {                                    /* 1 = sa dane, 0 = brak (koniec wejscia, blokada albo blad; flagi ustawione) */
    int64_t m;
    if (v->fast || v->eof) return 0;                               /* tryb otwarcia z prefiksu: nie ruszamy wejscia; po koncu wejscia nie czytamy ponownie */
    v->at = v->have = 0;
    m = v->host->read(v->host->user, v->rd, v->buf, v->cap);
    if (m < 0) { v->blocked = 1; return 0; }
    if (m == 0) { v->eof = 1; return 0; }
    v->have = (size_t)m; v->in_pos += (uint64_t)m;
    return 1;
}
static size_t cb_read(void *u, void *out, size_t n) {
    flac_t *v = (flac_t *)u; uint8_t *o = (uint8_t *)out; size_t got = 0;
    while (got < n) {
        size_t t;
        if (v->at >= v->have && !refill(v)) break;
        t = v->have - v->at; if (t > n - got) t = n - got;
        memcpy(o + got, v->buf + v->at, t); v->at += t; got += t;
    }
    return got;
}
static drflac_bool32 cb_seek(void *u, int offset, drflac_seek_origin origin) {
    flac_t *v = (flac_t *)u; int64_t target, win_start;
    if (offset < 0) return DRFLAC_FALSE;
    if (origin == DRFLAC_SEEK_SET) target = offset; else if (origin == DRFLAC_SEEK_CUR) target = logical_pos(v) + offset; else return DRFLAC_FALSE;
    if (v->size >= 0 && target > v->size) return DRFLAC_FALSE;
    win_start = (int64_t)v->in_pos - (int64_t)v->have;
    if (target >= win_start && target <= (int64_t)v->in_pos) { v->at = (size_t)(target - win_start); return DRFLAC_TRUE; }
    if (v->fast) return DRFLAC_FALSE;
    if (v->seekable) {
        if (v->host->seek(v->host->user, v->rd, target, 0) < 0) return DRFLAC_FALSE;
        v->at = v->have = 0; v->in_pos = (uint64_t)target; v->eof = 0;
        return DRFLAC_TRUE;
    }
    if (target < (int64_t)v->in_pos) return DRFLAC_FALSE;            /* nieprzewijalne: tylko do przodu (odczyt z odrzuceniem) */
    v->at = v->have;
    while ((uint64_t)target > v->in_pos) {
        uint64_t left = (uint64_t)target - v->in_pos;
        if (!refill(v)) return DRFLAC_FALSE;
        if (left < v->have) { v->at = (size_t)left; break; }
        v->at = v->have;
    }
    return DRFLAC_TRUE;
}
static drflac_bool32 cb_tell(void *u, drflac_int64 *cursor) { *cursor = logical_pos((const flac_t *)u); return DRFLAC_TRUE; }
static void *cb_malloc(size_t sz, void *u) { const flac_t *v = (const flac_t *)u; return v->host->alloc(v->host->user, sz); }
static void cb_free(void *p, void *u) { const flac_t *v = (const flac_t *)u; if (p) v->host->free(v->host->user, p); }

static int32_t MAKTABASAUTI_CALL flac_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 4u && memcmp(p, "fLaC", 4) == 0) ? 50 : 0;
}

/* wyrownanie do ramek: ile bajtow bufora posredniego trzeba miec przed dekodowaniem nowej ramki */
static size_t need_bytes(uint32_t maxbs, uint32_t ch, uint32_t bps) {
    const uint64_t bs = maxbs ? maxbs : 65535u;
    const uint64_t bound = 18u + (uint64_t)ch * (8u + (bs * bps + 7u) / 8u);          /* ramka z samymi podramkami VERBATIM + naglowek + CRC (max_framesize z STREAMINFO jest pomijany: nie jest wiarygodny i pozwalalby wymusic duzy bufor) */
    uint64_t need = 2u * bound + 1024u + 4096u;                                          /* + odczyt z wyprzedzeniem dr_flac (jego bufor L2 ma 4 KiB) */
    if (need < NEED_MIN) need = NEED_MIN;
    if (need > NEED_MAX) need = NEED_MAX;
    return (size_t)need;
}

static maktabasauti_result_t gate(flac_t *v) {                    /* bufor ma >= need bajtow albo wejscie sie skonczylo */
    if (v->eof || v->have - v->at >= v->need) return MAKTABASAUTI_OK;
    if (v->at) { memmove(v->buf, v->buf + v->at, v->have - v->at); v->have -= v->at; v->at = 0; }
    while (v->have < v->need) {
        const int64_t m = v->host->read(v->host->user, v->rd, v->buf + v->have, v->cap - v->have);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) { v->eof = 1; break; }
        v->have += (size_t)m; v->in_pos += (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL flac_read(void *state, void *dst, size_t n, size_t *frames) {
    flac_t *v = (flac_t *)state; float *o = (float *)dst; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK, end = MAKTABASAUTI_OK; int failed = 0;
    *frames = 0;
    if (v->broken) return MAKTABASAUTI_E_IO;
    if (v->pend != MAKTABASAUTI_OK) return v->pend;
    while (done < n) {
        uint64_t want = n - done, got;
        if (v->total_known && v->pos >= v->total) { r = MAKTABASAUTI_EOF; break; }
        if (v->total_known && want > v->total - v->pos) want = v->total - v->pos;
        if (v->f->currentFLACFrame.pcmFramesRemaining == 0) {                                  /* nowa ramka: najpierw upewnij sie, ze jest w calosci w buforze */
            r = gate(v);
            if (r != MAKTABASAUTI_OK) break;
            want = 1;
        } else if (want > v->f->currentFLACFrame.pcmFramesRemaining) want = v->f->currentFLACFrame.pcmFramesRemaining;
        v->blocked = 0;
        got = drflac_read_pcm_frames_f32(v->f, want, o + done * v->channels);
        done += (size_t)got; v->pos += got;
        if (got < want) { failed = 1; break; }
        r = MAKTABASAUTI_OK;
    }
    if (failed) {                                                                                /* dr_flac nie dostarczyl ramki: koniec danych albo uszkodzenie */
        if (v->blocked) end = MAKTABASAUTI_E_CORRUPT_DATA;                                       /* ramka dluzsza niz limit z naglowka: bufor sie skonczyl, a wejscie dalej nie ma danych */
        else if (v->eof && v->at >= v->have) end = (v->total_known && v->pos < v->total) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF;
        else end = MAKTABASAUTI_E_CORRUPT_DATA;
        r = end;
    }
    *frames = done;
    if (r == MAKTABASAUTI_E_TRUNCATED || r == MAKTABASAUTI_E_CORRUPT_DATA) v->pend = r;          /* lepki */
    if (done) return MAKTABASAUTI_OK;
    return r;
}
static maktabasauti_result_t flac_goto(flac_t *v, uint64_t frame) {
    if (frame == v->total) { v->pos = frame; v->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }  /* koniec: dr_flac nie jest ruszany */
    v->blocked = 0;
    if (!drflac_seek_to_pcm_frame(v->f, frame)) { v->broken = 1; return v->blocked ? MAKTABASAUTI_E_WOULD_BLOCK : MAKTABASAUTI_E_IO; }
    v->broken = 0; v->pos = frame; v->pend = MAKTABASAUTI_OK;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL flac_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    flac_t *v = (flac_t *)state; maktabasauti_result_t r; (void)flags;
    if (!v->can_seek) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (frame > v->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    r = flac_goto(v, frame);
    if (r == MAKTABASAUTI_OK) *actual = frame;
    return r;
}
static maktabasauti_result_t MAKTABASAUTI_CALL flac_reset(void *state) { flac_t *v = (flac_t *)state; return v->can_seek ? flac_goto(v, 0) : MAKTABASAUTI_E_NOT_SEEKABLE; }
static maktabasauti_result_t MAKTABASAUTI_CALL flac_flush(void *state) { ((flac_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL flac_last_error(const void *state) { (void)state; return NULL; }
static maktabasauti_result_t MAKTABASAUTI_CALL flac_get_info(const void *state, maktabasauti_stream_info_t *info) { *info = ((const flac_t *)state)->info; return MAKTABASAUTI_OK; }
static void free_all(const maktabasauti_host_t *h, flac_t *v) {
    if (!v) return;
    if (v->f) drflac_close(v->f);
    if (v->buf) h->free(h->user, v->buf);
    h->free(h->user, v);
}
static void MAKTABASAUTI_CALL flac_close(void *state) { flac_t *v = (flac_t *)state; if (v) free_all(v->host, v); }

static maktabasauti_result_t MAKTABASAUTI_CALL flac_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    uint8_t si[42]; int64_t n; uint32_t fl, rate, ch, bps, maxbs; flac_t *v; drflac_allocation_callbacks ac; maktabasauti_result_t r; size_t need;
    (void)self; *state = NULL;
    n = h->peek(h->user, rd, si, sizeof si);
    if (n < 0) return (maktabasauti_result_t)n;
    if (n < (int64_t)sizeof si || memcmp(si, "fLaC", 4) != 0 || (si[4] & 0x7fu) != 0 || be24(si + 5) != 34u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;       /* pierwszy blok to STREAMINFO (34 B) */
    maxbs = ((uint32_t)si[10] << 8) | si[11];
    rate = ((uint32_t)si[18] << 12) | ((uint32_t)si[19] << 4) | (si[20] >> 4);
    ch = (uint32_t)((si[20] >> 1) & 7u) + 1u;
    bps = ((uint32_t)(si[20] & 1u) << 4 | (si[21] >> 4)) + 1u;
    if (rate == 0 || ch > 8u || bps < 4u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    need = need_bytes(maxbs, ch, bps);
    v = (flac_t *)h->alloc(h->user, sizeof *v);
    if (!v) return MAKTABASAUTI_E_NO_MEMORY;
    memset(v, 0, sizeof *v);
    v->host = h; v->rd = rd; v->need = need; v->cap = need + STAGE_EXTRA;
    v->buf = (uint8_t *)h->alloc(h->user, v->cap);
    if (!v->buf) { h->free(h->user, v); return MAKTABASAUTI_E_NO_MEMORY; }
    fl = h->input_flags ? h->input_flags(h->user, rd) : 0u;
    v->seekable = (fl & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
    v->size = (fl & MAKTABASAUTI_INPUT_SIZE_KNOWN) && h->size ? h->size(h->user, rd) : -1;
    /* prefiks: caly naglowek (metadane + pierwsza ramka) w oknie czytnika = otwarcie bez zuzycia wejscia */
    n = h->peek(h->user, rd, v->buf, PEEK_BYTES);
    if (n < 0) { free_all(h, v); return (maktabasauti_result_t)n; }
    {   uint64_t off = 4u; int last = 0; unsigned blocks = 0;
        while (!last && off + 4u <= (uint64_t)n && blocks++ < 65536u) { last = (v->buf[off] & 0x80u) != 0; off += 4u + be24(v->buf + off + 1); }
        v->fast = last && (off + 48u <= (uint64_t)n || (n < (int64_t)PEEK_BYTES && off <= (uint64_t)n));
    }
    if (v->fast) { v->have = (size_t)n; v->in_pos = (uint64_t)n; v->at = 0; }
    else { v->have = v->at = 0; v->in_pos = 0; }
    memset(&ac, 0, sizeof ac); ac.pUserData = v; ac.onMalloc = cb_malloc; ac.onFree = cb_free;
    v->f = drflac_open(cb_read, cb_seek, cb_tell, v, &ac);
    if (!v->f) { r = v->blocked ? MAKTABASAUTI_E_WOULD_BLOCK : MAKTABASAUTI_E_UNSUPPORTED_FORMAT; free_all(h, v); return r; }
    if (v->f->sampleRate == 0 || v->f->channels < 1 || v->f->channels > 8) { free_all(h, v); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
    if (v->fast) {                                                                                      /* zuzyj z wejscia dokladnie to, co dr_flac przeczytal z prefiksu; reszta bufora to kopia tego, co zostaje w wejsciu */
        size_t used = v->at;
        while (used) {
            uint8_t tmp[512]; const size_t w = used < sizeof tmp ? used : sizeof tmp; const int64_t m = h->read(h->user, rd, tmp, w);
            if (m <= 0) { free_all(h, v); return m < 0 ? (maktabasauti_result_t)m : MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
            used -= (size_t)m;
        }
        v->in_pos = v->at; v->have = v->at = 0; v->fast = 0; v->eof = 0;
    }
    v->channels = v->f->channels;
    v->total = v->f->totalPCMFrameCount; v->total_known = v->total != 0;
    v->can_seek = v->seekable && v->total_known;
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = v->f->sampleRate; info->channels = v->channels; info->channel_mask = default_mask(v->channels);
    info->native_format = MAKTABASAUTI_FORMAT_F32; info->bits_per_sample = v->f->bitsPerSample;
    info->total_frames = v->total_known ? v->total : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = (v->can_seek ? MAKTABASAUTI_STREAM_SEEKABLE : 0u) | MAKTABASAUTI_STREAM_LOSSLESS;
    strncpy(info->codec_name, "flac", sizeof info->codec_name - 1u);
    strncpy(info->container_name, "flac", sizeof info->container_name - 1u);
    v->info = *info;
    *state = v;
    return MAKTABASAUTI_OK;
}

const maktabasauti_codec_t maktabasauti_flac_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "flac-drflac", "FLAC (dr_flac, streaming)", "maktabasauti_flac",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    flac_probe, flac_open, flac_read, flac_seek, flac_reset, flac_flush, NULL, flac_last_error, flac_get_info, flac_close, { NULL }, { NULL }
};
