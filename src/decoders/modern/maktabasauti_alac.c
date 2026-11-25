/* MaktabaSauti, modul maktabasauti_alac: Apple Lossless (ALAC) w kontenerze MP4 / M4A, napisany od zera w czystym C.
 *
 * Plik jest INDEKSOWANY, nie wczytywany: przy otwarciu przegladane sa pudelka najwyzszego poziomu (naglowki; mdat jest pomijany), czytana jest tylko zawartosc `moov`, z niej powstaje
 * lista pakietow {pozycja, rozmiar} (12 B na pakiet), a kazdy pakiet jest pobierany przez seek + read i dekodowany osobno (w ALAC pakiety sa niezalezne: zadnej historii miedzy
 * nimi, wiec seek jest dokladny i tani).  Wejscie musi byc PRZEWIJALNE (moov bywa na koncu pliku).
 *
 * Format (opis algorytmu, nie kod referencyjny): `magic cookie` (24 B: dlugosc ramki, glebia bitowa, parametry kodera Rice'a pb / mb / kb, kanaly, czestotliwosc), potem pakiety:
 * elementy (SCE / CPE / LFE; DSE i FIL sa pomijane), kazdy z naglowkiem (partial frame, bajty przesuniete, escape), parametrami predyktora na kanal (rzad do 31, wspolczynniki 16 bit,
 * przesuniecie mianownika), dodatkowymi bitami mlodszymi (glebia > 16), resztami kodowanymi adaptacyjnie Golomb-Rice'm (z trybem serii zer) i predyktorem adaptacyjnym (znak-znak LMS);
 * stereo: macierz mid / side (mixBits, mixRes).  Pakiet "escape" niesie surowe probki.  Wyjscie: f32 = probka / 2^(glebia - 1) (dla 32 bitow gubi najmlodsze bity f32).
 * Kolejnosc kanalow wielokanalowych jak w FFmpeg: FL FR FC [LFE / BC] ...: patrz tablica map_tab.
 *
 * Nieobslugiwane (E_UNSUPPORTED_FORMAT / FEATURE, nic nie zastepuje): MP4 bez sciezki alac, fragmentowane MP4, CAF, tryb predykcji != 0, elementy CCE / PCE, glebia poza 16 / 20 / 24 / 32,
 * lista edycji i iTunSMPB (ALAC nie ma opoznienia kodera; wartosc stts decyduje o dlugosci). */
#include "maktabasauti/maktabasauti_codec.h"
#include <stdint.h>
#include <string.h>

#define FCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define FCC_MOOV FCC('m', 'o', 'o', 'v')
#define FCC_TRAK FCC('t', 'r', 'a', 'k')
#define FCC_MDIA FCC('m', 'd', 'i', 'a')
#define FCC_MINF FCC('m', 'i', 'n', 'f')
#define FCC_STBL FCC('s', 't', 'b', 'l')
#define FCC_STSD FCC('s', 't', 's', 'd')
#define FCC_STTS FCC('s', 't', 't', 's')
#define FCC_STSZ FCC('s', 't', 's', 'z')
#define FCC_STSC FCC('s', 't', 's', 'c')
#define FCC_STCO FCC('s', 't', 'c', 'o')
#define FCC_CO64 FCC('c', 'o', '6', '4')
#define FCC_ALAC FCC('a', 'l', 'a', 'c')
#define FCC_WAVE FCC('w', 'a', 'v', 'e')

#define MOOV_MAX ((size_t)64 << 20)
#define PACKETS_MAX ((uint32_t)4u << 20)
#define FRAME_MAX 65536u
#define CH_MAX 8u

typedef struct { uint32_t frame_len, max_bytes, rate; uint8_t bits, pb, mb, kb, ch; } cfg_t;

typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd;
    cfg_t c; unsigned map[CH_MAX];
    uint64_t *offs; uint32_t *sizes; uint32_t npkt;
    uint64_t total;
    int32_t *mix[2], *pred, *pcm; uint32_t *ex[2]; uint8_t *buf; size_t buf_cap;
    uint32_t next_pkt, pos, pkt_frames; uint64_t skip;
    maktabasauti_result_t pend;
} alac_t;

static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

