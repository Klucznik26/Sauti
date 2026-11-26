/* MaktabaSauti, modul maktabasauti_vorbis: Ogg Vorbis I, napisany od zera.
 * Opis formatu: Vorbis I specification, Xiph.Org, 4 lipca 2020.
 * Tablica floor1_inverse_dB jest z sekcji 10.1 tej specyfikacji.
 * Ciala funkcji z stb_vorbis, libvorbis i FFmpeg nie sa wklejone.
 * RTP (RFC 5215), ID3 przed Ogg i ReplayGain sa poza tym modulem.
 * Kanaly 1 i 2 maja maske WAVE. Od 3 w gore maska jest 0, a bufor
 * zostaje w kolejnosci Vorbis I sekcja 4.3.9. */
#include "maktabasauti/maktabasauti_codec.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const double V_PI = 3.14159265358979323846;

enum {
    V_MAX_CH = 32,
    V_MAX_DIM = 16,
    V_MAX_ENT = 16384,
    V_MAX_PKT = 2000000,
    V_MAX_PKT_BYTES = 1 << 20,
    V_MAX_LINKS = 32,
    V_MAX_CMT = 65536,
    V_MAX_CMT_ONE = 1 << 20,
    V_MAX_CMT_ALL = 8 << 20,
    V_MAX_VQ = 1 << 20,
    V_MAX_FILE = 1 << 30,                 /* the whole file is read into memory (the end of the stream gives the length): more than 1 GiB is refused */
    BR_OK = 0,
    BR_EOP = 1,
    BR_BAD = 2,
    BR_UNUSED = 3,
    PK_AUDIO = 0,
    PK_IGNORE = 1,
    PK_DISCARD = 2
};

/* Jedyna korekta skali IMDCT. Specyfikacja odsyla do Sporera i nie podaje czynnika.
 * 1 oznacza y = (1/N) * suma, N = blocksize/2. */
static const double IMDCT_K = 1.0;

#include "maktabasauti_vorbis_floor1.inc"
_Static_assert(sizeof fl_inv_db / sizeof fl_inv_db[0] == 256, "floor1 table");

typedef struct { const uint8_t *p; size_t nbits, bit; int eop; } Br;
typedef struct { uint32_t code; int entry; } HCode;

typedef struct {
    int dim, entries, lookup, single, used;
    int lo[33], cnt[33];
    HCode *tab;
    float *vq;
} Book;

typedef struct {
    int order, rate, bark, amp_bits, amp_off, nbooks;
    int books[16];
} Fl0;

typedef struct {
    int parts, mult, values;
    uint8_t cls[32], dim[16], sub[16];
    int master[16], subbook[16][8], x[65];
} Fl1;

typedef struct { int type; Fl0 f0; Fl1 f1; } Floor;

typedef struct {
    int type, begin, end, part, classes, classbook;
    uint8_t cascade[64];
    int books[64][8];
} Res;

typedef struct {
    int submaps, coupling;
    uint8_t mux[V_MAX_CH], fl[16], rs[16];
    uint8_t *mag, *ang;
} Map;

typedef struct { int blockflag, mapping; } Mode;

typedef struct {
    size_t off, len;
    int64_t granule;
    int has_g, page;
} Pkt;

typedef struct {
    int channels, bs0, bs1, nbooks, nfloors, nres, nmaps, nmodes, ncmt;
    int missing_eos, gapless, holes;
    uint32_t rate, bitrate;
    int32_t nominal;
    uint64_t total, head, tail;
    size_t pkt0, npkt;
    Book *books;
    Floor *floors;
    Res *res;
    Map *maps;
    Mode *modes;
    char *vendor;
    char **cmt;
} Link;

/* IMDCT through a DCT-IV of N points computed with an N/2-point complex FFT; the tables are made once per block size */
enum { ITAB_MAX = 2 * V_MAX_LINKS, WIN_MAX = 6 * V_MAX_LINKS };
typedef struct { int N; double *pc, *ps, *wc, *ws; } ImdctTab;     /* pre / post twiddles (N/2), FFT twiddles (N/4) */
typedef struct { int n, sl, sr; float *w; } WinC;                   /* windows already built: size, short left, short right */

typedef struct {
    const maktabasauti_host_t *host;
    uint8_t *file;
    size_t file_n;
    uint8_t *blob;
    size_t blob_n, blob_cap;
    Pkt *pkts;
    size_t pkt_n, pkt_cap;
    void **bag;
    size_t bag_n, bag_cap;
    size_t vq_floats;
    Link links[V_MAX_LINKS];
    int nlinks, link;
    int max_ch, max_bs, prev_n, link_done;
    size_t n2_stride, pkt_i;
    uint64_t head_left, out_pos;
    float *pcm, *lap, *time, *spec, *residue, *inter, *window;
    int *curve, *fmap;
    uint8_t *cls;
    ImdctTab tabs[ITAB_MAX];
    int ntabs;
    WinC wins[WIN_MAX];
    int nwins;
    double *fre, *fim;
    size_t pcm_n, pcm_i;
    maktabasauti_result_t pend;
    const char *err;
} Vorb;

static maktabasauti_result_t verr(Vorb *v, maktabasauti_result_t r, const char *m) {
    v->err = m;
    return r;
}

static void *va(Vorb *v, size_t n) {
    const maktabasauti_host_t *h = v->host;
    void *p;
    if (v->bag_n == v->bag_cap) {
        size_t nc = v->bag_cap ? v->bag_cap * 2u : 32u;
        void **nb;
        if (!v->bag) nb = (void **)h->alloc(h->user, nc * sizeof(void *));
        else nb = (void **)h->realloc(h->user, v->bag, nc * sizeof(void *));
        if (!nb) return NULL;
        v->bag = nb;
        v->bag_cap = nc;
    }
    p = h->alloc(h->user, n ? n : 1u);
    if (!p) return NULL;
    memset(p, 0, n ? n : 1u);
    v->bag[v->bag_n++] = p;
    return p;
}

static void vorbis_free(Vorb *v) {
    const maktabasauti_host_t *h;
    size_t i;
    if (!v) return;
    h = v->host;
    if (v->bag) {
        for (i = 0; i < v->bag_n; i++) h->free(h->user, v->bag[i]);
        h->free(h->user, v->bag);
    }
    if (v->file) h->free(h->user, v->file);
    if (v->blob) h->free(h->user, v->blob);
    if (v->pkts) h->free(h->user, v->pkts);
    h->free(h->user, v);
}

static int br_u(Br *b, int n, uint32_t *o) {
    uint32_t v = 0;
    int i;
    if (b->eop) return -1;
    if (n == 0) { *o = 0; return 0; }
    if (n < 0 || n > 32) { b->eop = 1; return -1; }
    if (b->bit + (size_t)n > b->nbits) { b->eop = 1; return -1; }
    for (i = 0; i < n; i++) {
        size_t pos = b->bit++;
        v |= (uint32_t)((b->p[pos >> 3] >> (pos & 7)) & 1u) << i;
    }
    *o = v;
    return 0;
}

static int br_tail0(Br *b) {
    while (b->bit < b->nbits) {
        uint32_t bit;
        if (br_u(b, 1, &bit)) return -1;
        if (bit) return -1;
    }
    return 0;
}

static int ilog(int x) {
    int r = 0;
    if (x <= 0) return 0;
    while (x > 0) { r++; x >>= 1; }
    return r;
}

static int iabs(int x) { return x < 0 ? -x : x; }

static float float32_unpack(uint32_t x) {
    int32_t mant = (int32_t)(x & 0x1fffffu);
    int exp = (int)((x & 0x7fe00000u) >> 21);
    if (x & 0x80000000u) mant = -mant;
    return (float)ldexp((double)mant, exp - 788);
}

static int lookup1_values(int entries, int dim) {
    int lo, hi;
    if (dim <= 1) return entries;
    lo = 1;
    hi = entries + 1;
    while (lo + 1 < hi) {
        int mid = lo + (hi - lo) / 2;
        int k, p = 1, ok = 1;
        for (k = 0; k < dim; k++) {
            if (p > entries / mid) { ok = 0; break; }
            p *= mid;
        }
        if (ok && p <= entries) lo = mid;
        else hi = mid;
    }
    return lo;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n) {
    size_t i;
    int b;
    for (i = 0; i < n; i++) {
        crc ^= (uint32_t)p[i] << 24;
        for (b = 0; b < 8; b++)
            crc = (crc & 0x80000000u) ? ((crc << 1) ^ 0x04C11DB7u) : (crc << 1);
    }
    return crc;
}

static int ogg_crc_ok(const uint8_t *page, size_t n, uint32_t stored) {
    uint8_t z[4] = {0, 0, 0, 0};
    uint32_t c;
    if (n < 26) return 0;
    c = crc_update(0, page, 22);
    c = crc_update(c, z, 4);
    c = crc_update(c, page + 26, n - 26);
    return c == stored;
}

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int64_t rd_le64(const uint8_t *p) {
    uint64_t lo = rd_le32(p), hi = rd_le32(p + 4);
    return (int64_t)(lo | (hi << 32));
}

static int book_entry(Br *b, const Book *bk, int *entry) {
    uint32_t acc = 0;
    int L;
    if (bk->single >= 0) {
        uint32_t bit;
        if (br_u(b, 1, &bit)) return BR_EOP;
        *entry = bk->single;
        return BR_OK;
    }
    for (L = 1; L <= 32; L++) {
        uint32_t bit;
        int lo, hi, n;
        if (br_u(b, 1, &bit)) return BR_EOP;
        acc = (acc << 1) | bit;
        n = bk->cnt[L];
        if (!n) continue;
        lo = bk->lo[L];
        hi = lo + n;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (bk->tab[mid].code < acc) lo = mid + 1;
            else hi = mid;
        }
        if (lo < bk->lo[L] + n && bk->tab[lo].code == acc) {
            *entry = bk->tab[lo].entry;
            return BR_OK;
        }
    }
    return BR_BAD;
}

static int book_vq(Br *b, const Book *bk, float *dst) {
    int e, i, r;
    if (!bk->lookup) return BR_BAD;
    r = book_entry(b, bk, &e);
    if (r) return r;
    for (i = 0; i < bk->dim; i++) dst[i] = bk->vq[(size_t)e * (size_t)bk->dim + (size_t)i];
    return BR_OK;
}

