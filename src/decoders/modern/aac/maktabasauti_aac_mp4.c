/* MaktabaSauti, modul maktabasauti_aac: kontener MP4/M4A/M4B (ISO BMFF, takze fragmentowany) dla wspolnego silnika strumieniowego (maktabasauti_aac_engine.h).
 *
 * Plik jest INDEKSOWANY, nie wczytywany: przy otwarciu przegladane sa pudelka najwyzszego poziomu (naglowki; mdat jest pomijany), zawartosc `moov` i kolejnych `moof` jest czytana
 * pojedynczo, parsowana przez sauti_aac_mp4_index_* i zwalniana, a w pamieci zostaje tylko LISTA JEDNOSTEK DOSTEPU {pozycja, rozmiar} (16 B na probke, jak w kazdym demuxerze MP4).
 * Probka jest pobierana przez seek + read i owijana w ADTS; pozycja kontenera dla punktow kontrolnych to numer probki.  Przyciecie gapless (lista edycji / iTunSMPB) jak w starym adapterze
 * (silnik: trim_skip / avail).  Wejscie PRZEWIJALNE: indeks jak wyzej (dane probek leza gdziekolwiek w mdat).
 * Wejscie NIEPRZEWIJALNE (tryb NA ZYWO, np. fMP4 z sieci): pudelka sa czytane po kolei jednym przebiegiem; moov musi byc PRZED mdat (init-segment / faststart; moov na koncu: E_NOT_SEEKABLE),
 * kolejne moof sa indeksowane, gdy nadchodza, a indeks jest zapominany po zuzyciu fragmentu (pamiec rosnie z rozmiarem fragmentu, nie dlugoscia strumienia).  Probki musza lezec w
 * strumieniu w kolejnosci i za biezaca pozycja (inaczej E_UNSUPPORTED_FEATURE).  Kazde pobranie jest WZNAWIALNE po E_WOULD_BLOCK (zadnego cofania wejscia).  Dlugosc jest znana tylko
 * dla pliku bez fragmentow (liczba probek z moov); przewijanie: E_NOT_SEEKABLE.  Otwarcie (przejrzenie pudelek az do moov) nie jest wznawialne po E_WOULD_BLOCK (wejscia nieprzewijalnego
 * nie da sie cofnac): zrodlo na zywo ma dostarczyc ftyp+moov przed open().
 * Nieobslugiwane (E_UNSUPPORTED_FORMAT / FEATURE, nic nie zastepuje): brak sciezki AAC, ALAC i inne kodeki, czestotliwosc poza tablica ADTS, AU > 8184 B, uszkodzone tablice. */
#include "maktabasauti_aac_engine.h"
#include "sauti_aac_mp4.h"

#define MOOV_MAX ((size_t)128 << 20)
#define MOOF_MAX ((size_t)16 << 20)

typedef struct {
    SautiAacMp4Index *idx;
    SautiAacMp4Info mi;
    uint64_t pos;                                        /* numer nastepnej probki (w trybie na zywo: licznik od poczatku strumienia, indeks zawiera probki od `base`) */
    /* tryb NA ZYWO (wejscie nieprzewijalne): jedyny przebieg po strumieniu, caly stan wznawialny */
    int live, moov_seen, acc_active;
    uint64_t cur;                                        /* bezwzgledna pozycja nastepnego nieprzeczytanego bajtu */
    uint64_t boundary;                                   /* pozycja naglowka nastepnego nieprzeczytanego pudelka najwyzszego poziomu (UINT64_MAX = ostatnie pudelko bez konca) */
    uint64_t base;                                       /* numer probki odpowiadajacej wpisowi 0 indeksu */
    uint8_t *acc; size_t acc_have, acc_need, acc_cap;    /* zawartosc moov/moof w trakcie wczytywania */
    uint32_t acc_type; uint64_t acc_at;
    size_t got;                                          /* bajty biezacej probki juz w d->frame */
} mp4_priv_t;

#define FCC_MOOV 0x6d6f6f76u
#define FCC_MOOF 0x6d6f6f66u
#define FCC_MDAT 0x6d646174u

