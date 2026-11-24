# MaktabaSauti: projekt publicznego API (2026-11-19)

Nazwa: **MaktabaSauti** (swahili: „biblioteka dźwięku"). Status: **wersja rozwojowa 0.1.0-dev; ABI niestabilne i niezamrożone** (`MAKTABASAUTI_ABI_VERSION 0`; po zamrożeniu stanie się 1 i będzie numerem SONAME). Wersja 1.0 i stabilne ABI nie są ogłaszane, dopóki rdzeń nie zostanie przetestowany z rzeczywistymi kodekami (decyzja właściciela projektu). Nagłówki: `include/maktabasauti/maktabasauti.h` (API aplikacji) i `maktabasauti_codec.h` (interfejs modułu kodeka). Plan całej biblioteki, licencje i pochodzenie danych: `BIBLIOTEKA_PLAN.md`, `POCHODZENIE_DANYCH.md`.

## 1. Nazwy i układ bibliotek

| element | nazwa |
|---|---|
| rodzina | MaktabaSauti |
| wspólne API | `maktabasauti.h` (+ `maktabasauti_codec.h` dla autorów modułów) |
| moduł AAC (AAC, HE-AAC v1/v2, dalsze profile) | `maktabasauti_aac` → `libmaktabasauti_aac.so.1` / `maktabasauti_aac.dll` |
| moduł Atari DVS (DVSM) | `maktabasauti_dvs` → `libmaktabasauti_dvs.so.1` |
| moduł PCM (WAV, AIFF) | `maktabasauti_pcm` → `libmaktabasauti_pcm.so.1` |
| moduł VOC | `maktabasauti_voc` → `libmaktabasauti_voc.so.1` |
| moduł FLAC | `maktabasauti_flac` → `libmaktabasauti_flac.so.1` |
| opcjonalna wspólna biblioteka | `libmaktabasauti.so.1`: rdzeń (rejestr, wejścia, konwersje PCM, API) z wbudowanymi modułami |
| funkcje, typy, makra | `maktabasauti_*`, `maktabasauti_*_t`, `MAKTABASAUTI_*` |
| pakiety | `maktabasauti.pc`, `maktabasauti_aac.pc`, … oraz `maktabasauti-config.cmake` |

Numer w nazwie `.so.1` to **wersja ABI** (SONAME), nie wersja wydania. Moduł można zbudować **osobno** (wtedy nie linkuje się z rdzeniem, patrz §6), **statycznie** do wspólnej biblioteki albo napisać jako zewnętrzny.

## 2. Zasady ABI

1. Każda struktura publiczna zaczyna się od `uint32_t struct_size` (ustawia wywołujący: `sizeof` ze swojego nagłówka). Biblioteka czyta ze struktury wejściowej tylko `struct_size` bajtów, do wyjściowej zapisuje tylko tyle, ile się zmieści. Nowe pola tylko na końcu → stare programy działają z nowszą biblioteką.
2. Wartości wyliczeń są stałe (zmiana lub ponowne użycie = niezgodna zmiana ABI). `tests/test_maktabasauti_headers.c` przypina je statycznie razem z układem struktur wyjściowych.
3. Tylko typy o stałej szerokości; struktury wyjściowe mają rozmiar niezależny od wskaźników (tablice znaków o stałej długości zamiast `char *`).
4. Wersje: `maktabasauti_abi_version()` kontra `MAKTABASAUTI_ABI_VERSION` z nagłówka; `maktabasauti_decoder_open` odrzuca niezgodność kodem `E_ABI_MISMATCH`. `maktabasauti_version()` / `_version_string()` to wersja wydania.
5. C99, zgodne z C++ (test kompilacji `-std=c99 -pedantic` i `-std=c++17`). Poza `<stddef.h>` i `<stdint.h>` **zero zależności** od systemu operacyjnego, SDL, PipeWire, FFmpeg. Eksport symboli: `MAKTABASAUTI_API` (`dllexport`/`dllimport`/`visibility`), konwencja `MAKTABASAUTI_CALL` (`__cdecl` na Windows).

## 3. Model błędów

Jedna liczba całkowita: dodatnie = sukces z informacją (`MAKTABASAUTI_EOF`), `0` = OK, ujemne = błąd. Każdy kod ma stałą wartość i jednoznaczne znaczenie (`INVALID_ARGUMENT`, `NO_MEMORY`, `IO`, `UNSUPPORTED_FORMAT` (nikt nie rozpoznał danych) i `UNSUPPORTED_FEATURE` (rozpoznane, wariant nieobsługiwany) są rozdzielone celowo; `CORRUPT_DATA` i `TRUNCATED`; `NOT_SEEKABLE`, `OUT_OF_RANGE`, `BAD_STATE`, `ABI_MISMATCH`, `BUFFER_TOO_SMALL`, `WOULD_BLOCK`, `CANCELLED`, `NOT_IMPLEMENTED`, `INTERNAL`, `FORMAT_CHANGED`). `maktabasauti_result_name()` i `_message()` dają tekst. Szczegół błędu konkretnego uchwytu: `maktabasauti_decoder_last_error()`. **Błąd jest „lepki”**: po błędzie `read` zwraca ten sam błąd, dopóki nie nastąpi `seek`, `reset` lub `flush`; ramki wydane poprawnie przed błędem są zwracane w `*frames_read`.

## 4. Pamięć i własność

**Alokator (kontrakt, decyzja właściciela projektu).** Wszystkie alokacje przechodzą przez alokator (`NULL` = libc), także w modułach (przez `host->alloc/realloc/free`).
- `alloc(size)`: `size > 0` (biblioteka nigdy nie prosi o 0 bajtów), wynik wyrównany co najmniej do 16 bajtów albo `NULL`.
- `free(ptr)`: `ptr` pochodzi z `alloc`/`realloc` tego alokatora; biblioteka **nigdy nie woła `free(NULL)`**; `free` **nie dostaje rozmiaru**.
- `realloc` jest **opcjonalny**. Gdy go nie ma, biblioteka **nie kopiuje starego bloku „na ślepo”**: każdy blok ma ukryty 16-bajtowy nagłówek z zamówionym rozmiarem, więc zastępcze `realloc` (alloc nowego, kopia dokładnie `min(stary, nowy)`, free starego) zna rozmiar, a alokator użytkownika nie musi znać żadnych rozmiarów. Cena: 16 B narzutu na blok i wymóg wyrównania 16 B. Host dostarcza modułom `realloc` zawsze.
- Alokator jest kopiowany przy tworzeniu uchwytu (wskaźnik `user` musi żyć do jego zamknięcia); musi być bezpieczny wątkowo, jeśli używają go uchwyty w różnych wątkach. Pola poza `struct_size` krótszej struktury wywołującego są traktowane jako `NULL` (wymagane `alloc` i `free`).
- Test: `tests/test_maktabasauti_core.c` używa alokatora **bez `realloc`**, który zatruwa świeże bloki, zlicza `alloc`/`free` i wykrywa `free(NULL)`; po każdym cyklu liczba żywych bloków musi wrócić do zera (rejestr, wejście z pamięci, pełny cykl dekodera, nieudane otwarcie, wczytanie całego wejścia przez most).

**Własność wejścia (decyzja właściciela projektu, bez wyjątków).** Wejście przekazane do `maktabasauti_decoder_open()` przechodzi na własność biblioteki **wyłącznie po udanym otwarciu** (`MAKTABASAUTI_OK`): wtedy `maktabasauti_decoder_close()` wywoła `io.close` dokładnie raz. **Przy każdym błędzie** (`E_INVALID_ARGUMENT`, `E_ABI_MISMATCH`, `E_NO_MEMORY`, `E_UNSUPPORTED_FORMAT`, `E_NOT_SEEKABLE`, `E_IO`, …) wejście **zostaje własnością wywołującego**: biblioteka nie woła `close` ani nie zachowuje wskaźnika, zwalnia tylko to, co sama zajęła, a `*out == NULL`. Pozycja wejścia po błędzie jest nieokreślona (biblioteka mogła czytać). Każda ścieżka błędu w `mks_decoder.c` idzie przez `release_partial()`, która nie zna `io.close`; test sprawdza to dla każdego rodzaju błędu (licznik wywołań `close` musi pozostać 0).

- Biblioteka nie oddaje wskaźników do wewnętrznych buforów poza łańcuchami opisanymi w nagłówku. PCM zawsze trafia do bufora wywołującego.
- `maktabasauti_decoder_close(NULL)` i `maktabasauti_registry_destroy(NULL)` są bezpieczne.

## 5. Wejście strumieniowe i cykl życia dekodera

**Wejście** (`maktabasauti_io_t`): `read` (jak `read(2)`, w tym `E_WOULD_BLOCK` dla źródeł na żywo), `seek` (opcjonalny; `NULL` = nieprzewijalne), `size` (opcjonalny), `close`. Cały plik nigdy nie musi być w pamięci. Wygodne wejścia: `maktabasauti_io_from_memory`, `maktabasauti_io_from_file` (ścieżka UTF-8, Windows → UTF-16). Wejście nieprzewijalne dopuszcza tylko kodeki oznaczone `STREAMING`; kodek `NEEDS_SEEKABLE_INPUT` (np. MP4 z `moov` na końcu) daje `E_NOT_SEEKABLE`.

**Cykl życia:**

```
open ──► READY ──read──► READY ──► (koniec) EOF ──read──► EOF
            │  ▲  seek / reset / flush: z każdego stanu wraca do READY (czyści błąd)
            ▼  │
          ERROR (lepki do seek/reset/flush)
close: z każdego stanu (zamyka też wejście)
```

- `open`: wykrycie kodeka po prefiksie (do 4096 B, bez czytania całości), niezależny stan, `get_info`.
- `read(dst, ramki, format, &odczytano)`: konwersja formatu po stronie biblioteki (`U8/S16/S24/S32/F32/F64`, dither opcjonalny).
- `seek(ramka, EXACT|APPROX, &faktyczna)`, `tell`; pozycje w **ramkach PCM po przycięciu gapless** (ramka 0 = pierwsza ramka, którą zwróci `read`; decyzja właściciela projektu). Dokładne zachowanie:
  - **EXACT:** po `OK` następny `read` zaczyna dokładnie od `frame`, `*actual == frame`.
  - **APPROX:** po `OK` następny `read` zaczyna od `*actual <= frame` (nigdy dalej niż zadano; różnica co najwyżej granulacja punktów dostępu kodeka; kodek bez punktów dostępu może zwrócić `*actual == frame`); `tell()` zwraca `*actual`.
  - **Poza końcem, długość znana:** `frame == total_frames` jest dozwolone (`OK`, następny `read` daje EOF); `frame > total_frames` daje `E_OUT_OF_RANGE` i **nie zmienia stanu** (pozycji, błędu, EOF).
  - **Poza końcem, długość nieznana:** `OK` na ostatniej istniejącej pozycji (`*actual` = koniec), następny `read` daje EOF.
  - `E_INVALID_ARGUMENT` (złe flagi) i `E_NOT_SEEKABLE` nie zmieniają stanu; inny błąd w trakcie seek (np. `E_IO`, `E_CORRUPT_DATA`) zostawia dekoder w stanie błędu do następnego udanego `seek`/`reset`; udany seek czyści błąd i EOF.
- `reset`: powrót na początek z pełnym wyczyszczeniem stanu. `flush`: porzucenie historii dekodera **bez przewijania wejścia** (wznowienie po nieciągłości w strumieniu na żywo).
- `FORMAT_CHANGED`: parametry strumienia zmieniły się w środku (np. łańcuchowy Ogg); `get_info` zwraca nowe.

## 6. Moduły kodeków i rejestr

Moduł udostępnia `maktabasauti_codec_t` (probe, open, read, seek, reset, flush, tagi, last_error, close) i **nie linkuje się z rdzeniem**: dane, pamięć, opcje i log dostaje przez tablicę usług `maktabasauti_host_t` przekazaną przy otwarciu. Rdzeń trzyma **rejestr** kodeków: domyślny (niezmienny, linkowany z wersją biblioteki, bezpieczny bez synchronizacji) albo własny (`maktabasauti_registry_create/add`). Wybór kodeka: najwyższa pewność z `probe`; `params.codec` wymusza nazwę. Moduł wymaga: zero modyfikowalnego stanu globalnego (poza tablicami tylko do odczytu inicjowanymi atomowo), pamięć tylko przez hosta, bez własnych wątków, dane tylko przez reader. Dynamiczne ładowanie (`dlopen`/`LoadLibrary`) **nie jest w rdzeniu**: to opcjonalna warstwa platformowa przez wspólny symbol `maktabasauti_module_entry`. Rezerwy w strukturze kodeka (`reserved_encoder`, `reserved`) pozwalają dodać koder bez zmiany ABI.

## 7. PCM: częstotliwość, kanały, rozmieszczenie, format

`maktabasauti_stream_info_t`: `sample_rate`, `channels`, `channel_mask` (bity jak `WAVEFORMATEXTENSIBLE`; kanały w buforze w kolejności rosnących bitów; `0` = rozmieszczenie nieokreślone), `native_format` i `bits_per_sample` (odczyt w formacie natywnym nie traci dokładności), `total_frames` (lub `MAKTABASAUTI_UNKNOWN_LENGTH`), `priming_frames`/`end_padding_frames` (gapless, już odcięte), `bitrate`, flagi (`SEEKABLE`, `LIVE`, `LOSSLESS`, `VBR`, `GAPLESS_TRIMMED`), nazwy kodeka, profilu i kontenera. Układ kanałów AAC (7.1 z PCE itd.) dostaje maskę zgodną z tym, co dekoder już oddaje (kolejność FFmpeg/WAVE, SBR-99).

## 8. Sprawdzenie spójności z kodekami obecnymi i przyszłymi

| kodek (w repozytorium lub planowany) | strumieniowo? | przewijanie | długość | uwagi dla API |
|---|---|---|---|---|
| WAV, AIFF, VOC, AVR | tak (sekwencyjnie) | proste (offset) | znana | `native_format` U8/S16/S24/S32/F32; AVR/VOC bloki: `FORMAT_CHANGED` gdy zmienia się częstotliwość |
| **DVS/DVSM i AVR** (PCM, Pack-2, Pack-4; ADPCM, PACK override planowane) | tak (moduł strumieniowy od 2026-11-20) | PCM/AVR z arytmetyki; delta (Pack-2/4) przez punkty kontrolne akumulatorów | znana przy znanym rozmiarze (AVR: z nagłówka) | pamięć niezależna od długości (bufor 4 KiB + do 1024 punktów kontrolnych); DTM zostaje adapterem z całym buforem (format modułowy) |
| **FLAC** | tak | seektable / bisekcja | znana (STREAMINFO) | 24 bity w `S32`/`F32` bez straty; tagi Vorbis comment |
| MP3 | tak (synchronizacja ramek) | tabela z VBR lub szacunek | szacowana, jeśli brak nagłówka VBR | `E_TRUNCATED`/`CORRUPT_DATA` przy urwanej ramce, gapless z LAME |
| Ogg Vorbis / Opus | tak | bisekcja stron (wymaga przewijalnego wejścia) | z ostatniej strony, inaczej nieznana | łańcuchowe pliki → `FORMAT_CHANGED` |
| WavPack | tak | tak | znana | — |
| **AAC** (ADTS/LATM/LOAS/ADIF/MP4/fMP4) | ADTS, LOAS, fMP4 z `moov` na początku, ADIF: tak; MP4/fMP4 z `moov` przed danymi (faststart, init-segment): tak, jednoprzebiegowo; MP4 z `moov` na końcu: `E_NOT_SEEKABLE` | punkty kontrolne co 256 ramek HE; dokładne z `elst`/`iTunSMPB` | znana z kontenera, inaczej nieznana | profile i warianty w `codec_profile`; gapless z kontenera |
| przyszłe AAC (USAC, SSR, BSAC; LD, ELD, LC 960, Main i LTP już są) | j.w. | j.w. | j.w. | zmiana długości ramki nie wpływa na API |
| YM, SNDH (syntezowane) | tak (generowane) | przez odtworzenie od początku | nieznana lub z metadanych | opcja `sample_rate` (§ w nagłówku); `loop`; `UNKNOWN_LENGTH` |
| MIDI (syntezator) | tak | przez odtworzenie | szacowana | opcje: `sample_rate`, `midi.soundfont` (moduł) |
| kodery (AAC itd.) | — | — | — | zarezerwowane `maktabasauti_encoder_*` i miejsca w strukturze kodeka |

**Luki wykryte przy tym przeglądzie i wprowadzone do API przed zamrożeniem:** (1) zmiana parametrów w środku strumienia → `E_FORMAT_CHANGED` i odświeżone `get_info`; (2) źródła syntezowane bez własnej częstotliwości → wspólna opcja `sample_rate`; (3) źródła bez znanej długości lub z pętlą → `UNKNOWN_LENGTH` i opcja `loop`; (4) wznowienie po nieciągłości bez przewijania wejścia → `flush`; (5) wejście bez danych chwilowo → `E_WOULD_BLOCK`.

## 9. Wątki i niezależność instancji

Brak stanu globalnego. Różne uchwyty (dekodery, rejestry, wejścia) są w pełni niezależne i mogą pracować równolegle bez synchronizacji; ten sam uchwyt wymaga zewnętrznej synchronizacji. Biblioteka nie tworzy wątków. Weryfikacja: `scripts/test_aac_threads.sh` (wielowątkowo, wynik bit w bit); po przejściu na nowe API ten sam test dla każdego modułu, **w CI pod ThreadSanitizerem bez możliwości pominięcia** (patrz §11).

## 10. Plan wdrożenia etapami (bez utraty funkcjonalności)

1. **Etap 0 (zrobiony):** projekt nagłówków, test układu ABI, usunięcie stanu współdzielonego z dekoderów AAC, wewnętrzne `sauti_legacy_decoder_*` jako warstwa starego API.
2. **Etap 1 (zrobiony w tej wersji rozwojowej; patrz § 12):** rdzeń `src/maktabasauti/` (wersje, kody błędów, alokator z ukrytym nagłówkiem, wejścia pamięć/plik, czytnik buforowany, rejestr, konwersje PCM z ditherem TPDF, `decoder_open/read/seek/tell/reset/flush/close`, tagi, zarezerwowany koder) i most do starych dekoderów jako opakowania `NEEDS_FULL_BUFFER`, z wynikiem PCM bit w bit jak w starym API.
3. **Etap 2:** prawdziwe moduły strumieniowe: AAC (ADTS/LOAS najpierw), FLAC, DVS; każdy z testem wielowątkowym; wejście strumieniowe bez ładowania pliku.
4. **Etap 3:** przejście odtwarzacza (SDL) na nowe API; stare API tylko wewnętrznie, potem usunięte.
5. **Etap 4:** budowanie: CMake, biblioteki dzielone i statyczne, `pkg-config`, SONAME, pakiety; ścieżki C dla asm (Windows/ARM/BSD).

## 11. CI i wielowątkowość pod ThreadSanitizerem (wymaganie)

Test `tests/test_aac_threads.c` ma dziś zapasowy wariant z ASan, bo na tej maszynie brakuje runtime TSan. **W CI TSan jest wymagany i brak jego runtime ma wywracać zadanie, nie pomijać go.** W tym celu: (a) fixtury strumieni w `tests/data/` (syntetyczny sygnał zakodowany raz, nie dane użytkownika), żeby test w CI **nie wymagał** fdkaac ani FFmpeg i nie mógł się „SKIP-nąć”; (b) zadanie CI `tsan` na Linuksie x86-64 z `libtsan` (instalowanym w zadaniu; brak = błąd), uruchamiające `test_aac_threads` pod `-fsanitize=thread`; (c) analogiczne zadania ASan/UBSan. Zadania dla Windows, ARM i BSD są dodawane razem z etapem 4 (do czasu ścieżek C zamiast asm nie zbudują się: oznaczone jako dozwolone niepowodzenie z komentarzem, nie ukryte).

## 12. Stan etapu 1 (2026-11-19)

**Zrobione i przetestowane:** rdzeń C99 (`-std=c99 -pedantic -Werror`; most do starych dekoderów w C11), 19 kodeków z repozytorium opakowanych w rejestrze domyślnym (AAC: 8 wejść ADTS/HE/MP4/LOAS/ADIF, DVS/AVR/DTM, FLAC, WAV/AIFF/VOC, Vorbis, MIDI, YM, MP3; bez opakowań opartych o FFmpeg i inne zewnętrzne biblioteki, zgodnie z niezależnością od FFmpeg). Testy (wszystkie pod ASan i UBSan):
- `test_maktabasauti_core`: wersje, kody błędów, alokator **bez `realloc`** (zatruwanie, zliczanie, brak `free(NULL)`, powrót do zera), wejścia pamięć/plik, konwersje PCM (nasycenie, NaN, S24 pakowane, dither deterministyczny, rozłączność buforów), własność wejścia przy każdym rodzaju błędu, **syntetyczny kodek strumieniowy** (odczyt kilku ramek z 200 KB czyta tylko ułamek wejścia), `seek` EXACT/APPROX/na koniec/poza koniec/długość nieznana, lepkie błędy i ich czyszczenie, `reset`, `flush`, `E_FORMAT_CHANGED`, `E_WOULD_BLOCK` (widoczny i niewidoczny), wejście nieprzewijalne, most do starych dekoderów (WAV: PCM bit w bit jak w starym API, konwersja do S16 zgodna ze wzorem), lista kodekow wg modułów;
- `test_maktabasauti_regression`: wszystkie fixtury AAC (LC, HE v1, HE v2, M4A, LOAS, ADIF, 5.1) nowym i starym API: **PCM identyczny bit w bit**, te same parametry, te same wyniki po 12 przewinięciach i po `reset`, dla wejścia z pamięci i z pliku;
- `test_maktabasauti_threads`: 8 wątków × 7 plików × 3 formaty, własny alokator na wątek (po każdym cyklu liczba żywych bloków = 0), wspólne rejestry tylko do odczytu; w CI pod **ThreadSanitizerem (wymagane)**;
- wariant **przenośny** rdzenia (bez mostu do starych dekoderów, bez NASM): CI na Linuksie x86-64 i ARM64, macOS, Windows (MinGW) i FreeBSD.

**Ograniczenia tej wersji (świadomie opisane):**
1. Opakowania starych dekoderów (od 2026-11-20 zostały tylko Vorbis, MIDI, YM, MP3 i DTM; AAC, AVR, DVSM, WAV, AIFF, VOC i FLAC mają moduły strumieniowe) **nie są strumieniowe**: wczytują całe wejście do pamięci (flaga `NEEDS_FULL_BUFFER`), a każde otwarcie przez rejestr domyślny wczytuje plik, bo stare probe'y dostają cały plik. Strumieniowe moduły (AAC najpierw) zastępują opakowania w etapie 2; API i testy z syntetycznym kodekiem strumieniowym pokazują, że rdzeń to wspiera.
2. Stare dekodery nie podają przyczyny niepowodzenia: błąd otwarcia mapuje się na `E_UNSUPPORTED_FEATURE`, a niepowodzenie `seek`/`reset` na `E_NOT_SEEKABLE` (od 2026-11-20; stan dekodera bez zmian, np. budżet pracy MIDI); `flush` nie jest dostępny (`E_NOT_IMPLEMENTED`); tagi niedostępne.
3. Maska kanałów dla opakowanych dekoderów jest podawana tylko dla 1, 2, 6 i 8 kanałów (mono, stereo, 5.1, 7.1); w pozostałych przypadkach 0 (nie zgadujemy rozmieszczenia).
4. Opakowane dekodery oddają `float` (`native_format = F32`); konwersja do `S16/S24/S32/U8/F64` po stronie biblioteki.
5. Wczytanie całości jest ograniczone do 1 GiB (`E_NO_MEMORY` powyżej).
6. Wejście z pliku na Windows (UTF-8 → UTF-16) i budowa na pozostałych platformach są weryfikowane dopiero w CI; lokalnie przetestowano Linux x86-64.
7. Dynamiczne ładowanie modułów (`maktabasauti_module_entry`) i pakowanie (CMake, `.so.1`, `pkg-config`) to etapy 3 i 4.

## 13. Odporność rozpoznawania i wejścia (audyt sond, 2026-11-20)

Pełny raport: `docs/AUDYT_SOND.md`; fuzzing oparty na pokryciu: `docs/FUZZING.md`. Zasady, które rdzeń i moduły muszą spełniać (sprawdzane testami `make test-maktabasauti-probe-audit`, `make test-maktabasauti-hostile-io`, wielowątkowym rozpoznawaniem w `scripts/test_maktabasauti_threads.sh`):

**Sonda (`probe`)** — czysta funkcja nagłówka: przyjmuje DOWOLNE dane o dowolnej długości, w tym 0 bajtów, i nigdy nie zakłada, że dane należą do jej kodeka. Nie czyta poza `[prefix, prefix + n)` (także przy skrajnych polach długości i offsetach: rachunki w `size_t`/`uint64_t` z sprawdzeniem przed każdym odczytem), nie zapisuje do wejścia, nie alokuje, nie ma stanu, nie używa niezainicjowanych zmiennych, zwraca 0 albo 1..100 (wynik spoza zakresu rdzeń obcina). Sonda wołana na całym pliku (`NEEDS_FULL_BUFFER`) ma kosztować O(1) względem rozmiaru wejścia (czyta tylko okno nagłówka). Kod bibliotek zewnętrznych użyty w sondzie podlega tym samym zasadom (wniosek z błędu minimp3, commit `b2bdd74`).

**Wejście (`maktabasauti_io_t`)** — rdzeń nie ufa wartościom zwracanym przez callbacki użytkownika: `read` zwracający więcej niż zażądano daje `E_IO` (bez zapisu poza buforem), kod błędu spoza `E_*` (np. `INT64_MIN`) jest normalizowany do `E_IO` (wcześniej po obcięciu do `int` mógł dać `OK`), `size` jest tylko wskazówką (początkowy bufor całego wejścia ≤ 64 MiB, dalej rośnie wyłącznie o faktycznie odczytane dane, twardy limit 1 GiB), `seek` z dowolnym `offset`/`whence` nie powoduje przepełnienia ze znakiem. Wejście, które nigdy nie zgłasza końca danych, kończy się `E_NO_MEMORY` po 1 GiB przy kodekach wymagających całości (strumieniowe moduły czytają dalej, tylko ile potrzebują).

**Znane, opisane ryzyka:** (R1, rozwiązane 2026-11-20) sondy dostają prefiks 16 KiB, całość mapuje dopiero `open()` legacy kodeka; (R2) `seek` MIDI ma ograniczoną pracę (cisza pomijana, budżet dla grających głosów, `E_NOT_SEEKABLE` po przekroczeniu), docelowo punkty kontrolne; pozostałe w `docs/AUDYT_SOND.md`.

## 14. Etap 2: strumieniowy AAC, wspólny silnik i cztery kontenery (2026-11-20)

Moduł `maktabasauti_aac` ma jeden **silnik** (`maktabasauti_aac_engine.c`: dekodowanie ramek ADTS, punkty kontrolne, seek, flush, przycięcie gapless) i cztery **kontenery**, każdy jako osobny kodek
rejestru domyślnego (każdy zastępuje adapter legacy „cała pamięć”): `aac-adts` (`..._adts.c`), `aac-loas` (`..._loas.c`), `aac-adif` (`..._adif.c`), `aac-mp4` (`..._mp4.c`). Kontener dostarcza kolejne ramki
w postaci ADTS (`fetch`), umie się zresynchronizować (`resync`) i policzyć ramki (`scan`); opcjonalnie: `boundary/discard` (kolejka podramek LOAS), `prepare/seek_to/position/trim` (MP4: indeks próbek).
Algorytm dekodowania bez zmian (te same funkcje poziomu ramki; PCM bit w bit jak w starym API: `test_maktabasauti_regression`, 25 fixtur), zmienił się sposób dostarczania danych:
- czytanie po jednej ramce przez `host->peek/read`; ramka jest zużywana dopiero, gdy kompletna, więc `E_WOULD_BLOCK` w dowolnym miejscu zostawia stan nietknięty;
- pamięć: stan dekodera + ramka + PCM jednej ramki + pula punktów kontrolnych ≤ 8 MiB (stride rośnie dwukrotnie po zapełnieniu), niezależnie od długości. Wyjątek opisany: MP4 trzyma indeks próbek (16 B na próbkę, jak każdy demuxer);
- ADTS/LOAS/ADIF na wejściu nieprzewijalnym: odczyt i `flush`, długość nieznana, `seek`/`reset` = `E_NOT_SEEKABLE`; na przewijalnym: jedno przejście po nagłówkach przy otwarciu (dokładna długość; opcja
  `aac.scan=off` albo `adts.scan=off` wyłącza), `seek` dokładny z najbliższego punktu kontrolnego; bajty za ostatnią ramką (np. ID3v1) są pomijane;
  **uniDRC (USAC, opcje wyjściowe, domyślnie wyłączone):** `aac.drc=on` stosuje zestawy DRC wybrane jak w dekoderze wzorcowym (jednopasmowe, dziedzina czasu), `aac.drc_effect=1..8` żąda efektu (1 night, 2 noisy, 3 limited, 4 low level, 5 dialog, 6 general compression, 7 expand, 8 artistic), `aac.loudness_target=<LUFS>` normalizuje głośność do wartości docelowej (FDK domyślnie -24), `aac.drc_boost=` / `aac.drc_compress=` (współczynniki, domyślnie 1); szczegóły i pomiary: `docs/UNIDRC_PLAN.md`; `aac.pns_seed=fdk` zaczyna szum PNS od ziarna 0 jak FDK (domyślnie 0x1f2e3d4c jak FFmpeg; ziarno nie jest normatywne). `aac.drc_align=fdk` odtwarza oś czasu FDK (58 dB względem niego zamiast 21 dB na `usac_d1`): wzmocnienia opóźnione o 257 próbek, gdy za rdzeniem stoi SBR lub MPEG Surround, krzywa przechodzi też przez jednostki AudioPreRoll, a z sekwencji bierze się tylko pierwsze 16 węzłów (ograniczenie FDK). Domyślnie jak libxaac.
- **LOAS/LATM:** element po elemencie (3 B nagłówka + długość); konfiguracja ze strumienia (powtarzana musi być identyczna, zmiana = `E_UNSUPPORTED_FEATURE`); elementy przed pierwszą konfiguracją pomijane (po przewinięciu do początku
  konfiguracja jest zapominana, żeby było tak samo jak przy pierwszym odczycie); wiele podramek w elemencie obsługiwane (punkt kontrolny tylko na granicy elementu); resynchronizacja po uszkodzeniu;
- **ADIF:** bloki bez długości i bez synchronizacji: granice z parsowania elementów (okno peek 2 KiB, potem 8 KiB), nagłówek czytany ponownie po przewinięciu do początku; **brak resynchronizacji** (uszkodzony blok kończy strumień);
- **MP4/M4A (też fragmentowany):** pliku nie wczytujemy; w pamięci zostaje tylko lista jednostek dostępu `{pozycja, rozmiar}` (16 B na próbkę).
  - *Wejście przewijalne:* przegląd pudełek najwyższego poziomu (mdat pomijany), zawartość `moov` i kolejnych `moof` (≤ 128 MiB / 16 MiB) czytana pojedynczo do `sauti_aac_mp4_index_*` i zwalniana; próbka = seek + read; dokładny seek;
  - *Wejście nieprzewijalne (na żywo, np. fMP4 z sieci):* jeden przebieg po pudełkach. `moov` musi być PRZED `mdat` (init-segment albo faststart; `mdat` przed `moov` = `E_NOT_SEEKABLE`, bez cofania nie da się go odczytać).
    Kolejne `moof` są indeksowane, gdy nadchodzą, a indeks jest zapominany po zużyciu fragmentu (`sauti_aac_mp4_index_forget_units`): pamięć rośnie z rozmiarem JEDNEGO fragmentu, nie z długością strumienia (test: generator ~100 MB, szczyt < 1,5 MiB).
    Zawartość `moov`/`moof` jest czytana do rosnącego bufora (pamięć idzie za danymi, które naprawdę przyszły, nie za deklarowanym rozmiarem); deklaracja > limitu = `E_UNSUPPORTED_FEATURE`. Próbki muszą leżeć w strumieniu za bieżącą pozycją
    i w kolejności (fMP4 z `mdat` przed `moof` = `E_UNSUPPORTED_FEATURE` już przy otwarciu). Pobieranie jest WZNAWIALNE po `E_WOULD_BLOCK` (stan w kontenerze, wejście nie jest cofane);
    **otwarcie** (przejrzenie pudełek do `moov`) nie jest wznawialne (wejścia nieprzewijalnego nie da się cofnąć): źródło na żywo ma dostarczyć `ftyp`+`moov` przed `open()`. Przewijanie / `reset`: `E_NOT_SEEKABLE`.
    Długość: znana dla pliku bez fragmentów (liczba próbek z `moov`), dla fMP4 tylko z listy edycji / iTunSMPB (jeśli ją podaje), inaczej `UNKNOWN_LENGTH`;
  - przycięcie gapless (lista edycji / iTunSMPB) jak w starym adapterze; wyłącza je `MAKTABASAUTI_OPEN_NO_GAPLESS` (wcześniej MP4 tę flagę ignorował: naprawione);
  - nieobsługiwane (ALAC, inny kodek, AU > 8184 B) = czysty błąd;
- błędy (ADTS/LOAS): zła ramka lub nagłówek → `E_CORRUPT_DATA` po wydaniu poprawnych ramek, lepki do `flush` (gubi historię, resynchronizuje, HE czeka na ramkę z nagłówkiem SBR); urwana ramka → `E_TRUNCATED`;
  zmiana parametrów w środku → `E_UNSUPPORTED_FEATURE`;
- host: usługa `input_flags` (`MAKTABASAUTI_INPUT_SEEKABLE/SIZE_KNOWN`); `peek` zgłasza błąd wejścia (np. `E_WOULD_BLOCK`), gdy nie może spełnić żądania, zamiast udawać krótki plik;
- `open()` przy `E_WOULD_BLOCK` zostawia wejście u wywołującego i, gdy się przewija, cofa je na początek (ponowienie działa); wejście nieprzewijalne nie może tego zagwarantować: źródła na żywo powinny dostarczyć pierwsze KiB przed `open()`.
**Moduł DVS: strumieniowe DVSM i AVR (od 2026-11-20).** `src/decoders/retro/atari/maktabasauti_dvs.c`, czysty C, bez NASM i bez wczytywania pliku: dane idą przez bufor 4 KiB i są dekodowane ramka po ramce, więc wejście może być
nieprzewijalne, bez rozmiaru i dowolnie długie (test: generator ~100 MB Pack-2, szczyt pamięci dekodera < 64 KiB). PCM jest IDENTYCZNY bit w bit z dawnym adapterem (te same wyrażenia zmiennoprzecinkowe, ta sama tablica kroków Pack-2,
to samo nasycanie): test porównuje oba na losowych plikach wszystkich trybów (PCM 8/16 bit, Pack-2, Pack-4, mono i stereo z blokami, AVR 8/16 bit ze znakiem i bez). Nagłówek jest walidowany jak dawniej. Zachowanie przy niepełnych
danych jest strumieniowe: ramki z kompletnych jednostek są wydawane, potem `E_TRUNCATED` (lepki do `seek`/`reset`/`flush`); dawny adapter odrzucał wtedy cały plik. Przy znanym rozmiarze długość wynika z arytmetyki formatu (PCM: bajty / ramka;
Pack-2 mono: bajty − 1; Pack-4 mono: 1 + 2·(bajty − 2); stereo: ramki bloków) i puste dane są odrzucane przy otwarciu jak dawniej; bez rozmiaru długość jest nieznana. Przewijanie wymaga wejścia przewijalnego ze znanym rozmiarem: PCM i AVR
z arytmetyki pozycji, delta przez punkty kontrolne akumulatorów (pula 1024, przerzedzana przy wyczerpaniu, krok podwajany; potem odtworzenie od najbliższego punktu), dokładnie. Odczyt jest wznawialny po `E_WOULD_BLOCK` w każdym punkcie
(bajty zostają w buforze pośrednim). Test: `make test-maktabasauti-dvs-stream` (16 plików: wejścia przewijalne/nieprzewijalne, 1- i 7-bajtowe odczyty, losowe `WOULD_BLOCK`, `WOULD_BLOCK` w każdym numerze wywołania, seek losowy i na granicach
bloków, obcięcia na każdej długości ze znanym i nieznanym rozmiarem, plik 9 MB z wyczerpaniem puli punktów, wrogie nagłówki; sprawdzone mutacją). Nie dotyczy: DTM (Digital Tracker) to format modułowy (próbki + wzorce), który z natury
wymaga całego pliku: zostaje opakowanym adapterem `NEEDS_FULL_BUFFER`.

**Moduły PCM (WAV, AIFF) i VOC: strumieniowe (od 2026-11-20).** `src/decoders/modern/maktabasauti_pcm.c` (`wav-pcm`, `aiff-pcm`, `au-pcm`; G.711 mu-law / A-law w WAV, AIFF-C i AU: `docs/G711.md`) i `src/decoders/retro/pc/maktabasauti_voc.c` (`creative-voc-pcm`), czysty C, bez wczytywania pliku (bufor 4 KiB;
test: generator ~100 MB bez rozmiaru, szczyt pamięci dekodera < 64 KiB). Nazwy kodeków, nazwy strumienia (`pcm` / `wav|aiff|voc`) i PCM dla poprawnych plików są IDENTYCZNE bit w bit z dawnymi adapterami (te same wyrażenia:
8 bit bez znaku `(v−128)/128`, pozostałe wartość ze znakiem / 2^(bity−1), AIFF z `sowt`, VOC 8/16 bit); stary adapter jest w testach wzorcem. **Nagłówek** jest czytany z prefiksu okna czytnika (`peek`, 512 B), więc
`E_WOULD_BLOCK` przy otwarciu jest wznawialny; gdy nagłówek jest dłuższy (duży chunk `LIST` przed danymi), moduł przechodzi chunk po chunku (seek albo odczyt z odrzuceniem; wtedy otwarcie z wejścia nieprzewijalnego nie jest
wznawialne). Kontrole nagłówka jak dawniej (rozmiary chunków, `fmt`, `align`, liczba kanałów, częstotliwość AIFF, `COMM`/`SSND`, bloki VOC); test porównawczy „macierz nagłówków” przechodzi po wartościach granicznych (kanały, bity,
częstotliwości, rozmiary danych, AIFF-C) i wymaga, by stary adapter i moduł przyjmowały dokładnie to samo (258 przyjętych plików), a mutacje nagłówków wymagają, by wszystko, co przyjmuje stary adapter, przyjmował też moduł.
**Różnice wynikające ze strumieniowania:** (1) plik ucięty: ramki sprzed ucięcia są wydawane, potem `E_TRUNCATED` (lepki; stary adapter odrzucał cały plik); (2) WAV: chunki za danymi nie są sprawdzane, gdy `fmt` jest przed danymi
(wejście przewijalne szuka `fmt` za danymi; nieprzewijalne = `E_NOT_SEEKABLE` bez zużycia wejścia); AIFF analogicznie dla `COMM` za `SSND`; (3) WAV zapisany strumieniowo (rozmiar RIFF 0 / `0xFFFFFFFF`, rozmiar `data` 0 / `0xFFFFFFFF`):
długość = reszta wejścia (znana, gdy znany rozmiar wejścia; inaczej `UNKNOWN_LENGTH`), niespójny rozmiar `data` przy prawdziwym RIFF jest odrzucany jak dawniej; (4) **WAV IEEE float 32/64 bit** (format 3, także `WAVE_FORMAT_EXTENSIBLE`; NaN/Inf → 0)
jest dekodowany (dawny adapter odrzucał te pliki stałą kontrolą `format == 1`; jego stary kod odczytu float był nieosiągalny); (5) VOC: dwa tryby. Wejście **przewijalne** buduje przy otwarciu indeks czesci z nagłówków blokow
(do 16384, jak dawniej; więcej = `E_UNSUPPORTED_FEATURE`), zna długość i przewija dokładnie. Wejście **nieprzewijalne** dekoduje bloki po kolei, długość jest nieznana, nie ma limitu części, a błąd w bloku za już wydanymi ramkami
(blok powtórzenia / ADPCM / inna częstotliwość) wychodzi po tych ramkach (stary adapter odrzucał cały plik); blok 8 bez bloku danych na końcu jest przyjmowany (stary adapter odrzucał plik). Błędy VOC: `E_UNSUPPORTED_FEATURE`
(powtórzenia 6/7, ADPCM, nieznane bloki, zmiana częstotliwości/kanałów), `E_CORRUPT_DATA` (niespójne długości).
Testy: `make test-maktabasauti-pcm-stream` i `make test-maktabasauti-voc-stream` (wejścia przewijalne/nieprzewijalne, 1- i 7-bajtowe odczyty, losowe `WOULD_BLOCK`, `WOULD_BLOCK` w każdym numerze wywołania i przy otwarciu,
seek dokładny, obcięcia na każdej długości (VOC: z niezależnym modelem granic bloków), nagłówki graniczne, mutacje nagłówków, wrogie nagłówki, pamięć). Oba w CI.
**AAC-LD i AAC-ELD w MP4 i LOAS (od 2026-11-20).** Kodeki `aac-mp4` i `aac-loas` dekodują także jednostki dostępu AAC-LD (AOT 23) i AAC-ELD (AOT 39) bez SBR i bez narzędzi odporności (mono i stereo, 48–22,05 kHz, ramki 512 albo 480): opis strumienia ma `codec_profile` `LD` / `ELD`, a wszystkie własności silnika (wejście nieprzewijalne, `E_WOULD_BLOCK`, seek dokładny, punkty kontrolne, gapless z `elst` / iTunSMPB, `E_FORMAT_CHANGED`) działają bez zmian. Zgodność z FFmpeg: 139 dB, długość i opóźnienie identyczne. ELD z SBR i konfiguracje wielokanałowe dają `E_UNSUPPORTED_FEATURE` / `E_UNSUPPORTED_FORMAT` (jak dawniej). Szczegóły: `docs/AAC_FAMILY_ASM.md` SBR-100; test `make test-aac-er` i fixtury `tests/data/{ld,eld}_{st,mono}.m4a`.

**AAC-LC z ramkami 960 próbek w MP4 i LOAS (od 2026-11-20).** Kodeki `aac-mp4` i `aac-loas` dekodują AAC-LC z `frameLengthFlag = 1` (960 linii widma na ramkę, osiem krótkich okien po 120; DAB+, DRM i niektóre tory nadawcze), mono i stereo, wszystkie 13 częstotliwości z tablicy `sampling_frequency_index`. Opis strumienia ma `codec_profile` `LC`, długość wynika z 960 próbek na jednostkę dostępu, a pozostałe własności silnika (wejście nieprzewijalne, `E_WOULD_BLOCK`, seek dokładny, gapless, `E_FORMAT_CHANGED`) działają bez zmian. Ramka 960 NIE istnieje w ADTS (nagłówek ADTS nie niesie flagi długości ramki), więc ADTS i ADIF są dekodowane wyłącznie jako 1024. HE-AAC z ramką 960: jednostka z danymi SBR daje `E_UNSUPPORTED_FEATURE` (nie jest dekodowana jako 1024), konfiguracje wielokanałowe `E_UNSUPPORTED_FORMAT` (jak dla LD/ELD). Zgodność: FFmpeg 136–139 dB (długość i opóźnienie identyczne), FDK AAC 86–92 dB (arytmetyka stałoprzecinkowa). Szczegóły: `docs/AAC_FAMILY_ASM.md` SBR-101; test `make test-aac-lc960`, fixtury `tests/data/lc960_*.{m4a,loas}` (generator własny, `tests/data/make_lc960_fixtures.sh`).

**AAC Main (od 2026-11-21).** Kodeki `aac-adts`, `aac-mp4` i `aac-loas` dekodują AAC Main (AOT 1, w ADTS `profile = 0`) z predykcją wsteczną: mono i stereo, indeksy częstotliwości 0..11, ramki 1024. `codec_profile` = `Main`; wszystkie własności silnika (wejście nieprzewijalne, `E_WOULD_BLOCK`, seek dokładny przez punkty kontrolne, które niosą stan predyktorów, gapless, `E_FORMAT_CHANGED`) działają bez zmian. Od 2026-11-21 także Main wielokanałowy (konfiguracje 3..7), Main z 960 liniami (mono / stereo), ADTS z kontrolą błędów (CRC-16) i ADIF. Poza zakresem (`E_UNSUPPORTED_FEATURE` / `E_UNSUPPORTED_FORMAT`): 7350 Hz, SBR w jednostkach, konfiguracja 0 (PCE), CRC dla więcej niż dwóch kanałów. Zgodność: FFmpeg 138 dB na strumieniach o stałym `max_sfb` (przy zmiennym FFmpeg ma własny błąd flag predykcji), FAAD2 135 dB. Szczegóły: `docs/AAC_FAMILY_ASM.md` SBR-102; test `make test-aac-main`, fixtury `tests/data/main_*`.

**AAC LTP (od 2026-11-21).** Kodeki `aac-adts`, `aac-mp4` i `aac-loas` dekodują AAC LTP (AOT 4, w ADTS `profile = 3`) z predykcją długoterminową: mono i stereo, wszystkie 13 częstotliwości, ramki 1024. `codec_profile` = `LTP`; stan predykcji (trzy bloki czasowe na kanał) jest częścią stanu dekodera, więc seek dokładny (punkty kontrolne), wejście nieprzewijalne, `E_WOULD_BLOCK`, gapless i `E_FORMAT_CHANGED` działają bez zmian. Od 2026-11-21 także LTP wielokanałowy (3..7), ADTS z kontrolą błędów i ADIF. Poza zakresem: LTP z 960 liniami (żaden dekoder tego nie ma), SBR w jednostkach, CRC dla więcej niż dwóch kanałów. ER AAC LC / LTP (AOT 17 / 19, bez narzędzi odporności) w MP4 i LOAS: `codec_profile` `ER-LC` / `ER-LTP`, mono, stereo i wielokanałowo; ER LTP stereo nie ma niezależnego dekodera (patrz SBR-104). Zgodność: FFmpeg 134–138 dB (FDK nie dekoduje LTP, FAAD2 ma zgrubny bufor: 58–73 dB). Szczegóły: `docs/AAC_FAMILY_ASM.md` SBR-103; test `make test-aac-ltp`, fixtury `tests/data/ltp_*`; ER: `docs/AAC_FAMILY_ASM.md` SBR-104, test `make test-aac-er-syntax`.

**Moduł FLAC: strumieniowy (od 2026-11-20).** `src/decoders/modern/maktabasauti_flac.c`: `dr_flac` (third_party, bez zmian) z wywołaniami zwrotnymi czytającymi przez host; własna, prywatna kopia implementacji (`DRFLAC_API static`), więc moduł nie zależy od starego adaptera.
Dekodowanie robi ta sama biblioteka co stary adapter (`drflac_read_pcm_frames_f32`), więc PCM jest IDENTYCZNY bit w bit (porównanie z `sauti_decoder_flac_ops` na 6 fixturach: stereo 16/24 bit, mono, ramki 1152..16384, okładka w bloku PICTURE; fixtury generuje
`tests/data/make_flac_fixtures.sh`, repozytorium nosi ich WYNIK). Wejście może być nieprzewijalne, bez rozmiaru i dowolnie długie; **pamięć zależy od nagłówka, nie od długości pliku**: bufor pośredni ma ~2× największą możliwą ramkę (z `max_blocksize`, kanałów i bitów
w STREAMINFO; pole `max_framesize` jest świadomie pomijane, bo nie jest wiarygodne) + 20 KiB, czyli zwykle 40–100 KiB, w najgorszym przypadku ~1,1 MiB, a bufory `dr_flac` (`max_blocksize × kanały × 4 B`) dochodzą do ~2 MiB (test: szczyt < 5 MiB dla 3000 losowych STREAMINFO, < 256 KiB dla
strumienia 30 MB z nagłówkiem 4096). **`E_WOULD_BLOCK`:** `dr_flac` czyta synchronicznie i wywołanie zwrotne nie może przerwać dekodowania, więc przed dekodowaniem KAŻDEJ nowej ramki moduł upewnia się, że bufor ma bajty na całą ramkę (albo wejście się skończyło), inaczej zwraca
`E_WOULD_BLOCK` bez utraty danych; skutek to opóźnienie rzędu dwóch ramek, a ramka większa od tego limitu (niezgodna z STREAMINFO) daje `E_CORRUPT_DATA` po wydanych ramkach. **Otwarcie:** gdy nagłówek (wszystkie bloki metadanych + nagłówek pierwszej ramki) mieści się w oknie czytnika
(16 KiB), jest czytany przez `peek` i moduł nie zużywa z wejścia nic do udanego otwarcia; dłuższe metadane (okładka) idą wywołaniami zwrotnymi wprost z wejścia, a po błędzie wejście nieprzewijalne jest przesunięte (ponowienie nieprzewijalnego rozstrzyga host: czytnik jest budowany od nowa, więc nie ma gwarancji). Różnice wobec starego adaptera:
długość 0 w STREAMINFO (zapis strumieniowy, np. potok) jest przyjmowana (długość nieznana, `seek`/`reset` wyłączone, opis strumienia bez `SEEKABLE`; moduł dostarcza `get_info`, dzięki czemu rdzeń sprawdza flagę przed ruszeniem wejścia); plik ucięty daje ramki do ucięcia (te same co stary adapter na tych
samych bajtach) i `E_TRUNCATED`; kanały 1–2 jak dawniej. Test: `make test-maktabasauti-flac-stream` (10 konfiguracji wejścia, `WOULD_BLOCK` w każdym numerze wywołania i przy otwarciu, seek dokładny, obcięcia na każdej długości względem starego adaptera, 7 plików, 600 mutacji bajtów na plik
w dwóch trybach, losowe STREAMINFO, pamięć). W CI.
Kontrola mutacjami (2026-11-20): WAV/AIFF 93 poprawne mutanty, 67 zabitych, 26 przeżyło i wszystkie 26 są równoważne (obrona w głąb: ta sama odmowa zachodzi drugą drogą, np. dane poza plikiem odrzuca `seek`; strażnik pętli `off <= riff_end` pokrywa się z `break`; sondy i nagłówki
krótsze niż 12/26 B chroni dodatkowo wyzerowany bufor prefiksu; `2147483647.0f == 2147483648.0f` w `float`; oszczędność odczytu zamiast wyniku); VOC 96 mutantów, 83 zabite, 13 równoważnych (redundantne kontrole `last_channels`, kanały w bloku 8 i `fmt_set`, brak kontroli `ended` po
nieprzesuniętym bajcie końca, `pend` odtwarzany z ponownego odczytu, `st_clear` powtórzony przy ponownym pozycjonowaniu). Szukanie ocalałych mutantów ujawniło luki w testach (nagłówek `fmt` rozszerzony, rozmiary `COMM`/`SSND`, liczby graniczne, odczyt po końcu, seek po błędzie lepkim, błąd `seek` przy
otwarciu), które uzupełniono; jedna zmiana w kodzie: strażnik `off <= riff_end` w pętli chunków WAV. MSan (`test-msan-replay`) sprawdza nowe moduły dopiero w CI (lokalnie brak `clang`).

Testy: `make test-maktabasauti-aac-stream` (ADTS, LOAS, ADIF, MP4; wejścia nieprzewijalne, odczyty po 1 bajcie, `WOULD_BLOCK` w każdym punkcie, urwane i uszkodzone strumienie, pamięć ograniczona, `seek` dokładny także po
wyczerpaniu puli punktów kontrolnych), audyt sond/open i testy wielowątkowe obejmują wszystkie cztery. **ID3v2 przed ADTS (od 2026-11-20):** znaczniki na początku (także ze stopką i kilka z rzędu) są pomijane przy otwarciu i po przewinięciu do początku; sonda patrzy za znacznik, gdy mieści się w prefiksie, a przy znaczniku dłuższym od 16 KiB zwraca 55 (tuż nad MP3 z tym samym znacznikiem; gdy za znacznikiem nie ma ADTS, `open()` zawodzi i rdzeń przechodzi do kolejnego kandydata); wejście nieprzewijalne obsługuje znaczniki mieszczące się w oknie czytnika (dłuższe: czysty błąd bez zużycia danych). Test: `test_id3v2` w `test_maktabasauti_aac_stream.c` (mały, ze stopką, dwa, duży 40 KiB, z ID3v1 na końcu; odczyt, seek, reset). **Zmiana parametrów w środku strumienia (ADTS i LOAS, od 2026-11-20):** inna częstotliwość, układ kanałów albo CRC → po wydaniu wszystkich ramek sprzed zmiany `read` zwraca `E_FORMAT_CHANGED` (0 ramek, nielepki; zaległy blok HE-AAC v2 jest wydany z ciszą jako wyprzedzeniem), rdzeń odświeża opis przez `get_info` kodeka, a dekoder startuje ze świeżym stanem na ramce z nowymi parametrami. Po zmianie długość jest nieznana, a `seek`/`reset` wyłączone (`E_NOT_SEEKABLE`, opis strumienia bez `SEEKABLE`; rdzeń sprawdza flagę przed ruszeniem wejścia). LOAS: zmiana konfiguracji LATM jest przyjmowana (nowa konfiguracja), nowy układ nieobsługiwany = `E_UNSUPPORTED_FEATURE`. Test: `test_format_change` (13 par: kanały, częstotliwość, LC↔HE, HE v2↔LC, 5.1; wejście przewijalne i nieprzewijalne; PCM obu części bit w bit jak odczytane osobno). **MP4 z wejścia nieprzewijalnego (od 2026-11-20):** patrz opis MP4 wyżej; testy: pełny zestaw wejść (1-bajtowe odczyty, `WOULD_BLOCK` co drugie wywołanie i losowo) dla `*_fast.m4a` (faststart) i `lc_frag.m4a` (fMP4), obcięcia w ~11 punktach każdego pudełka (wynik = prefiks PCM), wrogie nagłówki `moof` (1 GiB, 10 MiB urwane po 100 B, rozmiar < nagłówka, rozmiar 2^64-1), `mdat` przed `moof`, generator ~100 MB (szczyt pamięci), `OPEN_NO_GAPLESS`. Nie zrobione: moduły strumieniowe dla pozostałych formatów (FLAC, DVS, WAV...).