/* ============================================ czytnik bitow (MSB najpierw) ============================================ */
typedef struct { const uint8_t *p; size_t nbits, pos; int over; } br_t;
static uint32_t br_peek(const br_t *b, unsigned n) {
    size_t byte = b->pos >> 3, nbytes = b->nbits >> 3; unsigned sh = (unsigned)(b->pos & 7u), i; uint64_t acc = 0;
    if (n == 0) return 0;
    for (i = 0; i < 5u; i++) acc = (acc << 8) | (byte + i < nbytes ? b->p[byte + i] : 0u);
    acc >>= 40u - sh - n;
    return (uint32_t)(acc & ((1ull << n) - 1u));
}
static void br_skip(br_t *b, size_t n) { b->pos += n; if (b->pos > b->nbits) { b->pos = b->nbits; b->over = 1; } }
static uint32_t br_get(br_t *b, unsigned n) { const uint32_t v = br_peek(b, n); br_skip(b, n); return v; }

/* ====================================== kodowanie Golomb-Rice'a z seria zer ======================================== */
/* jedna wartosc: prefiks z jedynek (do 9; 9 = wartosc surowa na maxbits bitach), potem zero i reszta na k bitach z przypadkiem "reszta < 2 = k - 1 bitow" */
static uint32_t scalar(br_t *b, unsigned k, unsigned maxbits) {
    unsigned pre = 0; uint32_t x; const uint32_t w = br_peek(b, 9);
    while (pre < 9u && (w & (0x100u >> pre))) pre++;
    if (pre >= 9u) { br_skip(b, 9); return br_get(b, maxbits); }
    br_skip(b, pre + 1u);
    x = pre * ((1u << k) - 1u);
    if (k != 1u) {
        const uint32_t v = br_peek(b, k);
        if (v > 1u) { x += v - 1u; br_skip(b, k); } else br_skip(b, k - 1u);
    }
    return x;
}
static unsigned log2u(uint32_t v) { unsigned r = 0; while (v >>= 1) r++; return r; }
static unsigned lead(uint32_t v) { unsigned n = 0; uint32_t c = 0x80000000u; while (n < 32u && !(v & c)) { n++; c >>= 1; } return n; }

/* reszty bloku: 0 = ok, -1 = blad (zla dlugosc serii, koniec danych) */
static int rice_block(br_t *b, int32_t *out, uint32_t n, unsigned kb, uint32_t mb0, uint32_t mult, unsigned maxbits) {
    uint32_t history = mb0, xmod = 0, i;
    for (i = 0; i < n; i++) {
        unsigned k = log2u((history >> 9) + 3u); uint32_t x;
        if (k > kb) k = kb;
        if (k < 1u) k = 1u;
        x = scalar(b, k, maxbits) + xmod;
        out[i] = (int32_t)((x >> 1) ^ (0u - (x & 1u)));
        xmod = 0;
        if (x > 0xffffu) history = 0xffffu; else history = history + x * mult - ((history * mult) >> 9);
        if (history < 128u && i + 1u < n) {
            uint32_t blk, j;
            k = lead(history) - 24u + ((history + 16u) >> 6);
            if (k > kb) k = kb;
            if (k < 1u) k = 1u;
            blk = scalar(b, k, 16u);
            if (blk > 0) {
                if (blk >= n - i) return -1;
                for (j = 1; j <= blk; j++) out[i + j] = 0;
                i += blk;
            }
            if (blk <= 0xffffu) xmod = 1;
            history = 0;
        }
        if (b->over) return -1;
    }
    return 0;
}

