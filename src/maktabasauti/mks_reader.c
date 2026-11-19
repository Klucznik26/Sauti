/* MaktabaSauti: czytnik buforowany (peek/read/seek/tell/size) nad maktabasauti_io_t, wspolny dla wszystkich modulow.  Brak stanu globalnego. */
#include "mks_internal.h"
#include <string.h>

maktabasauti_result_t mks_reader_init(mks_reader_t *r, const mks_alloc_t *alloc, const maktabasauti_io_t *io) {
    int64_t sz;
    memset(r, 0, sizeof *r);
    r->io = *io;
    r->alloc = alloc;
    r->cap = MKS_READER_BUFFER;
    r->buf = (uint8_t *)mks_alloc(alloc, r->cap);
    if (!r->buf) return MAKTABASAUTI_E_NO_MEMORY;
    sz = io->size ? io->size(io->user) : -1;
    r->size = sz >= 0 ? sz : -1;
    return MAKTABASAUTI_OK;
}

void mks_reader_free(mks_reader_t *r) {
    if (!r) return;
    mks_free(r->alloc, r->buf);
    mks_free(r->alloc, r->mem);
    r->buf = NULL; r->mem = NULL;
}

/* dopelnia bufor tak, zeby od pos bylo dostepne co najmniej `need` bajtow (need <= cap), o ile wejscie je ma; zwraca ile jest dostepne albo kod bledu (gdy nic nie ma) */
static int64_t fill(mks_reader_t *r, size_t need) {
    if (r->len - r->pos >= need) return (int64_t)(r->len - r->pos);
    if (r->pos > 0) {                                    /* kompaktacja */
        memmove(r->buf, r->buf + r->pos, r->len - r->pos);
        r->base += (int64_t)r->pos;
        r->len -= r->pos;
        r->pos = 0;
    }
    while (r->len < need) {
        int64_t got = r->io.read(r->io.user, r->buf + r->len, r->cap - r->len);
        if (got < 0) return r->len ? (int64_t)r->len : got;   /* mamy czesc danych: oddajemy je, blad wroci przy kolejnym wywolaniu */
        if (got == 0) break;                              /* koniec danych */
        r->len += (size_t)got;
    }
    return (int64_t)(r->len - r->pos);
}

int64_t mks_reader_read(mks_reader_t *r, void *dst, size_t n) {
    uint8_t *out = (uint8_t *)dst;
    size_t done = 0;
    if (n == 0) return 0;
    if (r->mem) {
        size_t left = r->mem_size - r->mem_pos;
        if (n > left) n = left;
        if (n) memcpy(dst, r->mem + r->mem_pos, n);
        r->mem_pos += n;
        return (int64_t)n;
    }
    while (done < n) {
        size_t avail = r->len - r->pos;
        if (avail == 0 && n - done >= r->cap) {          /* duzy odczyt: bez posredniego bufora */
            int64_t got;
            r->base += (int64_t)r->len; r->len = 0; r->pos = 0;
            got = r->io.read(r->io.user, out + done, n - done);
            if (got < 0) return done ? (int64_t)done : got;
            if (got == 0) break;
            r->base += got;                               /* bufor jest pusty: base = pozycja logiczna */
            done += (size_t)got;
            continue;
        }
        if (avail == 0) {
            int64_t f = fill(r, 1);
            if (f < 0) return done ? (int64_t)done : f;
            if (f == 0) break;
            avail = r->len - r->pos;
        }
        {
            size_t take = n - done < avail ? n - done : avail;
            memcpy(out + done, r->buf + r->pos, take);
            r->pos += take;
            done += take;
        }
    }
    return (int64_t)done;
}

int64_t mks_reader_peek(mks_reader_t *r, void *dst, size_t n) {
    int64_t avail;
    if (n == 0) return 0;
    if (r->mem) {
        size_t left = r->mem_size - r->mem_pos;
        if (n > left) n = left;
        if (n) memcpy(dst, r->mem + r->mem_pos, n);
        return (int64_t)n;
    }
    if (n > r->cap) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    avail = fill(r, n);
    if (avail < 0) return avail;
    if ((size_t)avail > n) avail = (int64_t)n;
    memcpy(dst, r->buf + r->pos, (size_t)avail);
    return avail;
}

