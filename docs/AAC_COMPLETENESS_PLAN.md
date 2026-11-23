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
| HE-AAC (SBR) z ramką 960 (DAB+), mono i stereo | OK-syn (2026-11-23) | libxaac enc + dec: mono 68,4 dB, stereo 60,3 dB; FDK dec: mono 82,4 dB (stereo: FDK różni się od wszystkich, też przy 1024: patrz SBR-130) | fixtury `he960_mono/stereo.m4a`; brak prawdziwego pliku DAB+ |
| **HE-AAC v2 (PS) z ramką 960** | **BRAK** (odrzucane) | libxaac enc `-aot:29 -framesize:960`; FDK dec | PS ma 30 kolumn czasu zamiast 32 (`MAX_NUM_COLUMNS_960`), wymaga uogólnienia mostu PS |
| ER AAC LC / LTP (AOT 17 / 19) | OK-syn | | |
| AAC Scalable (AOT 6), ER AAC Scalable (AOT 20) | BRAK, BLOK | żaden dostępny dekoder; brak strumieni | |
| TwinVQ (AOT 7), ER TwinVQ (AOT 21) | BRAK, BLOK | FFmpeg (twinvq) jako wzorzec; poza „AAC" w ścisłym sensie | |
| ER BSAC (AOT 22) | BRAK, BLOK | żaden dostępny dekoder (faad2 bez BSAC) | tablice arytmetyczne tylko w normie |
| AAC-LD (AOT 23) 480 / 512 | OK-syn | FDK enc + dec | |
| **AAC-LD + SBR, AAC-ELD (AOT 39) + SBR (ELD-SBR, 480 / 512, niskoopóźnieniowy QMF)** | **BRAK** (`no codec recognised`) | FDK enc (`AACENC_SBR_MODE`) + dec, libxaac enc/dec (`is_ld_sbr`) | wysoki priorytet (VoIP, FaceTime) |
| AAC-ELD v2 (ELD + SBR + PS) | BRAK | FDK dec | |
| ER Parametric Stereo, ER CELP, HVXC, HILN, TTSI, SLS, SLS-nonCore | BRAK, poza AAC | | patrz „Spoza AAC" |
| USAC (AOT 42, xHE-AAC): FD, LPD (ACELP, TCX, FAC), eSBR 2:1 / 8:3 / 4:1, PVC, inter-TES, HBE 2:1 i 4:1, MPS212 (idx 1 / 2 / 3, IPD, TSD, STP, GES), predykcja zespolona, AudioPreRoll | OK | Fraunhofer: 82 – 93 dB wobec libxaac i FDK; HBE 4:1 stereo wobec FDK 86,7 dB | |
| USAC: MPS212 z niezerową resztą w parze mieszanej FD+LPD | BRAK | generator + libxaac dec | rzadkie |
| USAC: LFE z eSBR | BRAK | FDK dec | rzadkie |
| USAC: time-warped MDCT (`tw_mdct`) | BRAK | libxaac dec (jest `tw`); ŻADEN enkoder go nie emituje; FDK nie ma | strumienie z generatora `sauti_usac_gen` |
| USAC / MPEG-D: uniDRC, normalizacja głośności (loudnessInfoSet, DRC) | BRAK | FDK stosuje domyślnie; libxaac | opcja wyjściowa, nie domyślna |
| USAC rdzeń LPD wobec FDK | ~51 dB (pasmo 0 – 800 Hz; arytmetyka FDK) | | nie naprawiane |

## Kontenery i transport
ADTS, ADIF, LATM/LOAS (AudioMuxElement, StreamMuxConfig), MP4 / M4A / 3GP / fMP4 (także `moov` w atomie `free`), ER (`.m4a` z ASC) OK. Brak: MPEG-2 TS / DVB (PES) bezpośrednio, Matroska, FLV (FFmpeg-owe pliki testowe przechodzą przez remux), DAB+ superframe z RS (opcjonalnie), MP4 z `esds` bez ASC w nietypowych układach: sprawdzać na plikach.

## Decyzje
* SSR / Scalable / BSAC: bez tekstu normy i bez dekodera referencyjnego o dozwolonej licencji nie da się ich zrobić ani zweryfikować. Jedyny dostępny dekoder SSR to faad2 (GPL-2): jako WZORZEC WYJŚCIA (czarna skrzynka, jak FDK) nie przenosi licencji na naszą bibliotekę, ale algorytm trzeba zaimplementować z wiedzy o normie, nie z kodu GPL. Do rozstrzygnięcia z użytkownikiem; do tego czasu SSR / Scalable / BSAC oznaczone BLOK.
* Wzorzec dla niejednoznacznych miejsc: FDK (producent) przed libxaac (patrz SBR-128, Błąd 8; SBR-129: kanał 1 pary w libxaac).