static void *a_alloc(void *u, size_t n) { const maktabasauti_host_t *h = (const maktabasauti_host_t *)u; return h->alloc(h->user, n); }
static void a_free(void *u, void *p) { const maktabasauti_host_t *h = (const maktabasauti_host_t *)u; h->free(h->user, p); }

/* czyta naglowek pudelka na pozycji `at`: *size = calkowity rozmiar (0 = do konca pliku), *type, *hdr = dlugosc naglowka.  OK / EOF / blad */
static maktabasauti_result_t box_header(dec_t *d, int64_t at, uint64_t *size, uint32_t *type, unsigned *hdr, int64_t file_size) {
    const maktabasauti_host_t *h = d->host; uint8_t b[16]; int64_t n;
    if (h->seek(h->user, d->rd, at, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    n = h->peek(h->user, d->rd, b, 16u);
    if (n < 0) return (maktabasauti_result_t)n;
    if (n < 8) return MAKTABASAUTI_EOF;
    *size = ((uint64_t)b[0] << 24) | ((uint64_t)b[1] << 16) | ((uint64_t)b[2] << 8) | b[3];
    *type = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) | b[7];
    *hdr = 8u;
    if (*size == 1u) {
        if (n < 16) return MAKTABASAUTI_E_TRUNCATED;
        *size = 0; { unsigned i; for (i = 0; i < 8u; i++) *size = (*size << 8) | b[8 + i]; }
        *hdr = 16u;
    } else if (*size == 0u) *size = file_size >= 0 ? (uint64_t)(file_size - at) : 0u;
    if (*size < *hdr) return MAKTABASAUTI_E_CORRUPT_DATA;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t read_payload(dec_t *d, int64_t at, uint64_t size, unsigned hdr, size_t max, uint8_t **out, size_t *n) {
    const maktabasauti_host_t *h = d->host; uint8_t *buf; size_t pn, got = 0;
    *out = NULL; *n = 0;
    if (size - hdr > max) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    pn = (size_t)(size - hdr);
    buf = (uint8_t *)h->alloc(h->user, pn ? pn : 1u);
    if (!buf) return MAKTABASAUTI_E_NO_MEMORY;
    if (h->seek(h->user, d->rd, at + (int64_t)hdr, 0) < 0) { h->free(h->user, buf); return MAKTABASAUTI_E_NOT_SEEKABLE; }
    while (got < pn) {
        int64_t m = h->read(h->user, d->rd, buf + got, pn - got);
        if (m < 0) { h->free(h->user, buf); return (maktabasauti_result_t)m; }
        if (m == 0) { h->free(h->user, buf); return MAKTABASAUTI_E_TRUNCATED; }
        got += (size_t)m;
    }
    *out = buf; *n = pn;
    return MAKTABASAUTI_OK;
}

/* ---- tryb NA ZYWO (wejscie nieprzewijalne) ---- */
static maktabasauti_result_t live_discard(dec_t *d, mp4_priv_t *p, uint64_t target) {
    const maktabasauti_host_t *h = d->host; uint8_t tmp[2048];
    while (p->cur < target) {
        uint64_t want = target - p->cur; int64_t m;
        if (want > sizeof tmp) want = sizeof tmp;
        m = h->read(h->user, d->rd, tmp, (size_t)want);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_EOF;
        p->cur += (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}
static void live_acc_free(dec_t *d, mp4_priv_t *p) {
    if (p->acc) d->host->free(d->host->user, p->acc);
    p->acc = NULL; p->acc_have = p->acc_need = p->acc_cap = 0; p->acc_active = 0;
}
/* wczytuje zawartosc moov/moof (rosnacy bufor: pamiec idzie za danymi, ktore naprawde przyszly, nie za deklarowanym rozmiarem) */
static maktabasauti_result_t live_pump(dec_t *d, mp4_priv_t *p) {
    const maktabasauti_host_t *h = d->host; SautiAacMp4Alloc al;
    while (p->acc_have < p->acc_need) {
        size_t want; int64_t m;
        if (p->acc_have == p->acc_cap) {
            size_t nc = p->acc_cap ? p->acc_cap * 2u : 65536u; uint8_t *nb;
            if (nc > p->acc_need) nc = p->acc_need;
            nb = (uint8_t *)h->alloc(h->user, nc);
            if (!nb) return MAKTABASAUTI_E_NO_MEMORY;
            if (p->acc_have) memcpy(nb, p->acc, p->acc_have);
            if (p->acc) h->free(h->user, p->acc);
            p->acc = nb; p->acc_cap = nc;
        }
        want = p->acc_cap - p->acc_have;
        m = h->read(h->user, d->rd, p->acc + p->acc_have, want);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_TRUNCATED;
        p->acc_have += (size_t)m; p->cur += (uint64_t)m;
    }
    al.user = (void *)h; al.alloc = a_alloc; al.free = a_free;
    if (p->acc_type == FCC_MOOV) {
        p->idx = sauti_aac_mp4_index_new(&al, p->acc, p->acc_need, &p->mi);
        live_acc_free(d, p);
        if (!p->idx) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        p->moov_seen = 1;
    } else {
        const int ok = sauti_aac_mp4_index_add_moof(p->idx, p->acc_at, p->acc, p->acc_need);
        live_acc_free(d, p);
        if (!ok) return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    return MAKTABASAUTI_OK;
}
/* przechodzi do nastepnego pudelka najwyzszego poziomu: moov i moof sa wczytywane (zawartosc), reszta jest tylko oznaczana jako pominiecie.  OK = postep, EOF = koniec strumienia */
static maktabasauti_result_t live_next_box(dec_t *d, mp4_priv_t *p) {
    const maktabasauti_host_t *h = d->host; uint8_t b[16]; uint64_t size, end, at; uint32_t type; unsigned hdr, got = 0; int64_t n; maktabasauti_result_t r;
    if (p->boundary == UINT64_MAX) return MAKTABASAUTI_EOF;
    r = live_discard(d, p, p->boundary);
    if (r != MAKTABASAUTI_OK) return r;                                                  /* EOF: urwane ostatnie pudelko = koniec (jak w starym demuxerze) */
    n = h->peek(h->user, d->rd, b, 16u);
    if (n < 0) return (maktabasauti_result_t)n;
    if (n < 8) return MAKTABASAUTI_EOF;                                                   /* smieci krotsze niz naglowek za ostatnim pudelkiem */
    size = ((uint64_t)b[0] << 24) | ((uint64_t)b[1] << 16) | ((uint64_t)b[2] << 8) | b[3];
    type = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) | b[7];
    hdr = 8u;
    if (size == 1u) {
        unsigned i;
        if (n < 16) return MAKTABASAUTI_EOF;
        size = 0; for (i = 0; i < 8u; i++) size = (size << 8) | b[8 + i];
        hdr = 16u;
    }
    if (size != 0 && size < hdr) return MAKTABASAUTI_E_CORRUPT_DATA;
    at = p->cur;
    if (size == 0) end = UINT64_MAX; else { if (at > UINT64_MAX - size) return MAKTABASAUTI_E_CORRUPT_DATA; end = at + size; }
    if (type == FCC_MDAT && !p->moov_seen) return MAKTABASAUTI_E_NOT_SEEKABLE;           /* dane przed moov: bez cofania nie da sie ich zdekodowac */
    while (got < hdr) {                                                                   /* naglowek jest w buforze czytnika po peek: bez wejscia */
        int64_t m = h->read(h->user, d->rd, b + got, hdr - got);
        if (m <= 0) return m < 0 ? (maktabasauti_result_t)m : MAKTABASAUTI_E_INTERNAL;
        got += (unsigned)m;
    }
    p->cur += hdr; p->boundary = end;
    if ((type == FCC_MOOV && !p->moov_seen) || (type == FCC_MOOF && p->moov_seen && sauti_aac_mp4_index_has_fragments(p->idx))) {
        const size_t max = type == FCC_MOOV ? MOOV_MAX : MOOF_MAX;
        if (size == 0 || size - hdr > max) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        p->acc_type = type; p->acc_at = at; p->acc_need = (size_t)(size - hdr); p->acc_have = 0; p->acc_cap = 0; p->acc = NULL; p->acc_active = 1;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t live_prepare(dec_t *d, mp4_priv_t *p) {
    const maktabasauti_host_t *h = d->host; int64_t t = h->tell(h->user, d->rd); maktabasauti_result_t r;
    p->live = 1; p->cur = t > 0 ? (uint64_t)t : 0u; p->boundary = p->cur;
    while (!p->moov_seen) {
        r = p->acc_active ? live_pump(d, p) : live_next_box(d, p);
        if (r == MAKTABASAUTI_EOF || r == MAKTABASAUTI_E_CORRUPT_DATA || r == MAKTABASAUTI_E_TRUNCATED) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        if (r != MAKTABASAUTI_OK) return r;
    }
    if (!sauti_aac_mp4_index_has_fragments(p->idx) && sauti_aac_mp4_index_count(p->idx) == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    return MAKTABASAUTI_OK;
}
static void wrap_adts(dec_t *d, const mp4_priv_t *p, uint32_t sz) {
    const size_t l = 7u + sz;
    if (p->mi.aot == 23u || p->mi.aot == 39u) { d->frame_len = aac_er_wrap(d->frame, p->mi.aot, p->mi.rate_index, p->mi.channel_config, p->mi.frame480, sz); return; }
    d->frame[0] = 0xff; d->frame[1] = 0xf1;
    d->frame[2] = (uint8_t)(0x40u | (p->mi.rate_index << 2) | ((p->mi.channel_config >> 2) & 1u));
    d->frame[3] = (uint8_t)(((p->mi.channel_config & 3u) << 6) | ((l >> 11) & 3u));
    d->frame[4] = (uint8_t)(l >> 3);
    d->frame[5] = (uint8_t)(((l & 7u) << 5) | 0x1fu);
    d->frame[6] = 0xfc;
    d->frame_len = l;
}
static maktabasauti_result_t live_fetch(dec_t *d, mp4_priv_t *p) {
    const maktabasauti_host_t *h = d->host;
    for (;;) {
        uint64_t off; uint32_t sz; maktabasauti_result_t r;
        if (p->acc_active) { r = live_pump(d, p); if (r != MAKTABASAUTI_OK) return r; continue; }
        if (p->pos - p->base >= sauti_aac_mp4_index_count(p->idx)) {                      /* indeks wyczerpany: potrzebny nastepny fragment */
            if (!sauti_aac_mp4_index_has_fragments(p->idx)) return MAKTABASAUTI_EOF;
            sauti_aac_mp4_index_forget_units(p->idx); p->base = p->pos;
            r = live_next_box(d, p);
            if (r != MAKTABASAUTI_OK) return r;
            continue;
        }
        (void)sauti_aac_mp4_index_get(p->idx, p->pos - p->base, &off, &sz);
        if (sz == 0u || sz > 8184u) return MAKTABASAUTI_E_CORRUPT_DATA;
        if (p->got == 0) {
            if (off < p->cur) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;                 /* dane przed biezaca pozycja: bez cofania nie do odczytania */
            if (off >= p->boundary && p->boundary != UINT64_MAX) {                        /* probka lezy w jeszcze nie przejrzanym pudelku (np. mdat po moof) */
                r = live_next_box(d, p);
                if (r == MAKTABASAUTI_EOF) return MAKTABASAUTI_E_TRUNCATED;
                if (r != MAKTABASAUTI_OK) return r;
                continue;
            }
            r = live_discard(d, p, off);
            if (r == MAKTABASAUTI_EOF) return MAKTABASAUTI_E_TRUNCATED;
            if (r != MAKTABASAUTI_OK) return r;
        }
        while (p->got < sz) {
            int64_t m = h->read(h->user, d->rd, d->frame + 7u + p->got, sz - p->got);
            if (m < 0) return (maktabasauti_result_t)m;                                    /* WOULD_BLOCK: dotychczasowe bajty zostaja w d->frame, ponowienie wznawia */
            if (m == 0) return MAKTABASAUTI_E_TRUNCATED;
            p->got += (size_t)m; p->cur += (uint64_t)m;
        }
        wrap_adts(d, p, sz);
        p->got = 0; p->pos++;
        return MAKTABASAUTI_OK;
    }
}

static maktabasauti_result_t mp4_prepare(dec_t *d) {
    const maktabasauti_host_t *h = d->host; mp4_priv_t *p; SautiAacMp4Alloc al; int64_t fsize, at; int pass;
    maktabasauti_result_t r;
    p = (mp4_priv_t *)h->alloc(h->user, sizeof *p);
    if (!p) return MAKTABASAUTI_E_NO_MEMORY;
    memset(p, 0, sizeof *p); d->cptr = p;
    if (!d->seekable) return live_prepare(d, p);
    al.user = (void *)h; al.alloc = a_alloc; al.free = a_free;
    fsize = h->size(h->user, d->rd);
    for (pass = 0; pass < 2; pass++) {                                                  /* 1: moov, 2: moof (po znalezieniu moov) */
        if (pass == 1 && !(p->idx && sauti_aac_mp4_index_has_fragments(p->idx))) break;
        for (at = 0;;) {
            uint64_t size = 0; uint32_t type = 0; unsigned hdr = 0; uint8_t *buf; size_t bn;
            r = box_header(d, at, &size, &type, &hdr, fsize);
            if (r == MAKTABASAUTI_EOF) break;
            if (r == MAKTABASAUTI_E_CORRUPT_DATA || r == MAKTABASAUTI_E_TRUNCATED) { if (p->idx) break; return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }   /* smieci/znacznik za ostatnim pudelkiem: koniec przegladu (jak w starym demuxerze) */
            if (r != MAKTABASAUTI_OK) return r;
            if (fsize >= 0 && at + (int64_t)size > fsize) break;                       /* urwane ostatnie pudelko: ignorujemy (jak w starym demuxerze) */
            if (pass == 0 && type == 0x6d6f6f76u && !p->idx) {                         /* 'moov' */
                r = read_payload(d, at, size, hdr, MOOV_MAX, &buf, &bn);
                if (r != MAKTABASAUTI_OK) return r;
                p->idx = sauti_aac_mp4_index_new(&al, buf, bn, &p->mi);
                h->free(h->user, buf);
                if (!p->idx) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            } else if (pass == 1 && type == 0x6d6f6f66u) {                              /* 'moof' */
                r = read_payload(d, at, size, hdr, MOOF_MAX, &buf, &bn);
                if (r != MAKTABASAUTI_OK) return r;
                if (!sauti_aac_mp4_index_add_moof(p->idx, (uint64_t)at, buf, bn)) { h->free(h->user, buf); return MAKTABASAUTI_E_CORRUPT_DATA; }
                h->free(h->user, buf);
            }
            if (size == 0) break;
            at += (int64_t)size;
        }
    }
    if (!p->idx || sauti_aac_mp4_index_count(p->idx) == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    p->pos = 0;
    return h->seek(h->user, d->rd, 0, 0) < 0 ? MAKTABASAUTI_E_NOT_SEEKABLE : MAKTABASAUTI_OK;
}

static maktabasauti_result_t mp4_fetch(dec_t *d) {
    const maktabasauti_host_t *h = d->host; mp4_priv_t *p = (mp4_priv_t *)d->cptr; uint64_t off; uint32_t sz; size_t got = 0;
    if (!p) return MAKTABASAUTI_E_INTERNAL;
    if (p->live) return live_fetch(d, p);
    if (!sauti_aac_mp4_index_get(p->idx, p->pos, &off, &sz)) return MAKTABASAUTI_EOF;
    if (sz == 0u || sz > 8184u) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (off > (uint64_t)INT64_MAX || h->seek(h->user, d->rd, (int64_t)off, 0) < 0) return MAKTABASAUTI_E_CORRUPT_DATA;   /* pozycja poza plikiem */
    while (got < sz) {
        int64_t m = h->read(h->user, d->rd, d->frame + 7u + got, sz - got);
        if (m < 0) return (maktabasauti_result_t)m;                                    /* WOULD_BLOCK: numer probki sie nie zmienia, ponowienie czyta ja od nowa */
        if (m == 0) return MAKTABASAUTI_E_TRUNCATED;
        got += (size_t)m;
    }
    wrap_adts(d, p, sz);
    p->pos++;
    return MAKTABASAUTI_OK;
}
static int64_t mp4_position(dec_t *d) { const mp4_priv_t *p = (const mp4_priv_t *)d->cptr; return p ? (int64_t)p->pos : 0; }
static maktabasauti_result_t mp4_seek_to(dec_t *d, int64_t pos) { mp4_priv_t *p = (mp4_priv_t *)d->cptr; if (!p || pos < 0) return MAKTABASAUTI_E_INVALID_ARGUMENT; p->pos = (uint64_t)pos; return MAKTABASAUTI_OK; }
static maktabasauti_result_t mp4_resync(dec_t *d) { (void)d; return MAKTABASAUTI_EOF; }
static maktabasauti_result_t mp4_scan(dec_t *d, int64_t resume, uint64_t *count, int *consistent) {
    mp4_priv_t *p = (mp4_priv_t *)d->cptr; (void)resume;
    if (p->live && sauti_aac_mp4_index_has_fragments(p->idx)) { *count = 0; *consistent = 0; return MAKTABASAUTI_OK; }   /* na zywo z fragmentami: dlugosc nieznana */
    *count = sauti_aac_mp4_index_count(p->idx); *consistent = 1;                      /* liczba jednostek znana z indeksu: bez przejscia po danych */
    return MAKTABASAUTI_OK;
}
/* przyciecie jak w starym adapterze (sauti_decoder_aac_mp4.c); wartosci z elst/iTunSMPB sa w probkach RDZENIA, HE podwaja czestotliwosc wyjscia */
static int mp4_trim(dec_t *d, uint64_t total, uint64_t *skip_out, uint64_t *avail_out) {
    const mp4_priv_t *p = (const mp4_priv_t *)d->cptr; const SautiAacMp4Info *mi = &p->mi;
    const uint64_t num = d->out_rate, den = mi->sample_rate;
    uint64_t skip = mi->skip_samples * num / den, available = 0;
    const int itunes = mi->gapless && !mi->skip_samples && !mi->duration_samples;     /* bez listy edycji: iTunSMPB (wartosci w probkach rdzenia, skalowane jak lista edycji) */
    if (itunes) skip = mi->gapless_priming * num / den;
    if (total == 0u) {                                                                 /* dlugosc strumienia nieznana (np. fMP4 na zywo): pominiecie poczatku + dlugosc z listy edycji / iTunSMPB, jesli ja podaje */
        const uint64_t dur = itunes ? mi->gapless_samples : mi->duration_samples;
        *skip_out = skip; *avail_out = dur ? dur * num / den : 0u; return 1;
    }
    if (skip >= total) return 0;                                                       /* jak w starym adapterze: pusty wynik = nieobslugiwane */
    available = total - skip;
    {   const uint64_t dur = itunes ? mi->gapless_samples : mi->duration_samples;
        if (dur) { const uint64_t dd = dur * num / den; if (dd && dd < available) available = dd; } }
    *skip_out = skip; *avail_out = available;
    return 1;
}
static void mp4_release(dec_t *d) {
    mp4_priv_t *p = (mp4_priv_t *)d->cptr;
    if (p) { live_acc_free(d, p); if (p->idx) sauti_aac_mp4_index_free(p->idx); d->host->free(d->host->user, p); d->cptr = NULL; }
}

static const aac_container_t mp4_container = { "mp4", mp4_fetch, mp4_resync, mp4_scan, NULL, NULL, mp4_release, mp4_prepare, mp4_seek_to, mp4_position, mp4_trim, 1 };

static int32_t MAKTABASAUTI_CALL mp4_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && sauti_aac_mp4_probe(p, n)) ? 60 : 0;                                   /* ftyp: ALAC/inne odrzuci open */
}
static maktabasauti_result_t MAKTABASAUTI_CALL mp4_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    return aac_eng_open(self, &mp4_container, host, rd, info, state);
}

const maktabasauti_codec_t maktabasauti_aac_mp4_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "aac-mp4", "AAC in MP4/M4A/M4B (LC, HE-AAC v1/v2; also fragmented): indexed on seekable input, one-pass live on non-seekable input", "maktabasauti_aac",
    MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    mp4_probe, mp4_open, aac_eng_read, aac_eng_seek, aac_eng_reset, aac_eng_flush, NULL, aac_eng_last_error, aac_eng_get_info, aac_eng_close, { NULL }, { NULL }
};
const maktabasauti_codec_t *maktabasauti_aac_mp4_codec(void) { return &maktabasauti_aac_mp4_desc; }
