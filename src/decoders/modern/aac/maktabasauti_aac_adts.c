/* MaktabaSauti, modul maktabasauti_aac: AAC w ADTS jako PRAWDZIWIE STRUMIENIOWY kodek (etap 2, 2026-11-20).
 *
 * Zastepuje w rejestrze domyslnym piec opakowan starych dekoderow "cala pamiec" (LC stereo/mono/wielokanalowy, HE-AAC mono/CPE).  Algorytm dekodowania jest TEN SAM:
 * wolane sa te same funkcje poziomu ramki (sauti_aac_lc_adts_*_decode_ex, sauti_aac_he_adts_*_decode_ex, mostek SBR/PS); zmienil sie tylko sposob dostarczania danych:
 *
 *   - wejscie czytane przez usluge hosta (peek/read/seek), po jednej ramce ADTS; NIGDY cale w pamieci.  Pamiec: stan dekodera + ramka (<= 8191 B) + PCM jednej ramki +
 *     ograniczona pula punktow kontrolnych (<= 8 MiB) -- niezalezna od dlugosci nagrania;
 *   - ramka jest zuzywana dopiero, gdy jest KOMPLETNA (peek, potem read): E_WOULD_BLOCK w dowolnym miejscu zostawia stan nietkniety, a wywolanie mozna powtorzyc;
 *   - wejscie nieprzewijalne: otwarcie, odczyt, flush; dlugosc nieznana (UNKNOWN_LENGTH), seek/reset = E_NOT_SEEKABLE;
 *   - wejscie przewijalne: przy otwarciu jedno sekwencyjne przejscie po NAGLOWKACH liczy ramki (dokladna dlugosc, jak w starym dekoderze), potem seek DOKLADNY: stan dekodera
 *     (historia IMDCT, PNS, QMF/SBR/PS) jest odtwarzany z najblizszego punktu kontrolnego i doliczany do celu; punkty kontrolne zapisuje odczyt, ich liczba jest ograniczona
 *     (stride rosnie dwukrotnie, gdy pula sie zapelni), wiec koszt seek rosnie z dlugoscia, pamiec nie;
 *   - uszkodzony strumien: zla ramka -> E_CORRUPT_DATA po wydaniu poprawnych ramek (lepki do flush/seek/reset; flush gubi historie dekodera i resynchronizuje na nastepnej
 *     poprawnej ramce), urwana ramka na koncu -> E_TRUNCATED, zmiana parametrow w srodku -> E_UNSUPPORTED_FEATURE;
 *   - brak stanu globalnego, cala pamiec przez host->alloc/free: wiele instancji w wielu watkach.
 *
 * Wybor wariantu jak w starym rejestrze (kolejnosc: LC stereo, LC mono, LC wielokanalowy, HE mono, HE CPE) wg PIERWSZEJ ramki: ramka musi sie w pelni zdekodowac danym
 * wariantem; strumien HE (SBR w FIL) odrzuca rdzen LC i trafia do HE.  PCM jest bit w bit rowny staremu API (tests/test_maktabasauti_regression.c).
 *
 * ID3v2 przed pierwsza ramka (znacznik na poczatku pliku, takze z stopka i kilka znacznikow z rzedu) jest pomijany przy otwarciu i po kazdym przewinieciu do poczatku; sonda widzi ramke za
 * znacznikiem, gdy miesci sie w prefiksie, a przy znaczniku dluzszym od prefiksu zwraca 55 (tuz nad MP3 z tym samym znacznikiem: gdy za znacznikiem nie ma ADTS, open() zawodzi, a rdzen
 * przechodzi do nastepnego kandydata).  Wejscie NIEprzewijalne obsluguje znaczniki mieszczace sie w oknie czytnika (16 KiB), dluzsze odrzuca bez zuzycia danych.
 *
 * Opcje: "adts.scan" = "off" wylacza przejscie po naglowkach przy otwarciu (dlugosc nieznana, seek bez sprawdzania zakresu). */
#include "maktabasauti_aac_engine.h"

static maktabasauti_result_t skip_id3(dec_t *d);

static maktabasauti_result_t adts_fetch(dec_t *d) {
    const maktabasauti_host_t *h = d->host;
    int64_t n, m; aac_adts_hdr_t hd; SautiAacAdts full;
    if (d->core.frames_in == 0 && d->core.in_offset == 0) {                 /* poczatek strumienia: znaczniki ID3v2 */
        maktabasauti_result_t sr = skip_id3(d);
        if (sr != MAKTABASAUTI_OK) return sr;
    }
    n = h->peek(h->user, d->rd, d->frame, ADTS_HEADER_PEEK);
    if (n < 0) return (maktabasauti_result_t)n;
    if (n == 0) return MAKTABASAUTI_EOF;
    if (!aac_adts_parse_header(d->frame, (size_t)n, &hd)) {
        /* urwany naglowek na koncu wejscia (bajty sa prefiksem poprawnej synchronizacji) albo smieci */
        const int prefix = d->frame[0] == 0xFFu && (n < 2 || (d->frame[1] & 0xF6u) == 0xF0u);
        return (prefix && n < (int64_t)ADTS_HEADER_PEEK) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_E_CORRUPT_DATA;
    }
    m = h->peek(h->user, d->rd, d->frame, hd.frame_bytes);
    if (m < 0) return (maktabasauti_result_t)m;
    if ((uint64_t)m < hd.frame_bytes) return MAKTABASAUTI_E_TRUNCATED;
    if (!sauti_aac_adts_parse_asm(d->frame, hd.frame_bytes, &full) || full.frame_bytes != hd.frame_bytes) return MAKTABASAUTI_E_CORRUPT_DATA;
    m = h->read(h->user, d->rd, d->frame, hd.frame_bytes);
    if (m < 0) return (maktabasauti_result_t)m;
    if ((uint64_t)m != hd.frame_bytes) return MAKTABASAUTI_E_IO;        /* peek obiecal te bajty */
    d->frame_len = hd.frame_bytes;
    return MAKTABASAUTI_OK;
}

