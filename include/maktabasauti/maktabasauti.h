/* MaktabaSauti: wspolne API do dekodowania (i, docelowo, kodowania) audio.  Jedno API dla wszystkich kodekow (AAC, DVS, FLAC, ...), ktore sa niezaleznymi modulami
 * (maktabasauti_aac, maktabasauti_dvs, maktabasauti_flac, ...); opcjonalna wspolna biblioteka libmaktabasauti laczy rdzen z modulami.
 *
 * STATUS: PROJEKT DO PRZEGLADU.  ABI zostaje zamrozone dopiero w pierwszym wydaniu (MAKTABASAUTI_ABI_VERSION 1); do tego czasu zmiany sa dozwolone.  Opis zasad:
 * docs/MAKTABASAUTI_API.md.
 *
 * Zasady ABI (obowiazuja po zamrozeniu):
 *   - Kazda struktura publiczna zaczyna sie od `uint32_t struct_size` ustawianego przez wywolujacego na sizeof(struktury) z jego naglowka.  Biblioteka czyta z struktury
 *     wejsciowej tylko tyle bajtow, ile wskazuje struct_size (starsze programy dzialaja z nowsza biblioteka), a do struktury wyjsciowej zapisuje tylko tyle, ile zmiesci
 *     struct_size.  Nowe pola dopisuje sie wylacznie na koncu.
 *   - Wartosci wyliczen nigdy nie sa zmieniane ani uzywane ponownie.  Bez pol bitowych, bez wskaznikow do funkcji w strukturach wyjsciowych poza opisanymi.
 *   - Tylko typy o stalej szerokosci; calosc w C99, zgodna z C++.  Brak zaleznosci od SDL, PipeWire, FFmpeg i jakiegokolwiek systemu operacyjnego poza <stddef.h>/<stdint.h>.
 *
 * Zasady watkow: NIE ma stanu globalnego.  Rozne uchwyty (dekodery, rejestry, wejscia) sa calkowicie niezalezne i moga pracowac rownolegle w roznych watkach bez
 * synchronizacji.  Ten sam uchwyt wymaga zewnetrznej synchronizacji.  Biblioteka nie tworzy watkow. */
#ifndef MAKTABASAUTI_H
#define MAKTABASAUTI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- eksport symboli i konwencja wywolan --------------------------------------------------------------------------------------------------------------- */
#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(MAKTABASAUTI_BUILD_SHARED)
#    define MAKTABASAUTI_API __declspec(dllexport)
#  elif defined(MAKTABASAUTI_USE_SHARED)
#    define MAKTABASAUTI_API __declspec(dllimport)
#  else
#    define MAKTABASAUTI_API
#  endif
#  define MAKTABASAUTI_CALL __cdecl
#else
#  if defined(__GNUC__) && __GNUC__ >= 4
#    define MAKTABASAUTI_API __attribute__((visibility("default")))
#  else
#    define MAKTABASAUTI_API
#  endif
#  define MAKTABASAUTI_CALL
#endif

/* ---- wersje -------------------------------------------------------------------------------------------------------------------------------------------- */
#define MAKTABASAUTI_VERSION_MAJOR 1u
#define MAKTABASAUTI_VERSION_MINOR 0u
#define MAKTABASAUTI_VERSION_PATCH 0u
#define MAKTABASAUTI_VERSION_STRING "1.0.0"
#define MAKTABASAUTI_ABI_VERSION 1u          /* zmienia sie tylko przy niezgodnej zmianie ABI; to samo co numer SONAME (libmaktabasauti_aac.so.1) */

MAKTABASAUTI_API uint32_t MAKTABASAUTI_CALL maktabasauti_abi_version(void);   /* wersja ABI biblioteki; porownac z MAKTABASAUTI_ABI_VERSION */
MAKTABASAUTI_API void MAKTABASAUTI_CALL maktabasauti_version(uint32_t *major, uint32_t *minor, uint32_t *patch);
MAKTABASAUTI_API const char *MAKTABASAUTI_CALL maktabasauti_version_string(void);

/* ---- wyniki i bledy ------------------------------------------------------------------------------------------------------------------------------------ */
/* Dodatnie = sukces z informacja, 0 = OK, ujemne = blad.  Kazda funkcja zwraca jednoznaczny kod; szczegolowy opis ostatniego bledu danego uchwytu:
 * maktabasauti_decoder_last_error().  Wartosci sa czescia ABI i nie zmieniaja sie. */
