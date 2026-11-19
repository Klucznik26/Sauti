/* MaktabaSauti: wejscia z pamieci i z pliku.  Plik: stdio (UTF-8; na Windows konwersja do UTF-16 i _wfopen).  To jedyne miejsce rdzenia zalezne od systemu. */
#if !defined(_WIN32)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _FILE_OFFSET_BITS
#    define _FILE_OFFSET_BITS 64
#  endif
#endif
#include "mks_internal.h"
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <wchar.h>
#endif

/* ---- z pamieci ---- */
typedef struct { mks_alloc_t alloc; const uint8_t *data; size_t size, pos; } mem_io_t;

static int64_t MAKTABASAUTI_CALL mem_read(void *user, void *buffer, size_t size) {
    mem_io_t *m = (mem_io_t *)user;
    size_t left = m->size - m->pos;
    if (size > left) size = left;
    if (size) memcpy(buffer, m->data + m->pos, size);
    m->pos += size;
    return (int64_t)size;                                /* 0 = koniec */
}
static int64_t MAKTABASAUTI_CALL mem_seek(void *user, int64_t offset, int whence) {
    mem_io_t *m = (mem_io_t *)user;
    int64_t base;
    if (whence == 0) base = 0;
    else if (whence == 1) base = (int64_t)m->pos;
    else if (whence == 2) base = (int64_t)m->size;
    else return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if ((offset > 0 && base > INT64_MAX - offset) || (offset < 0 && base < INT64_MIN - offset)) return MAKTABASAUTI_E_OUT_OF_RANGE;
    base += offset;
    if (base < 0 || (uint64_t)base > (uint64_t)m->size) return MAKTABASAUTI_E_OUT_OF_RANGE;
    m->pos = (size_t)base;
    return base;
}
static int64_t MAKTABASAUTI_CALL mem_size(void *user) { return (int64_t)((mem_io_t *)user)->size; }
static void MAKTABASAUTI_CALL mem_close(void *user) {
    mem_io_t *m = (mem_io_t *)user;
    mks_alloc_t a = m->alloc;
    mks_free(&a, m);
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_io_from_memory(const void *data, size_t size, const maktabasauti_allocator_t *allocator, maktabasauti_io_t *out) {
    mks_alloc_t a;
    mem_io_t *m;
    maktabasauti_result_t r;
    if (!out || (!data && size) || size > (size_t)INT64_MAX) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    r = mks_alloc_init(&a, allocator);
    if (r != MAKTABASAUTI_OK) return r;
    m = (mem_io_t *)mks_alloc(&a, sizeof *m);
    if (!m) return MAKTABASAUTI_E_NO_MEMORY;
    m->alloc = a; m->data = (const uint8_t *)data; m->size = size; m->pos = 0;
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    out->user = m; out->read = mem_read; out->seek = mem_seek; out->size = mem_size; out->close = mem_close;
    return MAKTABASAUTI_OK;
}

/* ---- z pliku ---- */
typedef struct { mks_alloc_t alloc; FILE *fp; int64_t size; } file_io_t;

#if defined(_WIN32)
#  define MKS_FSEEK(fp, off, wh) _fseeki64((fp), (off), (wh))
#  define MKS_FTELL(fp) _ftelli64(fp)
#else
#  define MKS_FSEEK(fp, off, wh) fseeko((fp), (off_t)(off), (wh))
#  define MKS_FTELL(fp) ((int64_t)ftello(fp))
#endif

static int64_t MAKTABASAUTI_CALL file_read(void *user, void *buffer, size_t size) {
    file_io_t *f = (file_io_t *)user;
    size_t got;
    if (size == 0) return 0;
    got = fread(buffer, 1, size, f->fp);
    if (got == 0 && ferror(f->fp)) return MAKTABASAUTI_E_IO;
    return (int64_t)got;
}
static int64_t MAKTABASAUTI_CALL file_seek(void *user, int64_t offset, int whence) {
    file_io_t *f = (file_io_t *)user;
    int w = whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END;
    if (whence < 0 || whence > 2) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (MKS_FSEEK(f->fp, offset, w) != 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    return MKS_FTELL(f->fp);
}
static int64_t MAKTABASAUTI_CALL file_size(void *user) { return ((file_io_t *)user)->size; }
static void MAKTABASAUTI_CALL file_close(void *user) {
    file_io_t *f = (file_io_t *)user;
    mks_alloc_t a = f->alloc;
    if (f->fp) fclose(f->fp);
    mks_free(&a, f);
}

#if defined(_WIN32)
static FILE *open_utf8(const char *path) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *w;
    FILE *fp;
    if (n <= 0) return NULL;
    w = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, w, n);
    fp = _wfopen(w, L"rb");
    HeapFree(GetProcessHeap(), 0, w);
    return fp;
}
#else
static FILE *open_utf8(const char *path) { return fopen(path, "rb"); }
#endif

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_io_from_file(const char *path_utf8, const maktabasauti_allocator_t *allocator, maktabasauti_io_t *out) {
    mks_alloc_t a;
    file_io_t *f;
    maktabasauti_result_t r;
    if (!path_utf8 || !out) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    r = mks_alloc_init(&a, allocator);
    if (r != MAKTABASAUTI_OK) return r;
    f = (file_io_t *)mks_calloc(&a, sizeof *f);
    if (!f) return MAKTABASAUTI_E_NO_MEMORY;
    f->alloc = a;
    f->fp = open_utf8(path_utf8);
    if (!f->fp) { mks_free(&a, f); return MAKTABASAUTI_E_IO; }
    f->size = -1;
    if (MKS_FSEEK(f->fp, 0, SEEK_END) == 0) {            /* rozmiar: tylko gdy plik da sie przewijac */
        int64_t end = MKS_FTELL(f->fp);
        if (end >= 0) f->size = end;
        if (MKS_FSEEK(f->fp, 0, SEEK_SET) != 0) { fclose(f->fp); mks_free(&a, f); return MAKTABASAUTI_E_IO; }
    }
    memset(out, 0, sizeof *out);
    out->struct_size = sizeof *out;
    out->user = f; out->read = file_read; out->seek = file_seek; out->size = file_size; out->close = file_close;
    return MAKTABASAUTI_OK;
}