static int huff_build(Vorb *v, Book *bk, const uint8_t *len) {
    const maktabasauti_host_t *h = v->host;
    uint32_t marker[33];
    uint32_t *code;
    uint64_t acc = 0;
    int i, j, used = 0, single = -1, run;
    code = (uint32_t *)h->alloc(h->user, (size_t)bk->entries * sizeof(uint32_t));
    if (!code) return -1;
    memset(marker, 0, sizeof marker);
    memset(code, 0, (size_t)bk->entries * sizeof(uint32_t));
    for (i = 0; i < bk->entries; i++) {
        unsigned L = len[i];
        uint32_t entry, e2;
        if (L == 0) continue;
        if (L > 32) { h->free(h->user, code); return -2; }
        entry = marker[L];
        if (L == 32) {
            if (entry != 0) { h->free(h->user, code); return -2; }
        } else if ((entry >> L) != 0) {
            h->free(h->user, code);
            return -2;
        }
        code[i] = entry;
        used++;
        single = i;
        for (j = (int)L; j > 0; j--) {
            if (marker[j] & 1u) {
                if (j == 1) marker[1]++;
                else marker[j] = marker[j - 1] << 1;
                break;
            }
            marker[j]++;
        }
        e2 = entry;
        for (j = (int)L + 1; j <= 32; j++) {
            if ((marker[j] >> 1) == e2) {
                e2 = marker[j];
                marker[j] = marker[j - 1] << 1;
            } else break;
        }
        acc += 1ull << (32u - L);
    }
    if (used == 1) {
        if (len[single] != 1) { h->free(h->user, code); return -2; }
        bk->single = single;
    } else if (used == 0 || acc != (1ull << 32)) {
        h->free(h->user, code);
        return -2;
    } else bk->single = -1;
    bk->used = used;
    memset(bk->cnt, 0, sizeof bk->cnt);
    for (i = 0; i < bk->entries; i++) if (len[i]) bk->cnt[len[i]]++;
    run = 0;
    for (i = 1; i <= 32; i++) { bk->lo[i] = run; run += bk->cnt[i]; bk->cnt[i] = 0; }
    if (used) {
        bk->tab = (HCode *)va(v, (size_t)used * sizeof(HCode));
        if (!bk->tab) { h->free(h->user, code); return -1; }
        for (i = 0; i < bk->entries; i++) {
            int L, pos;
            if (!len[i]) continue;
            L = len[i];
            pos = bk->lo[L] + bk->cnt[L]++;
            bk->tab[pos].code = code[i];
            bk->tab[pos].entry = i;
        }
        for (i = 1; i <= 32; i++) {
            int a = bk->lo[i], n = bk->cnt[i], s;
            for (s = 1; s < n; s++) {
                HCode key = bk->tab[a + s];
                int t = s;
                while (t > 0 && bk->tab[a + t - 1].code > key.code) {
                    bk->tab[a + t] = bk->tab[a + t - 1];
                    t--;
                }
                bk->tab[a + t] = key;
            }
        }
    }
    h->free(h->user, code);
    return 0;
}

static int vq_fill(Vorb *v, Book *bk, const uint32_t *mult, int lookup_values, float minv, float del, int seq) {
    int e, i;
    if (bk->lookup == 0) return 0;
    if ((size_t)bk->entries * (size_t)bk->dim > V_MAX_VQ - v->vq_floats) return -3;
    bk->vq = (float *)va(v, (size_t)bk->entries * (size_t)bk->dim * sizeof(float));
    if (!bk->vq) return -1;
    v->vq_floats += (size_t)bk->entries * (size_t)bk->dim;
    for (e = 0; e < bk->entries; e++) {
        float last = 0.f;
        uint64_t divis = 1;
        for (i = 0; i < bk->dim; i++) {
            uint32_t m;
            float val;
            if (bk->lookup == 1) {
                uint64_t off = divis ? ((uint64_t)e / divis) % (uint64_t)lookup_values : 0;
                m = mult[off];
                if (divis && (uint64_t)lookup_values > ~0ull / divis) divis = 0;
                else divis *= (uint64_t)lookup_values;
            } else m = mult[(size_t)e * (size_t)bk->dim + (size_t)i];
            val = (float)m * del + minv + last;
            if (seq) last = val;
            bk->vq[(size_t)e * (size_t)bk->dim + (size_t)i] = val;
        }
    }
    return 0;
}

static maktabasauti_result_t read_book(Vorb *v, Br *b, Book *bk) {
    uint32_t sync, dim, ent, ordered, sparse = 0, lookup;
    uint8_t *lens;
    int i, rc;
    if (br_u(b, 24, &sync) || sync != 0x564342u) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "zla synchronizacja ksiazki");
    if (br_u(b, 16, &dim) || br_u(b, 24, &ent) || br_u(b, 1, &ordered)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana ksiazka");
    if (dim == 0 || ent == 0) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "pusta ksiazka");
    if (dim > V_MAX_DIM || ent > V_MAX_ENT) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "ksiazka ponad limit");
    bk->dim = (int)dim;
    bk->entries = (int)ent;
    bk->single = -1;
    lens = (uint8_t *)va(v, ent);
    if (!lens) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    if (!ordered) {
        if (br_u(b, 1, &sparse)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana ksiazka");
        for (i = 0; i < (int)ent; i++) {
            uint32_t used = 1, L;
            if (sparse && br_u(b, 1, &used)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana ksiazka");
            if (!used) { lens[i] = 0; continue; }
            if (br_u(b, 5, &L)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana ksiazka");
            lens[i] = (uint8_t)(L + 1u);
        }
    } else {
        uint32_t cur = 0, clen;
        if (br_u(b, 5, &clen)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana ksiazka");
        clen += 1u;
        while (cur < ent) {
            uint32_t number;
            if (clen > 32) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "dlugosc kodu");
            if (br_u(b, ilog((int)ent - (int)cur), &number)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana ksiazka");
            if (number > ent - cur) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "nadmiar kodow");
            for (i = 0; i < (int)number; i++) lens[cur + (uint32_t)i] = (uint8_t)clen;
            cur += number;
            clen++;
        }
    }
    if (br_u(b, 4, &lookup)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana ksiazka");
    if (lookup > 2) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "nieznane mapowanie");
    bk->lookup = (int)lookup;
    rc = huff_build(v, bk, lens);
    if (rc == -1) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    if (rc) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "drzewo Huffmana");
    if (lookup) {
        uint32_t minb, delb, bits, seqp, nval;
        uint32_t *mult;
        int lv, fr;
        float minv, del;
        if (br_u(b, 32, &minb) || br_u(b, 32, &delb) || br_u(b, 4, &bits) || br_u(b, 1, &seqp))
            return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana ksiazka");
        bits += 1u;
        minv = float32_unpack(minb);
        del = float32_unpack(delb);
        if (lookup == 1) lv = lookup1_values((int)ent, (int)dim);
        else lv = (int)ent * (int)dim;
        if (lv <= 0) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "puste mapowanie");
        if ((size_t)lv > V_MAX_VQ) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "mapowanie ponad limit");
        mult = (uint32_t *)va(v, (size_t)lv * sizeof(uint32_t));
        if (!mult) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
        for (i = 0; i < lv; i++) {
            if (br_u(b, (int)bits, &nval)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana ksiazka");
            mult[i] = nval;
        }
        fr = vq_fill(v, bk, mult, lv, minv, del, (int)seqp);
        if (fr == -1) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
        if (fr) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "za duzo wektorow");
    }
    return MAKTABASAUTI_OK;
}

static int book_ok(const Link *L, int n) { return n >= 0 && n < L->nbooks; }

