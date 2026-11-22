# Sauti jako biblioteka kodeków (plan, 2026-11-19)

## Cel (od właściciela projektu)

Dekoder **i koder** audio w postaci biblioteki, jak FFmpeg: do zainstalowania na **AyoOS, Linuksie (x86-64 i ARM), Windowsie i BSD**, z której programy (w tym odtwarzacze audio) korzystają przez publiczne API. Licencja: **freeware (darmowa) z zamkniętym kodem** (życzenie: „idealnie"). Dalszy cel jakościowy: maksymalna wierność brzmieniowa względem dekoderów referencyjnych we wszystkich wersjach AAC i pokrewnych formatach (mapa: `AAC_FAMILY_ASM.md`, SBR-99).

## Stan na dziś względem celu

| wymaganie biblioteki | stan | działanie |
|---|---|---|
| dekodowanie AAC-LC, HE-AAC v1/v2, kontenery ADTS/MP4/fMP4/LATM/ADIF | działa, zweryfikowane | rozszerzać wg SBR-99 |
| **koder** | **brak** (w repozytorium nie ma żadnego kodera AAC) | osobny duży projekt, patrz niżej |
| stabilne publiczne API | brak: 129 nagłówków wewnętrznych, niski `SautiDecoderOps` | jeden nagłówek publiczny, uchwyty nieprzezroczyste, kody błędów, wersjonowanie ABI |
| wejście strumieniowe | brak: `open(bajty, długość)` wymaga całego pliku w pamięci | callbacki odczytu/seek/rozmiaru |
| wielobieżność / wątki | **nie**: statyczne bufory (m.in. `sauti_decoder_aac_he.c`, `ps_qmf_low`, `ps_decorrelate`, `sbr_hf_predict`, moje `adif`/`latm`) | usunąć stan globalny, test z wieloma dekoderami i wątkami pod TSan |
| przenośność | 25 plików NASM tylko x86-64 SysV; Windows x64 ma inną konwencję wywołań; ARM bez asm | ścieżki C dla każdej funkcji asm + dyspozytor czasu działania; asm jako przyspieszenie per platforma |
| system budowania | tylko Makefile | CMake (lub Meson): statyczna i dzielona, instalacja nagłówków, `pkg-config`, CI Linux/Windows/BSD/ARM |
| licencja zamkniętego kodu | **zablokowana**: patrz niżej | izolacja i wymiana danych pochodzących z FFmpeg |

## Licencja: co blokuje zamknięty kod

FFmpeg (libavcodec) jest na LGPL-2.1+. LGPL pozwala **linkować** bibliotekę do programów zamkniętych, ale nie pozwala **włączyć jej kodu lub danych do własnego zamkniętego dzieła** bez udostępnienia źródeł tej części i możliwości jej wymiany. W repozytorium 50 plików wspomina o FFmpeg (głównie komentarze „sprawdzone względem"), ale realne dane pochodne to:

| dane | gdzie | pochodzenie |
|---|---|---|
| kodowe księgi Huffmana AAC (skalowanie, widmowe 1–11) | `sauti_aac_tables.c` | „cross-checked against FFmpeg libavcodec/aactab.c (LGPL-2.1-or-later)" |
| księgi Huffmana SBR i PS (IID/ICC/IPD/OPD) | `sauti_aac_sbr_huffman.c`, `sauti_aac_syntax.c` | cross-checked z `aacsbrdata.h`, `aacpsdata.c` |
| okno prototypu QMF (640 współczynników) | `sauti_aac_sbr_qmf_synth_window.c` | „SBR prototype table cross-checked against the public FFmpeg n6.1" |
| ROM szumu SBR (512 par) | `sauti_aac_sbr_noise_rom.c` | „standardized reference data, cross-checked" |
| przesunięcia pasm skalowania | `sauti_aac_bands.c` | tablice normatywne |
| przesunięcia pasm skalowania dla ramek 512 / 480 (AAC-LD / ELD) i `tns_max_bands` | `sauti_aac_bands.c` | tablice normatywne; liczby sprawdzone z `aactab.c` FFmpeg (jak wyżej), dopisane 2026-11-20 |
| przesunięcia pasm skalowania dla ramek 960 / 120 (AAC-LC z `frameLengthFlag = 1`) | `sauti_aac_bands.c` | tablice normatywne ISO/IEC 14496-3; liczby sprawdzone z `aactab.c` FFmpeg (jak wyżej), dopisane 2026-11-20; potwierdzone dekodowaniem względem FFmpeg (136–139 dB) i FDK AAC (86–92 dB) na wszystkich 13 indeksach częstotliwości (`tests/data/lc960_r*.m4a`); wymagają tego samego przeglądu licencyjnego co tablice 512 / 480 |
| tablice USAC (kodowanie arytmetyczne: 64 × 17 częstości skumulowanych MSB, 3 × 4 LSB, tablica skrótów kontekstu 742 + 742; przesunięcia pasm 768 / 96 linii, `tns_max_bands`, początki wypełniania szumem, tablica częstotliwości próbkowania) | `sauti_usac_tables.c` | tablice normatywne ISO/IEC 23003-3; **liczby przepisane z `aactab.c` FFmpeg** (LGPL-2.1-or-later, © 2024 Lynne i inni), plik GENEROWANY skryptem `tools/gen_usac_tables.py`; ten sam przegląd licencyjny / wymiana danymi z normy co okno ELD (dopisane 2026-11-21) |
| filtry MDST predykcji zespolonej USAC (bieżąca ramka 4 × 4 × 7, poprzednia 4 × 4) | `sauti_usac_mdst.c` | **WYPROWADZONE z definicji** (MDCT / MDST, okna sinus i KBD, okna start / stop / stop-start) skryptem `tools/gen_usac_mdst.py`; żaden kod ani tablica innego dekodera nie jest źródłem. Wynik jest identyczny co do 6 miejsc z opublikowanymi tablicami (FFmpeg `ff_aac_usac_mdst_filt_cur`, FDK `mdst_filt_coef_prev`: 128 liczb, różnica 0). Do sprawdzenia osobno: ograniczenia patentowe samego algorytmu (opis w publikacjach patentowych, nie jest zgodą na użycie) (dopisane 2026-11-21) |
| okno filterbanku AAC-ELD (1920 + 1800 współczynników) | `sauti_aac_eld_window.c` | tablica normatywna ISO/IEC 14496-3; **liczby przepisane z `aactab.c` FFmpeg** (`ff_aac_eld_window_512/_480`, LGPL), skrypt `tools/gen_eld_window.py`; wymaga tego samego przeglądu licencyjnego albo zastąpienia danymi z normy |
| część algorytmów SBR | `sauti_aac_sbr_gain_partition.c` i pokrewne | komentarz „Numerics follow FFmpeg libavcodec/aacsbr.c:sbr_gain_calc()" |
| adapter do libavcodec | `sauti_decoder_ffmpeg.c` | **nie wchodzi do wydawanej biblioteki** (opcjonalny, wyłącznie do testów i zapasowo w odtwarzaczu) |

To są **dane normatywne** (zdefiniowane w ISO/IEC 14496-3), ale kopiowanie ich z pliku FFmpeg przenosi na nas jego warunki. Samo sprawdzenie wyniku względem FFmpeg (jak w testach) niczego nie przenosi.

**Strategia (dwutorowa):**
1. **Izolacja natychmiast.** Wszystkie stałe pochodzące z FFmpeg przenieść do osobnego modułu `sauti-tables` (osobne pliki, osobny obiekt), udostępnionego na LGPL ze źródłami i z możliwością wymiany. Reszta biblioteki (cały kod własny) może być zamknięta i freeware. To jest standardowy, dozwolony układ. Adapter FFmpeg wyłączony z wydania.
2. **Wymiana docelowa.** Zastępować dane z FFmpeg wartościami z **samej normy ISO/IEC 14496-3** (wymaga zakupu lub dostępu do normy, czego ja nie mam) albo wyliczanymi (okna sinus i KBD, twiddle'e, tablice potęg są wyprowadzalne ze wzorów). Gdy wszystkie dane są niezależne, moduł `sauti-tables` znika z wydania. Przed tym należy też przepisać pozostałe algorytmy, które „follow FFmpeg" (SBR gain), z normy (część SBR przepisano już wg struktury normy w SBR-82).

To **nie jest porada prawna**; przed wydaniem komercyjnym lub szerokim: przegląd prawnika. Dwie dodatkowe kwestie do sprawdzenia przy dystrybucji:
- **Patenty AAC** (Via Licensing; AAC-LC w większości wygasły, część HE-AAC/xHE-AAC i narzędzi może być jeszcze objęta): dotyczy zarówno dekodera, jak i kodera, zależnie od jurysdykcji.
- Biblioteki użyte tylko jako **wzorce testowe** (FFmpeg, FAAD2 na GPL, libfdk-aac na własnej licencji z zakazem dystrybucji wersji niezgodnych) **nie mogą wchodzić do wydawanej biblioteki**; używamy ich wyłącznie w narzędziach testowych poza pakietem.

## Architektura biblioteki (propozycja)

- **Rdzeń w C99**, bez zależności od libc poza `memcpy`, `malloc`-owalnych alokatorów (hooki alokatora), `math.h`; bez `dlopen` i bez wątków w rdzeniu; wszystkie kodeki **wielobieżne** (stan wyłącznie w uchwycie).
- **Publiczne API** (jeden nagłówek `sauti.h` + `sauti_codec.h`): 
  - `sauti_open(io, opcje, &uchwyt)` z `SautiIO` (callbacki `read`, `seek`, `size`), plus wygodne `sauti_open_memory`;
  - `sauti_info`: częstotliwość, kanały i **układ kanałów** (maska jak WAVE), czas, kodek, tagi/gapless;
  - `sauti_read_f32` / `sauti_read_s16` (konwersja i dithering po stronie biblioteki), `sauti_seek`, `sauti_close`;
  - kody błędów (`SAUTI_E_*`), wersja i test zgodności ABI (`sauti_abi_version`).
  - **Koder**: `sauti_enc_open(kodek, parametry)`, `sauti_enc_encode`, `sauti_enc_flush`, muxery (ADTS, M4A, LATM) w tym samym stylu.
- **Warstwy**: `core` (transformaty, tablice), `codecs` (AAC, DVSM, itd.), `containers` (demuxery/muxery), `api`. Kodeki jako rejestr z funkcjami tworzącymi (bez statycznych list w odtwarzaczu).
- **Dyspozytor SIMD**: dla każdej funkcji gorącej wersja C (zawsze), wersje asm/intrinsics na x86-64 (SysV i Win64), ARM64 NEON; wybór w czasie działania lub budowania.

## Uwagi prawne po przeglądzie (doprecyzowanie)

- **Podobny algorytm nie jest automatycznie objęty licencją FFmpeg.** Liczy się, czy skopiowano chronioną implementację, czy napisano własną na podstawie normy. Komentarze „follow FFmpeg" w kodzie SBR trzeba więc przejrzeć pod kątem tego, czy kod jest własny (struktura wg normy, SBR-82) czy przepisany; nie zakładam z góry, że każdy jest pochodny.
- **Każdą tablicę trzeba ocenić osobno**: nie wszystkie liczby matematyczne podlegają ochronie prawnoautorskiej (wzory, wartości wynikające z obliczeń, fakty z normy); inaczej kod, inaczej układ i dobór danych w pliku FFmpeg.
- **Wydzielenie `sauti-tables` samo nie gwarantuje zgodności z LGPL.** Trzeba sprawdzić licencję każdego źródła, sposób łączenia (dzielona biblioteka albo obiekty do przelinkowania) i możliwość zastąpienia komponentu przez użytkownika.

Dlatego kolejność prac jest następująca (zamiast tej z pierwszej wersji planu, która stawiała koder wcześniej): **1.** usunąć współdzielony stan i przetestować wielowątkowo (bez zmian algorytmów), **2.** równolegle sporządzić wykaz pochodzenia każdej tablicy i fragmentu pochodnego od FFmpeg, **3.** zdefiniować stabilne strumieniowe API i granice modułów, **4.** zapewnić niezależne, odpowiednio licencjonowane tablice i dopracować budowanie wieloplatformowe, **5.** na końcu zdecydować o budowie kodera AAC (dekoder jest zaawansowany; najpierw solidna, legalnie rozpowszechnialna biblioteka).

## Kamienie milowe

**Stan (2026-11-19): kamień 1 wykonany dla dekoderów AAC.** Wszystkie modyfikowalne dane globalne w kodzie własnym usunięte: bufory robocze adaptera HE (`g2`, `grid`, `qmf38`, 68 KB) są teraz polami uchwytu dekodera, a bufory demuxera ADIF należą do jednego wywołania. Zostały wyłącznie tablice inicjalizowane **jednorazowo i atomowo**, potem tylko do odczytu (`standard_trie`, `plan128/1024` IMDCT). Kod osób trzecich (`stb_vorbis`: tablica CRC; `dr_flac`: flagi CPU) ma własny leniwy stan poza zakresem tej zmiany. Test: `scripts/test_aac_threads.sh` (8 wątków × 3 rundy × 7 rodzajów strumieni: LC, HE v1, HE v2, M4A, LOAS, ADIF, 5.1; wyjście musi być bit w bit równe dekodowaniu w jednym wątku, także po przewinięciu); wariant ze starymi statycznymi buforami daje 46 niezgodności (test działa), poprawiony 0. **ThreadSanitizer nie jest dostępny na tej maszynie** (brak pakietu `libtsan`), więc skrypt używa AddressSanitizera i porównania wyniku, a wyścigów bez uszkodzenia danych nie wyklucza; do uruchomienia pod TSan na maszynie z `libtsan` (CI).

1. **Wielobieżność**: usunięcie wszystkich statycznych buforów; test dwóch dekoderów w dwóch wątkach (TSan) i 1000 otwarć/zamknięć (ASan, brak wycieków).
2. **Publiczne API + wejście strumieniowe + testy API** (w tym zgodność wyników z obecnym rejestrem).
3. **Ścieżki C dla całego asm + budowanie na Windows/ARM/BSD** (*stan 2026-11-20: ścieżki C dla WSZYSTKICH jąder kodeków — AAC, AVR, DVSM, YM — są w `src/portable/`, bit w bit jak NASM (`make test-portable-equivalence`); `make PORTABLE=1 OBJDIR=build_portable test-aac-native` buduje i testuje bez NASM; zostają: CMake/pakiet, jądra wizualizacji (`sauti_core`, `fft`, …) i wyniki na prawdziwym ARM64 w CI*) (CMake, CI), pakiet instalacyjny (`.so/.dll/.a`, nagłówki, `pkg-config`, `.cmake`).
4. **Izolacja `sauti-tables`** i wyłączenie adaptera FFmpeg z wydania; raport pochodzenia danych.
5. **Koder AAC-LC** (najpierw CBR/VBR stereo z M/S i TNS bez PNS; potem SBR/HE-AAC; model psychoakustyczny własny), test: dekodery FFmpeg/FAAD2 odtwarzają nasze strumienie, a zmierzone SNR/PEAQ-podobne metryki są porównywalne z fdkaac przy tym samym bitrate.
6. Dalsze dekodery wg SBR-99. **Zrobione:** AAC-LD i AAC-ELD bez SBR (SBR-100), AAC-LC z ramkami 960 / 120 (SBR-101, 2026-11-20), AAC Main z predykcją (SBR-102, 2026-11-21), AAC LTP (SBR-103, 2026-11-21). Main i LTP domknięte w SBR-104 (wielokanałowość, CRC ADTS, ADIF, Main 960, ER AAC LC / LTP). USAC / xHE-AAC rozpoczęty w SBR-105 (2026-11-21: UsacConfig + rdzeń w dziedzinie częstotliwości, mono i stereo; reszta niżej). **Kolejność dalej (zatwierdzona przez użytkownika, cel: kompletny AAC):** USAC / xHE-AAC (eSBR, LPD, MPS212, predykcja zespolona, integracja z silnikiem) (FFmpeg 8.x w `~/Sauti-tools/ffmpeg-sbr54w` ma `aacdec_usac.c`: wzorzec), SSR / Scalable / BSAC; odłożone: LTP z 960 liniami (brak dekodera-wzorca), ELD z SBR (tablica CLDFB tylko w źródle FDK: decyzja licencyjna), ER-LC (AOT 17), wielokanałowe LD / ELD, HE-AAC z ramką 960, ELD 480 na prawdziwym strumieniu. Formaty pozostałe (DVSM ADPCM, itd.) poza AAC.

Kolejność 1–4 poprzedza dalsze poszerzanie dekoderów, bo wada architektury (stan globalny, brak API) kosztuje tym więcej, im więcej kodu powstanie.


## Stan wykonania (2026-11-19, etap 1 MaktabaSauti)

Rdzeń MaktabaSauti (`src/maktabasauti/`, API w `include/maktabasauti/`) jest zaimplementowany i przetestowany w wersji rozwojowej 0.1.0-dev (ABI niestabilne, niezamrożone; wersja 1.0 nie jest ogłaszana do czasu sprawdzenia z rzeczywistymi, strumieniowymi kodekami). Opis, decyzje (własność wejścia tylko po sukcesie, `seek` po przycięciu gapless, kontrakt alokatora bez `realloc`) i ograniczenia: `MAKTABASAUTI_API.md` §4, §5, §12. Dotychczasowa funkcjonalność Sauti jest zachowana: PCM przez nowe API jest identyczny bit w bit ze starym na wszystkich fixturach AAC, a stare API zostaje jako warstwa `sauti_legacy_decoder_*`. CI wymaga ThreadSanitizera dla testów wielowątkowych starego i nowego API. **Moduły strumieniowe (stan 2026-11-20):** AAC (ADTS, LOAS, ADIF, MP4 także z wejścia nieprzewijalnego), AVR, DVSM, WAV, AIFF, VOC i FLAC działają bez wczytywania pliku (wejście nieprzewijalne, `E_WOULD_BLOCK`, pamięć niezależna od długości); opakowania z pełnym buforem zostają dla Vorbis, MIDI, YM, MP3 i DTM (format modułowy). Szczegóły i różnice względem starych adapterów: `MAKTABASAUTI_API.md` §14.

## Otwarte decyzje i blokady rodziny AAC (stan 2026-11-21)

Co jest zrobione, co zostaje i **od czego zależy** — jedna tabela dla decyzji użytkownika. „Wzorzec” = dekoder, z którym można porównać wynik (FFmpeg 8.x, FDK 2.0.3 z 32-bitowym PCM, FAAD2 2.11.4 jako `libfaad.so.2`).

| Element | Czego brakuje | Wzorzec dziś | Odblokowanie |
|---|---|---|---|
| USAC LPD (ACELP / TCX / FAC), przejścia FD ↔ LPD | tablice ROM (książki LSF, AVQ, filtry ACELP, okna FAC), ok. 2 300 linii logiki (ocena z libxaac) | **libxaac (koder LPD 16 / 24 kHz + dekoder)** i FDK (konfiguracje 1–2) | plan: `docs/USAC_LPD_PLAN.md`; tablice: wyprowadzenie z normy albo, po zgodzie, z libxaac z wpisem w rejestrze |
| harmoniczny transposer (HBE) (ZROBIONE w SBR-115 dla 2:1 i 4:1, tablice z libxaac: rejestr poniżej; 8:3 niesprawdzone), PVC (ZROBIONE w SBR-114, tablice z libxaac: rejestr poniżej), inter-TES (ZROBIONE w SBR-113, bez tablicy); eSBR 8:3 i 4:1 ZROBIONE w SBR-109 / SBR-112: okna 24 i 16 pasm wyprowadzone ze wzoru, bez tablicy z FDK / libxaac | tablice PVC / HBE | tylko FDK | jw. |
| MPS212 (stereo parametryczne USAC) | tablice kwantyzacji i dekorelatorów | FDK (FFmpeg robi tylko downmix) | jw. |
| ELD + SBR (CLDFB) | tablica okna CLDFB | FDK | jw. (decyzja czeka od 2026-11-20) |
| LFE z eSBR (upsampling QMF), konfiguracje kanałów > 7, jawne pozycje kanałów, wielokanałowe eSBR w generatorze | logika, bez nowych tablic; brak wzorca dla wielokanałowych (FDK 2.0.3 czyta w USAC tylko konfiguracje 1–2, FFmpeg czyta LFE jak SCE) | brak | wzorzec wielokanałowy (np. libxaac, niżej) |
| AAC SSR (AOT 3), Scalable (AOT 6), BSAC (AOT 22) | tablice z normy (PQF SSR, tablice Huffmana BSAC), składnia warstw | **żaden**: FAAD2 2.11.4 odrzuca AOT 3 i 6 (`NeAACDecInit2` = −1), FFmpeg nie ma SSR / BSAC i odrzuca Scalable, FDK nie ma | norma ISO/IEC 14496-3 (tekst) albo dekoder referencyjny |
| HE-AAC 960, LTP 960 | — | brak | brak wzorca |

**Propozycja odblokowania: libxaac (ittiam-systems/libxaac, Apache 2.0).** To pełny dekoder **i koder** xHE-AAC (USAC: FD, LPD z ACELP / TCX, eSBR z HBE, PVC, MPS212, wielokanałowość, DRC) na licencji Apache 2.0 — zgodnej z rozdawaniem tablic w bibliotece (wymaga zachowania noty i pliku NOTICE; daje też jawną licencję patentową współautorów kodu, **bez** rozstrzygania patentów MPEG-D / xHE-AAC jako całości, to nadal kwestia dla prawnika). Dawałby: (1) **drugi wzorzec** dla wielokanałowego USAC, LFE, LPD i eSBR 8:3 / 4:1 (FDK 2.0.3 ich nie czyta), (2) **koder** do tworzenia prawdziwych strumieni testowych (dziś mamy tylko własny generator losowych strumieni), (3) źródło tablic z licencją, którą można wpisać do rejestru powyżej (jak tablice z FFmpeg). Stan (2026-11-21): za zgodą użytkownika repozytorium (commit 2fbadd5) sklonowane i **zbudowane lokalnie** w `~/Sauti-tools/libxaac/build` (poza repozytorium Sauti, bez instalacji systemowej, kod niezmieniony; `xaacenc`, `xaacdec`). Warunki użytkownika: używać jako niezależnego wzorca testowego (dekoder i koder do generowania strumieni, zgodność LPD / USAC, analiza brakujących funkcji); **nie kopiować automatycznie kodu ani tablic do Sauti**; każdy element przeznaczony do biblioteki wymaga udokumentowanego pochodzenia i zgodności licencyjnej; cel: własna, możliwie kompletna implementacja AAC bez obowiązkowej zależności od libxaac. Dotychczas: strumienie testowe `tests/data/usac_x1..x4.m4a` to WYJŚCIE koderu libxaac z syntetycznych sygnałów (nic z kodu ani tablic libxaac w Sauti); skrypt porównania `scripts/test_usac_vs_xaac.sh` używa `xaacdec` poza repozytorium. Plan LPD: `docs/USAC_LPD_PLAN.md`. Bez zgody na tablice z libxaac / FDK zostaje wyprowadzanie z samej normy (wymaga jej tekstu).

**Wykonane wcześniej, niezależne od powyższego:** USAC FD (SCE / CPE / LFE, do 8 kanałów, TNS, M/S, predykcja zespolona z własnymi, wyprowadzonymi filtrami MDST, szum, okna), eSBR 2:1 w postaci klasycznej, MP4 i LOAS / LATM, silnik z seekiem; AAC Main / LTP (także wielokanałowe), LC 960, ER (LD / ELD bez SBR, ER LC / LTP), HE-AAC v1 / v2.

**Zgoda użytkownika na tablice z libxaac dla USAC LPD (2026-11-21).** Użytkownik zezwolił na „to, czego potrzeba w dążeniu do osiągnięcia celu" (własna, możliwie kompletna implementacja AAC; libxaac nadal bez obowiązkowej zależności). Zasady wykonania, które zachowuję: (1) przenoszone są wyłącznie DANE normatywne (tablice, współczynniki) potrzebne do LPD, nie fragmenty logiki: logikę piszę sam; (2) każda przeniesiona tablica dostaje wpis w rejestrze poniżej: nazwa w Sauti, nazwa i plik w libxaac (commit 2fbadd5), licencja Apache 2.0, sposób weryfikacji (zgodność PCM ze strumieniami libxaac); (3) plik NOTICE / nota licencyjna Apache 2.0 trafia do repozytorium razem z pierwszą przeniesioną tablicą; (4) tablice, które da się wyprowadzić ze wzoru (okna, interpolacje), są liczone, nie przepisywane; (5) przeniesienie tablic robi skrypt generujący plik źródłowy z pliku libxaac (powtarzalne, bez ręcznego przepisywania), a skrypt i jego wejście są wpisane w rejestr.

**Rejestr tablic LPD z libxaac** (generuje je `tools/gen_usac_lpd_tables.py` z `~/Sauti-tools/libxaac`, commit 2fbadd57; licencja Apache 2.0, Copyright (C) 2018 The Android Open Source Project, Ittiam Systems; `third_party/libxaac/LICENSE` i `NOTICE`; wynik: `src/decoders/modern/aac/sauti_usac_lpd_tables.c`, `include/sauti_usac_lpd_tables.h`). Weryfikacja każdej: zgodność LSF (0,000000 Hz), syntezy (98 – 130 dB, ACELP bit w bit) i PCM (71 – 78 dB) ze strumieniami libxaac (docs SBR-110).

| Tablica w Sauti | Nazwa w libxaac (plik) | Znaczenie |
|---|---|---|
| `sauti_usac_lpd_factorial_7` | `ixheaacd_factorial_7` (avq_rom) | silnie 7! … 0! |
| `sauti_usac_lpd_abs_leaders` | `ixheaacd_absolute_leader_tab_da` (avq_rom) | absolutne liderzy kratownicy RE8 (Q2 – Q4) |
| `sauti_usac_lpd_iso_code_data / _num / _index` | `ixheaacd_iso_code_data_table / _num_table / _index_table` | wzory znaków i liczności podzbiorów liderów |
| `sauti_usac_lpd_signed_leader_is` | `ixheaacd_signed_leader_is` | offsety liczności liderów ze znakiem |
| `sauti_usac_lpd_pos_abs_leaders_a3 / _a4`, `sauti_usac_lpd_card_offset_i3 / _i4` | `ixheaacd_pos_abs_leaders_a3 / a4`, `ixheaacd_cardinality_offset_table_i3 / _tab_i4` | indeks lidera i offsety liczności dla Q2 / Q3 i Q4 |
| `sauti_usac_lpd_lsf_weight` | `ixheaacd_weight_table_avq` (avq_rom) | wagi rezydualnego AVQ dla każdego wektora pierwszego stopnia |
| `sauti_usac_lpd_lsf_codebook` | `ixheaacd_dico_lsf_abs_8b_flt` (rom) | książka LSF pierwszego stopnia (256 × 16, Hz) |
| `sauti_usac_lpd_lsf_init` | `lsf_init` (avq_rom) | LSF stanu początkowego |
| `sauti_usac_lpd_gain_table` | `ixheaacd_int_leave_gain_table` (rom) | kwantyzator wzmocnień ACELP (128 × 2) |
| `sauti_usac_lpd_bpf_fir` | `ixheaacd_fir_lp_filt` (avq_rom) | filtr dolnoprzepustowy filtra basowego (13 współczynników) |
| `sauti_usac_lpd_interpol_filt` | `ixheaacd_interpol_filt` (avq_rom) | filtr interpolacji książki adaptacyjnej (65) |

**Rejestr tablic PVC z libxaac** (generuje je `tools/gen_usac_pvc_tables.py` z `~/Sauti-tools/libxaac`, commit 2fbadd57; ta sama licencja i ten sam NOTICE; wynik: `src/decoders/modern/aac/sauti_usac_pvc_tables.c`, `include/sauti_usac_pvc_tables.h`; zgoda właściciela z 2026-11-21 na tablice z libxaac: tylko dane, skrypt generujący, wpis per tablica). Weryfikacja: wszystkie 1 918 liczb całkowitych (72 + 1 024 + 54 + 768) tablic współczynników sprawdzone niezależnie od generatora (inny parser szesnastkowych literałów, `tests/test_usac_pvc.c` ma też wartości kontrolne), predyktor zgodny z libxaac do 1e-3 (docs SBR-114).

| Tablica w Sauti | Nazwa w libxaac (plik) | Znaczenie |
|---|---|---|
| `sauti_usac_pvc_smooth_ns4 / _ns16 / _ns3 / _ns12` | `ixheaacd_pvc_smoothing_wind_tab_ns4 / ns16 / ns3 / ns12` (pvc_rom) | okna wygładzania energii niskiego pasma (tryb 1: 4 / 16, tryb 2: 3 / 12 szczelin) |
| `sauti_usac_pvc_id_boundary_m1 / _m2` | `ixheaacd_pred_coeff_pvc_id_boundaries_1 / _2` | granice trzech grup współczynników po `pvc_id` (17 / 68, 16 / 52) |
| `sauti_usac_pvc_qfac_m1 / _m2` | `ixheaacd_q_factor_table_mode_1 / _2` | kwanta współczynników predykcji (trzy grupy niskie i przesunięcie) |
| `sauti_usac_pvc_coef1_m1 / _m2` | `ixheaacd_pred_coeff_table_1_mode_1 / _2` | współczynniki predykcji [grupa id][grupa niska][grupa wysoka], 3 × 3 × 8 / 6 |
| `sauti_usac_pvc_coef2_m1 / _m2` | `ixheaacd_pred_coeff_table_2_mode_1 / _2` | przesunięcia po `pvc_id` [128][8 / 6] |

**Rejestr tablic HBE z libxaac** (generuje je `tools/gen_usac_hbe_tables.py` z `~/Sauti-tools/libxaac`, commit 2fbadd57, plik `common/ixheaac_esbr_rom.c`; ta sama licencja Apache 2.0 i ten sam NOTICE; wynik: `src/decoders/modern/aac/sauti_usac_hbe_tables.c`, `include/sauti_usac_hbe_tables.h`; ta sama zgoda właściciela: tylko dane, skrypt, wpis per tablica; logika transposera napisana dla Sauti po lekturze `ixheaacd_hbe_trans.c`, `ixheaacd_esbr_polyphase.c`, `ixheaacd_sbr_dec.c`, `ixheaacd_sbrdec_lpfuncs.c`, `ixheaacd_esbr_envcal.c`, bez kopiowania kodu: inna struktura danych, `double`, DFT zamiast ręcznie rozpisanych FFT).

| Tablica w Sauti | Nazwa w libxaac | Znaczenie |
|---|---|---|
| `sauti_usac_hbe_window` | `ixheaac_sub_samp_qmf_window_coeff` (2000) | okna prototypowe banków transposera, 10 · L współczynników dla L = 4 .. 40 |
| `sauti_usac_hbe_start_band_to_kl` | `ixheaac_start_subband2kL_tbl` (33) | pierwsze pasmo syntezy transposera dla pasma startowego SBR |
| `sauti_usac_hbe_cos_trans_qmf` | `ixheaac_cos_table_trans_qmf` (7 × 64) | pary (cos, sin) zamiany próbki QMF na wejście rzeczywiste syntezy |
| `sauti_usac_hbe_pv_cos / _sin` | `ixheaac_phase_vocoder_cos_table / _sin_table` (64) | obrót fazy wyjścia transposera po paśmie |
| `sauti_usac_hbe_interp_coeff`, `sauti_usac_hbe_sel_case` | `ixheaac_hbe_post_anal_proc_interp_coeff` (4 × 2), `ixheaac_sel_case` (5 × 8) | interpolacja sąsiednich kolumn / pasm w iloczynach rzędu 3 i krzyżowych |
| `sauti_usac_hbe_xprod_cos_2 / _3 / _4 / _4_1` | `ixheaac_hbe_x_prod_cos_table_trans_2 / _3 / _4 / _4_1` (po 512) | (cos, sin) korekty fazy iloczynów krzyżowych po `sbrPitchInBins` (+ 128 dla 4:1) |
| `sauti_usac_hbe_synth_cos_4 / _8 / _12 / _16 / _20` | `ixheaac_synth_cos_table_kl_4 .. _20` (16 .. 800) | dopasowanie faz syntezy (macierz kosinusów dla 20 pasm) |
| `sauti_usac_hbe_analy_8 / _16 / _24 / _32 / _40` | `ixheaac_analy_cos_sin_table_kl_8 .. _40` (32 .. 3200) | wstępny obrót analizy zespolonej (macierz dla 40 pasm) |

Weryfikacja: transposer zgodny z libxaac do 2,4e-7 (szum float) w strumieniach libxaac i do 1e-6 w strumieniach generatora z pitch 0 / 1..127 (docs SBR-115); `tests/test_usac_hbe.c` ma wartości kontrolne (długości, jednostkowość obrotów).

Liczone, nie przepisywane: okna sinusoidalne FAC / TCX, `0,92^i` (ważenie LPC), `10^(i/128)`, twiddle DCT-IV (DCT-IV liczone wprost).

