# AAC w MaktabaSauti: plan pełnego pokrycia (2026-11-23)

Wymaganie użytkownika: kodek AAC w bibliotece ma obsługiwać WSZYSTKIE wersje, typy, rodzaje i wariacje. Ten dokument jest rejestrem: co jest, czym zweryfikowane, co brakuje, jakie wzorce (oracle) i strumienie są do dyspozycji, co blokuje. Aktualizować w KAŻDEJ rundzie.

## Legenda
`OK` zrobione i zweryfikowane na prawdziwym strumieniu / wzorcu; `OK-syn` zrobione, zweryfikowane tylko na strumieniach syntetycznych / z enkodera referencyjnego; `BRAK` nie zrobione; `BLOK` nie da się zrobić bez czegoś, czego nie mamy.

## Obiekty audio i narzędzia (MPEG-4 Audio 14496-3 i USAC 23003-3)
| Pozycja | Stan | Wzorzec / dowód | Uwagi |
|---|---|---|---|
| AAC Main (AOT 1), predykcja | OK | prawdziwy plik Hulu (140 dB wobec FFmpeg) | |
| AAC-LC (AOT 2), 1024, mono / stereo / 5.1 / 7.1, TNS, PNS, M/S, intensity, CRC, PCE | OK | 95 plików Fraunhofer + 21 z ffmpeg: 110 – 140 dB wobec FFmpeg | do 8 kanałów sprawdzone; >8 przez PCE: BRAK testu |
| AAC-LC ramka 960 (frameLengthFlag) | OK-syn | libxaac, MP4 z ASC | brak prawdziwego pliku |
| AAC SSR (AOT 3) | BRAK, BLOK | FFmpeg, libxaac i FDK tego nie dekodują; jedyny wzorzec to faad2 (GPL) | patrz „Decyzje"; potrzebny strumień: `A-codecs/AAC/ssr/Dailymotion…mp4` (50 MB) |
| AAC LTP (AOT 4) | OK-syn | libxaac / FFmpeg na strumieniach generatora | brak prawdziwego pliku |
| SBR (AOT 5), HE-AAC v1, 1024 | OK | pakiet CT, 21 plików ffmpeg, Fraunhofer | |
| PS, HE-AAC v2 (AOT 29), 1024 | OK | pakiet CT: 78,4 dB wobec wzorca producenta (3201 z 692224 próbek ±1 LSB) | znak IPD/OPD: wzorzec producenta zgodny |
| HE-AAC (SBR) z ramką 960 (DAB+), mono i stereo | OK-syn (2026-11-23) | libxaac enc + dec: mono 68,4 dB, stereo 60,3 dB; FDK dec (QMF wysokiej jakości): mono 82,4 dB, stereo 81,8 / 83,2 dB | fixtury `he960_mono/stereo.m4a`; brak prawdziwego pliku DAB+ |
| HE-AAC v2 (PS) z ramką 960 | OK-syn (2026-11-23, SBR-132) | FDK dec: 75 – 85 dB (muzyka), 81 – 84 dB (fixtura) | fixtura `he960_ps.m4a`; brak prawdziwego pliku DAB+; stereo (CPE) z PS nie istnieje w normie |
| ER AAC LC / LTP (AOT 17 / 19) | OK-syn | | |
| AAC Scalable (AOT 6), ER AAC Scalable (AOT 20) | BRAK, BLOK | żaden dostępny dekoder; brak strumieni | |
| TwinVQ (AOT 7), ER TwinVQ (AOT 21) | BRAK, BLOK | FFmpeg (twinvq) jako wzorzec; poza „AAC" w ścisłym sensie | |
| ER BSAC (AOT 22) | BRAK, BLOK | żaden dostępny dekoder (faad2 bez BSAC) | tablice arytmetyczne tylko w normie |
| AAC-LD (AOT 23) 480 / 512 | OK-syn | FDK enc + dec | |
| AAC-ELD (AOT 39) + SBR niskoopóźnieniowe (CLDFB), JEDNOKROTNE i PODWÓJNE, 512 | OK (2026-11-23, SBR-131) | FDK dec: 77 – 83 dB na muzyce (fixtury syntetyczne: mediana 70 / 74 dB), libxaac dec 59 – 72 dB | fixtury `eldsbr_single/dual.m4a`, `test_eldsbr`; ramka 480 bez strumienia testowego |
| ELD + SBR niskoopóźnieniowe w LATM/LOAS | OK (2026-11-24) | identyczne PCM jak z MP4 (fixtury `eldsbr_*.loas`) | |
| **AAC-LD + zwykły SBR** | **BRAK**, brak enkodera do strumienia testowego (FDK i libxaac kodują LD bez SBR) | | |
| **AAC-ELD v2 (ELD + SBR + MPEG Surround 212 niskoopóźnieniowy, LD-SAC)** | **ZROBIONE** w trybie podwójnym (2026-11-24, SBR-133): 56 - 66 dB względem FDK; ramka 480 ZWERYFIKOWANA (2026-11-25: 66,6 dB mediana 66,5 / najgorszy 60,8, fixtura `eldsbr_v2_480.m4a`); otwarte: pojedyncza częstotliwość, STP / GES | FDK enc (`CHMODE=128` = `MODE_212`, `SBRRATIO=2`) + dec | ELD v2 NIE używa PS: `ELDEXT_LDSAC` w ASC, ładunek SAC za ładunkiem SBR; SBR pracuje na banku QMF MPS-LD (nie CLDFB) |
| ER Parametric Stereo, ER CELP, HVXC, HILN, TTSI, SLS, SLS-nonCore | BRAK, poza AAC | | patrz „Spoza AAC" |
| USAC (AOT 42, xHE-AAC): FD, LPD (ACELP, TCX, FAC), eSBR 2:1 / 8:3 / 4:1, PVC, inter-TES, HBE 2:1 i 4:1, MPS212 (idx 1 / 2 / 3, IPD, TSD, STP, GES), predykcja zespolona, AudioPreRoll | OK | Fraunhofer: 82 – 93 dB wobec libxaac i FDK; HBE 4:1 stereo wobec FDK 86,7 dB | |
| USAC: MPS212 z niezerową resztą w parze mieszanej FD+LPD | BRAK | generator + libxaac dec | rzadkie |
| USAC: LFE z eSBR / wielokanałowe USAC z eSBR | BRAK, BLOK (sprawdzone 2026-11-24) | koder libxaac nie koduje 6 kanałów ("Error in calculating loudness", pusty strumień); FDK czyta tylko konfiguracje kanałów 1 i 2; generator `sauti_usac_gen` odrzuca `--config` razem z `--sbr` | żaden wzorzec ani źródło strumieni; rzadkie |
| USAC: time-warped MDCT (`tw_mdct`) | BRAK, BLOK (2026-11-24: sprawdzone) | ŻADNEGO wzorca: FDK nie ma `tw_mdct`, libxaac dekoder ODRZUCA strumień z `tw_mdct = 1` (`ixheaacd_create.c`: `return -1`, chyba że włączone ec_flag, wtedy wyłącza tw), koder libxaac go nie emituje (pole `tw_mdct` tylko w konfiguracji) | tylko strumienie z generatora `sauti_usac_gen` bez wzorca; bez tekstu normy i wzorca nie do zweryfikowania |
| USAC / MPEG-D: uniDRC, normalizacja głośności (loudnessInfoSet, DRC) | **CZĘŚCIOWO ZROBIONE (2026-11-24, `docs/UNIDRC_PLAN.md`)**: parsery, wybór zestawu jak w wzorcu, krzywa wzmocnienia i zastosowanie (jednopasmowe, dziedzina czasu), normalizacja głośności; opcje silnika `aac.drc`, `aac.drc_effect`, `aac.loudness_target` (domyślnie wyłączone) | normalizacja: FDK (skala 1,0000, 82 - 90 dB na 7 fixturach); DRC dynamiczne: libxaac 73 dB na `usac_d1` (FDK 35 dB, otwarte) | brak: wiele pasm (banki filtrów), downmix, ogranicznik szczytów, EQ, parametryczne DRC |
| USAC rdzeń LPD wobec FDK | ~51 dB (pasmo 0 – 800 Hz; arytmetyka FDK) | | nie naprawiane |