int64_t mks_reader_tell(const mks_reader_t *r) {
    if (r->mem) return (int64_t)r->mem_pos;
    return r->base + (int64_t)r->pos;
}

int64_t mks_reader_size(const mks_reader_t *r) {
    if (r->mem) return (int64_t)r->mem_size;
    return r->size;
}

int64_t mks_reader_seek(mks_reader_t *r, int64_t offset, int whence) {
    int64_t target, cur = mks_reader_tell(r), sz = mks_reader_size(r);
    if (whence == 0) target = offset;
    else if (whence == 1) target = cur + offset;
    else if (whence == 2) { if (sz < 0) return MAKTABASAUTI_E_NOT_SEEKABLE; target = sz + offset; }
    else return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (target < 0) return MAKTABASAUTI_E_OUT_OF_RANGE;
    if (r->mem) {
        if ((uint64_t)target > (uint64_t)r->mem_size) return MAKTABASAUTI_E_OUT_OF_RANGE;
        r->mem_pos = (size_t)target;
        return target;
    }
    if (target >= r->base && target <= r->base + (int64_t)r->len) {   /* w oknie bufora: bez dotykania wejscia */
        r->pos = (size_t)(target - r->base);
        return target;
    }
    if (!r->io.seek) return MAKTABASAUTI_E_NOT_SEEKABLE;
    {
        int64_t got = r->io.seek(r->io.user, target, 0);
        if (got < 0) return got;
        r->base = got; r->pos = 0; r->len = 0;
        return got;
    }
}

maktabasauti_result_t mks_reader_map(mks_reader_t *r, const uint8_t **data, size_t *size) {
    size_t cap, used = 0;
    uint8_t *mem;
    if (r->mem) { *data = r->mem; *size = r->mem_size; return MAKTABASAUTI_OK; }
    /* od poczatku wejscia: albo bufor jeszcze go trzyma, albo trzeba przewinac */
    if (r->base != 0) {
        if (!r->io.seek) return MAKTABASAUTI_E_NOT_SEEKABLE;
        if (r->io.seek(r->io.user, 0, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
        r->base = 0; r->pos = 0; r->len = 0;
    }
    cap = r->size >= 0 ? (size_t)r->size : MKS_READER_BUFFER * 4u;
    if (r->size >= 0 && (uint64_t)r->size > MKS_MAX_FULL_BUFFER) return MAKTABASAUTI_E_NO_MEMORY;
    if (cap < r->len) cap = r->len;
    if (cap == 0) cap = 1;
    mem = (uint8_t *)mks_alloc(r->alloc, cap);
    if (!mem) return MAKTABASAUTI_E_NO_MEMORY;
    if (r->len) { memcpy(mem, r->buf, r->len); used = r->len; }   /* bajty, ktore juz mamy w buforze (base == 0) */
    for (;;) {
        int64_t got;
        if (used == cap) {
            uint8_t *bigger;
            size_t ncap = cap * 2u;
            if (ncap > MKS_MAX_FULL_BUFFER || ncap < cap) { mks_free(r->alloc, mem); return MAKTABASAUTI_E_NO_MEMORY; }
            bigger = (uint8_t *)mks_realloc(r->alloc, mem, ncap);
            if (!bigger) { mks_free(r->alloc, mem); return MAKTABASAUTI_E_NO_MEMORY; }
            mem = bigger; cap = ncap;
        }
        got = r->io.read(r->io.user, mem + used, cap - used);
        if (got < 0) { mks_free(r->alloc, mem); return (maktabasauti_result_t)got; }
        if (got == 0) break;
        used += (size_t)got;
    }
    r->mem = mem; r->mem_size = used;
    r->mem_pos = (size_t)(r->base + (int64_t)r->pos < (int64_t)used ? r->base + (int64_t)r->pos : (int64_t)used);
    *data = r->mem; *size = r->mem_size;
    return MAKTABASAUTI_OK;
}
