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
6. Dalsze dekodery wg SBR-99 (LD, ELD, 960, Main, LTP, USAC, …) i formaty pozostałe (DVSM ADPCM, itd.).

Kolejność 1–4 poprzedza dalsze poszerzanie dekoderów, bo wada architektury (stan globalny, brak API) kosztuje tym więcej, im więcej kodu powstanie.


## Stan wykonania (2026-11-19, etap 1 MaktabaSauti)

Rdzeń MaktabaSauti (`src/maktabasauti/`, API w `include/maktabasauti/`) jest zaimplementowany i przetestowany w wersji rozwojowej 0.1.0-dev (ABI niestabilne, niezamrożone; wersja 1.0 nie jest ogłaszana do czasu sprawdzenia z rzeczywistymi, strumieniowymi kodekami). Opis, decyzje (własność wejścia tylko po sukcesie, `seek` po przycięciu gapless, kontrakt alokatora bez `realloc`) i ograniczenia: `MAKTABASAUTI_API.md` §4, §5, §12. Dotychczasowa funkcjonalność Sauti jest zachowana: PCM przez nowe API jest identyczny bit w bit ze starym na wszystkich fixturach AAC, a stare API zostaje jako warstwa `sauti_legacy_decoder_*`. CI wymaga ThreadSanitizera dla testów wielowątkowych starego i nowego API. **Moduły strumieniowe (stan 2026-11-20):** AAC (ADTS, LOAS, ADIF, MP4 także z wejścia nieprzewijalnego), AVR, DVSM, WAV, AIFF, VOC i FLAC działają bez wczytywania pliku (wejście nieprzewijalne, `E_WOULD_BLOCK`, pamięć niezależna od długości); opakowania z pełnym buforem zostają dla Vorbis, MIDI, YM, MP3 i DTM (format modułowy). Szczegóły i różnice względem starych adapterów: `MAKTABASAUTI_API.md` §14.