## Kontenery i transport
ADTS, ADIF, LATM/LOAS (AudioMuxElement, StreamMuxConfig), MP4 / M4A / 3GP / fMP4 (także `moov` w atomie `free`), ER (`.m4a` z ASC) OK. Brak: MPEG-2 TS / DVB (PES) bezpośrednio, Matroska, FLV (FFmpeg-owe pliki testowe przechodzą przez remux), DAB+ superframe z RS (opcjonalnie), MP4 z `esds` bez ASC w nietypowych układach: sprawdzać na plikach.

## Decyzje
* SSR / Scalable / BSAC: bez tekstu normy i bez dekodera referencyjnego o dozwolonej licencji nie da się ich zrobić ani zweryfikować. Jedyny dostępny dekoder SSR to faad2 (GPL-2): jako WZORZEC WYJŚCIA (czarna skrzynka, jak FDK) nie przenosi licencji na naszą bibliotekę, ale algorytm trzeba zaimplementować z wiedzy o normie, nie z kodu GPL. Do rozstrzygnięcia z użytkownikiem; do tego czasu SSR / Scalable / BSAC oznaczone BLOK.
* Wzorzec dla niejednoznacznych miejsc: FDK (producent) przed libxaac (patrz SBR-128, Błąd 8; SBR-129: kanał 1 pary w libxaac).

