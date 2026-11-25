# Amiga IFF 8SVX (PCM, delta Fibonacciego i wykładnicza) — MaktabaSauti

Moduł strumieniowy `src/decoders/retro/amiga/maktabasauti_svx.c` (kodek `amiga-8svx`, kontener `iff`), napisany w czystym C; pamięć nie zależy od długości pliku (bufor 4 KiB na kanał).

## Format
`FORM <rozmiar> 8SVX`, chunki (rozmiar parzysty dopełniany): `VHDR` (20 bajtów: oneShotHiSamples, repeatHiSamples, samplesPerHiCycle `u32`, samplesPerSec `u16`, ctOctave `u8`, **sCompression `u8`**, volume `u32`), opcjonalnie `CHAN` (6 = stereo; 2 albo 4 = jeden kanał), `BODY`; inne chunki (`NAME`, `ANNO`, `(c) `, `ATAK`, `RLSE`...) są pomijane i mogą stać przed albo za danymi (przy wejściu przewijalnym `VHDR` i `CHAN` mogą też stać za `BODY`).
* **sCompression 0:** bajty ze znakiem; wyjście f32 = bajt / 128 (jak `pcm_s8` FFmpeg, po zaokrągleniu do 16 bitów identycznie).
* **sCompression 1, delta Fibonacciego:** blok kanału = bajt wypełniający, wartość początkowa (ze znakiem), potem bajty z dwoma kodami 4-bitowymi, **MŁODSZY półbajt pierwszy**; wartość += `{-34,-21,-13,-8,-5,-3,-2,-1,0,1,2,3,5,8,13,21}[kod]`, obcięta do -128..127. Z jednego bloku o `n` bajtach wychodzi `2 (n - 2)` próbek.
* **sCompression 2, delta wykładnicza** (rozszerzenie poza specyfikacją Commodore, obsługiwane jak w FFmpeg): ten sam układ, tablica `{-128,-64,-32,-16,-8,-4,-2,-1,0,1,2,4,8,16,32,64}`.
* **Stereo:** `BODY` = cały kanał lewy, potem cały prawy (skompresowane: każdy z własnym nagłówkiem 2 bajty). Wymaga wejścia PRZEWIJALNEGO (dwie pozycje odczytu); nieprzewijalne stereo daje `E_NOT_SEEKABLE`, mono działa na dowolnym wejściu.
* `VHDR.volume` i pętle (`repeatHiSamples`) nie są stosowane (FFmpeg też nie). Inne wartości sCompression: `E_UNSUPPORTED_FEATURE`; brak `VHDR` / `BODY`, częstotliwość 0 lub > 384 kHz, nie-8SVX: `E_UNSUPPORTED_FORMAT`.

## Pozostałe decyzje
* Plik urwany: ramki sprzed urwania są wydawane, potem lepki `E_TRUNCATED` (czyszczony przez seek / reset / flush). Urwany STEREO: kanał prawy zachowuje swoje miejsce wg zadeklarowanego rozmiaru; FFmpeg dzieli bajty, które są, na pół (prawy kanał zaczyna się wtedy w złym miejscu), więc tych plików nie porównujemy.
* `CHAN` urwany za danymi jest ignorowany (jak FFmpeg).
* Przewijanie: PCM z arytmetyki pozycji; delta przez punkty kontrolne stanu (co 4096 ramek, pula 1024 punktów, krok podwajany) i odtworzenie od najbliższego.

## Weryfikacja (wzorzec: FFmpeg `iff` + `8svx_fib` / `8svx_exp` / `pcm_s8`)
* **12 prawdziwych plików** (`samples.ffmpeg.org/8svx/`, mono 22095 Hz, wszystkie sCompression 0; poza repozytorium w `~/Sauti-tools/real-files/8svx/dl`, licencja nieznana): identyczne z FFmpeg (`scripts/test_real_8svx.py`).
* **Prawdziwych plików z kompresją Fibonacciego nie znaleziono w publicznych zbiorach** (FFmpeg ma tylko powyższe, bez kompresji); delta jest więc sprawdzona na plikach SYNTETYCZNYCH, których układ (młodszy półbajt pierwszy, wartość początkowa w drugim bajcie, obcinanie, stereo) ustalono doświadczalnie na FFmpeg: `scripts/test_synth_8svx.py`, 600 losowych plików (PCM / Fibonacci / wykładnicza, mono / stereo, dodatkowe chunki, nieparzyste rozmiary, urwane, `CHAN` za danymi): 561 identycznych, 12 odrzuconych przez oba, 1 odrzucony tylko przez FFmpeg (urwany chunk metadanych), 26 urwanych stereo pominiętych, **0 różnic**.
* `make test-svx` (`tests/test_svx.c`): cztery fixtury `tests/data/svx_*.8svx` z oczekiwanym PCM z FFmpeg (`make_svx_fixtures.py`), odczyty dowolnej długości, przewijanie przez punkty kontrolne, odrzucenia.

## Otwarte
Wieloktawowe `8SVX` (kilka oktaw w jednym `BODY`) nie jest obsługiwane. Nieskompresowane `16SV` jest osobnym kodekiem (`docs/IFF_16SV.md`). AIFF-C / `8SVX` w kontenerze innym niż FORM nie występuje.