/* po uszkodzeniu: pomija bajty do najblizszego miejsca wygladajacego na poprawna ramke (naglowek + synchronizacja nastepnej ramki, gdy widac ja w oknie), zaczynajac od bajtu PO
 * obecnej pozycji.  OK = wejscie stoi na kandydacie; EOF = nic nie znaleziono do konca wejscia albo limitu; blad wejscia (np. WOULD_BLOCK) = bez zmiany tego, co juz pominieto. */

static maktabasauti_result_t adts_resync(dec_t *d) {
    const maktabasauti_host_t *h = d->host;
    size_t skipped = 0; int first = 1;
    while (skipped < RESYNC_LIMIT) {
        uint8_t win[4096]; int64_t n; size_t i, adv; aac_adts_hdr_t hd;
        n = h->peek(h->user, d->rd, win, sizeof win);
        if (n < 0) return (maktabasauti_result_t)n;
        if (n < 7) { if (n > 0) (void)h->read(h->user, d->rd, win, (size_t)n); return MAKTABASAUTI_EOF; }
        for (i = first ? 1u : 0u; i + 7u <= (size_t)n; i++) {
            aac_adts_hdr_t nx;
            if (win[i] != 0xFFu || (win[i + 1] & 0xF6u) != 0xF0u || !aac_adts_parse_header(win + i, (size_t)n - i, &hd)) continue;
            if (i + (size_t)hd.frame_bytes + 7u <= (size_t)n && !aac_adts_parse_header(win + i + hd.frame_bytes, (size_t)n - i - hd.frame_bytes, &nx)) continue;   /* widac nastepna ramke: musi byc poprawna */
            if (i) { int64_t rd = h->read(h->user, d->rd, win, i); if (rd < 0) return (maktabasauti_result_t)rd; }
            return MAKTABASAUTI_OK;
        }
        adv = (size_t)n > 6u ? (size_t)n - 6u : (size_t)n;                 /* zostawiamy 6 bajtow: kandydat moze zaczynac sie tuz przed koncem okna */
        { int64_t rd = h->read(h->user, d->rd, win, adv); if (rd < 0) return (maktabasauti_result_t)rd; skipped += (size_t)rd; }
        first = 0;
    }
    return MAKTABASAUTI_EOF;
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */

/* calkowita dlugosc znacznika ID3v2 na poczatku (naglowek 10 B + dane + opcjonalna stopka 10 B) albo 0, gdy to nie jest poprawny naglowek ID3v2 */
static size_t id3v2_len(const uint8_t *p, size_t n) {
    if (!p || n < 10u || memcmp(p, "ID3", 3) != 0 || p[3] == 0xFFu || p[4] == 0xFFu || (p[5] & 0x0Fu) != 0u || ((p[6] | p[7] | p[8] | p[9]) & 0x80u)) return 0;
    return 10u + (((size_t)p[6] << 21) | ((size_t)p[7] << 14) | ((size_t)p[8] << 7) | (size_t)p[9]) + ((p[5] & 0x10u) ? 10u : 0u);
}

static int32_t probe_at(const uint8_t *p, size_t n) {
    aac_adts_hdr_t h, nx;
    if (!p || !aac_adts_parse_header(p, n, &h) || h.raw_blocks != 1u) return 0;
    if (n >= (size_t)h.frame_bytes + 7u) {                                /* druga ramka widoczna w prefiksie: lancuch musi sie zgadzac (strumien samosynchronizujacy) */
        if (!aac_adts_parse_header(p + h.frame_bytes, n - h.frame_bytes, &nx) || nx.rate_index != h.rate_index || nx.config != h.config) return 0;
        return 90;
    }
    if (n == h.frame_bytes) return 85;                                    /* plik z jedna ramka */
    if (n < h.frame_bytes) return 70;                                     /* ramka dluzsza niz prefiks (nie da sie sprawdzic lancucha) */
    /* kilka bajtow za ramka (< 7): musza byc poczatkiem synchronizacji nastepnej ramki (strumien urwany w naglowku), inaczej smieci */
    return (p[h.frame_bytes] == 0xFFu && (n - h.frame_bytes < 2u || (p[h.frame_bytes + 1u] & 0xF6u) == 0xF0u)) ? 75 : 0;
}

static int32_t MAKTABASAUTI_CALL adts_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    size_t t; unsigned guard; (void)self; (void)hint;
    for (guard = 0; guard < 8u; guard++) {                                /* kilka znacznikow z rzedu */
        t = id3v2_len(p, n);
        if (!t) return probe_at(p, n);
        if (t + 10u > n) return (t + 7u > n && n >= 10u) ? 55 : probe_at(p + t, n - t);   /* znacznik siega poza prefiks: nie widac, co jest za nim */
        p += t; n -= t;
    }
    return 0;
}

