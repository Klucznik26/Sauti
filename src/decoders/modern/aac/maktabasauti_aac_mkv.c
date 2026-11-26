/* MaktabaSauti, modul maktabasauti_aac: AAC w Matroska / WebM (EBML) dla wspolnego silnika.  Plik jest czytany JEDEN RAZ, sekwencyjnie (takze z wejscia nieprzewijalnego): elementy sa
 * splaszczone (Segment, Cluster i BlockGroup nie sa omijane, ich rozmiary nie sa potrzebne, wiec dziala tez rozmiar nieznany), Tracks jest parsowane w pamieci, a z kazdego
 * SimpleBlock / Block pierwszej sciezki A_AAC* zostaja jednostki dostepu (z lacingiem Xiph / EBML / staly) na stercie hosta (maktabasauti_aac_aulist.h).  AudioSpecificConfig =
 * CodecPrivate; bez niego (stare pliki) ASC powstaje z CodecID (A_AAC/MPEG2|4/MAIN|LC|SSR|LTP), SamplingFrequency i Channels.  Nieobslugiwane (fail-closed): ContentEncodings
 * (kompresja / szyfrowanie sciezki), rozmiar danych > AULIST_MAX_BYTES.  Brak przyciecia opoznienia kodera (CodecDelay / DiscardPadding nie sa uzywane). */
#include "maktabasauti_aac_aulist.h"

typedef struct { dec_t *d; const maktabasauti_host_t *h; maktabasauti_result_t err; uint64_t pos; } mkr_t;

static int mk_read(mkr_t *r, uint8_t *p, size_t n) {
    size_t got = 0;
    while (got < n) {
        int64_t m = r->h->read(r->h->user, r->d->rd, p + got, n - got);
        if (m < 0) { r->err = (maktabasauti_result_t)m; return 0; }
        if (m == 0) { r->err = got || n ? MAKTABASAUTI_EOF : MAKTABASAUTI_OK; return 0; }
        got += (size_t)m;
    }
    r->pos += n;
    return 1;
}
static int mk_skip(mkr_t *r, uint64_t n) {
    uint8_t b[1024];
    while (n) { size_t w = n < sizeof b ? (size_t)n : sizeof b; if (!mk_read(r, b, w)) return 0; n -= w; }
    return 1;
}
/* element id (marker kept) */
static int mk_id(mkr_t *r, uint32_t *id) {
    uint8_t b; unsigned len, i; uint32_t v;
    if (!mk_read(r, &b, 1)) return 0;
    if (b & 0x80u) len = 1; else if (b & 0x40u) len = 2; else if (b & 0x20u) len = 3; else if (b & 0x10u) len = 4; else { r->err = MAKTABASAUTI_E_CORRUPT_DATA; return 0; }
    v = b;
    for (i = 1; i < len; i++) { uint8_t c; if (!mk_read(r, &c, 1)) { if (r->err == MAKTABASAUTI_EOF) r->err = MAKTABASAUTI_E_TRUNCATED; return 0; } v = (v << 8) | c; }
    *id = v; return 1;
}
/* data size; *unknown = all ones */
static int mk_size(mkr_t *r, uint64_t *size, int *unknown) {
    uint8_t b; unsigned len = 1, i; uint64_t v; uint8_t mask = 0x80u;
    if (!mk_read(r, &b, 1)) { if (r->err == MAKTABASAUTI_EOF) r->err = MAKTABASAUTI_E_TRUNCATED; return 0; }
    while (len <= 8u && !(b & mask)) { len++; mask >>= 1; }
    if (len > 8u) { r->err = MAKTABASAUTI_E_CORRUPT_DATA; return 0; }
    v = b & (uint8_t)(mask - 1u);
    { uint64_t all = (uint64_t)(mask - 1u);
      for (i = 1; i < len; i++) { uint8_t c; if (!mk_read(r, &c, 1)) { r->err = MAKTABASAUTI_E_TRUNCATED; return 0; } v = (v << 8) | c; all = (all << 8) | 0xffu; }
      *unknown = (v == all); }
    *size = v; return 1;
}
/* in-memory element walk (Tracks) */
typedef struct { const uint8_t *p; size_t n, at; } mm_t;
static int mm_vint(mm_t *m, uint64_t *v, int is_id) {
    uint8_t b, mask = 0x80u; unsigned len = 1, i;
    if (m->at >= m->n) return 0;
    b = m->p[m->at++];
    while (len <= 8u && !(b & mask)) { len++; mask >>= 1; }
    if (len > 8u || m->at + len - 1u > m->n) return 0;
    *v = is_id ? b : (uint64_t)(b & (uint8_t)(mask - 1u));
    for (i = 1; i < len; i++) *v = (*v << 8) | m->p[m->at++];
    return 1;
}
static int mm_next(mm_t *m, uint32_t *id, const uint8_t **data, size_t *size) {
    uint64_t i, s;
    if (!mm_vint(m, &i, 1) || !mm_vint(m, &s, 0)) return 0;
    if (s > m->n - m->at) return 0;
    *id = (uint32_t)i; *data = m->p + m->at; *size = (size_t)s; m->at += (size_t)s;
    return 1;
}
static uint64_t be_uint(const uint8_t *p, size_t n) { uint64_t v = 0; size_t i; for (i = 0; i < n && i < 8u; i++) v = (v << 8) | p[i]; return v; }
static double be_float(const uint8_t *p, size_t n) {
    if (n == 4) { uint32_t u = (uint32_t)be_uint(p, 4); float f; memcpy(&f, &u, 4); return (double)f; }
    if (n == 8) { uint64_t u = be_uint(p, 8); double f; memcpy(&f, &u, 8); return f; }
    return 0.0;
}