## AAC-ELD / AAC-LD z SBR (notatki z lektury libxaac, 2026-11-23; ELD + SBR ZROBIONE, szczegóły SBR-131 w `AAC_FAMILY_ASM.md`; zostają PS dla ELD v2, AAC-LD + zwykły SBR, LATM/LOAS)
Stan: `sauti_aac_er_asc_parse` zna już `ldSbrPresentFlag`, `ldSbrSamplingRate`, `ldSbrCrcFlag`; silnik odrzuca strumień przy otwarciu (`no codec recognised`). Strumień testowy: FDK (`AACENC_SBR_MODE`, mój mały enkoder `enc.c`: `SBRMODE=1 ./enc raw 48000 2 39 48000 512 0 out.aus out.asc`, potem `tools/mux_raw_aus_mp4.py ... 512`), dekoder FDK (`sauti_fdk_raw_oracle`) i libxaac (`-aot:39`, `is_ld_sbr`) jako wzorce.
Różnice względem klasycznego SBR (z `ixheaacd_*`): (1) ładunek SBR NIE jest elementem FIL: to reszta bitów jednostki dostępu po elemencie rdzenia (typ rozszerzenia wynika z `ldSbrCrcFlag`: 13 albo 14, bez czterech bitów typu), liczba bitów niekoniecznie wielokrotność 8; (2) granice obwiedni są w szczelinach QMF (`timeStep = 1`, bez mnożenia ×2), 16 szczelin dla ramki 512 albo 15 dla 480; siatka z tablicami LD (`LD_TRAN`, `ld_env_table_512/480`), nagłówki `ld_sbr_header` w ASC; (3) `op_delay = 0`: brak 6 szczelin nakładki / dopływu na końcu ramki, obwiednie nie wychodzą poza ramkę; (4) kowariancja LPC i `auto_corr_length` liczone inaczej (bez gałęzi 36); (5) niskoopóźnieniowy bank QMF (CLDFB, prototyp 640 próbek), inna kompensacja opóźnienia niż w klasycznym SBR; (6) brak rezerwacji / skalowania nakładki w `env_calc`. Etapy: ASC + ładunek (koniec elementu rdzenia z `sauti_aac_er_decode`), siatka LD i tabele, CLDFB (tablice z libxaac: dozwolone, Apache 2.0, wpis per tablica), kanał SBR z `op_delay = 0`, PS dla ELD v2.

