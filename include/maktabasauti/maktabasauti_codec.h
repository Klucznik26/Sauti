/* MaktabaSauti: interfejs MODULU KODEKA.  Kodek (AAC, DVS, FLAC, ...) to niezalezny modul, ktory udostepnia opis `maktabasauti_codec_t` (tablica funkcji) i nie linkuje sie
 * z rdzeniem: cale wejscie, pamiec i logowanie dostaje przez tablice uslug hosta (`maktabasauti_host_t`) przekazana przy otwarciu.  Dzieki temu modul mozna zbudowac i
 * wydawac osobno (libmaktabasauti_aac.so.1, libmaktabasauti_dvs.so.1, ...), zlinkowac statycznie do wspolnej libmaktabasauti albo napisac jako zewnetrzny, bez zmiany
 * glownego API (maktabasauti.h).
 *
 * STATUS: WERSJA ROZWOJOWA, ABI niestabilne (patrz maktabasauti.h); docelowe zasady ABI jak w maktabasauti.h (struct_size na poczatku, nowe pola tylko na koncu, wartosci wyliczen stale).
 *
 * Zasady dla autora modulu (wymagane, sprawdzane testami wielowatkowymi):
 *   - ZADNEGO stanu globalnego modyfikowalnego.  Caly stan kodeka jest w obiekcie zwracanym z open().  Dozwolone sa tylko tablice stale i leniwie inicjowane tablice
 *     tylko do odczytu, inicjowane bezpiecznie watkowo (atomowo).
 *   - Cala pamiec przez host->alloc/realloc/free.  Brak wlasnych watkow.
 *   - Dane tylko przez host->read/peek/seek (reader); zadnych plikow, gniazd ani systemowych wywolan.  Nie zakladaj, ze caly plik jest w pamieci.
 *   - Funkcje moga byc wolane z dowolnego watku, ale dla jednego obiektu zawsze po kolei (hosty serializuja). */
#ifndef MAKTABASAUTI_CODEC_H
#define MAKTABASAUTI_CODEC_H

#include "maktabasauti.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reader: wejscie z buforowaniem po stronie hosta (kodek nie musi sam buforowac).  Opisany tablica uslug hosta. */
typedef struct maktabasauti_reader maktabasauti_reader_t;

typedef struct maktabasauti_host {
    uint32_t struct_size;
    void *user;                                         /* pierwszy argument kazdej funkcji ponizej */
    /* pamiec (zawsze przez alokator uchwytu; host dostarcza realloc ZAWSZE, nawet gdy alokator uzytkownika go nie ma: rozmiary zna host).  Wyrownanie >= 16 B. */
    void *(MAKTABASAUTI_CALL *alloc)(void *user, size_t size);
    void *(MAKTABASAUTI_CALL *realloc)(void *user, void *ptr, size_t size);
    void (MAKTABASAUTI_CALL *free)(void *user, void *ptr);
    /* wejscie: pozycje bezwzgledne w bajtach od poczatku wejscia */
    int64_t (MAKTABASAUTI_CALL *read)(void *user, maktabasauti_reader_t *reader, void *buffer, size_t size);    /* >0 bajty, 0 EOF, <0 blad (w tym E_WOULD_BLOCK) */
    int64_t (MAKTABASAUTI_CALL *peek)(void *user, maktabasauti_reader_t *reader, void *buffer, size_t size);    /* jak read, bez przesuwania pozycji */
    int64_t (MAKTABASAUTI_CALL *seek)(void *user, maktabasauti_reader_t *reader, int64_t offset, int whence);   /* nowa pozycja albo E_NOT_SEEKABLE */
    int64_t (MAKTABASAUTI_CALL *tell)(void *user, maktabasauti_reader_t *reader);
    int64_t (MAKTABASAUTI_CALL *size)(void *user, maktabasauti_reader_t *reader);                               /* ujemne = nieznany */
    /* CALE wejscie w pamieci (dla opakowanych dekoderow o interfejsie "cala pamiec"): host wczytuje je przy pierwszym wywolaniu i trzyma do zamkniecia dekodera.
     * Kosztowne i nieskalowalne: moduly strumieniowe tego nie uzywaja.  E_NO_MEMORY / E_IO przy bledzie. */
    maktabasauti_result_t (MAKTABASAUTI_CALL *map)(void *user, maktabasauti_reader_t *reader, const uint8_t **data, size_t *size);
    /* opcje: wartosc dla klucza (z przedrostkiem "nazwa." lub wspolna) albo NULL */
    const char *(MAKTABASAUTI_CALL *option)(void *user, const char *key);
    /* log (level jak MAKTABASAUTI_LOG_*) */
    void (MAKTABASAUTI_CALL *log)(void *user, int level, const char *message);
} maktabasauti_host_t;

enum {                                                  /* maktabasauti_codec_t.flags */
    MAKTABASAUTI_CODEC_FLAG_NEEDS_SEEKABLE_INPUT = 1u << 0,   /* otwarcie wymaga seek (np. MP4 z moov na koncu); dla wejsc nieprzewijalnych host zwroci E_NOT_SEEKABLE */
    MAKTABASAUTI_CODEC_FLAG_STREAMING = 1u << 1,              /* dziala na wejsciu z samym read (bez seek, bez znajomosci rozmiaru) */
    MAKTABASAUTI_CODEC_FLAG_HAS_SEEK = 1u << 2,               /* implementuje seek */
    MAKTABASAUTI_CODEC_FLAG_HAS_TAGS = 1u << 3,
    MAKTABASAUTI_CODEC_FLAG_EXPERIMENTAL = 1u << 4,
    MAKTABASAUTI_CODEC_FLAG_NEEDS_FULL_BUFFER = 1u << 5       /* dla opakowanych starszych dekoderow: wymaga calego wejscia w pamieci (host->map); NIE jest strumieniowy */
};

