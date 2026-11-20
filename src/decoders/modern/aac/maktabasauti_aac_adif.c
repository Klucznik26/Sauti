/* MaktabaSauti, modul maktabasauti_aac: kontener ADIF dla wspolnego silnika strumieniowego (maktabasauti_aac_engine.h).  ADIF = naglowek z jednym program_config_element(), potem
 * raw_data_block() jeden za drugim BEZ dlugosci i bez synchronizacji: granice blokow wyznacza parsowanie elementow (sauti_aac_adif_block_length), wiec blok jest pobierany z oknem
 * rosnacym (2 KiB, potem do 8 KiB; ramka ADTS ma <= 8184 B ladunku) i dopiero po wyznaczeniu dlugosci zuzywany.  Brak slow synchronizacji = brak resynchronizacji po uszkodzeniu:
 * uszkodzony blok konczy strumien (E_CORRUPT_DATA, potem koniec). Poczatek strumienia (naglowek) jest ponownie czytany po przewinieciu do poczatku. */
#include "maktabasauti_aac_engine.h"
#include "sauti_aac_adif.h"

#define ADIF_HEADER_PEEK 512u
#define ADIF_BLOCK_MAX 8184u

typedef struct {
    SautiAacAdifHeader hd;
    int have_header;
    uint8_t buf[ADIF_BLOCK_MAX + 64u];
    /* bufor roboczy parsera: kolejny blok pamieci za struktura (alokacja w jednym kawalku) */
    size_t scratch_size;
} adif_priv_t;
#define ADIF_SCRATCH(p) ((void *)((uint8_t *)(p) + ((sizeof(adif_priv_t) + 15u) & ~(size_t)15u)))

static adif_priv_t *priv_of(dec_t *d) {
    if (!d->cptr) {
        const size_t sc = sauti_aac_adif_scratch_size(), base = (sizeof(adif_priv_t) + 15u) & ~(size_t)15u;
        adif_priv_t *p = (adif_priv_t *)d->host->alloc(d->host->user, base + sc);
        if (!p) return NULL;
        memset(p, 0, base + sc);
        p->scratch_size = sc;
        d->cptr = p;
    }
    return (adif_priv_t *)d->cptr;
}

/* na poczatku strumienia: czyta i pomija naglowek (zuzywa go dopiero po sprawdzeniu) */
static maktabasauti_result_t read_header(dec_t *d, adif_priv_t *p) {
    const maktabasauti_host_t *h = d->host; int64_t n, m; uint8_t hdr[ADIF_HEADER_PEEK];
    n = h->peek(h->user, d->rd, hdr, sizeof hdr);
    if (n < 0) return (maktabasauti_result_t)n;
    if (n == 0) return MAKTABASAUTI_EOF;
    if (!sauti_aac_adif_parse_header(hdr, (size_t)n, &p->hd)) return n < (int64_t)sizeof hdr ? MAKTABASAUTI_E_UNSUPPORTED_FORMAT : MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    m = h->read(h->user, d->rd, hdr, p->hd.header_bytes);
    if (m < 0) return (maktabasauti_result_t)m;
    if ((size_t)m != p->hd.header_bytes) return MAKTABASAUTI_E_IO;
    p->have_header = 1;
    return MAKTABASAUTI_OK;
}

/* wyznacza dlugosc nastepnego bloku (peek z rosnacym oknem); *len = 0 gdy brak danych.  OK / EOF / E_TRUNCATED / E_CORRUPT_DATA / blad wejscia */
static maktabasauti_result_t next_block_len(dec_t *d, adif_priv_t *p, size_t *len) {
    static const size_t windows[] = { 2048u, ADIF_BLOCK_MAX + 16u };
    const maktabasauti_host_t *h = d->host; size_t k;
    *len = 0;
    for (k = 0; k < sizeof windows / sizeof *windows; k++) {
        int64_t n = h->peek(h->user, d->rd, p->buf, windows[k]); size_t bl;
        if (n < 0) return (maktabasauti_result_t)n;
        if (n == 0) return MAKTABASAUTI_EOF;
        bl = sauti_aac_adif_block_length(p->buf, (size_t)n, &p->hd, ADIF_SCRATCH(p));
        if (bl) { if (bl > ADIF_BLOCK_MAX) return MAKTABASAUTI_E_CORRUPT_DATA; *len = bl; return MAKTABASAUTI_OK; }
        if ((size_t)n < windows[k]) return MAKTABASAUTI_E_TRUNCATED;      /* okno obejmuje caly pozostaly material, a blok sie nie konczy: urwany (lub uszkodzony) */
    }
    return MAKTABASAUTI_E_CORRUPT_DATA;                                   /* blok dluzszy niz ramka ADTS */
}