typedef int32_t maktabasauti_result_t;
enum {
    MAKTABASAUTI_EOF = 1,                         /* koniec strumienia (nie blad): dla read, 0 ramek */
    MAKTABASAUTI_OK = 0,
    MAKTABASAUTI_E_INVALID_ARGUMENT = -1,         /* NULL, zly rozmiar struktury, wartosc spoza zakresu */
    MAKTABASAUTI_E_NO_MEMORY = -2,
    MAKTABASAUTI_E_IO = -3,                       /* blad wejscia/wyjscia z callbacku wejscia */
    MAKTABASAUTI_E_UNSUPPORTED_FORMAT = -4,       /* zaden kodek nie rozpoznal danych */
    MAKTABASAUTI_E_UNSUPPORTED_FEATURE = -5,      /* dane rozpoznane, ale ten wariant (profil, opcja) nie jest obslugiwany */
    MAKTABASAUTI_E_CORRUPT_DATA = -6,             /* uszkodzony strumien; po reset/seek mozna probowac dalej */
    MAKTABASAUTI_E_TRUNCATED = -7,                /* dane urwane w srodku elementu */
    MAKTABASAUTI_E_NOT_SEEKABLE = -8,             /* przewijanie/reset niemozliwe dla tego wejscia lub kodeka */
    MAKTABASAUTI_E_OUT_OF_RANGE = -9,             /* pozycja poza strumieniem */
    MAKTABASAUTI_E_BAD_STATE = -10,               /* wywolanie niedozwolone w tym stanie uchwytu */
    MAKTABASAUTI_E_ABI_MISMATCH = -11,            /* biblioteka i naglowek maja rozne wersje ABI */
    MAKTABASAUTI_E_BUFFER_TOO_SMALL = -12,
    MAKTABASAUTI_E_WOULD_BLOCK = -13,             /* wejscie nie ma jeszcze danych (strumien na zywo); ponowic pozniej */
    MAKTABASAUTI_E_CANCELLED = -14,
    MAKTABASAUTI_E_NOT_IMPLEMENTED = -15,         /* funkcja zarezerwowana (np. kodowanie) lub kodek jej nie ma */
    MAKTABASAUTI_E_INTERNAL = -16,                /* blad w bibliotece: zglosic */
    MAKTABASAUTI_E_FORMAT_CHANGED = -17           /* parametry strumienia (czestotliwosc, kanaly, uklad) zmienily sie w srodku; ramki sprzed zmiany zostaly wydane,
                                                     maktabasauti_decoder_get_info() zwraca juz nowe parametry, kolejny read kontynuuje po zmianie */
};
MAKTABASAUTI_API const char *MAKTABASAUTI_CALL maktabasauti_result_name(maktabasauti_result_t result);      /* np. "MAKTABASAUTI_E_IO"; nigdy NULL */
MAKTABASAUTI_API const char *MAKTABASAUTI_CALL maktabasauti_result_message(maktabasauti_result_t result);   /* krotki opis po angielsku; nigdy NULL */

/* ---- pamiec -------------------------------------------------------------------------------------------------------------------------------------------- */
/* Wszystkie alokacje biblioteki przechodza przez alokator.  NULL = malloc/realloc/free z libc.  Alokator musi byc zywy i bezpieczny watkowo dla uchwytow, ktore go uzywaja.
 * Biblioteka nie zwraca wskaznikow do swoich buforow wewnetrznych poza lancuchami opisanymi w tym pliku (waznymi do nastepnego wywolania na tym uchwycie lub do jego zamkniecia). */
typedef struct maktabasauti_allocator {
    uint32_t struct_size;
    void *user;
    void *(MAKTABASAUTI_CALL *alloc)(void *user, size_t size);                  /* NULL gdy brak pamieci */
    void *(MAKTABASAUTI_CALL *realloc)(void *user, void *ptr, size_t size);     /* moze byc NULL: wtedy alloc+kopiowanie+free */
    void (MAKTABASAUTI_CALL *free)(void *user, void *ptr);
} maktabasauti_allocator_t;