static maktabasauti_result_t read_floors(Vorb *v, Link *L, Br *b) {
    uint32_t n, i;
    if (br_u(b, 6, &n)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor");
    n += 1u;
    L->nfloors = (int)n;
    L->floors = (Floor *)va(v, n * sizeof(Floor));
    if (!L->floors) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    for (i = 0; i < n; i++) {
        uint32_t t;
        Floor *f = &L->floors[i];
        if (br_u(b, 16, &t)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor");
        f->type = (int)t;
        if (t == 0) {
            uint32_t order, rate, bark, ab, ao, nb, k;
            if (br_u(b, 8, &order) || br_u(b, 16, &rate) || br_u(b, 16, &bark) || br_u(b, 6, &ab) || br_u(b, 8, &ao) || br_u(b, 4, &nb))
                return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor 0");
            nb += 1u;
            f->f0.order = (int)order;
            f->f0.rate = (int)rate;
            f->f0.bark = (int)bark;
            f->f0.amp_bits = (int)ab;
            f->f0.amp_off = (int)ao;
            f->f0.nbooks = (int)nb;
            for (k = 0; k < nb; k++) {
                uint32_t bn;
                if (br_u(b, 8, &bn)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor 0");
                if (!book_ok(L, (int)bn)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "ksiazka floor 0");
                f->f0.books[k] = (int)bn;
            }
        } else if (t == 1) {
            uint32_t parts, mult, rangebits, k, cls;
            int values = 2, maxc = -1;
            if (br_u(b, 5, &parts)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor 1");
            f->f1.parts = (int)parts;
            for (k = 0; k < parts; k++) {
                uint32_t c;
                if (br_u(b, 4, &c)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor 1");
                f->f1.cls[k] = (uint8_t)c;
                if ((int)c > maxc) maxc = (int)c;
            }
            for (cls = 0; (int)cls <= maxc; cls++) {                       /* no partitions: maximum_class = -1, no classes (libvorbis writes such floors for LFE) */
                uint32_t dim, sub, m = 0, j, ns;
                if (br_u(b, 3, &dim) || br_u(b, 2, &sub)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor 1");
                f->f1.dim[cls] = (uint8_t)(dim + 1u);
                f->f1.sub[cls] = (uint8_t)sub;
                f->f1.master[cls] = -1;
                if (sub) {
                    if (br_u(b, 8, &m)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor 1");
                    if (!book_ok(L, (int)m)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "masterbook floor 1");
                    f->f1.master[cls] = (int)m;
                }
                ns = 1u << sub;
                for (j = 0; j < ns; j++) {
                    uint32_t sb;
                    if (br_u(b, 8, &sb)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor 1");
                    if (sb == 0) f->f1.subbook[cls][j] = -1;
                    else if (!book_ok(L, (int)sb - 1)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "ksiazka floor 1");
                    else f->f1.subbook[cls][j] = (int)sb - 1;
                }
            }
            if (br_u(b, 2, &mult) || br_u(b, 4, &rangebits)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor 1");
            f->f1.mult = (int)mult + 1;
            f->f1.x[0] = 0;
            f->f1.x[1] = 1 << rangebits;
            for (k = 0; k < parts; k++) {
                int cd = f->f1.dim[f->f1.cls[k]];
                int j;
                for (j = 0; j < cd; j++) {
                    uint32_t x;
                    if (values >= 65) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "za duzo punktow floor 1");
                    if (br_u(b, (int)rangebits, &x)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany floor 1");
                    f->f1.x[values++] = (int)x;
                }
            }
            f->f1.values = values;
            for (k = 0; k < (uint32_t)values; k++) {
                uint32_t t2;
                for (t2 = k + 1; t2 < (uint32_t)values; t2++)
                    if (f->f1.x[k] == f->f1.x[t2]) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "powtorzony X floor 1");
            }
        } else return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "nieznany floor");
    }
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t read_residues(Vorb *v, Link *L, Br *b) {
    uint32_t n, i;
    if (br_u(b, 6, &n)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany residue");
    n += 1u;
    L->nres = (int)n;
    L->res = (Res *)va(v, n * sizeof(Res));
    if (!L->res) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    for (i = 0; i < n; i++) {
        uint32_t t, begin, end, part, classes, classbook, c, p;
        Res *r = &L->res[i];
        if (br_u(b, 16, &t)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany residue");
        if (t > 2) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "nieznany residue");
        if (br_u(b, 24, &begin) || br_u(b, 24, &end) || br_u(b, 24, &part) || br_u(b, 6, &classes) || br_u(b, 8, &classbook))
            return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany residue");
        r->type = (int)t;
        r->begin = (int)begin;
        r->end = (int)end;
        r->part = (int)part + 1;
        r->classes = (int)classes + 1;
        r->classbook = (int)classbook;
        if (!book_ok(L, (int)classbook)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "classbook");
        {
            int dim = L->books[classbook].dim;
            uint64_t acc = 1;
            int k;
            for (k = 0; k < dim; k++) {
                if (acc > ~0ull / (uint64_t)r->classes) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "classbook za maly");
                acc *= (uint64_t)r->classes;
            }
            if (acc > (uint64_t)L->books[classbook].entries) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "classbook za maly");
        }
        for (c = 0; c < (uint32_t)r->classes; c++) {
            uint32_t low, flag, high = 0;
            if (br_u(b, 3, &low) || br_u(b, 1, &flag)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany residue");
            if (flag && br_u(b, 5, &high)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany residue");
            r->cascade[c] = (uint8_t)(high * 8u + low);
        }
        for (c = 0; c < (uint32_t)r->classes; c++) {
            for (p = 0; p < 8; p++) {
                r->books[c][p] = -1;
                if (r->cascade[c] & (1u << p)) {
                    uint32_t bn;
                    int dim;
                    if (br_u(b, 8, &bn)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany residue");
                    if (!book_ok(L, (int)bn) || L->books[bn].lookup == 0)
                        return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "ksiazka residue");
                    dim = L->books[bn].dim;
                    if (dim == 0 || r->part % dim) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "podzial residue");
                    r->books[c][p] = (int)bn;
                }
            }
        }
    }
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t read_maps(Vorb *v, Link *L, Br *b) {
    uint32_t n, i;
    if (br_u(b, 6, &n)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana mapa");
    n += 1u;
    L->nmaps = (int)n;
    L->maps = (Map *)va(v, n * sizeof(Map));
    if (!L->maps) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    for (i = 0; i < n; i++) {
        uint32_t t, flag, sub = 1, coup = 0, reserved, j;
        Map *m = &L->maps[i];
        if (br_u(b, 16, &t) || t) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "typ mapy");
        if (br_u(b, 1, &flag)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana mapa");
        if (flag) {
            if (br_u(b, 4, &sub)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana mapa");
            sub += 1u;
        }
        m->submaps = (int)sub;
        if (br_u(b, 1, &flag)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana mapa");
        if (flag) {
            if (br_u(b, 8, &coup)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana mapa");
            coup += 1u;
            m->mag = (uint8_t *)va(v, coup);
            m->ang = (uint8_t *)va(v, coup);
            if (!m->mag || !m->ang) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
            for (j = 0; j < coup; j++) {
                uint32_t mag, ang;
                int bits = ilog(L->channels - 1);
                if (br_u(b, bits, &mag) || br_u(b, bits, &ang)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana mapa");
                if (mag == ang || mag >= (uint32_t)L->channels || ang >= (uint32_t)L->channels)
                    return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "sprzezenie");
                m->mag[j] = (uint8_t)mag;
                m->ang[j] = (uint8_t)ang;
            }
        }
        m->coupling = (int)coup;
        if (br_u(b, 2, &reserved) || reserved) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "rezerwa mapy");
        for (j = 0; j < (uint32_t)L->channels; j++) m->mux[j] = 0;
        if (sub > 1) {
            for (j = 0; j < (uint32_t)L->channels; j++) {
                uint32_t mx;
                if (br_u(b, 4, &mx)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana mapa");
                if (mx >= sub) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "mux");
                m->mux[j] = (uint8_t)mx;
            }
        }
        for (j = 0; j < sub; j++) {
            uint32_t dump, fl, rs;
            if (br_u(b, 8, &dump) || br_u(b, 8, &fl) || br_u(b, 8, &rs)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwana mapa");
            if (fl >= (uint32_t)L->nfloors || rs >= (uint32_t)L->nres) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "floor albo residue mapy");
            m->fl[j] = (uint8_t)fl;
            m->rs[j] = (uint8_t)rs;
        }
    }
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t read_modes(Vorb *v, Link *L, Br *b) {
    uint32_t n, i, framing;
    if (br_u(b, 6, &n)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany tryb");
    n += 1u;
    L->nmodes = (int)n;
    L->modes = (Mode *)va(v, n * sizeof(Mode));
    if (!L->modes) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    for (i = 0; i < n; i++) {
        uint32_t block, win, xfm, map;
        if (br_u(b, 1, &block) || br_u(b, 16, &win) || br_u(b, 16, &xfm) || br_u(b, 8, &map))
            return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany tryb");
        if (win || xfm || map >= (uint32_t)L->nmaps) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "tryb");
        L->modes[i].blockflag = (int)block;
        L->modes[i].mapping = (int)map;
    }
    if (br_u(b, 1, &framing) || !framing || br_tail0(b)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "ramka setup");
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t read_setup(Vorb *v, Link *L, Br *b) {
    uint32_t nb, nt, i, t;
    maktabasauti_result_t r;
    if (br_u(b, 8, &nb)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany setup");
    nb += 1u;
    L->nbooks = (int)nb;
    L->books = (Book *)va(v, nb * sizeof(Book));
    if (!L->books) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    for (i = 0; i < nb; i++) {
        r = read_book(v, b, &L->books[i]);
        if (r != MAKTABASAUTI_OK) return r;
    }
    if (br_u(b, 6, &nt)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany setup");
    nt += 1u;
    for (i = 0; i < nt; i++) {
        if (br_u(b, 16, &t) || t) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "czas");
    }
    r = read_floors(v, L, b);
    if (r != MAKTABASAUTI_OK) return r;
    r = read_residues(v, L, b);
    if (r != MAKTABASAUTI_OK) return r;
    r = read_maps(v, L, b);
    if (r != MAKTABASAUTI_OK) return r;
    return read_modes(v, L, b);
}

static maktabasauti_result_t read_ident(Vorb *v, Link *L, Br *b) {
    uint32_t ver, ch, rate, bs, framing;
    int32_t bmax, bnom, bmin;
    int e0, e1;
    if (br_u(b, 32, &ver) || ver) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "wersja");
    if (br_u(b, 8, &ch) || br_u(b, 32, &rate)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany ident");
    if (!ch || !rate) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "kanal albo czestotliwosc");
    if (ch > V_MAX_CH) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "za duzo kanalow");
    {
        uint32_t u0, u1, u2;
        if (br_u(b, 32, &u0) || br_u(b, 32, &u1) || br_u(b, 32, &u2) || br_u(b, 8, &bs) || br_u(b, 1, &framing))
            return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany ident");
        bmax = (int32_t)u0;
        bnom = (int32_t)u1;
        bmin = (int32_t)u2;
    }
    (void)bmax; (void)bmin;
    e0 = (int)(bs & 15u);
    e1 = (int)(bs >> 4);
    if (e0 < 6 || e0 > 13 || e1 < 6 || e1 > 13 || e0 > e1 || !framing || br_tail0(b))
        return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "blocksize");
    L->channels = (int)ch;
    L->rate = rate;
    L->nominal = bnom;
    L->bs0 = 1 << e0;
    L->bs1 = 1 << e1;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t read_comment(Vorb *v, Link *L, Br *b) {
    uint32_t vlen, nc, i, framing, total = 0;
    if (br_u(b, 32, &vlen)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany komentarz");
    if (vlen > V_MAX_CMT_ONE) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "vendor");
    L->vendor = (char *)va(v, (size_t)vlen + 1u);
    if (!L->vendor) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    for (i = 0; i < vlen; i++) {
        uint32_t c;
        if (br_u(b, 8, &c)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany komentarz");
        L->vendor[i] = (char)c;
    }
    if (br_u(b, 32, &nc)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany komentarz");
    if (nc > V_MAX_CMT) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "za duzo komentarzy");
    L->ncmt = (int)nc;
    if (nc) {
        L->cmt = (char **)va(v, (size_t)nc * sizeof(char *));
        if (!L->cmt) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    }
    for (i = 0; i < nc; i++) {
        uint32_t ln, k;
        char *s;
        if (br_u(b, 32, &ln)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany komentarz");
        if (ln > V_MAX_CMT_ONE || total > V_MAX_CMT_ALL - ln) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "komentarz");
        total += ln;
        s = (char *)va(v, (size_t)ln + 1u);
        if (!s) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
        for (k = 0; k < ln; k++) {
            uint32_t c;
            if (br_u(b, 8, &c)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "urwany komentarz");
            s[k] = (char)c;
        }
        L->cmt[i] = s;
    }
    if (br_u(b, 1, &framing) || !framing || br_tail0(b)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "ramka komentarza");
    return MAKTABASAUTI_OK;
}

static Br pkt_br(const Vorb *v, const Pkt *p) {
    Br b;
    b.p = v->blob + p->off;
    b.nbits = p->len * 8u;
    b.bit = 0;
    b.eop = 0;
    return b;
}

static int packet_head(const Link *L, Br *b, int *bs, int *prev_flag, int *next_flag, int *mode_out) {
    uint32_t type, mode, pf = 1, nf = 1;
    *prev_flag = 1;
    *next_flag = 1;
    *bs = L->bs0;
    if (mode_out) *mode_out = -1;
    if (br_u(b, 1, &type)) return PK_DISCARD;
    if (type) return PK_IGNORE;
    if (br_u(b, ilog(L->nmodes - 1), &mode)) return PK_DISCARD;
    if ((int)mode >= L->nmodes) return -1;
    if (mode_out) *mode_out = (int)mode;
    if (L->modes[mode].blockflag) {
        *bs = L->bs1;
        if (br_u(b, 1, &pf) || br_u(b, 1, &nf)) return PK_DISCARD;
        *prev_flag = (int)pf;
        *next_flag = (int)nf;
    }
    return PK_AUDIO;
}

static int header_packet(const uint8_t *p, size_t n, uint8_t type) {
    return n >= 7 && p[0] == type && memcmp(p + 1, "vorbis", 6) == 0;
}

static maktabasauti_result_t trim_link(Vorb *v, Link *L) {
    size_t i, a0 = L->pkt0 + 3u, a1 = L->pkt0 + L->npkt;
    int prev = 0, started = 0;
    uint64_t raw = 0;
    int64_t pcm0 = 0, final_g = 0;
    int got0 = 0, gotf = 0;
    size_t first_i = 0, last_i = 0;
    uint64_t head = 0, avail, declared, total;
    if (L->npkt < 3) return verr(v, MAKTABASAUTI_E_TRUNCATED, "brak naglowkow");
    for (i = a0; i < a1; i++) {
        Br b = pkt_br(v, &v->pkts[i]);
        int bs = 0, pf = 0, nf = 0;
        int k = packet_head(L, &b, &bs, &pf, &nf, NULL);
        (void)pf; (void)nf;
        if (k < 0) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "numer trybu");
        if (k == PK_AUDIO) {
            if (started) raw += (uint64_t)(prev / 4 + bs / 4);
            started = 1;
            prev = bs;
        }
        if (v->pkts[i].has_g && v->pkts[i].granule != -1 && raw > 0) {
            if (!got0) { pcm0 = v->pkts[i].granule - (int64_t)raw; got0 = 1; first_i = i; }
            final_g = v->pkts[i].granule;
            last_i = i;
            gotf = 1;
        }
    }
    /* Ujemne pcm0 to opoznienie kodera. Specyfikacja prosi, by nie przekraczalo
     * pierwszego zwrotu, ale libvorbis przy jednej granuli na koncu pliku
     * daje wieksze. Scinamy je przez kilka pakietow, byle nie dalej niz raw. */
    /* A single granule position (the last page of the stream) says where the stream ENDS: the excess is cut from the end (libvorbisfile does the same: sine.ogg, click.ogg).
     * Only a first granule that comes before the last one says where the stream starts. */
    if (got0 && first_i == last_i) { got0 = 0; pcm0 = 0; }
    if (got0 && pcm0 < 0) {
        head = (uint64_t)(-pcm0);
        if (head > raw) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "przyciecie poczatku");
    }
    avail = raw - head;
    if (!gotf) {
        L->total = avail;
        L->head = head;
        L->tail = 0;
        L->gapless = 0;
    } else {
        int64_t start = pcm0 > 0 ? pcm0 : 0;
        L->gapless = 1;
        if (final_g < start) declared = 0;
        else declared = (uint64_t)(final_g - start);
        total = declared < avail ? declared : avail;
        L->total = total;
        L->head = pcm0 < 0 ? head : 0;
        L->tail = avail - total;
    }
    if (L->nominal > 0) L->bitrate = (uint32_t)L->nominal;
    else if (L == &v->links[0] && L->total)
        L->bitrate = (uint32_t)(((uint64_t)v->file_n * 8u * L->rate) / L->total);
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t parse_link(Vorb *v, int idx) {
    Link *L = &v->links[idx];
    Pkt *base = &v->pkts[L->pkt0];
    Br b;
    maktabasauti_result_t r;
    if (L->npkt < 3) return verr(v, MAKTABASAUTI_E_TRUNCATED, "brak naglowkow");
    if (base[0].page == base[1].page) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "ident nie jest sam");
    if (L->npkt > 3 && base[2].page == base[3].page) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "setup nie konczy strony");
    if (!base[0].has_g || base[0].granule != 0) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "granula naglowka");
    if ((base[1].has_g && base[1].granule != 0) || (base[2].has_g && base[2].granule != 0))
        return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "granula naglowka");
    if (!header_packet(v->blob + base[0].off, base[0].len, 1)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "ident");
    if (!header_packet(v->blob + base[1].off, base[1].len, 3)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "komentarz");
    if (!header_packet(v->blob + base[2].off, base[2].len, 5)) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "setup");
    b = pkt_br(v, &base[0]);
    b.bit = 7u * 8u;
    r = read_ident(v, L, &b);
    if (r != MAKTABASAUTI_OK) return r;
    b = pkt_br(v, &base[1]);
    b.bit = 7u * 8u;
    r = read_comment(v, L, &b);
    if (r != MAKTABASAUTI_OK) return r;
    b = pkt_br(v, &base[2]);
    b.bit = 7u * 8u;
    r = read_setup(v, L, &b);
    if (r != MAKTABASAUTI_OK) return r;
    if (L->channels > v->max_ch) v->max_ch = L->channels;
    if (L->bs1 > v->max_bs) v->max_bs = L->bs1;
    return trim_link(v, L);
}

static int blob_add(Vorb *v, const uint8_t *p, size_t n) {
    const maktabasauti_host_t *h = v->host;
    if (v->blob_n + n < v->blob_n) return -1;
    if (v->blob_n + n > v->blob_cap) {
        size_t nc = v->blob_cap ? v->blob_cap : 4096u;
        uint8_t *nb;
        while (nc < v->blob_n + n) {
            if (nc > (SIZE_MAX / 2u)) return -1;
            nc *= 2u;
        }
        if (!v->blob) nb = (uint8_t *)h->alloc(h->user, nc);
        else nb = (uint8_t *)h->realloc(h->user, v->blob, nc);
        if (!nb) return -1;
        v->blob = nb;
        v->blob_cap = nc;
    }
    if (n) memcpy(v->blob + v->blob_n, p, n);
    v->blob_n += n;
    return 0;
}

static int add_pkt(Vorb *v, size_t off, size_t len, int page) {
    const maktabasauti_host_t *h = v->host;
    if (v->pkt_n >= V_MAX_PKT) return -2;
    if (len > V_MAX_PKT_BYTES) return -2;
    if (v->pkt_n == v->pkt_cap) {
        size_t nc = v->pkt_cap ? v->pkt_cap * 2u : 32u;
        Pkt *nb;
        if (!v->pkts) nb = (Pkt *)h->alloc(h->user, nc * sizeof(Pkt));
        else nb = (Pkt *)h->realloc(h->user, v->pkts, nc * sizeof(Pkt));
        if (!nb) return -1;
        v->pkts = nb;
        v->pkt_cap = nc;
    }
    v->pkts[v->pkt_n].off = off;
    v->pkts[v->pkt_n].len = len;
    v->pkts[v->pkt_n].granule = 0;
    v->pkts[v->pkt_n].has_g = 0;
    v->pkts[v->pkt_n].page = page;
    v->pkt_n++;
    return 0;
}

/* Ogg page layer.  Tolerant like libogg + libvorbisfile: bytes that are not a page, pages with a wrong CRC and a file that ends inside a page are skipped (a "hole"), a lost page
 * drops the packet that was being assembled, pages of other logical streams (Ogg grouping / multiplexing: Theora, Skeleton, Opus, ...) are ignored and the first Vorbis stream of a
 * link is the one that is decoded.  The headers of a link must still be intact (parse_link). */
static int page_is_vorbis_ident(const uint8_t *pg, uint32_t segs, size_t body_at) {
    uint32_t k, first = 0;
    for (k = 0; k < segs; k++) { first += pg[27 + k]; if (pg[27 + k] < 255u) break; }
    return first >= 7u && header_packet(pg + body_at, first, 1);
}

static maktabasauti_result_t demux(Vorb *v) {
    size_t pos = 0;
    int link = -1, expect_bos = 1, acc = 0, page_i = 0, skipping = 0;
    size_t acc_off = 0, acc_len = 0;
    uint32_t serial = 0, seq = 0, ign[16];
    int nign = 0, cut = 0;
    if (v->file_n < 4 || memcmp(v->file, "OggS", 4) != 0) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FORMAT, "to nie Ogg");
    while (pos + 27 <= v->file_n) {
        const uint8_t *pg = v->file + pos;
        uint32_t segs, body = 0, k, stored, ser, pseq;
        size_t header, page_n;
        int flags, finished = -1, i, foreign = 0;
        int64_t granule;
        if (memcmp(pg, "OggS", 4) != 0 || pg[4] != 0) {                 /* not a page: capture the next "OggS" */
            size_t q = pos + 1;
            while (q + 4 <= v->file_n && memcmp(v->file + q, "OggS", 4) != 0) q++;
            if (link >= 0 && !expect_bos) { acc = 0; skipping = 0; v->links[link].holes = 1; }
            if (q + 4 > v->file_n) break;
            pos = q;
            continue;
        }
        flags = pg[5];
        granule = rd_le64(pg + 6);
        segs = pg[26];
        header = 27u + segs;
        if (pos + header > v->file_n) { cut = 1; break; }                /* the file ends inside a page header */
        for (k = 0; k < segs; k++) body += pg[27 + k];
        page_n = header + body;
        if (pos + page_n > v->file_n) {                                  /* ... or inside the body: the partial page is dropped */
            if (link >= 0 && !expect_bos) v->links[link].holes = 1;
            cut = 1;
            break;
        }
        stored = rd_le32(pg + 22);
        if (!ogg_crc_ok(pg, page_n, stored)) {                           /* damaged page: lost, look for the next one right after its first byte */
            if (link >= 0 && !expect_bos) { acc = 0; skipping = 0; v->links[link].holes = 1; }
            pos += 1;
            continue;
        }
        ser = rd_le32(pg + 14);
        pseq = rd_le32(pg + 18);
        for (i = 0; i < nign; i++) if (ign[i] == ser) foreign = 1;
        if (!foreign) {
            if (flags & 2) {
                if (!expect_bos || (flags & 1) || !page_is_vorbis_ident(pg, segs, header)) {          /* BOS of another stream, or of a stream that is not Vorbis */
                    if (nign < 16) ign[nign++] = ser;
                    foreign = 1;
                }
            } else if (expect_bos || ser != serial) foreign = 1;         /* data page of a stream we do not decode */
        }
        if (foreign) { pos += page_n; page_i++; continue; }
        if (expect_bos) {
            if (link + 1 >= V_MAX_LINKS) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "za duzo linkow");
            link++;
            v->links[link].pkt0 = v->pkt_n;
            serial = ser;
            seq = pseq;
            expect_bos = 0;
            acc = 0;
            skipping = 0;
        } else if (pseq != seq) {                                        /* a lost page: the packet that was being assembled is gone, so is the start of the next one if it continues */
            v->links[link].holes = 1;
            acc = 0;
            skipping = 1;
            seq = pseq;
        }
        seq++;
        if (flags & 1) {
            if (!acc) skipping = 1;                                      /* continuation of a packet we do not have */
        } else {
            if (acc) acc = 0;                                            /* the packet was cut: dropped */
            skipping = 0;
        }
        {
            size_t data = pos + header;
            for (k = 0; k < segs; k++) {
                uint32_t Lseg = pg[27 + k];
                if (skipping) {
                    data += Lseg;
                    if (Lseg < 255u) skipping = 0;
                    continue;
                }
                if (!acc) { acc_off = v->blob_n; acc_len = 0; acc = 1; }
                if (acc_len > V_MAX_PKT_BYTES - Lseg) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "pakiet ponad limit");
                if (blob_add(v, v->file + data, Lseg)) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
                acc_len += Lseg;
                data += Lseg;
                if (Lseg < 255u) {
                    int ar = add_pkt(v, acc_off, acc_len, page_i);
                    if (ar == -2) return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "za duzo pakietow");
                    if (ar) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
                    finished = (int)v->pkt_n - 1;
                    acc = 0;
                }
            }
        }
        if (finished >= 0) {
            v->pkts[finished].has_g = 1;
            v->pkts[finished].granule = granule;
        }
        if (flags & 4) {
            v->links[link].npkt = v->pkt_n - v->links[link].pkt0;
            v->links[link].missing_eos = 0;
            expect_bos = 1;
            acc = 0;
            skipping = 0;
        }
        pos += page_n;
        page_i++;
    }
    if (link < 0) return cut || v->file_n < 27u ? verr(v, MAKTABASAUTI_E_TRUNCATED, "urwana strona") : verr(v, MAKTABASAUTI_E_UNSUPPORTED_FORMAT, "brak strumienia Vorbis");
    if (!expect_bos) {                                                   /* the last link has no end-of-stream page: truncated file */
        v->links[link].npkt = v->pkt_n - v->links[link].pkt0;
        v->links[link].missing_eos = 1;
    }
    v->nlinks = link + 1;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t load_file(Vorb *v, maktabasauti_reader_t *rd) {
    const maktabasauti_host_t *h = v->host;
    size_t cap = 4096, len = 0;
    uint8_t *buf = (uint8_t *)h->alloc(h->user, cap);
    if (!buf) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    for (;;) {
        int64_t n;
        if (len == cap) {
            size_t ncap = cap * 2u;
            if (len >= (size_t)V_MAX_FILE) { h->free(h->user, buf); return verr(v, MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "plik ponad limit"); }
            uint8_t *grown;
            if (ncap < cap) { h->free(h->user, buf); return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci"); }
            grown = (uint8_t *)h->realloc(h->user, buf, ncap);
            if (!grown) { h->free(h->user, buf); return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci"); }
            buf = grown;
            cap = ncap;
        }
        n = h->read(h->user, rd, buf + len, cap - len);
        if (n < 0) { h->free(h->user, buf); return verr(v, (maktabasauti_result_t)n, "odczyt"); }
        if (n == 0) break;
        len += (size_t)n;
    }
    if (!len) { h->free(h->user, buf); return verr(v, MAKTABASAUTI_E_TRUNCATED, "pusty odczyt"); }
    v->file = buf;
    v->file_n = len;
    return MAKTABASAUTI_OK;
}

static int low_neighbor(const int *x, int i) {
    int n = -1, k;
    for (k = 0; k < i; k++) if (x[k] < x[i] && (n < 0 || x[k] > x[n])) n = k;
    return n;
}

static int high_neighbor(const int *x, int i) {
    int n = -1, k;
    for (k = 0; k < i; k++) if (x[k] > x[i] && (n < 0 || x[k] < x[n])) n = k;
    return n;
}

static int render_point(int x0, int y0, int x1, int y1, int X) {
    int dy = y1 - y0, adx = x1 - x0, ady = iabs(dy);
    int off = adx ? (ady * (X - x0)) / adx : 0;
    return dy < 0 ? y0 - off : y0 + off;
}

static void render_line(int x0, int y0, int x1, int y1, int *v, int n) {
    int dy, adx, ady, base, sy, x, y, err, absb;
    if (x0 >= 0 && x0 < n) v[x0] = y0;
    if (x1 <= x0) return;
    dy = y1 - y0;
    adx = x1 - x0;
    ady = iabs(dy);
    base = dy / adx;
    absb = iabs(base);
    sy = dy < 0 ? base - 1 : base + 1;
    ady -= absb * adx;
    y = y0;
    err = 0;
    for (x = x0 + 1; x < x1 && x < n; x++) {
        err += ady;
        if (err >= adx) { err -= adx; y += sy; }
        else y += base;
        v[x] = y;
    }
}

static const int fl_range[4] = {256, 128, 86, 64};

static int floor1_curve(const Fl1 *f, const int *Y, int *curve, int n) {
    int final[65], step2[65], order[65], i, lx, ly, hx = 0, hy = 0;
    int range = fl_range[f->mult - 1];
    memset(curve, 0, (size_t)n * sizeof(int));
    memset(step2, 0, sizeof step2);
    step2[0] = step2[1] = 1;
    final[0] = Y[0];
    final[1] = Y[1];
    for (i = 2; i < f->values; i++) {
        int ln = low_neighbor(f->x, i), hn = high_neighbor(f->x, i);
        int pred, val, highroom, lowroom, room;
        if (ln < 0 || hn < 0) return -1;
        pred = render_point(f->x[ln], final[ln], f->x[hn], final[hn], f->x[i]);
        val = Y[i];
        highroom = range - pred;
        lowroom = pred;
        room = (highroom < lowroom ? highroom : lowroom) * 2;
        if (val) {
            step2[ln] = step2[hn] = step2[i] = 1;
            if (val >= room) {
                if (highroom > lowroom) final[i] = val - lowroom + pred;
                else final[i] = pred - val + highroom - 1;
            } else if (val & 1) final[i] = pred - ((val + 1) / 2);
            else final[i] = pred + (val / 2);
        } else {
            step2[i] = 0;
            final[i] = pred;
        }
    }
    for (i = 0; i < f->values; i++) {
        if (final[i] < 0) final[i] = 0;
        else if (final[i] >= range) final[i] = range - 1;
        order[i] = i;
    }
    for (i = 1; i < f->values; i++) {
        int key = order[i], j = i;
        while (j > 0 && f->x[order[j - 1]] > f->x[key]) { order[j] = order[j - 1]; j--; }
        order[j] = key;
    }
    lx = 0;
    ly = final[order[0]] * f->mult;
    for (i = 1; i < f->values; i++) {
        int id = order[i];
        if (!step2[id]) continue;
        hx = f->x[id];
        hy = final[id] * f->mult;
        render_line(lx, ly, hx, hy, curve, n);
        lx = hx;
        ly = hy;
        if (lx >= n) break;
    }
    if (hx < n) render_line(hx, hy, n, hy, curve, n);
    return 0;
}

static double bark(double x) {
    return 13.1 * atan(0.00074 * x) + 2.24 * atan(0.0000000185 * x * x) + 0.0001 * x;
}

static int floor0_curve(const Fl0 *f, const float *coeff, int amp, float *dst, int *map, int n) {
    double bnyq;
    int i = 0;
    if (!amp || !f->amp_bits || !f->bark || !f->rate) {
        memset(dst, 0, (size_t)n * sizeof(float));
        return 0;
    }
    bnyq = bark(0.5 * (double)f->rate);
    if (bnyq == 0.0) {
        memset(dst, 0, (size_t)n * sizeof(float));
        return 0;
    }
    for (i = 0; i < n; i++) {
        double idx = floor(bark((double)f->rate * (double)i / (2.0 * (double)n)) * (double)f->bark / bnyq);
        if (idx < 0.0) idx = 0.0;
        if (idx > (double)(f->bark - 1)) idx = (double)(f->bark - 1);
        map[i] = (int)idx;
    }
    map[n] = -1;
    i = 0;
    while (i < n) {
        double w = V_PI * (double)map[i] / (double)f->bark;
        double cw = cos(w), p, q, lin;
        int j, mark = map[i], odd = f->order & 1;
        if (odd) {
            p = 1.0 - cw * cw;
            q = 0.25;
            for (j = 0; j <= (f->order - 3) / 2; j++) {
                double d = cos(coeff[2 * j + 1]) - cw;
                p *= 4.0 * d * d;
            }
            for (j = 0; j <= (f->order - 1) / 2; j++) {
                double d = cos(coeff[2 * j]) - cw;
                q *= 4.0 * d * d;
            }
        } else {
            p = (1.0 - cw) * 0.5;
            q = (1.0 + cw) * 0.5;
            for (j = 0; j <= (f->order - 2) / 2; j++) {
                double d1 = cos(coeff[2 * j + 1]) - cw;
                double d0 = cos(coeff[2 * j]) - cw;
                p *= 4.0 * d1 * d1;
                q *= 4.0 * d0 * d0;
            }
        }
        if (p + q <= 0.0) lin = 0.0;
        else {
            double den = (ldexp(1.0, f->amp_bits) - 1.0) * sqrt(p + q);
            lin = exp(0.11512925 * (((double)amp * (double)f->amp_off) / den - (double)f->amp_off));
        }
        while (i < n && map[i] == mark) dst[i++] = (float)lin;
    }
    return 0;
}

static int residue_part(Br *b, const Book *bk, float *v, int offset, int part, int type0) {
    int dim = bk->dim, i, j;
    float tmp[V_MAX_DIM];
    if (type0) {
        int step = part / dim;
        {   /* libvorbis (vorbis_book_decodevs_add) reads all `step` codewords of the partition before it adds anything: a packet that ends inside the partition leaves it untouched */
            Br probe = *b;
            for (i = 0; i < step; i++) {
                int entry, r = book_entry(&probe, bk, &entry);
                if (r) return r;
            }
        }
        for (i = 0; i < step; i++) {
            int r = book_vq(b, bk, tmp);
            if (r) return r;
            for (j = 0; j < dim; j++) v[offset + i + j * step] += tmp[j];
        }
    } else {
        i = 0;
        while (i < part) {
            int r = book_vq(b, bk, tmp);
            if (r) return r;
            for (j = 0; j < dim; j++) v[offset + i++] += tmp[j];
        }
    }
    return BR_OK;
}

/* type 2 sklada kanaly paczki: v[i * ch + j] trafia do kanalu j, probki i. */
static void deinterleave(Vorb *v, const int *idx, int ch, int n2) {
    int i, j;
    for (i = 0; i < n2; i++) {
        for (j = 0; j < ch; j++) {
            v->residue[(size_t)idx[j] * v->n2_stride + (size_t)i] =
                v->inter[(size_t)i * (size_t)ch + (size_t)j];
        }
    }
}

/* Klasyfikacja: jedno slowo na grupe partycji, reszta z mlodszej cyfry.
 * partition_count rosnie raz na partycje, poza petla kanalow. */
static int residue_decode(Vorb *v, Br *b, const Link *L, const Res *rs, int n2, int ch, const uint8_t *dnd, const int *idx) {
    int actual, begin, end, nread, parts, pass, cw, j, orig, type2;
    size_t stride, cls_cap;
    const Book *cb;
    uint8_t live = 0;
    if (ch <= 0) return BR_OK;
    orig = ch;
    type2 = rs->type == 2;
    actual = n2;
    cb = &L->books[rs->classbook];
    if (type2) {
        int any = 0;
        for (j = 0; j < ch; j++) if (!dnd[j]) any = 1;
        if (!any) return BR_OK;
        actual = n2 * ch;
        memset(v->inter, 0, (size_t)actual * sizeof(float));
        ch = 1;
        dnd = &live;
    }
    begin = rs->begin < actual ? rs->begin : actual;
    end = rs->end < actual ? rs->end : actual;
    if (end < begin) end = begin;
    nread = end - begin;
    parts = rs->part ? nread / rs->part : 0;
    cw = cb->dim;
    if (cw < 1) return BR_BAD;
    if (!parts) {
        if (type2) deinterleave(v, idx, orig, n2);
        return BR_OK;
    }
    stride = (size_t)parts + (size_t)cw;
    cls_cap = (size_t)v->max_ch * (v->n2_stride + (size_t)V_MAX_DIM);
    if ((size_t)ch * stride > cls_cap) return BR_BAD;
    for (pass = 0; pass < 8; pass++) {
        int pc = 0;
        while (pc < parts) {
            if (pass == 0) {
                for (j = 0; j < ch; j++) {
                    int entry, r, i, temp;
                    if (dnd[j]) continue;
                    r = book_entry(b, cb, &entry);
                    if (r) {
                        if (type2 && r == BR_EOP) deinterleave(v, idx, orig, n2);
                        return r;
                    }
                    temp = entry;
                    for (i = cw - 1; i >= 0; i--) {
                        v->cls[(size_t)j * stride + (size_t)(i + pc)] = (uint8_t)(temp % rs->classes);
                        temp /= rs->classes;
                    }
                }
            }
            {
                int i;
                for (i = 0; i < cw && pc < parts; i++) {
                    for (j = 0; j < ch; j++) {
                        int vqclass, vqbook, r;
                        float *vec;
                        if (dnd[j]) continue;
                        vqclass = v->cls[(size_t)j * stride + (size_t)pc];
                        if (vqclass < 0 || vqclass >= rs->classes) return BR_BAD;
                        vqbook = rs->books[vqclass][pass];
                        if (vqbook < 0) continue;
                        vec = type2 ? v->inter : v->residue + (size_t)idx[j] * v->n2_stride;
                        r = residue_part(b, &L->books[vqbook], vec, begin + pc * rs->part, rs->part, rs->type == 0);
                        if (r) {
                            if (type2 && r == BR_EOP) deinterleave(v, idx, orig, n2);
                            return r;
                        }
                    }
                    pc++;
                }
            }
        }
    }
    if (type2) deinterleave(v, idx, orig, n2);
    return BR_OK;
}

/* Ostatnie last przechodzi miedzy wektorami. Wyzerowanie go przy kazdym wektorze
 * robiloby z dodawania nic, wiec petla wraca do odczytu VQ, nie do last = 0. */
static int floor0_pkt(Br *b, const Link *L, const Fl0 *f, float *dst, int *map, int n) {
    uint32_t amp, bn;
    float coeff[256], last = 0.f;
    int got = 0;
    const Book *bk;
    if (f->order > 255) return BR_BAD;
    if (br_u(b, f->amp_bits, &amp)) return BR_EOP;
    if (!amp) return BR_UNUSED;
    if (br_u(b, ilog(f->nbooks), &bn)) return BR_EOP;
    if ((int)bn >= f->nbooks) return BR_UNUSED;                 /* libvorbis: a book number outside the list leaves the floor of the channel unused (the spec calls the stream undecodable) */
    bk = &L->books[f->books[bn]];
    if (!bk->lookup || bk->dim < 1) return BR_BAD;
    while (got < f->order) {
        float tmp[V_MAX_DIM], base = last;
        int r = book_vq(b, bk, tmp), j;
        if (r) return r;
        /* Every scalar of the vector gets the same offset, the last value of the previous vector (spec 6.2.3, libvorbis floor0.c); the offset does NOT accumulate inside a vector. */
        for (j = 0; j < bk->dim; j++) {
            float val = tmp[j] + base;
            if (got < f->order) coeff[got++] = val;
            last = val;
        }
    }
    floor0_curve(f, coeff, (int)amp, dst, map, n);
    return BR_OK;
}

static int floor1_pkt(Vorb *v, Br *b, const Link *L, const Fl1 *f, float *dst, int n) {
    uint32_t nz, yv;
    int Y[65], range, bits, offset, i, j;
    if (br_u(b, 1, &nz)) return BR_EOP;
    if (!nz) return BR_UNUSED;
    if (f->mult < 1 || f->mult > 4 || f->values < 2 || f->values > 65) return BR_BAD;
    range = fl_range[f->mult - 1];
    bits = ilog(range - 1);
    if (br_u(b, bits, &yv)) return BR_EOP;
    Y[0] = (int)yv;
    if (br_u(b, bits, &yv)) return BR_EOP;
    Y[1] = (int)yv;
    offset = 2;
    for (i = 0; i < f->parts; i++) {
        int cls = f->cls[i];
        int cdim = f->dim[cls];
        int cbits = f->sub[cls];
        int csub = (1 << cbits) - 1;
        int cval = 0;
        if (offset + cdim > f->values) return BR_BAD;
        if (cbits > 0) {
            int entry, r = book_entry(b, &L->books[f->master[cls]], &entry);
            if (r) return r;
            cval = entry;
        }
        for (j = 0; j < cdim; j++) {
            int book = f->subbook[cls][cval & csub];
            cval >>= cbits;
            if (book >= 0) {
                int entry, r = book_entry(b, &L->books[book], &entry);
                if (r) return r;
                Y[offset + j] = entry;
            } else Y[offset + j] = 0;
        }
        offset += cdim;
    }
    if (floor1_curve(f, Y, v->curve, n)) return BR_BAD;
    for (i = 0; i < n; i++) {
        int idx = v->curve[i];
        if (idx < 0) idx = 0;
        if (idx > 255) idx = 255;
        dst[i] = fl_inv_db[idx];
    }
    return BR_OK;
}

static double win_slope(double t) {
    double s = sin(t * (V_PI * 0.5));
    return sin((V_PI * 0.5) * s * s);
}

/* Zbocze prawe jest lustrem. Krok 8 specyfikacji zeruje od right_window_start,
 * co skasowaloby zbocze: zera sa od right_window_end. */
static void build_window(float *w, int n, int bs0, int short_left, int short_right) {
    int center = n >> 1, ls, le, ln, rs, re, rn, i;
    if (short_left) {
        ls = n / 4 - bs0 / 4;
        le = n / 4 + bs0 / 4;
        ln = bs0 / 2;
    } else {
        ls = 0;
        le = center;
        ln = center;
    }
    if (short_right) {
        rs = (n * 3) / 4 - bs0 / 4;
        re = (n * 3) / 4 + bs0 / 4;
        rn = bs0 / 2;
    } else {
        rs = center;
        re = n;
        rn = center;
    }
    for (i = 0; i < n; i++) w[i] = 0.f;
    if (ln < 1 || rn < 1) return;
    for (i = ls; i < le; i++) w[i] = (float)win_slope(((double)(i - ls) + 0.5) / (double)ln);
    for (i = le; i < rs; i++) w[i] = 1.f;
    for (i = rs; i < re; i++) {
        double t = ((double)(i - rs) + 0.5) / (double)rn;
        double s = sin(t * (V_PI * 0.5) + (V_PI * 0.5));
        w[i] = (float)sin((V_PI * 0.5) * s * s);
    }
}

static const float *get_win(Vorb *v, int n, int bs0, int sl, int sr) {
    int i;
    float *w;
    for (i = 0; i < v->nwins; i++) if (v->wins[i].n == n && v->wins[i].sl == sl && v->wins[i].sr == sr) return v->wins[i].w;
    if (v->nwins >= WIN_MAX) return NULL;
    w = (float *)va(v, (size_t)n * sizeof(float));
    if (!w) return NULL;
    build_window(w, n, bs0, sl, sr);
    v->wins[v->nwins].n = n; v->wins[v->nwins].sl = sl; v->wins[v->nwins].sr = sr; v->wins[v->nwins].w = w;
    v->nwins++;
    return w;
}

static const ImdctTab *get_tab(Vorb *v, int N) {
    ImdctTab *t;
    int i, h = N / 2;
    for (i = 0; i < v->ntabs; i++) if (v->tabs[i].N == N) return &v->tabs[i];
    if (v->ntabs >= ITAB_MAX || N < 4 || (N & (N - 1))) return NULL;
    t = &v->tabs[v->ntabs];
    t->pc = (double *)va(v, (size_t)h * sizeof(double));
    t->ps = (double *)va(v, (size_t)h * sizeof(double));
    t->wc = (double *)va(v, (size_t)(h / 2 + 1) * sizeof(double));
    t->ws = (double *)va(v, (size_t)(h / 2 + 1) * sizeof(double));
    if (!t->pc || !t->ps || !t->wc || !t->ws) return NULL;
    for (i = 0; i < h; i++) {
        double a = -V_PI * (double)(8 * i + 1) / (8.0 * (double)N);
        t->pc[i] = cos(a);
        t->ps[i] = sin(a);
    }
    for (i = 0; i <= h / 2; i++) {
        double a = -2.0 * V_PI * (double)i / (double)h;
        t->wc[i] = cos(a);
        t->ws[i] = sin(a);
    }
    t->N = N;
    v->ntabs++;
    return t;
}

/* radix-2 forward FFT of h points, the twiddle for stride h/len is wc / ws[k * (h / len)] */
static void fft_c(double *re, double *im, int h, const ImdctTab *t) {
    int i, j, len;
    for (i = 1, j = 0; i < h; i++) {
        int bit = h >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double x = re[i]; re[i] = re[j]; re[j] = x;
            x = im[i]; im[i] = im[j]; im[j] = x;
        }
    }
    for (len = 2; len <= h; len <<= 1) {
        int half = len >> 1, step = h / len, k;
        for (i = 0; i < h; i += len) {
            for (k = 0; k < half; k++) {
                int u = i + k, w = i + k + half, ti = k * step;
                double cr, ci, xr, xi;
                if (ti <= h / 2) { cr = t->wc[ti]; ci = t->ws[ti]; }
                else { cr = -t->wc[h - ti]; ci = t->ws[h - ti]; }
                xr = re[w] * cr - im[w] * ci;
                xi = re[w] * ci + im[w] * cr;
                re[w] = re[u] - xr; im[w] = im[u] - xi;
                re[u] += xr; im[u] += xi;
            }
        }
    }
}

/* y[i] = IMDCT_K * sum X[k] cos(pi/N * (i + 0.5 + N/2) * (k + 0.5)), N = n/2, through the DCT-IV z[m] = sum X[k] cos(pi/N (m+0.5)(k+0.5)):
 * y[i] = z[i + N/2] (i < N/2), -z[3N/2 - 1 - i] (N/2 <= i < 3N/2), -z[i - 3N/2] (i >= 3N/2). */
static int imdct(Vorb *v, const float *X, float *y, int n) {
    int N = n >> 1, h = N >> 1, j, i;
    const ImdctTab *t = get_tab(v, N);
    double *re = v->fre, *im = v->fim;
    if (!t || !re || !im) return 1;
    for (j = 0; j < h; j++) {
        double a = (double)X[2 * j], b = (double)X[N - 1 - 2 * j];
        re[j] = a * t->pc[j] - b * t->ps[j];
        im[j] = a * t->ps[j] + b * t->pc[j];
    }
    fft_c(re, im, h, t);
    /* z[2j] = Re, z[N-1-2j] = -Im of the post-twiddled value; the output is filled directly from them */
    for (j = 0; j < h; j++) {
        double dr = re[j] * t->pc[j] - im[j] * t->ps[j];
        double di = re[j] * t->ps[j] + im[j] * t->pc[j];
        int m, k;
        for (k = 0; k < 2; k++) {
            double z = k == 0 ? dr : -di;
            m = k == 0 ? 2 * j : N - 1 - 2 * j;
            z *= IMDCT_K;
            /* z[m] contributes to y[m - N/2] (m >= N/2), y[3N/2 - 1 - m'] ... */
            if (m >= N / 2) y[m - N / 2] = (float)z;
            if (m < N / 2) y[m + 3 * N / 2] = (float)-z;                  /* i = m + 3N/2 */
            i = 3 * N / 2 - 1 - m;                                       /* -z[3N/2-1-i] with m = 3N/2-1-i */
            if (i >= N / 2 && i < 3 * N / 2) y[i] = (float)-z;
        }
    }
    return 0;
}

static int overlap_channel(Vorb *v, int c, int ch, int n, const float *time) {
    int prev = v->prev_n, half = n >> 1, i;
    float *lap = v->lap + (size_t)c * v->n2_stride;
    if (prev == 0) {
        for (i = 0; i < half; i++) lap[i] = time[half + i];
        return 0;
    }
    {
        int len = prev / 4 + n / 4, ph = prev >> 1;
        for (i = 0; i < len; i++) {
            float s = 0.f;
            int ci = i - prev / 4 + n / 4;
            if (i < ph) s += lap[i];
            if (ci >= 0 && ci < n) s += time[ci];
            v->pcm[(size_t)i * (size_t)ch + (size_t)c] = s;
        }
        for (i = 0; i < half; i++) lap[i] = time[half + i];
        return len;
    }
}

static void inverse_couple(const Map *m, float *res, size_t stride, int n2) {
    int s, k;
    for (s = m->coupling - 1; s >= 0; s--) {
        float *mag = res + (size_t)m->mag[s] * stride;
        float *ang = res + (size_t)m->ang[s] * stride;
        for (k = 0; k < n2; k++) {
            float mv = mag[k], av = ang[k], nm, na;
            if (mv > 0.f) {
                if (av > 0.f) { nm = mv; na = mv - av; }
                else { na = mv; nm = mv + av; }
            } else if (av > 0.f) { nm = mv; na = mv + av; }
            else { na = mv; nm = mv - av; }
            mag[k] = nm;
            ang[k] = na;
        }
    }
}

static maktabasauti_result_t decode_packet(Vorb *v, int *nout) {
    Link *L = &v->links[v->link];
    Br b;
    int bs = 0, pf = 1, nf = 1, mode = -1, kind, n, n2, c, len = 0;
    *nout = 0;
    if (v->pkt_i >= L->pkt0 + L->npkt) {
        if (v->out_pos < L->total && !L->holes && !L->missing_eos) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "za malo probek");
        return MAKTABASAUTI_EOF;
    }
    b = pkt_br(v, &v->pkts[v->pkt_i]);
    kind = packet_head(L, &b, &bs, &pf, &nf, &mode);
    if (kind < 0) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "numer trybu");
    if (kind != PK_AUDIO) {
        v->pkt_i++;
        return MAKTABASAUTI_OK;
    }
    n = bs;
    n2 = n >> 1;
    if (n2 < 1 || (size_t)n2 > v->n2_stride || mode < 0) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "rozmiar bloku");
    {
        const Map *map = &L->maps[L->modes[mode].mapping];
        uint8_t no_res[V_MAX_CH];
        int floor_eop = 0, s;
        const float *win;
        int short_left = (n == L->bs1 && L->bs0 != L->bs1 && !pf);
        int short_right = (n == L->bs1 && L->bs0 != L->bs1 && !nf);
        for (c = 0; c < L->channels; c++) {
            float *dst = v->spec + (size_t)c * v->n2_stride;
            int sub = map->mux[c];
            Floor *fl = &L->floors[map->fl[sub]];
            int r;
            memset(dst, 0, (size_t)n2 * sizeof(float));
            no_res[c] = 1;
            if (floor_eop) continue;
            if (fl->type == 0) r = floor0_pkt(&b, L, &fl->f0, dst, v->fmap, n2);
            else r = floor1_pkt(v, &b, L, &fl->f1, dst, n2);
            if (r == BR_EOP) {
                floor_eop = 1;
                memset(dst, 0, (size_t)n2 * sizeof(float));
                continue;
            }
            if (r == BR_BAD) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "floor");
            if (r == BR_UNUSED) continue;
            if (r != BR_OK) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "floor");
            no_res[c] = 0;
        }
        if (floor_eop) {
            for (c = 0; c < L->channels; c++)
                memset(v->spec + (size_t)c * v->n2_stride, 0, (size_t)n2 * sizeof(float));
        } else {
            for (s = 0; s < map->coupling; s++) {
                int mag = map->mag[s], ang = map->ang[s];
                if (!no_res[mag] || !no_res[ang]) no_res[mag] = no_res[ang] = 0;
            }
            for (c = 0; c < L->channels; c++)
                memset(v->residue + (size_t)c * v->n2_stride, 0, (size_t)n2 * sizeof(float));
            for (s = 0; s < map->submaps; s++) {
                int idx[V_MAX_CH], bch = 0, j, r;
                uint8_t dnd[V_MAX_CH];
                for (j = 0; j < L->channels; j++) {
                    if (map->mux[j] != (uint8_t)s) continue;
                    idx[bch] = j;
                    dnd[bch] = no_res[j];
                    bch++;
                }
                r = residue_decode(v, &b, L, &L->res[map->rs[s]], n2, bch, dnd, idx);
                if (r == BR_BAD) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "residue");
            }
            inverse_couple(map, v->residue, v->n2_stride, n2);
            for (c = 0; c < L->channels; c++) {
                float *sp = v->spec + (size_t)c * v->n2_stride;
                float *rsv = v->residue + (size_t)c * v->n2_stride;
                int k;
                for (k = 0; k < n2; k++) sp[k] *= rsv[k];
            }
        }
        win = get_win(v, n, L->bs0, short_left, short_right);
        if (!win) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "okno");
        for (c = 0; c < L->channels; c++) {
            float *sp = v->spec + (size_t)c * v->n2_stride;
            int i, z = 1;
            for (i = 0; i < n2; i++) if (sp[i] != 0.f) { z = 0; break; }
            if (z) memset(v->time, 0, (size_t)n * sizeof(float));
            else {
                if (imdct(v, sp, v->time, n)) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "imdct");
                for (i = 0; i < n; i++) v->time[i] *= win[i];
            }
            len = overlap_channel(v, c, L->channels, n, v->time);
        }
        v->prev_n = n;
    }
    v->pkt_i++;
    *nout = len;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t produce(Vorb *v) {
    Link *L = &v->links[v->link];
    if (v->out_pos >= L->total) return MAKTABASAUTI_EOF;
    for (;;) {
        int nout = 0;
        maktabasauti_result_t r = decode_packet(v, &nout);
        size_t ch;
        if (r != MAKTABASAUTI_OK) return r;
        if (nout <= 0) continue;
        ch = (size_t)L->channels;
        if (v->head_left) {
            if (v->head_left >= (uint64_t)nout) {
                v->head_left -= (uint64_t)nout;
                continue;
            }
            memmove(v->pcm, v->pcm + (size_t)v->head_left * ch,
                    ((size_t)nout - (size_t)v->head_left) * ch * sizeof(float));
            nout -= (int)v->head_left;
            v->head_left = 0;
        }
        if ((uint64_t)nout > L->total - v->out_pos) nout = (int)(L->total - v->out_pos);
        if (nout <= 0) return MAKTABASAUTI_EOF;
        {   /* a damaged or extreme stream can drive floor 0 or the IMDCT to inf / NaN (libvorbis returns them as they are): the output stays finite */
            size_t q, tot = (size_t)nout * ch;
            for (q = 0; q < tot; q++) if (!(v->pcm[q] - v->pcm[q] == 0.f)) v->pcm[q] = 0.f;
        }
        v->pcm_n = (size_t)nout;
        v->pcm_i = 0;
        v->out_pos += (uint64_t)nout;
        return MAKTABASAUTI_OK;
    }
}

