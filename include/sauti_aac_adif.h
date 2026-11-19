#ifndef SAUTI_AAC_ADIF_H
#define SAUTI_AAC_ADIF_H
#include <stddef.h>
#include <stdint.h>

/* AAC w ADIF (ISO/IEC 14496-3, 1.A.2): naglowek `ADIF` z jednym program_config_element(), potem raw_data_block() jeden za drugim BEZ dlugosci ramek.  Granice blokow
 * wyznacza parsowanie elementow (SCE/CPE przez parsery AAC-LC, FIL, DSE, PCE, ID_END + wyrownanie do bajtu); kazdy blok jest przepisany do ADTS bez CRC, wiec natywne
 * adaptery ADTS (LC mono/stereo, HE-AAC z SBR w elementach FIL) dzialaja bez zmian.
 *
 * Obslugiwane: jeden program, profil LC (object_type 1), jeden element czolowy (SCE = mono albo CPE = stereo) bez kanalow bocznych/tylnych/LFE/dodatkowych, dowolna
 * czestotliwosc z tablicy ADTS, stala i zmienna przeplywnosc, identyfikator praw autorskich, komentarz w PCE.
 * Nieobslugiwane (0, bez PCM zastepczego): wiele programow, inne profile, wielokanalowosc, CCE, ucięty blok jest pomijany. */
typedef struct {
    unsigned rate_index;          /* indeks czestotliwosci z PCE */
    unsigned sample_rate;
    unsigned channel_config;      /* 1 = mono (SCE), 2 = stereo (CPE) */
    unsigned bitstream_type;      /* 0 = stala, 1 = zmienna przeplywnosc */
    uint32_t bitrate;             /* z naglowka (bit/s dla zmiennej, najwieksza dla stalej) */
    uint64_t frames;              /* liczba blokow */
} SautiAacAdifInfo;

/* 1 = zbudowano strumien ADTS (`*adts` z malloc, zwolnic free()), 0 = nie dotyczy / nieobslugiwane / bledne. */
int sauti_aac_adif_to_adts(const uint8_t *data,size_t size,uint8_t **adts,size_t *adts_length,SautiAacAdifInfo *info);
/* Test prefiksu: `ADIF` na poczatku. */
int sauti_aac_adif_probe(const uint8_t *data,size_t size);
#endif
