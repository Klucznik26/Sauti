/* MaktabaSauti: alokator.  Kazdy blok ma ukryty 16-bajtowy naglowek z zamowionym rozmiarem: dzieki temu realloc "zastepczy" (alloc + kopia + free) zna rozmiar starego
 * bloku i kopiuje dokladnie min(stary, nowy), a alokator uzytkownika nie musi znac rozmiarow.  Brak stanu globalnego. */
#include "mks_internal.h"
#include <stdlib.h>
#include <string.h>

#define MKS_HDR 16u

static void *MAKTABASAUTI_CALL libc_alloc(void *user, size_t size) { (void)user; return malloc(size); }
static void *MAKTABASAUTI_CALL libc_realloc(void *user, void *ptr, size_t size) { (void)user; return realloc(ptr, size); }
static void MAKTABASAUTI_CALL libc_free(void *user, void *ptr) { (void)user; free(ptr); }

maktabasauti_result_t mks_alloc_init(mks_alloc_t *out, const maktabasauti_allocator_t *user) {
    if (!out) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (!user) {
        out->user = NULL; out->alloc = libc_alloc; out->realloc = libc_realloc; out->free = libc_free;
        return MAKTABASAUTI_OK;
    }
    /* pola poza struct_size wywolujacego traktujemy jako NULL; alloc i free sa wymagane */
    if (!MKS_HAS_FIELD(user, maktabasauti_allocator_t, alloc) || !MKS_HAS_FIELD(user, maktabasauti_allocator_t, free) || !user->alloc || !user->free)
        return MAKTABASAUTI_E_INVALID_ARGUMENT;
    out->user = user->user;
    out->alloc = user->alloc;
    out->free = user->free;
    out->realloc = MKS_HAS_FIELD(user, maktabasauti_allocator_t, realloc) ? user->realloc : NULL;
    return MAKTABASAUTI_OK;
}

void *mks_alloc(const mks_alloc_t *a, size_t size) {
    unsigned char *raw;
    if (!a || size == 0 || size > (size_t)-1 - MKS_HDR) return NULL;
    raw = (unsigned char *)a->alloc(a->user, size + MKS_HDR);
    if (!raw) return NULL;
    memcpy(raw, &size, sizeof size);                      /* reszta naglowka nieistotna */
    return raw + MKS_HDR;
}

void *mks_calloc(const mks_alloc_t *a, size_t size) {
    void *p = mks_alloc(a, size);
    if (p) memset(p, 0, size);
    return p;
}

size_t mks_block_size(const void *ptr) {
    size_t size;
    if (!ptr) return 0;
    memcpy(&size, (const unsigned char *)ptr - MKS_HDR, sizeof size);
    return size;
}

void mks_free(const mks_alloc_t *a, void *ptr) {
    if (!a || !ptr) return;
    a->free(a->user, (unsigned char *)ptr - MKS_HDR);
}

void *mks_realloc(const mks_alloc_t *a, void *ptr, size_t size) {
    size_t old;
    void *fresh;
    if (!a || !ptr || size == 0 || size > (size_t)-1 - MKS_HDR) return NULL;
    old = mks_block_size(ptr);
    if (a->realloc) {                                     /* alokator potrafi realloc: semantyka libc na bloku z naglowkiem */
        unsigned char *raw = (unsigned char *)a->realloc(a->user, (unsigned char *)ptr - MKS_HDR, size + MKS_HDR);
        if (!raw) return NULL;
        memcpy(raw, &size, sizeof size);
        return raw + MKS_HDR;
    }
    fresh = mks_alloc(a, size);                           /* bez realloc: rozmiar starego bloku znamy z naglowka, kopiujemy dokladnie min(stary, nowy) */
    if (!fresh) return NULL;
    memcpy(fresh, ptr, old < size ? old : size);
    mks_free(a, ptr);
    return fresh;
}