/* ---- wejscie (strumieniowe) ---------------------------------------------------------------------------------------------------------------------------- */
/* Dekodowanie czyta dane przez callbacki; caly plik nigdy nie musi byc w pamieci.
 * read: zwraca liczbe odczytanych bajtow (>0), 0 = koniec danych, MAKTABASAUTI_E_WOULD_BLOCK (jako ujemna wartosc) gdy chwilowo brak, inna wartosc ujemna = blad.  Zwracanie
 *       mniej niz zadano jest dozwolone (jak read(2)).
 * seek: whence jak w stdio (0 = poczatek, 1 = biezaca, 2 = koniec); zwraca nowa pozycje (>=0) albo MAKTABASAUTI_E_NOT_SEEKABLE/blad.  NULL = wejscie nieprzewijalne
 *       (tylko kodeki strumieniowe, brak seek/reset).
 * size: calkowity rozmiar w bajtach albo ujemna wartosc, gdy nieznany; moze byc NULL.
 * close: wolane dokladnie raz, gdy uchwyt (lub nieudane otwarcie) konczy zycie wejscia; moze byc NULL.
 * Wlasnosc: wejscie przekazane do maktabasauti_decoder_open() przechodzi na biblioteke zawsze (takze po bledzie): callback close zostanie wywolany. */
typedef struct maktabasauti_io {
    uint32_t struct_size;
    void *user;
    int64_t (MAKTABASAUTI_CALL *read)(void *user, void *buffer, size_t size);
    int64_t (MAKTABASAUTI_CALL *seek)(void *user, int64_t offset, int whence);
    int64_t (MAKTABASAUTI_CALL *size)(void *user);
    void (MAKTABASAUTI_CALL *close)(void *user);
} maktabasauti_io_t;

/* Wygodne wejscia.  Z pamieci: dane musza zyc do zamkniecia wejscia; stan pozycji jest alokowany przez alokator i zwalniany przez close.  Z pliku: sciezka w UTF-8
 * (na Windows konwertowana do UTF-16); wymaga bibliotek standardowych systemu, ale rdzen bez tego dziala. */
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_io_from_memory(const void *data, size_t size, const maktabasauti_allocator_t *allocator, maktabasauti_io_t *out);
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_io_from_file(const char *path_utf8, const maktabasauti_allocator_t *allocator, maktabasauti_io_t *out);

/* ---- PCM: formaty probek i uklad kanalow --------------------------------------------------------------------------------------------------------------- */
/* Probki przeplatane (interleaved), w kolejnosci bajtow rodzimej dla platformy.  S24 = 3 bajty na probke, spakowane, bez wypelnienia.  Wartosci stale (ABI). */
typedef int32_t maktabasauti_sample_format_t;
enum {
    MAKTABASAUTI_FORMAT_UNKNOWN = 0,
    MAKTABASAUTI_FORMAT_U8 = 1,        /* 0..255, cisza = 128 */
    MAKTABASAUTI_FORMAT_S16 = 2,
    MAKTABASAUTI_FORMAT_S24 = 3,
    MAKTABASAUTI_FORMAT_S32 = 4,
    MAKTABASAUTI_FORMAT_F32 = 5,       /* -1.0 .. 1.0 */
    MAKTABASAUTI_FORMAT_F64 = 6,
    /* 0x100 + format: zarezerwowane na warianty planarne (wersja 1: MAKTABASAUTI_E_UNSUPPORTED_FEATURE) */
    MAKTABASAUTI_FORMAT_PLANAR_FLAG = 0x100
};
MAKTABASAUTI_API size_t MAKTABASAUTI_CALL maktabasauti_format_bytes(maktabasauti_sample_format_t format);   /* bajtow na probke, 0 gdy nieznany lub planarny */

/* Maska ukladu kanalow: te same bity co WAVEFORMATEXTENSIBLE.  Kanaly w buforze sa w kolejnosci rosnacych bitow maski (np. 5.1: FL FR FC LFE BL BR).
 * channel_mask == 0: liczba kanalow znana, rozmieszczenie nieokreslone (zrodlo nie podalo). */