/* probe: ocena rozpoznania po prefiksie danych (host daje do 4096 bajtow od poczatku wejscia; `n` < 4096 oznacza, ze to caly material; dla kodeka z flaga
 * NEEDS_FULL_BUFFER `prefix` to CALE wejscie).  `self` to opis kodeka (pozwala jednej funkcji obslugiwac wiele kodekow przez `user_data`).  Zwraca 0 = to nie moj format,
 * 1..100 = pewnosc (100 = nie ma watpliwosci).  Wygrywa najwyzsza pewnosc; przy remisie kolejnosc w rejestrze.  name_hint moze byc NULL.  Bez alokacji i bez stanu. */
struct maktabasauti_codec;
typedef int32_t (MAKTABASAUTI_CALL *maktabasauti_codec_probe_fn)(const struct maktabasauti_codec *self, const uint8_t *prefix, size_t n, const char *name_hint);

typedef struct maktabasauti_codec {
    uint32_t struct_size;
    uint32_t abi_version;                               /* MAKTABASAUTI_ABI_VERSION, z ktorym zbudowano modul */
    const char *name;                                   /* "aac" (krotka, male litery, jednoznaczna w rejestrze) */
    const char *description;                            /* "MPEG-4 AAC family (LC, HE-AAC v1/v2, ...)" */
    const char *module;                                 /* "maktabasauti_aac" */
    uint32_t flags;                                     /* MAKTABASAUTI_CODEC_FLAG_* */
    uint32_t reserved0;
    const void *user_data;                              /* dowolne dane modulu dla tego opisu (np. wskaznik na opakowany dekoder); host ich nie interpretuje */

    maktabasauti_codec_probe_fn probe;

    /* Otwarcie: czyta z `reader` przez host; wypelnia `info` (sample_rate, channels, channel_mask, native_format, total_frames, flagi, nazwy) i zwraca stan w *state.
     * Po bledzie *state = NULL i nic nie jest zajete.  Reader zostaje wlasnoscia hosta (nie zamykac). */
    maktabasauti_result_t (MAKTABASAUTI_CALL *open)(const struct maktabasauti_codec *self, const maktabasauti_host_t *host, maktabasauti_reader_t *reader,
                                                    maktabasauti_stream_info_t *info, void **state);

    /* Odczyt: do dst_frames ramek PRZEPLATANYCH w formacie info.native_format (host konwertuje do zadanego); wynik jak w maktabasauti_decoder_read:
     * OK i *frames >= 1, EOF i 0, albo blad (z *frames = ile poprawnie wydano). */
    maktabasauti_result_t (MAKTABASAUTI_CALL *read)(void *state, void *dst, size_t dst_frames, size_t *frames);

    /* Opcjonalne (NULL = brak): seek dokladny do `frame` (po gapless); *actual = ramka docelowa.  Wolane tylko gdy HAS_SEEK. */
    maktabasauti_result_t (MAKTABASAUTI_CALL *seek)(void *state, uint64_t frame, uint32_t flags, uint64_t *actual);
    /* reset: stan jak tuz po open (wejscie przewinal host na poczatek).  flush: porzuc historie dekodera bez przewijania wejscia. */
    maktabasauti_result_t (MAKTABASAUTI_CALL *reset)(void *state);
    maktabasauti_result_t (MAKTABASAUTI_CALL *flush)(void *state);
    maktabasauti_result_t (MAKTABASAUTI_CALL *get_tag)(const void *state, const char *key, char *buf, size_t cap, size_t *needed);
    const char *(MAKTABASAUTI_CALL *last_error)(const void *state);        /* opis ostatniego bledu (waznosc do nastepnego wywolania); NULL = brak */
    /* Aktualne parametry strumienia: wolane po E_FORMAT_CHANGED (i opcjonalnie w innych momentach); NULL = parametry nigdy sie nie zmieniaja. */
    maktabasauti_result_t (MAKTABASAUTI_CALL *get_info)(const void *state, maktabasauti_stream_info_t *info);
    void (MAKTABASAUTI_CALL *close)(void *state);                           /* zwalnia stan przez host->free */

    /* Zarezerwowane na koder (NULL w dekoderach): kodek moze dostarczyc koder bez zmiany ABI rdzenia. */
    void *reserved_encoder[6];
    void *reserved[8];
} maktabasauti_codec_t;

/* Wejscia modulu.  Kazdy modul dostarcza funkcje o swojej nazwie (statyczne budowanie wielu modulow w jednej bibliotece):
 *     const maktabasauti_codec_t *maktabasauti_aac_codec(void);    // maktabasauti_dvs_codec(), maktabasauti_flac_codec(), ...
 * oraz, dla ladowania dynamicznego (osobne .so/.dll), jeden wspolny symbol:
 *     maktabasauti_module_entry
 * Dynamiczne ladowanie modulow (dlopen/LoadLibrary) NIE nalezy do rdzenia: realizuje je opcjonalna, osobna warstwa platformowa. */
typedef maktabasauti_result_t (MAKTABASAUTI_CALL *maktabasauti_module_entry_fn)(uint32_t abi_version, const maktabasauti_codec_t *const **codecs, size_t *count);

#ifdef __cplusplus
}
#endif
#endif /* MAKTABASAUTI_CODEC_H */
