/* MaktabaSauti, modul maktabasauti_aac: WSPOLNY SILNIK strumieniowego dekodowania AAC (ADTS, LOAS/LATM, ADIF, MP4).  Silnik dekoduje ramki ADTS z d->frame; kontener (aac_container_t)
 * dostarcza kolejne ramki w postaci ADTS ze strumienia wejsciowego (fetch), umie sie zresynchronizowac po uszkodzeniu (resync) i policzyc ramki (scan).  Algorytm dekodowania
 * jest ten sam co w starych adapterach; patrz maktabasauti_aac_adts.c (kontener ADTS) i docs/MAKTABASAUTI_API.md. */
#ifndef MAKTABASAUTI_AAC_ENGINE_H
#define MAKTABASAUTI_AAC_ENGINE_H
#include "maktabasauti/maktabasauti_codec.h"
#include "sauti_aac_cpe.h"
#include "sauti_aac_er.h"
#include "sauti_aac_lcn.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_he_adts_cpe.h"
#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_he_ps_bridge.h"
#include "sauti_aac_lc_multi.h"
#include "sauti_aac_sbr_channel.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define ADTS_MAX_FRAME 8191u
#define ADTS_HEADER_PEEK 9u
#define HE_FRAME_PCM 2048u
#define LC_FRAME_PCM 1024u
#define CACHE_FLOATS (8u * LC_FRAME_PCM)                 /* LC 8 kanalow x 1024 = 8192; HE stereo 2 x 2048 = 4096 tez sie miesci */
#define CHECKPOINT_BUDGET ((size_t)8 << 20)              /* gorna granica pamieci punktow kontrolnych */
#define CHECKPOINT_MAX 256u
#define RESYNC_LIMIT ((size_t)1 << 20)                   /* ile bajtow szukamy synchronizacji po uszkodzeniu */

typedef enum { V_LC_STEREO = 0, V_LC_MONO, V_LC_MULTI, V_HE_MONO, V_HE_CPE, V_ER } variant_t;      /* V_ER: AAC-LD / AAC-ELD (sauti_aac_er), ramki z naglowkiem wewnetrznym (patrz aac_er_wrap) */

typedef struct {
    SautiAacHeAdtsMonoState mono;
    SautiAacHeAdtsCpeState cpe;
    SautiAacSbrChannel channel[2];
    SautiAacHePsBridgeState ps;
    SautiAacSbrComplex pending[SAUTI_AAC_SBR_CHANNEL_GRID];           /* tryb PS: siatka ramki czekajacej na wyprzedzenie */
    SautiAacSbrPayloadFrame pending_payload;
    uint32_t pending_valid;
    uint32_t flushed;
} he_state_t;

/* caly stan dekodowania: kopia = punkt kontrolny */
typedef struct {
    union {
        SautiAacLcSynthesis lc[2];
        SautiAacLcMulti multi;
        he_state_t he;
        SautiAacErState er;
        SautiAacLcnState lcn;                            /* V_ER z er_aot == 2: AAC-LC z ramka 960 */
    } u;
    uint64_t frames_in;                                  /* ramki ADTS zdekodowane */
    uint64_t pcm_out;                                    /* bloki PCM wyprodukowane (LC: = frames_in; HE-PS: o jeden mniej, patrz he_produce) */
    int64_t in_offset;                                   /* pozycja wejscia nastepnej, jeszcze nie pobranej ramki */
} core_t;

typedef struct { core_t core; } checkpoint_t;

struct aac_container;
typedef struct {
    const maktabasauti_codec_t *codec;
    const struct aac_container *cont;                    /* kontener: ADTS / LOAS / ... */
    union { uint64_t align; uint8_t b[1024]; } cs;       /* stan kontenera (nie jest czescia punktu kontrolnego: konfiguracja strumienia jest stala) */
    void *cptr;                                          /* dodatkowe dane kontenera (host->alloc), zwalniane przez cont->release */
    const maktabasauti_host_t *host;
    maktabasauti_reader_t *rd;
    variant_t variant;
    unsigned config;                                     /* channel_configuration z ADTS */
    unsigned rate_index, core_rate, core_channels, crc_present;
    unsigned er_aot, er_480;                             /* V_ER: typ obiektu (23 / 39; 2 = AAC-LC z ramka 960 bez narzedzi ER) i dlugosc ramki 480 zamiast 512 */
    unsigned out_channels, out_rate, frame_pcm, ps_mode;
    SautiAacLcLayout layout;                             /* tylko config 0 (PCE) */
    int seekable, total_known;
    uint64_t known_frames;                               /* liczba ramek ADTS z przejscia po naglowkach (gdy total_known) */
    core_t core;
    core_t cp0;                                          /* stan poczatkowy (blok 0) */
    checkpoint_t **cps; size_t cp_count, cp_cap; uint64_t cp_stride;
    size_t cp_max;
    float cache[CACHE_FLOATS];
    size_t cache_pos, cache_len;                         /* ramki PCM w cache */
    uint64_t position;                                   /* ramki PCM wydane */
    int have_frame; size_t frame_len;
    int src_eof;
    uint64_t trim_skip, avail, skip_left; int trimmed;   /* przyciecie gapless (MP4: lista edycji / iTunSMPB): pominiete ramki PCM na poczatku i dlugosc wyjscia */
    int fmt_pending, fmt_changed;                         /* zmiana parametrow w strumieniu: oczekuje (najpierw zalegle bloki) / juz zaszla (przewijanie wylaczone) */
    unsigned resyncing;                                  /* po flush: ramki, ktorych nie da sie zdekodowac (np. HE bez naglowka SBR po utracie stanu), sa pomijane do pierwszej udanej */
    maktabasauti_result_t pend;                          /* odlozony blad (po wydaniu ramek z cache) */
    char err[96];
    /* bufory robocze HE (nie sa czescia punktu kontrolnego) */
    SautiAacSbrComplex scratch_qmf38[38u * 64u];
    SautiAacSbrComplex scratch_grid[SAUTI_AAC_SBR_CHANNEL_GRID];
    SautiAacSbrComplex scratch_g2[2][SAUTI_AAC_SBR_CHANNEL_GRID];
    uint8_t frame[ADTS_MAX_FRAME + 16u];
} dec_t;

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
/* naglowek ADTS (C): te same warunki co sauti_aac_adts_parse_asm, ale na samych 7-9 bajtach, bez wymagania calej ramki (do sondy po prefiksie i do wyznaczania dlugosci) */
typedef struct { unsigned rate_index, rate, config, frame_bytes, header_bytes, raw_blocks, crc_present; unsigned er, er_aot, er_480; } aac_adts_hdr_t;     /* er: ramka AAC-LD / ELD (naglowek wewnetrzny) */