/* na poczatku strumienia: pomija znaczniki ID3v2 (w jednym kawalku przez seek, gdy wejscie sie przewija; inaczej czytamy je z bufora czytnika, do ktorego prepare je zmiescil) */
static maktabasauti_result_t skip_id3(dec_t *d) {
    const maktabasauti_host_t *h = d->host; unsigned guard;
    for (guard = 0; guard < 8u; guard++) {
        uint8_t hdr[10]; int64_t n; size_t t;
        n = h->peek(h->user, d->rd, hdr, sizeof hdr);
        if (n < 0) return (maktabasauti_result_t)n;
        t = id3v2_len(hdr, (size_t)n);
        if (!t) return MAKTABASAUTI_OK;
        if (d->seekable) {
            int64_t pos = h->tell(h->user, d->rd);
            if (pos < 0 || h->seek(h->user, d->rd, pos + (int64_t)t, 0) < 0) return MAKTABASAUTI_E_CORRUPT_DATA;      /* znacznik dluzszy niz plik */
        } else {
            uint8_t skip[512]; size_t left = t;
            while (left) {
                size_t want = left < sizeof skip ? left : sizeof skip; int64_t m = h->read(h->user, d->rd, skip, want);
                if (m < 0) return (maktabasauti_result_t)m;
                if (m == 0) return MAKTABASAUTI_E_TRUNCATED;
                left -= (size_t)m;
            }
        }
    }
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t adts_prepare(dec_t *d) {
    const maktabasauti_host_t *h = d->host; uint8_t win[16384]; int64_t n; size_t t;
    n = h->peek(h->user, d->rd, win, sizeof win);
    if (n < 0) return (maktabasauti_result_t)n;
    t = id3v2_len(win, (size_t)n);
    if (t && !d->seekable && t + ADTS_HEADER_PEEK > (size_t)n) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;   /* znacznik poza oknem czytnika na wejsciu nieprzewijalnym: odrzucamy, nic nie zuzywszy */
    return MAKTABASAUTI_OK;
}


static maktabasauti_result_t adts_scan(dec_t *d, int64_t resume, uint64_t *count, int *consistent) {
    const maktabasauti_host_t *h = d->host; uint64_t c = 1;               /* pierwsza ramka juz policzona */
    *consistent = 1;
    for (;;) {
        uint8_t hdr[ADTS_HEADER_PEEK]; int64_t n, m; aac_adts_hdr_t hd;
        n = h->peek(h->user, d->rd, hdr, ADTS_HEADER_PEEK);
        if (n < 0) return (maktabasauti_result_t)n;
        if (n == 0) break;
        if (!aac_adts_parse_header(hdr, (size_t)n, &hd)) break;               /* smieci albo urwany naglowek na koncu: koniec zliczania */
        if (!aac_same_stream(d, &hd)) { *consistent = 0; break; }
        {   uint8_t skip[1024]; size_t left = hd.frame_bytes;           /* przesuwamy sie po ramce kawalkami (NIE do d->frame: tam czeka pierwsza ramka) */
            while (left) {
                size_t want = left < sizeof skip ? left : sizeof skip;
                m = h->read(h->user, d->rd, skip, want);
                if (m < 0) return (maktabasauti_result_t)m;
                if (m == 0) break;
                left -= (size_t)m;
            }
            if (left) break;                                              /* ramka urwana: nie liczymy */
        }
        c++;
    }
    if (h->seek(h->user, d->rd, resume, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    *count = c;
    return MAKTABASAUTI_OK;
}


static const aac_container_t adts_container = { "adts", adts_fetch, adts_resync, adts_scan, NULL, NULL, NULL, adts_prepare, NULL, NULL, NULL };

static maktabasauti_result_t MAKTABASAUTI_CALL adts_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    return aac_eng_open(self, &adts_container, host, rd, info, state);
}

const maktabasauti_codec_t maktabasauti_aac_adts_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "aac-adts", "AAC in ADTS (LC mono/stereo/multichannel, HE-AAC v1/v2), streaming", "maktabasauti_aac",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    adts_probe, adts_open, aac_eng_read, aac_eng_seek, aac_eng_reset, aac_eng_flush, NULL, aac_eng_last_error, NULL, aac_eng_close, { NULL }, { NULL }
};
const maktabasauti_codec_t *maktabasauti_aac_adts_codec(void) { return &maktabasauti_aac_adts_desc; }
