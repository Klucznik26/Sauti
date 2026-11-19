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
    unsigned config;                                    /* channel_configuration ADTS */
    SautiAacLcSynthesis ch[SAUTI_AAC_LC_MULTI_MAX];     /* historia IMDCT na kanal, w kolejnosci dekodowania */
    uint32_t pns_seed;                                  /* wspolny strumien PNS */
} SautiAacLcMulti;

/* channel_configuration -> liczba kanalow (0 = nieobslugiwana). */
unsigned sauti_aac_lc_multi_config_channels(unsigned config);
/* map[i] = indeks kanalu dekodowanego (kolejnosc strumienia), ktory trafia na wyjscie i; 0 gdy config nieobslugiwana. */
int sauti_aac_lc_multi_output_map(unsigned config,uint8_t map[SAUTI_AAC_LC_MULTI_MAX]);
int sauti_aac_lc_multi_init(SautiAacLcMulti *state,unsigned config,unsigned previous_shape);

/* Atomowo: przy bledzie ani stan, ani PCM nie sa zmieniane.  pcm = 1024 ramek x channels, przeplot, kolejnosc wyjsciowa.
 * 1 = zdekodowano, 0 = zla/ucieta ramka, -1 = poprawna, ale nieobslugiwana (powod w `reason`). */
int sauti_aac_lc_adts_multi_decode_ex(const uint8_t *frame,size_t length,
                                      SautiAacLcMulti *state,float *pcm,
                                      SautiAacReason *reason);
#endif