/* Jednostka dostepu AAC-LD / AAC-ELD nie da sie zapisac w ADTS (pole profilu ma 2 bity), a silnik przekazuje ramki w postaci ADTS, wiec kontener owija ja w 7-bajtowy naglowek WEWNETRZNY,
 * ktory nie jest poprawnym ADTS: pole `layer` = 01 (w prawdziwym ADTS zawsze 00), pole profilu = 0 dla AAC-LD (23) albo 1 dla AAC-ELD (39), bit prywatny = ramka 480; reszta jak w ADTS (indeks
 * czestotliwosci, konfiguracja kanalow, dlugosc ramki z naglowkiem).  Naglowek nigdy nie opuszcza silnika.  Zwraca dlugosc ramki (7 + sz). */
static inline size_t aac_er_wrap(uint8_t *frame, unsigned aot, unsigned rate_index, unsigned channel_config, unsigned frame480, uint32_t sz) {
    return sauti_aac_er_wrap_header(frame, aot, rate_index, channel_config, frame480, sz);
}

static const unsigned aac_adts_rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };



typedef struct aac_container {
    const char *name;                                                                  /* nazwa kontenera w opisie strumienia ("adts", "loas", ...) */
    maktabasauti_result_t (*fetch)(dec_t *d);                                          /* nastepna ramka w postaci ADTS do d->frame/frame_len; OK, EOF, E_TRUNCATED, E_CORRUPT_DATA, blad wejscia; zuzywa wejscie tylko przy OK */
    maktabasauti_result_t (*resync)(dec_t *d);                                         /* po E_CORRUPT_DATA: ustaw wejscie na nastepnej poprawnej ramce */
    maktabasauti_result_t (*scan)(dec_t *d, int64_t resume, uint64_t *count, int *consistent);   /* policz ramki od biezacej pozycji, wroc na `resume` */
    int (*boundary)(const dec_t *d);                                                   /* NULL = zawsze: czy mozna zapisac punkt kontrolny (brak ramek w kolejce kontenera) */
    void (*discard)(dec_t *d);                                                         /* NULL = nic: porzuc kolejke ramek kontenera (po przewinieciu/flush) */
    void (*release)(dec_t *d);                                                         /* NULL = nic: zwolnij dane kontenera */
    maktabasauti_result_t (*prepare)(dec_t *d);                                        /* NULL = nic: tuz po alokacji dekodera, przed pierwszym fetch (np. indeks probek MP4); musi sprawdzic seekable sam */
    maktabasauti_result_t (*seek_to)(dec_t *d, int64_t in_offset);                     /* NULL = host->seek na bajt; in_offset to pozycja kontenera (bajt albo numer probki) */
    int64_t (*position)(dec_t *d);                                                    /* NULL = host->tell (bajt); pozycja kontenera po zuzyciu ramki: wartosc dla seek_to / punktow kontrolnych */
    int (*trim)(dec_t *d, uint64_t decoded_total, uint64_t *skip, uint64_t *avail);    /* NULL = bez przyciecia (gapless): ramki PCM do pominiecia na poczatku i dlugosc po przycieciu; 0 = odrzuc */
    int scan_without_seek;                                                             /* 1 = scan nie czyta wejscia (liczba z indeksu), wiec dziala tez na wejsciu nieprzewijalnym; brak = tylko przewijalne */
} aac_container_t;

/* silnik: ogolne funkcje opisu kodeka */
maktabasauti_result_t aac_eng_open(const maktabasauti_codec_t *self, const aac_container_t *cont, const maktabasauti_host_t *host, maktabasauti_reader_t *rd,
                                   maktabasauti_stream_info_t *info, void **state);
maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_read(void *state, void *dst, size_t n, size_t *frames);
maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual);
maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_reset(void *state);
maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_flush(void *state);
const char *MAKTABASAUTI_CALL aac_eng_last_error(const void *state);
maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_get_info(const void *state, maktabasauti_stream_info_t *info);
void MAKTABASAUTI_CALL aac_eng_close(void *state);
/* wspolne: ADTS header (C) i zgodnosc ramki ze strumieniem */
int aac_adts_parse_header(const uint8_t *p, size_t n, aac_adts_hdr_t *h);
int aac_same_stream(const dec_t *d, const aac_adts_hdr_t *h);                          /* parametry ramki zgodne z otwartym strumieniem (czestotliwosc, kanaly, CRC, 1 raw block) */
#endif