static maktabasauti_result_t alloc_scratch(Vorb *v) {
    size_t ch = (size_t)v->max_ch;
    size_t n2 = (size_t)v->max_bs / 2u;
    if (!ch || !n2) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "brak audio");
    v->n2_stride = n2;
    v->pcm = (float *)va(v, ch * n2 * sizeof(float));
    v->lap = (float *)va(v, ch * n2 * sizeof(float));
    v->time = (float *)va(v, (size_t)v->max_bs * sizeof(float));
    v->spec = (float *)va(v, ch * n2 * sizeof(float));
    v->residue = (float *)va(v, ch * n2 * sizeof(float));
    v->inter = (float *)va(v, ch * n2 * sizeof(float));
    v->window = (float *)va(v, (size_t)v->max_bs * sizeof(float));
    v->curve = (int *)va(v, n2 * sizeof(int));
    v->fmap = (int *)va(v, (n2 + 1u) * sizeof(int));
    v->cls = (uint8_t *)va(v, ch * (n2 + (size_t)V_MAX_DIM));
    v->fre = (double *)va(v, (n2 / 2u + 1u) * sizeof(double));
    v->fim = (double *)va(v, (n2 / 2u + 1u) * sizeof(double));
    if (!v->fre || !v->fim) return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    if (!v->pcm || !v->lap || !v->time || !v->spec || !v->residue || !v->inter || !v->window || !v->curve || !v->fmap || !v->cls)
        return verr(v, MAKTABASAUTI_E_NO_MEMORY, "brak pamieci");
    return MAKTABASAUTI_OK;
}

