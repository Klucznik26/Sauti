# MaktabaSauti: projekt publicznego API (2026-11-19)

Nazwa: **MaktabaSauti** (swahili: „biblioteka dźwięku"). Status: **projekt do przeglądu**; ABI zostaje zamrożone w pierwszym wydaniu (`MAKTABASAUTI_ABI_VERSION 1`). Nagłówki: `include/maktabasauti/maktabasauti.h` (API aplikacji) i `maktabasauti_codec.h` (interfejs modułu kodeka). Plan całej biblioteki, licencje i pochodzenie danych: `BIBLIOTEKA_PLAN.md`, `POCHODZENIE_DANYCH.md`.

## 1. Nazwy i układ bibliotek

| element | nazwa |
|---|---|
| rodzina | MaktabaSauti |
| wspólne API | `maktabasauti.h` (+ `maktabasauti_codec.h` dla autorów modułów) |
| moduł AAC (AAC, HE-AAC v1/v2, dalsze profile) | `maktabasauti_aac` → `libmaktabasauti_aac.so.1` / `maktabasauti_aac.dll` |
| moduł Atari DVS (DVSM) | `maktabasauti_dvs` → `libmaktabasauti_dvs.so.1` |
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

- Wszystkie alokacje przez alokator (`maktabasauti_allocator_t`, `NULL` = libc), także w modułach (przez `host->alloc`). Brak stanu globalnego.
- **Wejście przechodzi na własność biblioteki** przy `maktabasauti_decoder_open`, **zawsze, także po błędzie**: callback `close` zostanie wywołany dokładnie raz. Wywołujący nie musi osobno sprzątać po nieudanym otwarciu.
- Biblioteka nie oddaje wskaźników do wewnętrznych buforów poza łańcuchami opisanymi w nagłówku (ważnymi do kolejnego wywołania na tym uchwycie lub zamknięcia). PCM zawsze trafia do bufora wywołującego.
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
- `seek(ramka, EXACT|APPROX, &faktyczna)`, `tell`; pozycje liczone po przycięciu gapless.
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
| **DVS/DVSM** (PCM, Pack-2, Pack-4; ADPCM, PACK override planowane) | tak | bloki z ziarnem predyktora (Pack-2) → seek przez punkty kontrolne | znana | opcje modułu `dvs.*`; wolne od wymogu całego pliku (dziś dekoder dekoduje wszystko naraz: do refaktoryzacji) |
| **FLAC** | tak | seektable / bisekcja | znana (STREAMINFO) | 24 bity w `S32`/`F32` bez straty; tagi Vorbis comment |
| MP3 | tak (synchronizacja ramek) | tabela z VBR lub szacunek | szacowana, jeśli brak nagłówka VBR | `E_TRUNCATED`/`CORRUPT_DATA` przy urwanej ramce, gapless z LAME |
| Ogg Vorbis / Opus | tak | bisekcja stron (wymaga przewijalnego wejścia) | z ostatniej strony, inaczej nieznana | łańcuchowe pliki → `FORMAT_CHANGED` |
| WavPack | tak | tak | znana | — |
| **AAC** (ADTS/LATM/LOAS/ADIF/MP4/fMP4) | ADTS, LOAS, fMP4 z `moov` na początku, ADIF: tak; MP4 z `moov` na końcu: `NEEDS_SEEKABLE` | punkty kontrolne co 256 ramek HE; dokładne z `elst`/`iTunSMPB` | znana z kontenera, inaczej nieznana | profile i warianty w `codec_profile`; gapless z kontenera |
| przyszłe AAC (ER, LD, ELD, USAC, DAB+ 960) | j.w. | j.w. | j.w. | zmiana długości ramki nie wpływa na API |
| YM, SNDH (syntezowane) | tak (generowane) | przez odtworzenie od początku | nieznana lub z metadanych | opcja `sample_rate` (§ w nagłówku); `loop`; `UNKNOWN_LENGTH` |
| MIDI (syntezator) | tak | przez odtworzenie | szacowana | opcje: `sample_rate`, `midi.soundfont` (moduł) |
| kodery (AAC itd.) | — | — | — | zarezerwowane `maktabasauti_encoder_*` i miejsca w strukturze kodeka |

**Luki wykryte przy tym przeglądzie i wprowadzone do API przed zamrożeniem:** (1) zmiana parametrów w środku strumienia → `E_FORMAT_CHANGED` i odświeżone `get_info`; (2) źródła syntezowane bez własnej częstotliwości → wspólna opcja `sample_rate`; (3) źródła bez znanej długości lub z pętlą → `UNKNOWN_LENGTH` i opcja `loop`; (4) wznowienie po nieciągłości bez przewijania wejścia → `flush`; (5) wejście bez danych chwilowo → `E_WOULD_BLOCK`.

## 9. Wątki i niezależność instancji

Brak stanu globalnego. Różne uchwyty (dekodery, rejestry, wejścia) są w pełni niezależne i mogą pracować równolegle bez synchronizacji; ten sam uchwyt wymaga zewnętrznej synchronizacji. Biblioteka nie tworzy wątków. Weryfikacja: `scripts/test_aac_threads.sh` (wielowątkowo, wynik bit w bit); po przejściu na nowe API ten sam test dla każdego modułu, **w CI pod ThreadSanitizerem bez możliwości pominięcia** (patrz §11).

## 10. Plan wdrożenia etapami (bez utraty funkcjonalności)

1. **Etap 0 (zrobiony):** projekt nagłówków, test układu ABI, usunięcie stanu współdzielonego z dekoderów AAC, wewnętrzne `sauti_legacy_decoder_*` jako warstwa starego API.
2. **Etap 1:** rdzeń (`src/maktabasauti/`): wersje, kody błędów, alokator, wejścia z pamięci/pliku, bufor czytnika (peek/seek), rejestr, konwersje PCM i dither, `decoder_open/read/seek/reset/flush/close`. Most do starych dekoderów (`SautiDecoderOps`) jako moduły „legacy” (wymagają całego pliku, oznaczone w opisie), żeby cały dotychczasowy zestaw kodeków działał natychmiast przez nowe API; testy regresyjne: wynik bit w bit jak przez stare API.
3. **Etap 2:** prawdziwe moduły strumieniowe: AAC (ADTS/LOAS najpierw), FLAC, DVS; każdy z testem wielowątkowym; wejście strumieniowe bez ładowania pliku.
4. **Etap 3:** przejście odtwarzacza (SDL) na nowe API; stare API tylko wewnętrznie, potem usunięte.
5. **Etap 4:** budowanie: CMake, biblioteki dzielone i statyczne, `pkg-config`, SONAME, pakiety; ścieżki C dla asm (Windows/ARM/BSD).

## 11. CI i wielowątkowość pod ThreadSanitizerem (wymaganie)

Test `tests/test_aac_threads.c` ma dziś zapasowy wariant z ASan, bo na tej maszynie brakuje runtime TSan. **W CI TSan jest wymagany i brak jego runtime ma wywracać zadanie, nie pomijać go.** W tym celu: (a) fixtury strumieni w `tests/data/` (syntetyczny sygnał zakodowany raz, nie dane użytkownika), żeby test w CI **nie wymagał** fdkaac ani FFmpeg i nie mógł się „SKIP-nąć”; (b) zadanie CI `tsan` na Linuksie x86-64 z `libtsan` (instalowanym w zadaniu; brak = błąd), uruchamiające `test_aac_threads` pod `-fsanitize=thread`; (c) analogiczne zadania ASan/UBSan. Zadania dla Windows, ARM i BSD są dodawane razem z etapem 4 (do czasu ścieżek C zamiast asm nie zbudują się: oznaczone jako dozwolone niepowodzenie z komentarzem, nie ukryte).