#define MAKTABASAUTI_CH_FL   0x00000001ull
#define MAKTABASAUTI_CH_FR   0x00000002ull
#define MAKTABASAUTI_CH_FC   0x00000004ull
#define MAKTABASAUTI_CH_LFE  0x00000008ull
#define MAKTABASAUTI_CH_BL   0x00000010ull
#define MAKTABASAUTI_CH_BR   0x00000020ull
#define MAKTABASAUTI_CH_FLC  0x00000040ull
#define MAKTABASAUTI_CH_FRC  0x00000080ull
#define MAKTABASAUTI_CH_BC   0x00000100ull
#define MAKTABASAUTI_CH_SL   0x00000200ull
#define MAKTABASAUTI_CH_SR   0x00000400ull
#define MAKTABASAUTI_CH_TC   0x00000800ull
#define MAKTABASAUTI_CH_TFL  0x00001000ull
#define MAKTABASAUTI_CH_TFC  0x00002000ull
#define MAKTABASAUTI_CH_TFR  0x00004000ull
#define MAKTABASAUTI_CH_TBL  0x00008000ull
#define MAKTABASAUTI_CH_TBC  0x00010000ull
#define MAKTABASAUTI_CH_TBR  0x00020000ull
#define MAKTABASAUTI_LAYOUT_MONO     (MAKTABASAUTI_CH_FC)
#define MAKTABASAUTI_LAYOUT_STEREO   (MAKTABASAUTI_CH_FL | MAKTABASAUTI_CH_FR)
#define MAKTABASAUTI_LAYOUT_5_1      (MAKTABASAUTI_CH_FL | MAKTABASAUTI_CH_FR | MAKTABASAUTI_CH_FC | MAKTABASAUTI_CH_LFE | MAKTABASAUTI_CH_BL | MAKTABASAUTI_CH_BR)
#define MAKTABASAUTI_LAYOUT_7_1      (MAKTABASAUTI_LAYOUT_5_1 | MAKTABASAUTI_CH_SL | MAKTABASAUTI_CH_SR)

/* Konwersja bufora PCM miedzy formatami (uzyteczna dla odtwarzaczy).  samples = liczba probek (ramki x kanaly).  dither: MAKTABASAUTI_DITHER_*; src/dst moga sie
 * pokrywac tylko gdy formaty maja ten sam rozmiar probki i sa rowne.  Zwraca MAKTABASAUTI_E_UNSUPPORTED_FEATURE dla formatow planarnych. */
enum { MAKTABASAUTI_DITHER_NONE = 0, MAKTABASAUTI_DITHER_TPDF = 1 };
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_pcm_convert(const void *src, maktabasauti_sample_format_t src_format, void *dst,
                                                                                  maktabasauti_sample_format_t dst_format, size_t samples, uint32_t dither);

/* ---- opis strumienia ----------------------------------------------------------------------------------------------------------------------------------- */
#define MAKTABASAUTI_UNKNOWN_LENGTH UINT64_MAX
enum {                                              /* maktabasauti_stream_info_t.flags */
    MAKTABASAUTI_STREAM_SEEKABLE = 1u << 0,         /* seek i reset dzialaja */
    MAKTABASAUTI_STREAM_LIVE = 1u << 1,             /* strumien bez znanego konca (radio, ...) */
    MAKTABASAUTI_STREAM_LOSSLESS = 1u << 2,
    MAKTABASAUTI_STREAM_VBR = 1u << 3,
    MAKTABASAUTI_STREAM_GAPLESS_TRIMMED = 1u << 4   /* priming i dopelnienie zostaly juz odciete z total_frames i z odczytu */
};
typedef struct maktabasauti_stream_info {
    uint32_t struct_size;
    uint32_t sample_rate;                           /* Hz wyjscia */
    uint32_t channels;
    uint32_t flags;                                 /* MAKTABASAUTI_STREAM_* */
    uint64_t channel_mask;                          /* 0 = nieokreslone */
    maktabasauti_sample_format_t native_format;     /* naturalny format zrodla: odczyt w tym formacie nie traci dokladnosci */
    uint32_t bits_per_sample;                       /* istotne bity zrodla (np. 16, 24), 0 = nieznane */
    uint64_t total_frames;                          /* po przycieciu gapless; MAKTABASAUTI_UNKNOWN_LENGTH gdy nieznane */
    uint64_t priming_frames;                        /* informacyjnie: ile ramek opoznienia kodera odcieto */
    uint64_t end_padding_frames;                    /* informacyjnie: ile ramek dopelnienia odcieto */
    uint32_t bitrate;                               /* sredni, bit/s; 0 = nieznany */
    uint32_t reserved0;
    char codec_name[32];                            /* np. "aac", "flac", "dvs" (ASCII, zakonczone NUL) */
    char codec_profile[32];                         /* np. "HE-AAC v2", "LC" (puste gdy brak) */
    char container_name[32];                        /* np. "adts", "mp4", "loas", "ogg" */
} maktabasauti_stream_info_t;