/* ============================================ predyktor adaptacyjny (znak-znak LMS) ============================================ */
static int32_t sext(int32_t v, unsigned shift) { return (int32_t)((uint32_t)v << shift) >> shift; }
static int32_t sgn32(int32_t v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
static void unpc(const int32_t *in, int32_t *out, uint32_t num, int16_t *coefs, unsigned na, unsigned chanbits, unsigned denshift) {
    const unsigned chanshift = 32u - chanbits; const uint32_t denhalf = denshift ? (1u << (denshift - 1u)) : 0u; uint32_t j; int k;
    if (num == 0) return;
    out[0] = in[0];
    if (na == 0) { for (j = 1; j < num; j++) out[j] = in[j]; return; }
    if (na == 31u) { for (j = 1; j < num; j++) out[j] = sext((int32_t)((uint32_t)in[j] + (uint32_t)out[j - 1]), chanshift); return; }
    for (j = 1; j <= na && j < num; j++) out[j] = sext((int32_t)((uint32_t)in[j] + (uint32_t)out[j - 1]), chanshift);
    for (j = na + 1u; j < num; j++) {
        const int32_t top = out[j - na - 1u]; uint32_t sum1 = 0; int32_t del, del0, sg;
        for (k = 0; k < (int)na; k++) sum1 += (uint32_t)(int32_t)coefs[k] * ((uint32_t)out[j - 1u - (uint32_t)k] - (uint32_t)top);
        del = in[j]; del0 = del; sg = sgn32(del);
        del = (int32_t)((uint32_t)del + (uint32_t)top + (uint32_t)((int32_t)(sum1 + denhalf) >> denshift));
        out[j] = sext(del, chanshift);
        if (sg > 0) {
            for (k = (int)na - 1; k >= 0; k--) {
                const int32_t dd = (int32_t)((uint32_t)top - (uint32_t)out[j - 1u - (uint32_t)k]), s = sgn32(dd);
                coefs[k] = (int16_t)(coefs[k] - s);
                del0 -= (int32_t)(na - (unsigned)k) * ((s * dd) >> denshift);
                if (del0 <= 0) break;
            }
        } else if (sg < 0) {
            for (k = (int)na - 1; k >= 0; k--) {
                const int32_t dd = (int32_t)((uint32_t)top - (uint32_t)out[j - 1u - (uint32_t)k]), s = sgn32(dd);
                coefs[k] = (int16_t)(coefs[k] + s);
                del0 -= (int32_t)(na - (unsigned)k) * ((-s * dd) >> denshift);
                if (del0 >= 0) break;
            }
        }
    }
}

/* ============================================================ pakiet ============================================================ */
/* kolejnosc kanalow strumienia ALAC -> indeks wyjsciowy (jak FFmpeg: FL FR FC ...) */
static const uint8_t map_tab[CH_MAX][CH_MAX] = {
    { 0 }, { 0, 1 }, { 2, 0, 1 }, { 2, 0, 1, 3 }, { 2, 0, 1, 3, 4 }, { 2, 0, 1, 4, 5, 3 }, { 2, 0, 1, 4, 5, 6, 3 }, { 2, 6, 7, 0, 1, 4, 5, 3 },
};

static int element(alac_t *s, br_t *b, unsigned nc, unsigned first, uint32_t *nsamp_io) {
    const cfg_t *c = &s->c; unsigned hdr, partial, bshift, escape, chanbits, ch; uint32_t nsamp, i;
    (void)br_get(b, 4);                                                          /* element instance tag */
    if (br_get(b, 12) != 0) return -1;                                           /* nieuzywane: musza byc zerem */
    hdr = br_get(b, 4); partial = hdr >> 3; bshift = (hdr >> 1) & 3u; escape = hdr & 1u;
    if (partial) { nsamp = br_get(b, 16) << 16; nsamp |= br_get(b, 16); } else nsamp = c->frame_len;
    if (nsamp == 0 || nsamp > c->frame_len) return -1;
    if (*nsamp_io && *nsamp_io != nsamp) return -1;
    *nsamp_io = nsamp;
    if (!escape) {
        unsigned mixbits, na[2], denshift[2], pbf[2]; int mixres; int16_t coefs[2][32]; const unsigned xb = bshift * 8u; size_t extra_pos = 0, after = 0;
        chanbits = c->bits - xb + (nc - 1u);
        if (chanbits > 32u || chanbits < 2u) return -1;
        mixbits = br_get(b, 8); mixres = (int8_t)br_get(b, 8);
        if (mixbits > 31u) return -1;
        for (ch = 0; ch < nc; ch++) {
            unsigned h = br_get(b, 8), q; if ((h >> 4) != 0) return -2;               /* tryb predykcji != 0: nieobslugiwany */
            denshift[ch] = h & 15u; h = br_get(b, 8); pbf[ch] = h >> 5; na[ch] = h & 31u;
            for (q = 0; q < na[ch]; q++) coefs[ch][q] = (int16_t)br_get(b, 16);
        }
        if (b->over) return -1;
        if (xb) {                                                                /* bity mlodsze leza za parametrami, przed resztami: zapamietaj i przeskocz */
            extra_pos = b->pos; br_skip(b, (size_t)xb * nc * nsamp);
            if (b->over) return -1;
        }
        for (ch = 0; ch < nc; ch++) {
            if (rice_block(b, s->pred, nsamp, c->kb, c->mb, (uint32_t)c->pb * pbf[ch] / 4u, chanbits) != 0) return -1;
            unpc(s->pred, s->mix[ch], nsamp, coefs[ch], na[ch], chanbits, denshift[ch]);
        }
        if (xb) {
            after = b->pos; b->pos = extra_pos;
            for (i = 0; i < nsamp; i++) for (ch = 0; ch < nc; ch++) s->ex[ch][i] = br_get(b, xb);
            b->pos = after;
        }
        if (nc == 2u && mixres != 0) {
            for (i = 0; i < nsamp; i++) {
                const int32_t u = s->mix[0][i], v = s->mix[1][i];
                const int32_t l = (int32_t)((uint32_t)u + (uint32_t)v - (uint32_t)((int32_t)((int64_t)mixres * v) >> mixbits));
                s->mix[0][i] = l; s->mix[1][i] = (int32_t)((uint32_t)l - (uint32_t)v);
            }
        }
        for (ch = 0; ch < nc; ch++) {
            unsigned o = s->map[first + ch];
            for (i = 0; i < nsamp; i++) {
                uint32_t val = (uint32_t)s->mix[ch][i];
                if (xb) val = (val << xb) | s->ex[ch][i];
                s->pcm[(size_t)i * c->ch + o] = sext((int32_t)val, 32u - c->bits);
            }
        }
    } else {                                                                    /* pakiet nieskompresowany: probki surowe, naprzemiennie kanalami */
        const unsigned bits = c->bits;
        for (i = 0; i < nsamp; i++) for (ch = 0; ch < nc; ch++) s->pcm[(size_t)i * c->ch + s->map[first + ch]] = sext((int32_t)br_get(b, bits), 32u - bits);
        if (b->over) return -1;
    }
    return 0;
}

/* dekoduje jeden pakiet do s->pcm; zwraca liczbe ramek (>0) albo <0 (blad) */
static int64_t decode_packet(alac_t *s, const uint8_t *p, size_t n) {
    br_t b; unsigned done = 0; uint32_t nsamp = 0;
    b.p = p; b.nbits = n * 8u; b.pos = 0; b.over = 0;
    for (;;) {
        const unsigned tag = br_get(&b, 3); int r;
        if (b.over) return -1;
        if (tag == 7u) break;                                                      /* ID_END */
        if (tag == 0u || tag == 3u || tag == 1u) {                                  /* SCE, LFE, CPE */
            const unsigned nc = tag == 1u ? 2u : 1u;
            if (done + nc > s->c.ch) return -1;
            r = element(s, &b, nc, done, &nsamp);
            if (r != 0) return r == -2 ? -2 : -1;
            done += nc;
        } else if (tag == 4u) {                                                    /* DSE: pomijamy */
            unsigned cnt; int align;
            (void)br_get(&b, 4); align = (int)br_get(&b, 1); cnt = br_get(&b, 8);
            if (cnt == 255u) cnt += br_get(&b, 8);
            if (align) br_skip(&b, (8u - (unsigned)(b.pos & 7u)) & 7u);
            br_skip(&b, (size_t)cnt * 8u);
        } else if (tag == 6u) {                                                    /* FIL: pomijamy */
            unsigned cnt = br_get(&b, 4);
            if (cnt == 15u) cnt += br_get(&b, 8) - 1u;
            br_skip(&b, (size_t)cnt * 8u);
        } else return -2;                                                          /* CCE, PCE */
        if (b.over) return -1;
    }
    if (done != s->c.ch || nsamp == 0) return -1;
    return (int64_t)nsamp;
}

/* ============================================================== MP4 ============================================================== */
/* nastepne pudelko w [*pos, end): *type, *pay = poczatek zawartosci, *bend = koniec pudelka; 1 = ok, 0 = koniec lub blad */
static int next_box(const uint8_t *b, size_t end, size_t *pos, uint32_t *type, size_t *pay, size_t *bend) {
    uint64_t size; size_t hdr = 8;
    if (end - *pos < 8u) return 0;
    size = be32(b + *pos); *type = be32(b + *pos + 4u);
    if (size == 1u) { if (end - *pos < 16u) return 0; size = be64(b + *pos + 8u); hdr = 16; }
    else if (size == 0u) size = end - *pos;
    if (size < hdr || size > end - *pos) return 0;
    *pay = *pos + hdr; *bend = *pos + (size_t)size; *pos = *bend;
    return 1;
}
static int find_child(const uint8_t *b, size_t s, size_t e, uint32_t type, size_t *ps, size_t *pe) {
    size_t pos = s, pay, bend; uint32_t t;
    while (next_box(b, e, &pos, &t, &pay, &bend)) if (t == type) { *ps = pay; *pe = bend; return 1; }
    return 0;
}

static int parse_cookie(const uint8_t *p, size_t n, cfg_t *c) {
    if (n < 24u) return 0;
    c->frame_len = be32(p); c->bits = p[5]; c->pb = p[6]; c->mb = p[7]; c->kb = p[8]; c->ch = p[9];
    c->max_bytes = be32(p + 12); c->rate = be32(p + 20);
    return 1;
}
/* sciezka dzwiekowa alac: cookie z wpisu stsd (pudelko `alac` w pudelku `alac`, takze w `wave`) */
static int stsd_cookie(const uint8_t *b, size_t s, size_t e, cfg_t *c, uint32_t *entry_rate) {
    size_t pos, pay, bend; uint32_t t, cnt;
    if (e - s < 8u) return 0;
    cnt = be32(b + s + 4u); pos = s + 8u;
    while (cnt-- && next_box(b, e, &pos, &t, &pay, &bend)) {
        if (t == FCC_ALAC) {
            size_t cs = pay + 28u, ce = bend, ps, pe, pos2; unsigned ver;
            if (bend - pay < 28u) return 0;
            ver = (unsigned)be16(b + pay + 8u);                                    /* wersja wpisu audio: v1 ma 16 bajtow wiecej */
            if (ver == 1u) cs += 16u; else if (ver != 0u) return 0;
            if (cs > ce) return 0;
            *entry_rate = be32(b + pay + 24u) >> 16;
            if (find_child(b, cs, ce, FCC_ALAC, &ps, &pe) && pe - ps >= 28u) return parse_cookie(b + ps + 4u, pe - ps - 4u, c);
            if (find_child(b, cs, ce, FCC_WAVE, &ps, &pe)) { pos2 = ps; { uint32_t t2; size_t p2, e2; while (next_box(b, pe, &pos2, &t2, &p2, &e2)) if (t2 == FCC_ALAC && e2 - p2 >= 28u) return parse_cookie(b + p2 + 4u, e2 - p2 - 4u, c); } }
            return 0;
        }
    }
    return 0;
}

static maktabasauti_result_t build_index(alac_t *s, const uint8_t *b, size_t stbl_s, size_t stbl_e) {
    const maktabasauti_host_t *h = s->host; size_t ss, se, cs, ce, zs, ze, ts, te; uint32_t n, nchunk, nsc, i, pkt = 0, chunk, si = 0; int co64 = 0; uint64_t total, last;
    if (!find_child(b, stbl_s, stbl_e, FCC_STSZ, &zs, &ze) || ze - zs < 12u) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (!find_child(b, stbl_s, stbl_e, FCC_STSC, &ss, &se) || se - ss < 8u) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (find_child(b, stbl_s, stbl_e, FCC_STCO, &cs, &ce)) co64 = 0;
    else if (find_child(b, stbl_s, stbl_e, FCC_CO64, &cs, &ce)) co64 = 1;
    else return MAKTABASAUTI_E_CORRUPT_DATA;
    n = be32(b + zs + 8u);
    if (n == 0 || n > PACKETS_MAX) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    { const uint32_t fixed = be32(b + zs + 4u); if (fixed == 0 && (ze - zs - 12u) / 4u < n) return MAKTABASAUTI_E_CORRUPT_DATA; }
    nsc = be32(b + ss + 4u);
    if (nsc == 0 || (se - ss - 8u) / 12u < nsc) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (ce - cs < 8u) return MAKTABASAUTI_E_CORRUPT_DATA;
    nchunk = be32(b + cs + 4u);
    if ((ce - cs - 8u) / (co64 ? 8u : 4u) < nchunk) return MAKTABASAUTI_E_CORRUPT_DATA;
    s->offs = (uint64_t *)h->alloc(h->user, (size_t)n * sizeof(uint64_t));
    s->sizes = (uint32_t *)h->alloc(h->user, (size_t)n * sizeof(uint32_t));
    if (!s->offs || !s->sizes) return MAKTABASAUTI_E_NO_MEMORY;
    { const uint32_t fixed = be32(b + zs + 4u); for (i = 0; i < n; i++) s->sizes[i] = fixed ? fixed : be32(b + zs + 12u + 4u * (size_t)i); }
    for (chunk = 1; chunk <= nchunk && pkt < n; chunk++) {
        uint64_t off; uint32_t spc, k;
        while (si + 1u < nsc && be32(b + ss + 8u + 12u * (size_t)(si + 1u)) <= chunk) si++;
        spc = be32(b + ss + 8u + 12u * (size_t)si + 4u);
        off = co64 ? be64(b + cs + 8u + 8u * (size_t)(chunk - 1u)) : be32(b + cs + 8u + 4u * (size_t)(chunk - 1u));
        for (k = 0; k < spc && pkt < n; k++) { s->offs[pkt] = off; off += s->sizes[pkt]; pkt++; }
    }
    if (pkt != n) return MAKTABASAUTI_E_CORRUPT_DATA;
    s->npkt = n;
    /* dlugosc: suma stts; wszystkie pakiety poza ostatnim maja dokladnie frame_len ramek, ostatni 1..frame_len */
    total = (uint64_t)(n - 1u) * s->c.frame_len + s->c.frame_len;
    if (find_child(b, stbl_s, stbl_e, FCC_STTS, &ts, &te) && te - ts >= 8u) {
        uint32_t ne = be32(b + ts + 4u), e; uint64_t sum = 0;
        if ((te - ts - 8u) / 8u < ne) return MAKTABASAUTI_E_CORRUPT_DATA;
        for (e = 0; e < ne; e++) sum += (uint64_t)be32(b + ts + 8u + 8u * (size_t)e) * be32(b + ts + 12u + 8u * (size_t)e);
        last = sum - (uint64_t)(n - 1u) * s->c.frame_len;
        if (sum < (uint64_t)(n - 1u) * s->c.frame_len || last == 0 || last > s->c.frame_len) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;   /* pakiety o zmiennej dlugosci */
        total = sum;
    }
    s->total = total;
    return MAKTABASAUTI_OK;
}

static void alac_free(alac_t *s) {
    const maktabasauti_host_t *h = s->host; unsigned i;
    if (s->offs) h->free(h->user, s->offs);
    if (s->sizes) h->free(h->user, s->sizes);
    for (i = 0; i < 2u; i++) { if (s->mix[i]) h->free(h->user, s->mix[i]); if (s->ex[i]) h->free(h->user, s->ex[i]); }
    if (s->pred) h->free(h->user, s->pred);
    if (s->pcm) h->free(h->user, s->pcm);
    if (s->buf) h->free(h->user, s->buf);
    h->free(h->user, s);
}

static int32_t MAKTABASAUTI_CALL alac_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    size_t i;
    (void)self; (void)hint;
    if (!p || n < 12u || memcmp(p + 4, "ftyp", 4) != 0) return 0;
    for (i = 8; i + 4u <= n; i++) if (p[i] == 'a' && p[i + 1] == 'l' && p[i + 2] == 'a' && p[i + 3] == 'c') return 85;   /* moov widoczny w prefiksie i niesie alac */
    return 50;                                                                    /* ftyp: AAC jest wyzej (60), a jego open odrzuci ALAC; ten open odrzuci reszte */
}