## Prawdziwe pliki (przegląd 2026-11-25; `scripts/test_real_files.py`, pliki poza repozytorium w `~/Sauti-tools/real-files/`)
* **FFmpeg `ffsamples/aac` (21 plików: LC, HE-AAC v1/v2, Main, 8 kanałów, faac / Nero, LATM, fMP4):** wszystkie zgodne z FFmpeg 128 – 143 dB, z trzema pozornymi odchyleniami, które NIE są błędami dekodera: (1) `suite_AAC_asimovis.aac` to plik tekstowy (e-mail), FFmpeg też go odrzuca; (2) `suite_AAC_Frula…` to obcięty plik MP4 (942 KB wobec ~10 MB zadeklarowanych): wydajemy dokładnie tyle samo próbek co FFmpeg, potem `E_TRUNCATED`; (3) `AAC_channel_model_*` (FATE FFmpeg): próbki identyczne (140 dB na każdym z 6 kanałów po wyrównaniu), ale FFmpeg GUBI pierwszą ramkę (nietypowa konfiguracja kanałów w pierwszej jednostce) i w `bad_concat` jeszcze jedną w miejscu sklejenia konfiguracji, my dekodujemy wszystkie (253 / 477 ramek); surowe SNR skryptu (−2,2 / 4,2 dB) wynika wyłącznie z przesunięcia.
* **Fraunhofer IIS (100 plików: xHE-AAC i AAC-LC 320 kbit/s):** 99 z 100 ≥ 60 dB wobec libxaac i FDK; wyjątek `Sintel_24kbps` (eSBR 8:3, LPD, MPS): 76,9 dB wobec libxaac, 47,4 dB wobec FDK (znana różnica SBR względem FDK, patrz wyżej, SBR-79 / SBR-115).
* Wniosek: nie ma na prawdziwych plikach żadnej luki w dekodowaniu AAC, której nie opisano; otwarte zostają tylko pozycje z tabeli powyżej (SSR / Scalable / BSAC bez wzorca, TW-MDCT, LFE + eSBR, ELD v2, pozostałe tryby uniDRC).

## ELD v2 w trybie jednokrotnym (single rate): stan 2026-11-25
Wcześniej uznawany za „brak strumienia testowego"; to nieprawda. Koder FDK (`tools/sauti_fdk_aac_encoder.c`, `SBRMODE=1 SBRRATIO=1 CHMODE=128`, AOT 39, granule 512) tworzy taki strumień, ale **tylko dla częstotliwości 24 kHz** (32 i 48 kHz: `init failed`, niezależnie od bitrate). ASC: rate index 6, `ldSbrSamplingRate = 0`, SSC z `timeSlots = 16`, czyli 32 próbki na slot: bank QMF MPS-LD o **32 pasmach** (`qmf_mpsldfb_320`), nie 64. FDK dekoduje strumień bez błędów (283 ramki, wyrocznia `build/sauti_fdk_raw_oracle`), nasz silnik odrzuca go jawnie (`maktabasauti_aac_engine.c`: `d->eldsbr != 2u` przy SAC → `UNSUPPORTED_FEATURE`). Do zrobienia: SAC na 32 pasmach (tablice odwzorowania pasm parametrów na 32 pasma QMF zamiast `kernels_N_to_64`, dekorelator dla 32 pasm), SBR single rate na banku MPS-LD 32; wzorzec jest (FDK), strumień da się wytworzyć. Nie robione: wariant występuje wyłącznie przy rdzeniu 24 kHz i nie ma znanych prawdziwych plików.