/* ---- opcje, logowanie ---------------------------------------------------------------------------------------------------------------------------------- */
/* Opcje to pary tekstowe; klucze kodekowo-specyficzne maja przedrostek nazwy kodeka ("aac.downmix").  Nieznane klucze sa ignorowane (z ostrzezeniem w logu), zeby ten sam
 * zestaw opcji dzialal z roznymi kodekami.  Opcje wspolne: "dither" = "none"|"tpdf" (domyslnie wg maktabasauti_decoder_read), "gapless" = "on"|"off" (domyslnie on),
 * "sample_rate" = liczba Hz (zrodla syntezowane bez wlasnej czestotliwosci, np. MIDI i chiptune: zadana czestotliwosc wyjscia), "loop" = "off"|"on" (zrodla ze zdefiniowana petla). */
typedef struct maktabasauti_option {
    const char *key;
    const char *value;
} maktabasauti_option_t;

enum { MAKTABASAUTI_LOG_NONE = 0, MAKTABASAUTI_LOG_ERROR = 1, MAKTABASAUTI_LOG_WARNING = 2, MAKTABASAUTI_LOG_INFO = 3, MAKTABASAUTI_LOG_DEBUG = 4 };
typedef void (MAKTABASAUTI_CALL *maktabasauti_log_fn)(void *user, int level, const char *message);   /* message wazny tylko w czasie wywolania; moze byc z dowolnego watku uchwytu */

/* ---- rejestr kodekow (moduly) -------------------------------------------------------------------------------------------------------------------------- */
/* Rejestr to zbior kodekow dostepnych dekoderowi.  Rejestr domyslny zawiera kodeki zlinkowane z ta wersja biblioteki i jest niezmienny (bezpieczny bez synchronizacji).
 * Wlasny rejestr (maktabasauti_registry_create + add) pozwala dolaczyc kodek jako niezalezny modul bez przebudowy API (opis modulu: maktabasauti_codec.h). */
typedef struct maktabasauti_registry maktabasauti_registry_t;
struct maktabasauti_codec;                                          /* maktabasauti_codec.h */
MAKTABASAUTI_API const maktabasauti_registry_t *MAKTABASAUTI_CALL maktabasauti_default_registry(void);
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_registry_create(const maktabasauti_allocator_t *allocator, maktabasauti_registry_t **out);
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_registry_add(maktabasauti_registry_t *registry, const struct maktabasauti_codec *codec);
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_registry_add_registry(maktabasauti_registry_t *registry, const maktabasauti_registry_t *other);
MAKTABASAUTI_API void MAKTABASAUTI_CALL maktabasauti_registry_destroy(maktabasauti_registry_t *registry);   /* NULL bezpieczne; nie dotyczy rejestru domyslnego */
MAKTABASAUTI_API size_t MAKTABASAUTI_CALL maktabasauti_registry_codec_count(const maktabasauti_registry_t *registry);

typedef struct maktabasauti_codec_info {
    uint32_t struct_size;
    uint32_t flags;                                 /* MAKTABASAUTI_CODEC_CAN_DECODE | MAKTABASAUTI_CODEC_CAN_ENCODE | MAKTABASAUTI_CODEC_EXPERIMENTAL */
    char name[32];                                  /* "aac" */
    char description[96];
    char module[32];                                /* modul, z ktorego pochodzi: "maktabasauti_aac" */
} maktabasauti_codec_info_t;
enum { MAKTABASAUTI_CODEC_CAN_DECODE = 1u << 0, MAKTABASAUTI_CODEC_CAN_ENCODE = 1u << 1, MAKTABASAUTI_CODEC_EXPERIMENTAL = 1u << 2 };
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_registry_codec_info(const maktabasauti_registry_t *registry, size_t index, maktabasauti_codec_info_t *out);

