/* MaktabaSauti, modul maktabasauti_aac: wspolna czesc kontenerow, ktore wczytuja strumien AAC do pamieci jako LISTE JEDNOSTEK DOSTEPU (Matroska, MPEG-2 TS): dane na stercie hosta, pozycja
 * kontenera = numer jednostki, wiec przewijanie, liczenie dlugosci i punkty kontrolne silnika dzialaja tak samo jak dla MP4, takze na wejsciu nieprzewijalnym (wejscie jest czytane raz,
 * sekwencyjnie).  Dwa rodzaje jednostek: `adts` (cala ramka ADTS, TS) i surowa jednostka + ASC (Matroska: owijana w pseudo-naglowek ADTS przez aac_au_wrap). */
#ifndef MAKTABASAUTI_AAC_AULIST_H
#define MAKTABASAUTI_AAC_AULIST_H
#include "maktabasauti_aac_engine.h"

#define AULIST_MAX_BYTES ((size_t)768 << 20)             /* gorna granica pamieci na dane jednego strumienia */

typedef struct {
    uint8_t *data; size_t len, cap;
    uint32_t *off, *size; size_t count, cap_au;
    uint64_t pos;                                        /* numer nastepnej jednostki */
    int adts;                                            /* 1: jednostka to ramka ADTS */
    SautiAacMp4Info mi;                                  /* tylko !adts */
} aulist_t;

static inline void *au_alloc(dec_t *d, size_t n) { return d->host->alloc(d->host->user, n ? n : 1u); }
static inline void au_release_list(dec_t *d) {
    aulist_t *L = (aulist_t *)d->cptr;
    if (!L) return;
    if (L->data) d->host->free(d->host->user, L->data);
    if (L->off) d->host->free(d->host->user, L->off);
    if (L->size) d->host->free(d->host->user, L->size);
    d->host->free(d->host->user, L); d->cptr = NULL;
}
/* appends `n` bytes to the data area; returns the offset or (size_t)-1 */
static inline size_t au_append(dec_t *d, aulist_t *L, const uint8_t *p, size_t n) {
    size_t at = L->len;
    if (n > AULIST_MAX_BYTES || L->len + n > AULIST_MAX_BYTES) return (size_t)-1;
    if (L->len + n > L->cap) {
        size_t cap = L->cap ? L->cap : 65536u; uint8_t *nb;
        while (cap < L->len + n) cap *= 2u;
        nb = (uint8_t *)au_alloc(d, cap);
        if (!nb) return (size_t)-1;
        if (L->len) memcpy(nb, L->data, L->len);
        if (L->data) d->host->free(d->host->user, L->data);
        L->data = nb; L->cap = cap;
    }
    if (n) memcpy(L->data + L->len, p, n);
    L->len += n;
    return at;
}
static inline int au_add(dec_t *d, aulist_t *L, size_t off, size_t size) {
    if (L->count == L->cap_au) {
        size_t cap = L->cap_au ? L->cap_au * 2u : 4096u; uint32_t *no = (uint32_t *)au_alloc(d, cap * sizeof(uint32_t)), *ns = (uint32_t *)au_alloc(d, cap * sizeof(uint32_t));
        if (!no || !ns) { if (no) d->host->free(d->host->user, no); if (ns) d->host->free(d->host->user, ns); return 0; }
        if (L->count) { memcpy(no, L->off, L->count * sizeof(uint32_t)); memcpy(ns, L->size, L->count * sizeof(uint32_t)); }
        if (L->off) d->host->free(d->host->user, L->off);
        if (L->size) d->host->free(d->host->user, L->size);
        L->off = no; L->size = ns; L->cap_au = cap;
    }
    L->off[L->count] = (uint32_t)off; L->size[L->count] = (uint32_t)size; L->count++;
    return 1;
}
/* container callbacks */
static inline maktabasauti_result_t au_fetch(dec_t *d) {
    aulist_t *L = (aulist_t *)d->cptr; uint32_t sz;
    if (!L || L->pos >= L->count) return MAKTABASAUTI_EOF;
    sz = L->size[L->pos];
    if (L->adts) {
        if (sz < 7u || sz > FRAME_BUF_MAX) return MAKTABASAUTI_E_CORRUPT_DATA;
        memcpy(d->frame, L->data + L->off[L->pos], sz); d->frame_len = sz;
    } else {
        if (sz == 0u || sz > (L->mi.has_pce ? 49152u : 8184u)) return MAKTABASAUTI_E_CORRUPT_DATA;
        memcpy(d->frame + 7u, L->data + L->off[L->pos], sz);
        aac_au_wrap(d, &L->mi, sz);
    }
    L->pos++;
    return MAKTABASAUTI_OK;
}
static inline maktabasauti_result_t au_resync(dec_t *d) { (void)d; return MAKTABASAUTI_EOF; }
static inline maktabasauti_result_t au_scan(dec_t *d, int64_t resume, uint64_t *count, int *consistent) {
    const aulist_t *L = (const aulist_t *)d->cptr; (void)resume;
    *count = L ? L->count : 0u; *consistent = 1;
    return MAKTABASAUTI_OK;
}
static inline int64_t au_position(dec_t *d) { const aulist_t *L = (const aulist_t *)d->cptr; return L ? (int64_t)L->pos : 0; }
static inline maktabasauti_result_t au_seek_to(dec_t *d, int64_t pos) { aulist_t *L = (aulist_t *)d->cptr; if (!L || pos < 0) return MAKTABASAUTI_E_INVALID_ARGUMENT; L->pos = (uint64_t)pos; return MAKTABASAUTI_OK; }
#endif
