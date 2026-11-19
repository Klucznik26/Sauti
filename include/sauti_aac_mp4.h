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
    unsigned aot;                 /* typ obiektu z ASC (2 = LC; 5/29 = SBR/PS jawnie) */
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
#endif