/* ---- dekoder ------------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct maktabasauti_decoder maktabasauti_decoder_t;       /* nieprzezroczysty; wlasny, niezalezny stan */

enum {                                              /* maktabasauti_open_params_t.flags */
    MAKTABASAUTI_OPEN_REQUIRE_SEEKABLE = 1u << 0,   /* blad E_NOT_SEEKABLE, gdy nie da sie przewijac */
    MAKTABASAUTI_OPEN_NO_GAPLESS = 1u << 1          /* nie przycinaj priming/dopelnienia */
};
typedef struct maktabasauti_open_params {
    uint32_t struct_size;
    uint32_t abi_version;                           /* ustaw MAKTABASAUTI_ABI_VERSION */
    maktabasauti_io_t io;                           /* przechodzi na wlasnosc biblioteki (takze po bledzie) */
    const maktabasauti_allocator_t *allocator;      /* NULL = libc */
    const maktabasauti_registry_t *registry;        /* NULL = rejestr domyslny */
    uint32_t flags;                                 /* MAKTABASAUTI_OPEN_* */
    uint32_t reserved0;
    const char *codec;                              /* NULL = autodetekcja; inaczej nazwa kodeka ("flac") */
    const char *name_hint;                          /* nazwa pliku lub rozszerzenie pomagajace w autodetekcji (opcjonalnie) */
    const maktabasauti_option_t *options;           /* tablica opcji lub NULL */
    size_t option_count;
    maktabasauti_log_fn log;                        /* opcjonalny, na uchwyt */
    void *log_user;
    int32_t log_level;                              /* MAKTABASAUTI_LOG_*; 0 = bez logowania */
    int32_t reserved1;
} maktabasauti_open_params_t;

/* Otwiera dekoder: wykrywa kodek (po prefiksie danych, bez czytania calosci), tworzy niezalezny stan.  Zwraca E_UNSUPPORTED_FORMAT, gdy nikt nie rozpoznal danych. */
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_open(const maktabasauti_open_params_t *params, maktabasauti_decoder_t **out);
MAKTABASAUTI_API void MAKTABASAUTI_CALL maktabasauti_decoder_close(maktabasauti_decoder_t *decoder);   /* NULL bezpieczne; zamyka tez wejscie */

MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_get_info(const maktabasauti_decoder_t *decoder, maktabasauti_stream_info_t *out);

/* Czyta do dst_frames ramek (ramka = po jednej probce na kazdy kanal) w zadanym formacie (konwersja z formatu natywnego po stronie biblioteki; dither wg opcji).
 * Wynik: OK i *frames_read w [1, dst_frames] (moze byc mniej niz zadano, takze w srodku strumienia); MAKTABASAUTI_EOF i *frames_read = 0 na koncu;
 * blad: *frames_read = liczba ramek poprawnie wydanych przed bledem (moze byc > 0), a kolejne wywolania zwracaja ten sam blad az do seek/reset/flush.
 * E_WOULD_BLOCK: wejscie chwilowo bez danych (dla wejsc na zywo), stan zachowany, ponowic. */
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_read(maktabasauti_decoder_t *decoder, void *dst, size_t dst_frames,
                                                                                   maktabasauti_sample_format_t format, size_t *frames_read);

/* Przewijanie do ramki `frame` (liczonej od poczatku odtwarzanego materialu, po przycieciu gapless).  MAKTABASAUTI_SEEK_EXACT: dokladnie do ramki (kodek dekoduje
 * od najblizszego punktu dostepu i odrzuca nadmiar); MAKTABASAUTI_SEEK_APPROX: moze wyladowac wczesniej, szybciej.  *actual (opcjonalny) = ramka, na ktorej
 * wyladowano.  frame == total_frames jest dozwolone (nastepny read daje EOF); wiecej: E_OUT_OF_RANGE.  Czysci stan bledu. */
enum { MAKTABASAUTI_SEEK_EXACT = 0, MAKTABASAUTI_SEEK_APPROX = 1 };
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_seek(maktabasauti_decoder_t *decoder, uint64_t frame, uint32_t flags, uint64_t *actual);
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_tell(const maktabasauti_decoder_t *decoder, uint64_t *frame);