static void rewind_link(Vorb *v) {
    Link *L = &v->links[v->link];
    v->pkt_i = L->pkt0 + 3u;
    v->prev_n = 0;
    v->head_left = L->head;
    v->out_pos = 0;
    v->pcm_n = 0;
    v->pcm_i = 0;
    v->pend = MAKTABASAUTI_OK;
    if (v->lap && v->max_ch > 0 && v->n2_stride)
        memset(v->lap, 0, (size_t)v->max_ch * v->n2_stride * sizeof(float));
}

static void fill_info(const Vorb *v, maktabasauti_stream_info_t *info) {
    const Link *L = &v->links[v->link];
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = L->rate;
    info->channels = (uint32_t)L->channels;
    info->flags = MAKTABASAUTI_STREAM_SEEKABLE | MAKTABASAUTI_STREAM_VBR;
    if (L->gapless) info->flags |= MAKTABASAUTI_STREAM_GAPLESS_TRIMMED;
    if (L->channels == 1) info->channel_mask = MAKTABASAUTI_LAYOUT_MONO;
    else if (L->channels == 2) info->channel_mask = MAKTABASAUTI_LAYOUT_STEREO;
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->bits_per_sample = 0;
    info->total_frames = L->total;
    info->priming_frames = L->head;
    info->end_padding_frames = L->tail;
    info->bitrate = L->bitrate;
    memcpy(info->codec_name, "vorbis", 7);
    memcpy(info->codec_profile, "Vorbis I", 9);
    memcpy(info->container_name, "ogg", 4);
}