static maktabasauti_result_t MAKTABASAUTI_CALL alac_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd,
                                                        maktabasauti_stream_info_t *info, void **state) {
    uint8_t hd[16]; int64_t at = 0, fsize, m; uint8_t *moov = NULL; size_t moov_n = 0, pay, bend, mp = 0; uint32_t type; alac_t *s = NULL; maktabasauti_result_t r;
    cfg_t c; uint32_t entry_rate = 0; int found = 0; size_t stbl_s = 0, stbl_e = 0, got = 0; unsigned i;
    (void)self; *state = NULL;
    if (!h || !info || !state || !h->alloc || !h->free || !h->seek || !h->read || !h->peek) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    fsize = h->size ? h->size(h->user, rd) : -1;
    for (;;) {                                                                      /* pudelka najwyzszego poziomu: szukamy moov */
        uint64_t size; unsigned hl = 8;
        if (fsize >= 0 && at + 8 > fsize) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        if (h->seek(h->user, rd, at, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
        m = h->peek(h->user, rd, hd, 16u);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m < 8) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        size = be32(hd); type = be32(hd + 4);
        if (size == 1u) { if (m < 16) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; size = be64(hd + 8); hl = 16; }
        else if (size == 0u) { if (fsize < 0) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; size = (uint64_t)(fsize - at); }
        if (size < hl) return MAKTABASAUTI_E_CORRUPT_DATA;
        if (type == FCC_MOOV) {
            if (size - hl > MOOV_MAX) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
            moov_n = (size_t)(size - hl);
            moov = (uint8_t *)h->alloc(h->user, moov_n ? moov_n : 1u);
            if (!moov) return MAKTABASAUTI_E_NO_MEMORY;
            if (h->seek(h->user, rd, at + (int64_t)hl, 0) < 0) { h->free(h->user, moov); return MAKTABASAUTI_E_NOT_SEEKABLE; }
            while (got < moov_n) {
                m = h->read(h->user, rd, moov + got, moov_n - got);
                if (m < 0) { h->free(h->user, moov); return (maktabasauti_result_t)m; }
                if (m == 0) { h->free(h->user, moov); return MAKTABASAUTI_E_TRUNCATED; }
                got += (size_t)m;
            }
            break;
        }
        if (size > (uint64_t)INT64_MAX - (uint64_t)at) return MAKTABASAUTI_E_CORRUPT_DATA;
        at += (int64_t)size;
    }
    memset(&c, 0, sizeof c);
    while (next_box(moov, moov_n, &mp, &type, &pay, &bend)) {                         /* trak -> mdia -> minf -> stbl: pierwsza sciezka alac */
        size_t ms, me, ns, ne, ss, se, ds, de;
        if (type != FCC_TRAK) continue;
        if (!find_child(moov, pay, bend, FCC_MDIA, &ms, &me) || !find_child(moov, ms, me, FCC_MINF, &ns, &ne) || !find_child(moov, ns, ne, FCC_STBL, &ss, &se)) continue;
        if (!find_child(moov, ss, se, FCC_STSD, &ds, &de)) continue;
        if (stsd_cookie(moov, ds, de, &c, &entry_rate)) { found = 1; stbl_s = ss; stbl_e = se; break; }
    }
    if (!found) { h->free(h->user, moov); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
    if (c.frame_len == 0 || c.frame_len > FRAME_MAX || c.ch < 1u || c.ch > CH_MAX || (c.bits != 16u && c.bits != 20u && c.bits != 24u && c.bits != 32u)
        || c.kb > 24u) {                                                          /* pb / mb / kb = 0 w strumieniu verbatim (poziom 0): parametry Rice'a nieuzywane */ h->free(h->user, moov); return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
    if (c.rate == 0) c.rate = entry_rate;
    if (c.rate == 0 || c.rate > 768000u) { h->free(h->user, moov); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
    s = (alac_t *)h->alloc(h->user, sizeof *s);
    if (!s) { h->free(h->user, moov); return MAKTABASAUTI_E_NO_MEMORY; }
    memset(s, 0, sizeof *s);
    s->host = h; s->rd = rd; s->c = c;
    for (i = 0; i < c.ch; i++) s->map[i] = map_tab[c.ch - 1u][i];
    r = build_index(s, moov, stbl_s, stbl_e);
    h->free(h->user, moov);
    if (r != MAKTABASAUTI_OK) { alac_free(s); return r; }
    s->buf_cap = (size_t)c.frame_len * c.ch * 4u + 4096u;
    for (i = 0; i < 2u; i++) {
        s->mix[i] = (int32_t *)h->alloc(h->user, (size_t)c.frame_len * sizeof(int32_t));
        s->ex[i] = (uint32_t *)h->alloc(h->user, (size_t)c.frame_len * sizeof(uint32_t));
        if (!s->mix[i] || !s->ex[i]) { alac_free(s); return MAKTABASAUTI_E_NO_MEMORY; }
    }
    s->pred = (int32_t *)h->alloc(h->user, (size_t)c.frame_len * sizeof(int32_t));
    s->pcm = (int32_t *)h->alloc(h->user, (size_t)c.frame_len * c.ch * sizeof(int32_t));
    s->buf = (uint8_t *)h->alloc(h->user, s->buf_cap);
    if (!s->pred || !s->pcm || !s->buf) { alac_free(s); return MAKTABASAUTI_E_NO_MEMORY; }
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = c.rate; info->channels = c.ch;
    info->channel_mask = c.ch == 1u ? MAKTABASAUTI_LAYOUT_MONO : (c.ch == 2u ? MAKTABASAUTI_LAYOUT_STEREO : 0u);
    info->native_format = MAKTABASAUTI_FORMAT_F32; info->bits_per_sample = c.bits;
    info->total_frames = s->total;
    info->flags = MAKTABASAUTI_STREAM_SEEKABLE | MAKTABASAUTI_STREAM_LOSSLESS;
    strncpy(info->codec_name, "alac", sizeof info->codec_name - 1u);
    strncpy(info->container_name, "mp4", sizeof info->container_name - 1u);
    *state = s;
    return MAKTABASAUTI_OK;
}

/* wczytuje i dekoduje pakiet `idx` do s->pcm */
static maktabasauti_result_t load_packet(alac_t *s, uint32_t idx) {
    const maktabasauti_host_t *h = s->host; size_t got = 0, n; int64_t m, fr;
    n = s->sizes[idx];
    if (n == 0 || n > s->buf_cap) return MAKTABASAUTI_E_CORRUPT_DATA;
    if (s->offs[idx] > (uint64_t)INT64_MAX || h->seek(h->user, s->rd, (int64_t)s->offs[idx], 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    while (got < n) {
        m = h->read(h->user, s->rd, s->buf + got, n - got);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_TRUNCATED;
        got += (size_t)m;
    }
    fr = decode_packet(s, s->buf, n);
    if (fr == -2) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    if (fr < 0) return MAKTABASAUTI_E_CORRUPT_DATA;
    s->pkt_frames = (uint32_t)fr;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL alac_read(void *state, void *dst, size_t n, size_t *frames) {
    alac_t *s = (alac_t *)state; float *o = (float *)dst; const unsigned ch = s->c.ch; const float scale = 1.0f / (float)(1u << (s->c.bits - 1u));
    size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (s->pend != MAKTABASAUTI_OK) return s->pend;
    while (done < n) {
        size_t take, i;
        if (s->pos >= s->pkt_frames) {
            uint32_t idx;
            if (s->next_pkt >= s->npkt) { r = MAKTABASAUTI_EOF; break; }
            idx = s->next_pkt++; s->pos = 0; s->pkt_frames = 0;
            r = load_packet(s, idx);
            if (r != MAKTABASAUTI_OK) break;
            if (s->skip) { s->pos = s->skip < s->pkt_frames ? (uint32_t)s->skip : s->pkt_frames; s->skip = 0; }
            continue;
        }
        take = s->pkt_frames - s->pos;
        if (take > n - done) take = n - done;
        for (i = 0; i < take * ch; i++) o[(done * ch) + i] = (float)s->pcm[(size_t)s->pos * ch + i] * scale;
        s->pos += (uint32_t)take; done += take;
    }
    *frames = done;
    if (done) { if (r != MAKTABASAUTI_OK && r != MAKTABASAUTI_EOF) s->pend = r; return MAKTABASAUTI_OK; }
    if (r != MAKTABASAUTI_OK && r != MAKTABASAUTI_EOF) s->pend = r;
    return r;
}

static maktabasauti_result_t MAKTABASAUTI_CALL alac_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    alac_t *s = (alac_t *)state; (void)flags;
    if (frame > s->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    if (frame == s->total) { s->next_pkt = s->npkt; s->pos = s->pkt_frames = 0; s->skip = 0; }
    else { s->next_pkt = (uint32_t)(frame / s->c.frame_len); s->skip = frame % s->c.frame_len; s->pos = s->pkt_frames = 0; }
    s->pend = MAKTABASAUTI_OK; *actual = frame;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL alac_reset(void *state) { uint64_t a; return alac_seek(state, 0, 0, &a); }
static maktabasauti_result_t MAKTABASAUTI_CALL alac_flush(void *state) { ((alac_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static void MAKTABASAUTI_CALL alac_close(void *state) { if (state) alac_free((alac_t *)state); }

const maktabasauti_codec_t maktabasauti_alac_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "alac", "Apple Lossless (ALAC) in MP4 / M4A", "maktabasauti_alac",
    MAKTABASAUTI_CODEC_FLAG_NEEDS_SEEKABLE_INPUT | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0, NULL,
    alac_probe, alac_open, alac_read, alac_seek, alac_reset, alac_flush, NULL, NULL, NULL, alac_close, { NULL }, { NULL }
};