/* Reset: powrot na poczatek z calkowitym wyczyszczeniem stanu dekodera i stanu bledu (jak seek do 0, ale dziala tez gdy kodek nie ma seek, o ile wejscie da sie
 * przewinac do poczatku).  Flush: porzuca dane buforowane i historie dekodera BEZ przewijania wejscia: do wznowienia po nieciaglosci w strumieniu na zywo lub po
 * uszkodzonym fragmencie (dekoder zsynchronizuje sie na nastepnych danych).  Oba czyszcza stan bledu. */
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_reset(maktabasauti_decoder_t *decoder);
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_flush(maktabasauti_decoder_t *decoder);

/* Opis ostatniego bledu tego uchwytu (UTF-8, ASCII w praktyce); wazny do nastepnego wywolania na tym uchwycie.  Nigdy NULL ("" gdy brak). */
MAKTABASAUTI_API const char *MAKTABASAUTI_CALL maktabasauti_decoder_last_error(const maktabasauti_decoder_t *decoder);

/* Znaczniki (tagi): klucz bez rozrozniania wielkosci liter ("title", "artist", "album", ...).  Zapisuje wartosc UTF-8 do buf (z NUL); *needed = potrzebny rozmiar z NUL.
 * E_BUFFER_TOO_SMALL gdy cap za maly; E_OUT_OF_RANGE gdy brak takiego znacznika; E_NOT_IMPLEMENTED gdy kodek nie udostepnia znacznikow. */
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_get_tag(const maktabasauti_decoder_t *decoder, const char *key, char *buf, size_t cap, size_t *needed);

/* ---- kodowanie (zarezerwowane) ------------------------------------------------------------------------------------------------------------------------- */
/* API kodera jest zaprojektowane teraz, zeby dodanie kodeka kodujacego nie wymagalo zmiany ABI.  W wersji 1 kazda funkcja zwraca MAKTABASAUTI_E_NOT_IMPLEMENTED,
 * dopoki dany kodek nie dostarczy kodera.  Wejscie danych dla kodera to ta sama konwencja PCM co w dekoderze, a wyjscie to maktabasauti_io_t (zapis). */
typedef struct maktabasauti_encoder maktabasauti_encoder_t;
typedef struct maktabasauti_io_writer {
    uint32_t struct_size;
    void *user;
    int64_t (MAKTABASAUTI_CALL *write)(void *user, const void *buffer, size_t size);   /* liczba zapisanych bajtow lub blad (<0) */
    int64_t (MAKTABASAUTI_CALL *seek)(void *user, int64_t offset, int whence);         /* opcjonalny (kontenery z naglowkiem uzupelnianym na koncu) */
    void (MAKTABASAUTI_CALL *close)(void *user);
} maktabasauti_io_writer_t;
typedef struct maktabasauti_encoder_params {
    uint32_t struct_size;
    uint32_t abi_version;
    maktabasauti_io_writer_t out;
    const maktabasauti_allocator_t *allocator;
    const maktabasauti_registry_t *registry;
    const char *codec;                              /* np. "aac" */
    const char *container;                          /* np. "adts", "mp4"; NULL = domyslny dla kodeka */
    uint32_t sample_rate;
    uint32_t channels;
    uint64_t channel_mask;
    maktabasauti_sample_format_t input_format;      /* format bufora przekazywanego do encode */
    uint32_t bitrate;                               /* bit/s; 0 = domyslny */
    const maktabasauti_option_t *options;
    size_t option_count;
    maktabasauti_log_fn log;
    void *log_user;
    int32_t log_level;
    int32_t reserved0;
} maktabasauti_encoder_params_t;
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_encoder_open(const maktabasauti_encoder_params_t *params, maktabasauti_encoder_t **out);
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_encoder_encode(maktabasauti_encoder_t *encoder, const void *src, size_t frames);
MAKTABASAUTI_API maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_encoder_finish(maktabasauti_encoder_t *encoder);   /* wypycha reszte, domyka kontener */
MAKTABASAUTI_API void MAKTABASAUTI_CALL maktabasauti_encoder_close(maktabasauti_encoder_t *encoder);

#ifdef __cplusplus
}
#endif
#endif /* MAKTABASAUTI_H */