static int32_t MAKTABASAUTI_CALL vorbis_probe(const maktabasauti_codec_t *self, const uint8_t *prefix, size_t n, const char *name_hint) {
    size_t segs, body;
    (void)self;
    (void)name_hint;
    if (!prefix || n < 27) return 0;
    if (memcmp(prefix, "OggS", 4) != 0 || prefix[4] != 0) return 0;
    segs = prefix[26];
    if (segs < 1 || n < 27u + segs) return 0;
    if (prefix[27] < 7) return 0;
    body = 27u + segs;
    if (n < body + 7u) return 0;
    if (prefix[body] == 1 && memcmp(prefix + body + 1, "vorbis", 6) == 0) return 90;
    return 0;
}

static maktabasauti_result_t MAKTABASAUTI_CALL vorbis_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host,
                                                          maktabasauti_reader_t *reader, maktabasauti_stream_info_t *info, void **state) {
    Vorb *v;
    maktabasauti_result_t r;
    int i;
    (void)self;
    *state = NULL;
    if (!host || !host->alloc || !host->free || !host->read || !host->realloc || !info) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    v = (Vorb *)host->alloc(host->user, sizeof *v);
    if (!v) return MAKTABASAUTI_E_NO_MEMORY;
    memset(v, 0, sizeof *v);
    v->host = host;
    r = load_file(v, reader);
    if (r != MAKTABASAUTI_OK) { host->free(host->user, v); return r; }
    r = demux(v);
    if (r != MAKTABASAUTI_OK) { vorbis_free(v); return r; }
    for (i = 0; i < v->nlinks; i++) {
        r = parse_link(v, i);
        if (r != MAKTABASAUTI_OK) { vorbis_free(v); return r; }
    }
    r = alloc_scratch(v);
    if (r != MAKTABASAUTI_OK) { vorbis_free(v); return r; }
    v->link = 0;
    rewind_link(v);
    fill_info(v, info);
    *state = v;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t end_link(Vorb *v) {
    Link *L = &v->links[v->link];
    if (v->link + 1 < v->nlinks) {
        v->link++;
        rewind_link(v);
        return MAKTABASAUTI_E_FORMAT_CHANGED;
    }
    if (L->missing_eos) return verr(v, MAKTABASAUTI_E_TRUNCATED, "brak EOS");
    return MAKTABASAUTI_EOF;
}

static maktabasauti_result_t MAKTABASAUTI_CALL vorbis_read(void *state, void *dst, size_t dst_frames, size_t *frames) {
    Vorb *v = (Vorb *)state;
    float *o = (float *)dst;
    size_t done = 0;
    *frames = 0;
    if (v->pend != MAKTABASAUTI_OK) return v->pend;
    while (done < dst_frames) {
        Link *L = &v->links[v->link];
        size_t have, take, i;
        if (v->pcm_i >= v->pcm_n) {
            maktabasauti_result_t r;
            if (v->out_pos >= L->total) {
                if (done) { *frames = done; return MAKTABASAUTI_OK; }
                return end_link(v);
            }
            r = produce(v);
            if (r == MAKTABASAUTI_EOF) {
                if (done) { *frames = done; return MAKTABASAUTI_OK; }
                return end_link(v);
            }
            if (r != MAKTABASAUTI_OK) {
                if (done) { v->pend = r; *frames = done; return MAKTABASAUTI_OK; }
                v->pend = r;
                return r;
            }
            continue;
        }
        have = v->pcm_n - v->pcm_i;
        take = dst_frames - done;
        if (take > have) take = have;
        for (i = 0; i < take * (size_t)L->channels; i++)
            o[done * (size_t)L->channels + i] = v->pcm[v->pcm_i * (size_t)L->channels + i];
        v->pcm_i += take;
        done += take;
    }
    *frames = done;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL vorbis_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    Vorb *v = (Vorb *)state;
    Link *L = &v->links[v->link];
    (void)flags;
    if (frame > L->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    rewind_link(v);
    while (v->out_pos < frame) {
        maktabasauti_result_t r = produce(v);
        if (r == MAKTABASAUTI_EOF) {
            if (v->out_pos < frame) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "seek przed koncem");
            break;
        }
        if (r != MAKTABASAUTI_OK) return r;
        if (v->out_pos >= frame) {
            size_t back = (size_t)(v->out_pos - frame);
            if (back > v->pcm_n) return verr(v, MAKTABASAUTI_E_CORRUPT_DATA, "seek");
            v->pcm_i = v->pcm_n - back;
            break;
        }
    }
    if (actual) *actual = frame;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL vorbis_reset(void *state) {
    Vorb *v = (Vorb *)state;
    v->link = 0;
    rewind_link(v);
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL vorbis_flush(void *state) {
    ((Vorb *)state)->pend = MAKTABASAUTI_OK;
    return MAKTABASAUTI_OK;
}

static int fold_ascii(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A' + 'a');
    return (int)c;
}

static int name_eq(const char *a, const char *b) {
    size_t i = 0;
    for (; a[i] || b[i]; i++) if (fold_ascii((unsigned char)a[i]) != fold_ascii((unsigned char)b[i])) return 0;
    return 1;
}

static int field_is(const char *field, const char *key) {
    size_t i = 0;
    if (!key[0]) return 0;
    for (; key[i]; i++) if (fold_ascii((unsigned char)field[i]) != fold_ascii((unsigned char)key[i])) return 0;
    return field[i] == '=';
}

static maktabasauti_result_t write_tag(char *buf, size_t cap, size_t *needed, const char *s) {
    size_t n = strlen(s) + 1u;
    if (needed) *needed = n;
    if (!buf || cap < n) return MAKTABASAUTI_E_BUFFER_TOO_SMALL;
    memcpy(buf, s, n);
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL vorbis_get_tag(const void *state, const char *key, char *buf, size_t cap, size_t *needed) {
    const Vorb *v = (const Vorb *)state;
    const Link *L;
    int i;
    if (!v || !key) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    L = &v->links[v->link];
    if (name_eq(key, "vendor")) return write_tag(buf, cap, needed, L->vendor ? L->vendor : "");
    for (i = 0; i < L->ncmt; i++) {
        const char *eq;
        if (!L->cmt[i]) continue;
        eq = strchr(L->cmt[i], '=');
        if (!eq) continue;
        if (!field_is(L->cmt[i], key)) continue;
        return write_tag(buf, cap, needed, eq + 1);
    }
    return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
}

static const char *MAKTABASAUTI_CALL vorbis_last_error(const void *state) {
    return state ? ((const Vorb *)state)->err : NULL;
}

static maktabasauti_result_t MAKTABASAUTI_CALL vorbis_get_info(const void *state, maktabasauti_stream_info_t *info) {
    if (!state || !info) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    fill_info((const Vorb *)state, info);
    return MAKTABASAUTI_OK;
}

static void MAKTABASAUTI_CALL vorbis_close(void *state) {
    if (state) vorbis_free((Vorb *)state);
}

const maktabasauti_codec_t maktabasauti_vorbis_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "vorbis", "Ogg Vorbis I", "maktabasauti_vorbis",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK | MAKTABASAUTI_CODEC_FLAG_HAS_TAGS, 0, NULL,
    vorbis_probe, vorbis_open, vorbis_read, vorbis_seek, vorbis_reset, vorbis_flush,
    vorbis_get_tag, vorbis_last_error, vorbis_get_info, vorbis_close, { NULL }, { NULL }
};
