#ifndef SAUTI_AAC_LC_MULTI_H
#define SAUTI_AAC_LC_MULTI_H
#include "sauti_aac_cpe.h"

/* AAC-LC wielokanalowy (ADTS channel_configuration 3..7, takze 1 i 2): raw_data_block z elementami SCE / CPE / LFE, z pominieciem DSE, PCE i FIL (nie-SBR).
 * Kanaly sa dekodowane w kolejnosci elementow w strumieniu (konfiguracja 6: C, L, R, Ls, Rs, LFE) i zwracane w kolejnosci WYJSCIOWEJ jak w FFmpeg/WAVE
 * (konfiguracja 6: FL, FR, FC, LFE, BL, BR) wg sauti_aac_lc_multi_output_map.  Jeden wspolny generator szumu PNS dla calej ramki (jak w FFmpeg: kolejnosc elementow).
 * Niewspierane (fail-closed, -1): CCE, SBR, ADTS z CRC (ochrona zalezy od ukladu elementow), channel_configuration 0 (PCE w strumieniu) i >7. */
#define SAUTI_AAC_LC_MULTI_MAX 8u
typedef struct {
    unsigned channels;                                  /* 1..8, z channel_configuration */
    unsigned config;                                    /* channel_configuration ADTS; 0 = uklad z PCE (map ponizej) */
    uint8_t map[SAUTI_AAC_LC_MULTI_MAX];                /* tylko config 0: map[wyjscie] = kanal (miejsce wg PCE) */
    unsigned n_elements;                                /* tylko config 0: elementy z PCE; element strumienia trafia na miejsce wg (typ, element_instance_tag), jak w FFmpeg */
    uint8_t elem_type[SAUTI_AAC_LC_MULTI_MAX],elem_tag[SAUTI_AAC_LC_MULTI_MAX],elem_slot[SAUTI_AAC_LC_MULTI_MAX];
    SautiAacLcSynthesis ch[SAUTI_AAC_LC_MULTI_MAX];     /* historia IMDCT na kanal, w kolejnosci dekodowania */
    uint32_t pns_seed;                                  /* wspolny strumien PNS */
} SautiAacLcMulti;

/* channel_configuration -> liczba kanalow (0 = nieobslugiwana). */
unsigned sauti_aac_lc_multi_config_channels(unsigned config);
/* map[i] = indeks kanalu dekodowanego (kolejnosc strumienia), ktory trafia na wyjscie i; 0 gdy config nieobslugiwana. */
int sauti_aac_lc_multi_output_map(unsigned config,uint8_t map[SAUTI_AAC_LC_MULTI_MAX]);
int sauti_aac_lc_multi_init(SautiAacLcMulti *state,unsigned config,unsigned previous_shape);

/* Uklad z program_config_element (ADTS channel_configuration 0).  Miejsca kanalow stoja w kolejnosci elementow PCE (przod, bok, tyl, LFE), a element strumienia trafia na miejsce wg pary (typ, element_instance_tag), nie wg kolejnosci w strumieniu (kolejnosci moga sie roznic, np. FFmpeg 7.1); gdy PCE powtarza (typ, tag), n_elements = 0 i zostaje kolejnosc w strumieniu; wyjscie jest w kolejnosci bitow ukladu FFmpeg:
 * FL FR FC LFE BL BR FLC FRC BC SL SR.  Obslugiwane: <= 1 SCE przodu (FC) + pary przodu (FL FR, FLC FRC), jedna para boczna (SL SR), tyl: jedna para (BL BR) i/lub jeden SCE (BC),
 * <= 1 LFE, bez CCE.  Wiecej niz 8 kanalow albo inny uklad: -1. */
typedef struct {
    unsigned channels;
    uint8_t map[SAUTI_AAC_LC_MULTI_MAX];
    unsigned n_elements;                                /* elementy kanalowe PCE w kolejnosci listy: typ (0 SCE, 1 CPE, 3 LFE), tag, pierwsze miejsce kanalu */
    uint8_t elem_type[SAUTI_AAC_LC_MULTI_MAX],elem_tag[SAUTI_AAC_LC_MULTI_MAX],elem_slot[SAUTI_AAC_LC_MULTI_MAX];
} SautiAacLcLayout;
/* Szuka PCE w pierwszych elementach ramki ADTS (przed pierwszym elementem kanalowym).  1 = znaleziono i uklad obslugiwany, 0 = brak PCE albo ramka bledna, -1 = uklad nieobslugiwany. */
int sauti_aac_lc_multi_scan_pce(const uint8_t *frame,size_t length,SautiAacLcLayout *layout);
int sauti_aac_lc_multi_init_layout(SautiAacLcMulti *state,const SautiAacLcLayout *layout,unsigned previous_shape);

/* Atomowo: przy bledzie ani stan, ani PCM nie sa zmieniane.  pcm = 1024 ramek x channels, przeplot, kolejnosc wyjsciowa.
 * 1 = zdekodowano, 0 = zla/ucieta ramka, -1 = poprawna, ale nieobslugiwana (powod w `reason`). */
int sauti_aac_lc_adts_multi_decode_ex(const uint8_t *frame,size_t length,
                                      SautiAacLcMulti *state,float *pcm,
                                      SautiAacReason *reason);
#endif