typedef struct {
    uint64_t number; unsigned type; char codec[40]; uint8_t priv[512]; size_t priv_len; double rate, out_rate; unsigned channels; int encoded;
} track_t;
static int parse_track(const uint8_t *p, size_t n, track_t *t) {
    mm_t m; uint32_t id; const uint8_t *dp; size_t ds;
    m.p = p; m.n = n; m.at = 0; memset(t, 0, sizeof *t);
    while (m.at < m.n) {
        if (!mm_next(&m, &id, &dp, &ds)) return 0;
        switch (id) {
        case 0xD7: t->number = be_uint(dp, ds); break;
        case 0x83: t->type = (unsigned)be_uint(dp, ds); break;
        case 0x86: { size_t k = ds < sizeof t->codec - 1u ? ds : sizeof t->codec - 1u; memcpy(t->codec, dp, k); t->codec[k] = 0; break; }
        case 0x63A2: if (ds > sizeof t->priv) t->priv_len = (size_t)-1; else { memcpy(t->priv, dp, ds); t->priv_len = ds; } break;
        case 0x6D80: t->encoded = 1; break;
        case 0xE1: {
            mm_t a; uint32_t aid; const uint8_t *ap; size_t as;
            a.p = dp; a.n = ds; a.at = 0;
            while (a.at < a.n) {
                if (!mm_next(&a, &aid, &ap, &as)) return 0;
                if (aid == 0xB5) t->rate = be_float(ap, as); else if (aid == 0x78B5) t->out_rate = be_float(ap, as); else if (aid == 0x9F) t->channels = (unsigned)be_uint(ap, as);
            }
            break; }
        default: break;
        }
    }
    return 1;
}
static const unsigned asc_rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
/* ASC for tracks without CodecPrivate: AOT from the CodecID, rate and channels from the Audio element */
static size_t make_asc(const track_t *t, uint8_t out[4]) {
    unsigned aot = 2, ri = 99, i; const unsigned rate = (unsigned)(t->rate + 0.5);
    if (strstr(t->codec, "MAIN")) aot = 1; else if (strstr(t->codec, "SSR")) aot = 3; else if (strstr(t->codec, "LTP")) aot = 4;
    for (i = 0; i < 13u; i++) if (asc_rates[i] == rate) ri = i;
    if (ri == 99u || t->channels < 1u || t->channels > 7u) return 0;
    out[0] = (uint8_t)((aot << 3) | (ri >> 1)); out[1] = (uint8_t)(((ri & 1u) << 7) | (t->channels << 3));
    return 2;
}
static int add_frame(dec_t *d, aulist_t *L, const uint8_t *p, size_t n) {
    size_t at;
    if (!n) return 1;
    at = au_append(d, L, p, n);
    if (at == (size_t)-1) return 0;
    return au_add(d, L, at, n);
}
/* a block payload (after the track number): timecode(2), flags(1), laced frames */
static maktabasauti_result_t block_frames(dec_t *d, aulist_t *L, const uint8_t *p, size_t n) {
    unsigned lacing; size_t at = 3, nframes, i; size_t sizes[256];
    if (n < 4) return MAKTABASAUTI_E_CORRUPT_DATA;
    lacing = (p[2] >> 1) & 3u;
    if (lacing == 0) return add_frame(d, L, p + at, n - at) ? MAKTABASAUTI_OK : MAKTABASAUTI_E_NO_MEMORY;
    nframes = (size_t)p[at++] + 1u;
    if (lacing == 2u) {                                                      /* fixed-size */
        size_t each = (n - at) / nframes;
        if (!each || each * nframes != n - at) return MAKTABASAUTI_E_CORRUPT_DATA;
        for (i = 0; i < nframes; i++) if (!add_frame(d, L, p + at + i * each, each)) return MAKTABASAUTI_E_NO_MEMORY;
        return MAKTABASAUTI_OK;
    }
    { size_t total = 0;
      if (lacing == 1u) {                                                    /* Xiph */
          for (i = 0; i + 1u < nframes; i++) { size_t s = 0; uint8_t b; do { if (at >= n) return MAKTABASAUTI_E_CORRUPT_DATA; b = p[at++]; s += b; } while (b == 255u); sizes[i] = s; total += s; }
      } else {                                                               /* EBML */
          mm_t m; uint64_t v; int64_t prev = 0;
          m.p = p; m.n = n; m.at = at;
          for (i = 0; i + 1u < nframes; i++) {
              const size_t start = m.at; unsigned len = 1; uint8_t mask = 0x80u;
              if (!mm_vint(&m, &v, 0)) return MAKTABASAUTI_E_CORRUPT_DATA;
              while (len <= 8u && !(p[start] & mask)) { len++; mask >>= 1; }
              if (i == 0) prev = (int64_t)v; else prev += (int64_t)v - ((int64_t)1 << (7u * len - 1u)) + 1;
              if (prev < 0) return MAKTABASAUTI_E_CORRUPT_DATA;
              sizes[i] = (size_t)prev; total += (size_t)prev;
          }
          at = m.at;
      }
      if (total > n - at) return MAKTABASAUTI_E_CORRUPT_DATA;
      sizes[nframes - 1u] = n - at - total;
      for (i = 0; i < nframes; i++) { if (!add_frame(d, L, p + at, sizes[i])) return MAKTABASAUTI_E_NO_MEMORY; at += sizes[i]; }
    }
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t mkv_prepare(dec_t *d) {
    aulist_t *L; mkr_t r; uint32_t id; uint64_t size; int unknown, have_track = 0; uint64_t track_no = 0;
    maktabasauti_result_t res = MAKTABASAUTI_OK;
    L = (aulist_t *)au_alloc(d, sizeof *L);
    if (!L) return MAKTABASAUTI_E_NO_MEMORY;
    memset(L, 0, sizeof *L); d->cptr = L;
    r.d = d; r.h = d->host; r.err = MAKTABASAUTI_OK; r.pos = 0;
    d->seekable = 1;                                                         /* the list is in memory: virtual positions seek */
    for (;;) {
        if (!mk_id(&r, &id)) { if (r.err == MAKTABASAUTI_EOF) break; res = r.err; goto bad; }
        if (!mk_size(&r, &size, &unknown)) { res = r.err == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r.err; goto bad; }
        if (id == 0x18538067u || id == 0x1F43B675u || id == 0xA0) continue;       /* Segment, Cluster, BlockGroup: the children follow */
        if (unknown) { res = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto bad; }
        if (id == 0x1654AE6Bu) {                                             /* Tracks */
            uint8_t *buf; mm_t m; uint32_t eid; const uint8_t *ep; size_t es;
            if (size > ((uint64_t)4 << 20)) { res = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto bad; }
            buf = (uint8_t *)au_alloc(d, (size_t)size);
            if (!buf) { res = MAKTABASAUTI_E_NO_MEMORY; goto bad; }
            if (!mk_read(&r, buf, (size_t)size)) { d->host->free(d->host->user, buf); res = r.err == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r.err; goto bad; }
            m.p = buf; m.n = (size_t)size; m.at = 0;
            while (!have_track && m.at < m.n && mm_next(&m, &eid, &ep, &es)) {
                track_t *t;
                if (eid != 0xAE) continue;
                t = (track_t *)au_alloc(d, sizeof *t);
                if (!t) { d->host->free(d->host->user, buf); res = MAKTABASAUTI_E_NO_MEMORY; goto bad; }
                if (parse_track(ep, es, t) && t->type == 2u && !strncmp(t->codec, "A_AAC", 5)) {
                    uint8_t asc[4]; const uint8_t *ap = t->priv; size_t an = t->priv_len;
                    if (t->encoded || an == (size_t)-1) { d->host->free(d->host->user, t); d->host->free(d->host->user, buf); res = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto bad; }
                    if (!an) { an = make_asc(t, asc); ap = asc; }
                    if (!an || !sauti_aac_asc_info(ap, an, &L->mi)) { d->host->free(d->host->user, t); d->host->free(d->host->user, buf); res = MAKTABASAUTI_E_UNSUPPORTED_FORMAT; goto bad; }
                    track_no = t->number; have_track = 1;
                }
                d->host->free(d->host->user, t);
            }
            d->host->free(d->host->user, buf);
            continue;
        }
        if (id == 0xA3 || id == 0xA1) {                                      /* SimpleBlock, Block */
            if (!have_track) { if (!mk_skip(&r, size)) { res = r.err == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r.err; goto bad; } continue; }
            if (size < 4u || size > ((uint64_t)1 << 24)) { if (!mk_skip(&r, size)) { res = MAKTABASAUTI_E_TRUNCATED; goto bad; } continue; }
            {   /* the track number first: blocks of other tracks (video) are skipped without being stored */
                uint8_t tb[8]; unsigned tl = 1; uint8_t mask = 0x80u; uint64_t tnum;
                if (!mk_read(&r, tb, 1)) { res = r.err == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r.err; goto bad; }
                while (tl <= 8u && !(tb[0] & mask)) { tl++; mask >>= 1; }
                if (tl > 8u || tl >= size || (tl > 1u && !mk_read(&r, tb + 1, tl - 1u))) { res = MAKTABASAUTI_E_CORRUPT_DATA; goto bad; }
                tnum = tb[0] & (uint8_t)(mask - 1u);
                { unsigned i; for (i = 1; i < tl; i++) tnum = (tnum << 8) | tb[i]; }
                if (tnum != track_no) { if (!mk_skip(&r, size - tl)) { res = MAKTABASAUTI_E_TRUNCATED; goto bad; } continue; }
                { size_t rest = (size_t)(size - tl); uint8_t *buf = (uint8_t *)au_alloc(d, rest);
                  if (!buf) { res = MAKTABASAUTI_E_NO_MEMORY; goto bad; }
                  if (!mk_read(&r, buf, rest)) { d->host->free(d->host->user, buf); res = r.err == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r.err; goto bad; }
                  res = block_frames(d, L, buf, rest);
                  d->host->free(d->host->user, buf);
                  if (res != MAKTABASAUTI_OK) goto bad; }
            }
            continue;
        }
        if (!mk_skip(&r, size)) { res = r.err == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r.err; goto bad; }
    }
    if (!have_track || !L->count) { res = MAKTABASAUTI_E_UNSUPPORTED_FORMAT; goto bad; }
    aac_au_setup(d, &L->mi);
    return MAKTABASAUTI_OK;
bad:
    au_release_list(d);
    return res == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_UNSUPPORTED_FORMAT : res;
}
static void mkv_release(dec_t *d) { au_release_list(d); }

const aac_container_t aac_mkv_container = { "matroska", au_fetch, au_resync, au_scan, NULL, NULL, mkv_release, mkv_prepare, au_seek_to, au_position, NULL, 1 };

/* EBML header with DocType matroska / webm in the first bytes */
int aac_mkv_probe(const uint8_t *p, size_t n) {
    size_t i;
    if (!p || n < 16u || p[0] != 0x1Au || p[1] != 0x45u || p[2] != 0xDFu || p[3] != 0xA3u) return 0;
    for (i = 4; i + 8u <= n && i < 128u; i++) if (!memcmp(p + i, "matroska", 8) || (i + 4u <= n && !memcmp(p + i, "webm", 4))) return 1;
    return 0;
}