static maktabasauti_result_t adif_fetch(dec_t *d) {
    const maktabasauti_host_t *h = d->host; adif_priv_t *p = priv_of(d); maktabasauti_result_t r; size_t len, l; int64_t m;
    if (!p) return MAKTABASAUTI_E_NO_MEMORY;
    if (d->core.frames_in == 0 && d->core.in_offset == 0) {               /* poczatek strumienia: naglowek */
        r = read_header(d, p);
        if (r != MAKTABASAUTI_OK) return r;
        d->core.in_offset = (int64_t)p->hd.header_bytes;                  /* tylko do ponownego wejscia po WOULD_BLOCK: naglowek juz zuzyty */
    }
    r = next_block_len(d, p, &len);
    if (r != MAKTABASAUTI_OK) return r;
    m = h->read(h->user, d->rd, d->frame + 7u, len);
    if (m < 0) return (maktabasauti_result_t)m;
    if ((size_t)m != len) return MAKTABASAUTI_E_IO;
    l = 7u + len;
    d->frame[0] = 0xff; d->frame[1] = 0xf1;
    d->frame[2] = (uint8_t)(0x40u | (p->hd.rate_index << 2) | ((p->hd.channels >> 2) & 1u));
    d->frame[3] = (uint8_t)(((p->hd.channels & 3u) << 6) | ((l >> 11) & 3u));
    d->frame[4] = (uint8_t)(l >> 3);
    d->frame[5] = (uint8_t)(((l & 7u) << 5) | 0x1fu);
    d->frame[6] = 0xfc;
    d->frame_len = l;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t adif_resync(dec_t *d) { (void)d; return MAKTABASAUTI_EOF; }   /* surowe bloki: brak slow synchronizacji */

static maktabasauti_result_t adif_scan(dec_t *d, int64_t resume, uint64_t *count, int *consistent) {
    const maktabasauti_host_t *h = d->host; adif_priv_t *p = priv_of(d); uint64_t c = 1;
    if (!p) return MAKTABASAUTI_E_NO_MEMORY;
    *consistent = 1;
    for (;;) {
        size_t len; maktabasauti_result_t r = next_block_len(d, p, &len); int64_t m;
        if (r == MAKTABASAUTI_E_WOULD_BLOCK || r == MAKTABASAUTI_E_IO) return r;
        if (r != MAKTABASAUTI_OK) break;                                  /* koniec, urwany lub uszkodzony ogon: liczymy do tego miejsca */
        m = h->read(h->user, d->rd, p->buf, len);
        if (m < 0) return (maktabasauti_result_t)m;
        c++;
    }
    if (h->seek(h->user, d->rd, resume, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    *count = c;
    return MAKTABASAUTI_OK;
}

static void adif_release(dec_t *d) { if (d->cptr) { d->host->free(d->host->user, d->cptr); d->cptr = NULL; } }

static const aac_container_t adif_container = { "adif", adif_fetch, adif_resync, adif_scan, NULL, NULL, adif_release, NULL, NULL, NULL, NULL };

static int32_t MAKTABASAUTI_CALL adif_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    SautiAacAdifHeader h; (void)self; (void)hint;
    if (!p || n < 8u || memcmp(p, "ADIF", 4) != 0) return 0;
    return sauti_aac_adif_parse_header(p, n, &h) ? 90 : 40;               /* etykieta bez obslugiwanego naglowka: open zdecyduje */
}
static maktabasauti_result_t MAKTABASAUTI_CALL adif_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    return aac_eng_open(self, &adif_container, host, rd, info, state);
}

const maktabasauti_codec_t maktabasauti_aac_adif_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "aac-adif", "AAC in ADIF (LC mono/stereo, HE-AAC via SBR in FIL), streaming", "maktabasauti_aac",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    adif_probe, adif_open, aac_eng_read, aac_eng_seek, aac_eng_reset, aac_eng_flush, NULL, aac_eng_last_error, aac_eng_get_info, aac_eng_close, { NULL }, { NULL }
};
const maktabasauti_codec_t *maktabasauti_aac_adif_codec(void) { return &maktabasauti_aac_adif_desc; }
