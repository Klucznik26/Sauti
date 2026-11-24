#ifndef SAUTI_AAC_LATM_H
#define SAUTI_AAC_LATM_H
#include <stddef.h>
#include <stdint.h>

/* AAC w LOAS/LATM (ISO/IEC 14496-3, 1.7): strumien AudioSyncStream (synchronizacja 0x2B7 + 13-bitowa dlugosc) z AudioMuxElement w srodku.  Wszystkie jednostki dostepu sa
 * przepisane (przesuniete bitowo do granicy bajtu) do ADTS bez CRC, wiec natywne adaptery ADTS (LC, HE-AAC) dzialaja bez zmian.
 *
 * Obslugiwane: audioMuxVersion 0 i 1, jeden program i jedna warstwa, allStreamsSameTimeFraming = 1, frameLengthType 0, dowolna liczba subramek, useSameStreamMux (konfiguracja
 * powtarzana i pomijana), AudioSpecificConfig AAC-LC / SBR (AOT 5) / PS (AOT 29) jawnie i niejawnie (rozszerzenie 0x2B7 / 0x548), otherData i CRC pomijane.
 * Tylko strumieniowo (sauti_aac_latm_convert_element, silnik): AAC-LD / ELD, ER, Main / LTP oraz USAC (AOT 42, konfiguracje kanalow 1..7; ASC do 96 bajtow, patrz sauti_aac_latm_usac_asc).
 * Nieobslugiwane (0, bez PCM zastepczego): inne AOT, ramka 960, channelConfiguration 0 (PCE), kilka programow/warstw, CELP/HVXC, zmiana konfiguracji w trakcie strumienia,
 * LATM bez ramowania LOAS (nie jest samosynchronizujacy). */
typedef struct {
    unsigned aot;                 /* typ obiektu rdzenia (2 = LC); sbr/ps ponizej */
    unsigned rate_index;          /* indeks czestotliwosci rdzenia (ADTS) */
    unsigned sample_rate;         /* czestotliwosc rdzenia, Hz */
    unsigned channel_config;      /* channelConfiguration z ASC (1..7) */
    unsigned sbr,ps;              /* 1 = zasygnalizowane jawnie w ASC */
    uint64_t frames;              /* liczba jednostek dostepu */
} SautiAacLatmInfo;

/* 1 = zbudowano strumien ADTS (`*adts` z malloc, zwolnic free()), 0 = nie dotyczy / nieobslugiwane / bledne. */
int sauti_aac_latm_to_adts(const uint8_t *data,size_t size,uint8_t **adts,size_t *adts_length,SautiAacLatmInfo *info);
/* Test prefiksu: synchronizacja LOAS na poczatku i druga na koncu pierwszego elementu (odrzuca przypadkowe dane). */
int sauti_aac_latm_probe(const uint8_t *data,size_t size);

/* STRUMIENIOWO (po jednym elemencie LOAS): konwersja AudioMuxElement (bez 3-bajtowego naglowka LOAS) do ramek ADTS.  Stan trzyma konfiguracje strumienia (AudioSpecificConfig z pierwszego
 * elementu, stala; zmiana = blad).  Wynik: 1 = n >= 1 ramek ADTS w out (dlugosci w lens[]), 0 = element pominiety (brak konfiguracji / brak danych audio: tak jak w pelnej konwersji),
 * -1 = uszkodzony element (po wczesniejszej poprawnej konfiguracji), -2 = zmiana konfiguracji w trakcie strumienia (nieobslugiwana). */
typedef struct { uint64_t opaque[48]; } SautiAacLatmState;
void sauti_aac_latm_state_init(SautiAacLatmState *st);
/* USAC (xHE-AAC, AOT 42): the AudioSpecificConfig of the stream (bit-aligned copy, `*len` bytes, valid while `st` lives), or 0 when the stream is something else / has no configuration yet. */
int sauti_aac_latm_usac_asc(const SautiAacLatmState *st,const uint8_t **asc,size_t *len);
/* AAC-ELD with the low-delay SBR tool (AOT 39, ldSbrPresentFlag): the AudioSpecificConfig (bit-aligned copy), or 0 for other streams. */
int sauti_aac_latm_eld_asc(const SautiAacLatmState *st,const uint8_t **asc,size_t *len);
int sauti_aac_latm_convert_element(SautiAacLatmState *st,const uint8_t *payload,size_t len,uint8_t *out,size_t cap,uint16_t *lens,unsigned max_frames,unsigned *n);
#endif
