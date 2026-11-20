/* MaktabaSauti: wewnetrzny naglowek rdzenia (NIE publiczny).  C99, bez zaleznosci od systemu operacyjnego poza <stdio.h> w wejsciu z pliku. */
#ifndef MKS_INTERNAL_H
#define MKS_INTERNAL_H

#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include <stddef.h>
#include <stdint.h>

#define MKS_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
/* czy struktura wywolujacego (struct_size) obejmuje pole */
#define MKS_HAS_FIELD(ptr, type, field) ((ptr)->struct_size >= offsetof(type, field) + sizeof(((type *)0)->field))

/* ---- alokator: kopia alokatora uzytkownika + ukryty naglowek rozmiaru (16 B) ------------------------------------------------------------------------------- */
typedef struct mks_alloc {
    void *user;
    void *(MAKTABASAUTI_CALL *alloc)(void *user, size_t size);
    void *(MAKTABASAUTI_CALL *realloc)(void *user, void *ptr, size_t size);   /* moze byc NULL */
    void (MAKTABASAUTI_CALL *free)(void *user, void *ptr);
} mks_alloc_t;
/* NULL user = libc.  Zwraca MAKTABASAUTI_OK albo E_INVALID_ARGUMENT (brak alloc lub free w strukturze wywolujacego). */
maktabasauti_result_t mks_alloc_init(mks_alloc_t *out, const maktabasauti_allocator_t *user);
void *mks_alloc(const mks_alloc_t *a, size_t size);                 /* size > 0; NULL gdy brak pamieci lub przepelnienie */
void *mks_calloc(const mks_alloc_t *a, size_t size);
void *mks_realloc(const mks_alloc_t *a, void *ptr, size_t size);    /* ptr != NULL, size > 0; przy bledzie NULL i stary blok wazny */
void mks_free(const mks_alloc_t *a, void *ptr);                     /* NULL bezpieczne (nie jest przekazywane do alokatora) */
size_t mks_block_size(const void *ptr);                             /* rozmiar zamowiony przy alloc (z ukrytego naglowka) */

/* ---- czytnik buforowany nad maktabasauti_io_t ---------------------------------------------------------------------------------------------------------------- */
struct maktabasauti_reader {
    maktabasauti_io_t io;            /* kopia (nie wlasnosc, dopoki dekoder nie zostanie otwarty) */
    const mks_alloc_t *alloc;
    uint8_t *buf;
    size_t cap, pos, len;            /* okno bufora: buf[0..len) = bajty wejscia od pozycji `base` */
    int64_t base;
    int64_t size;                    /* rozmiar wejscia, ujemny = nieznany */
    uint8_t *mem;                    /* tryb "cale wejscie w pamieci" (mks_reader_map): wtedy czytamy stad */
    size_t mem_size;
    size_t mem_pos;
    int64_t io_err;                  /* ostatni blad z io.read (<0), zerowany po udanym odczycie: peek zglasza go, gdy nie moze spelnic zadania (WOULD_BLOCK a koniec danych) */
};
typedef struct maktabasauti_reader mks_reader_t;
maktabasauti_result_t mks_reader_init(mks_reader_t *r, const mks_alloc_t *alloc, const maktabasauti_io_t *io);
void mks_reader_free(mks_reader_t *r);                              /* zwalnia bufory; NIE zamyka wejscia */
int64_t mks_reader_read(mks_reader_t *r, void *dst, size_t n);      /* >0 bajty, 0 EOF, <0 kod bledu (np. E_WOULD_BLOCK) */
int64_t mks_reader_peek(mks_reader_t *r, void *dst, size_t n);      /* n <= pojemnosc bufora */
int64_t mks_reader_seek(mks_reader_t *r, int64_t offset, int whence);
int64_t mks_reader_tell(const mks_reader_t *r);
int64_t mks_reader_size(const mks_reader_t *r);
/* wczytuje cale wejscie (od jego poczatku) do pamieci i od tej pory czytnik obsluguje je z pamieci; limit MKS_MAX_FULL_BUFFER */
maktabasauti_result_t mks_reader_map(mks_reader_t *r, const uint8_t **data, size_t *size);
#define MKS_READER_BUFFER 16384u
#define MKS_PROBE_BYTES 16384u      /* prefiks dla sond: rowny oknu detekcji minimp3 i pojemnosci czytnika; sondy NIGDY nie dostaja calego pliku (R1) */
#define MKS_MAX_FULL_BUFFER ((size_t)1 << 30)
#define MKS_MAP_INITIAL_MAX ((size_t)64 << 20)   /* poczatkowa pojemnosc bufora calego wejscia: dalej rosnie tylko o faktycznie odczytane dane */

/* ---- PCM ----------------------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct mks_dither { uint32_t state; uint32_t enabled; } mks_dither_t;
void mks_dither_init(mks_dither_t *d, uint32_t enabled);
maktabasauti_result_t mks_pcm_convert(const void *src, maktabasauti_sample_format_t sf, void *dst, maktabasauti_sample_format_t df, size_t samples, mks_dither_t *dither);

/* ---- rejestr ------------------------------------------------------------------------------------------------------------------------------------------------- */
struct maktabasauti_registry {
    mks_alloc_t alloc;               /* tylko rejestr tworzony; domyslny jest stala */
    const maktabasauti_codec_t *const *codecs;
    size_t count, cap;
    int is_default;
};
/* wbudowane (starsze) kodeki opakowane w interfejs modulu; kolejnosc = kolejnosc dopasowania przy remisie */
const maktabasauti_codec_t *const *mks_legacy_codecs(size_t *count);

#endif
