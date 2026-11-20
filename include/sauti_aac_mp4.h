#ifndef SAUTI_AAC_MP4_H
#define SAUTI_AAC_MP4_H
#include <stddef.h>
#include <stdint.h>

/* Minimalny demuxer ISO BMFF (MP4/M4A/M4B) dla AAC: znajduje pierwsza ssciezke dzwiekowa `mp4a` (AAC), czyta AudioSpecificConfig i tablice probek (stsz, stsc,
 * stco/co64) i owija KAZDA jednostke dostepu w naglowek ADTS (bez CRC), dzieki czemu wszystkie natywne adaptery ADTS (LC mono/stereo/wielokanalowy, HE) dzialaja bez zmian.
 * Lista edycji (elst, pierwszy wpis niepusty) daje przyciecie jak w FFmpeg: `skip_samples` (opoznienie kodera) i `duration_samples`.
 * Fragmentowane MP4 (moov z mvex/trex + moof/traf/trun, rozmiary z trun, tfhd albo trex, podstawa offsetow z base_data_offset albo poczatek moof): jednostki z tablic stbl i z
 * fragmentow tworza jedna liste; niepelny (ucięty) ostatni fragment jest pomijany, kompletne zostaja.
 * Warianty kontenera: esds takze w pudelku `wave` (QuickTime, wersja 1/2 wpisu mp4a), rozmiary z stsz albo stz2 (4/8/16 bitow), segmenty DASH (styp/sidx pomijane).
 * Nieobslugiwane (zwraca 0, bez PCM zastepczego): brak sciezki AAC, inny kodek (np. ALAC), czestotliwosc poza tablica ADTS, AU > 8184 B, uszkodzone tabele. */
typedef struct {
    unsigned aot;                 /* typ obiektu z ASC (2 = LC; 5/29 = SBR/PS jawnie; 23 = AAC-LD, 39 = AAC-ELD bez SBR: tylko sciezka strumieniowa silnika) */
    unsigned frame480;            /* AAC-LD / ELD: ramka 480 zamiast 512 probek; AAC-LC (aot 2): frameLengthFlag = 1, ramka 960 (tylko mono i stereo bez SBR) */
    unsigned rate_index;          /* indeks czestotliwosci rdzenia (ADTS) */
    unsigned sample_rate;         /* czestotliwosc rdzenia, Hz */
    unsigned channel_config;      /* channel_configuration z ASC (0 = PCE w strumieniu) */
    uint64_t sample_count;        /* liczba jednostek dostepu (po 1024 probki rdzenia) */
    uint64_t skip_samples;        /* z elst: probki do pominiecia na poczatku (czestotliwosc rdzenia) */
    uint64_t duration_samples;    /* z elst: dlugosc po przycieciu (0 = bez ograniczenia) */
    /* iTunSMPB (informacja "gapless" iTunes) w moov, uzyta tylko gdy nie ma listy edycji: wartosci w probkach RDZENIA jak w pliku (dla HE-AAC 24 kHz plik ma 2048 / 768 /
     * 288000, FFmpeg podwaja je do 4096 / 1536 / 576000): adapter skaluje je jak liste edycji. */
    uint32_t gapless;             /* 1 = jest iTunSMPB */
    uint64_t gapless_priming;     /* probki do pominiecia na poczatku */
    uint64_t gapless_samples;     /* dlugosc po przycieciu (0 = bez ograniczenia) */
} SautiAacMp4Info;

/* 1 = zbudowano strumien ADTS (`*adts` z malloc, zwolnic free()), 0 = nie dotyczy / nieobslugiwane / bledne. */
int sauti_aac_mp4_to_adts(const uint8_t *data,size_t size,uint8_t **adts,size_t *adts_length,SautiAacMp4Info *info);
/* Tani test prefiksu: `ftyp` w pierwszych bajtach. */
int sauti_aac_mp4_probe(const uint8_t *data,size_t size);

/* STRUMIENIOWO / INDEKS PROBEK: ten sam demuxer bez wczytywania pliku.  Wywolujacy czyta tylko pudelko moov (jego zawartosc) i kolejne moof (zawartosc + pozycja w pliku), a indeks
 * trzyma liste jednostek dostepu {pozycja w pliku, rozmiar} (16 B na probke: pamiec O(liczba probek), jak w kazdym demuxerze MP4; nie ma w niej danych audio).  Cala pamiec przez
 * podane funkcje alokatora.  Jednostki z tablic stbl (moov) i z fragmentow (moof, w kolejnosci dodania) tworza jedna liste, jak w sauti_aac_mp4_to_adts. */
typedef struct {
    void *user;
    void *(*alloc)(void *user,size_t size);
    void (*free)(void *user,void *ptr);                  /* ptr != NULL */
} SautiAacMp4Alloc;
typedef struct SautiAacMp4Index SautiAacMp4Index;
/* Tworzy indeks z zawartosci pudelka `moov` (bez naglowka pudelka).  NULL = brak sciezki AAC / nieobslugiwane / blad (przyczyny jak w sauti_aac_mp4_to_adts).  `info` wypelnione jak tam
 * (bez sample_count; skip/duration z elst albo iTunSMPB). */
SautiAacMp4Index *sauti_aac_mp4_index_new(const SautiAacMp4Alloc *a,const uint8_t *moov,size_t moov_size,SautiAacMp4Info *info);
/* Dodaje jednostki z fragmentu: `box_start` = pozycja pudelka moof w pliku (domyslna podstawa offsetow), `payload` = zawartosc pudelka (bez naglowka). 1 = ok, 0 = blad (indeks nietkniety). */
int sauti_aac_mp4_index_add_moof(SautiAacMp4Index *idx,uint64_t box_start,const uint8_t *payload,size_t payload_size);
uint64_t sauti_aac_mp4_index_count(const SautiAacMp4Index *idx);
/* pozycja i rozmiar jednostki i; 1 = ok, 0 = poza zakresem */
int sauti_aac_mp4_index_get(const SautiAacMp4Index *idx,uint64_t i,uint64_t *offset,uint32_t *size);
int sauti_aac_mp4_index_has_fragments(const SautiAacMp4Index *idx);   /* moov ma mvex: moga istniec moof */
/* Zapomina wszystkie dotychczasowe jednostki (indeks zostaje uzyteczny: nastepne moof dopisuja od zera).  Dla wejscia NA ZYWO: pamiec indeksu jest ograniczona rozmiarem jednego
 * fragmentu, a nie dlugoscia strumienia. */
void sauti_aac_mp4_index_forget_units(SautiAacMp4Index *idx);
void sauti_aac_mp4_index_free(SautiAacMp4Index *idx);
#endif
