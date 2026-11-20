# Fuzzing MaktabaSauti (2026-11-20)

Trzy warstwy, od najtańszej:

1. **Audyt deterministyczny** (`make test-maktabasauti-probe-audit`, `docs/AUDYT_SOND.md`): kompletny przegląd nagłówków, skrajne wartości, bufory o dokładnym rozmiarze ze stronami ochronnymi. Szybki, bez clanga.
2. **Powtórka korpusu** (`make test-fuzz-replay`, w każdym przebiegu CI): te same cele co libFuzzer uruchamiane przez prosty sterownik (`fuzz/standalone_main.c`) na korpusie początkowym (gcc + ASan/UBSan). Wykrywa regresje znalezionych błędów
   bez potrzeby clanga; korpus początkowy generuje `fuzz/make_seeds.py` z `tests/data` (dane syntetyczne, bez materiału użytkownika).
3. **libFuzzer z pokryciem kodu** (`make fuzz-build`, `make fuzz-run FUZZ_SECONDS=90`, zadanie CI `libfuzzer`): Clang, `-fsanitize=fuzzer,address,undefined` (z `-fsanitize=function`, którego gcc nie ma),
   `-timeout=30 -rss_limit_mb=3072`, wykrywanie wycieków. Awaria, wyciek, UB albo zawieszenie = błąd zadania; artefakty (`build/fuzz_artifact_*`) są wysyłane z CI.

## Cele

| cel | co wywołuje | dane |
|---|---|---|
| `fuzz_probe` | wszystkie sondy rejestru na bufor o dokładnym rozmiarze, wynik 0..100 | surowe |
| `fuzz_open` | pełne API: rozpoznanie, czytnik, kontenery, dekodery; `read`, `seek`, `flush`, `reset`, `close`, własność wejścia, alokator (wyciek = abort) | bajt 0: przewijalne/rozmiar/limit porcji/losowe `WOULD_BLOCK`; bajt 1: ziarno; reszta: plik |
| `fuzz_container` | kontenery AAC wymuszone po nazwie (`aac-adts/-loas/-adif/-mp4`), omija sondy | bajt 0: kodek + przewijalność; reszta: plik |
| `fuzz_hostile_io` | wejście łamiące kontrakt (zwraca więcej niż zażądano, INT64_MIN/MAX, błędy, kłamiący rozmiar i `seek`) | 8 B deklarowany rozmiar + skrypt zachowania |

## Uruchamianie lokalnie

```
make test-fuzz-replay                         # bez clanga
make fuzz-run CLANG=clang FUZZ_SECONDS=300    # z clangiem (libFuzzer wbudowany w clang)
```
Bez zainstalowanego clanga da się go użyć bez roota: `dnf download clang clang-libs compiler-rt llvm-libs clang-resource-filesystem` (tylko pakiety x86_64), `rpm2cpio | cpio -idm` do osobnego katalogu,
`LD_LIBRARY_PATH=<katalog>/usr/lib64`, `CLANG="<katalog>/usr/bin/clang -resource-dir <katalog>/usr/lib/clang/22"`.

## Wyniki

Pierwsze 4 minuty libFuzzera (2026-11-20): `fuzz_probe` 1,27 mln wywołań, `fuzz_hostile_io` 12,9 mln, `fuzz_container` 23 tys. (dekodery są wolne): bez błędów. `fuzz_open` w kilka minut znalazł **UB w `tml.h`**
(rzutowanie wskaźnika funkcji `int(*)(struct tml_stream_memory*,…)` na `int(*)(void*,…)`, wykryte przez `-fsanitize=function`, którego gcc nie ma; poprawione opakowaniem o dokładnym typie,
`third_party/tml/README-SAUTI.md`, `docs/upstream-patches/tml.patch`).

Kampania 6 × 10 min (`fuzz_open`, `fuzz_container`, 2026-11-20, po poprawce tml): **cztery nowe awarie `fuzz_open`**, wszystkie z tej samej rodziny (konwersja zmiennoprzecinkowa na liczbę całkowitą z wartością poza zakresem, wykrywana
przez clanga `-fsanitize=float-cast-overflow`, którego gcc nie ma): dr_flac (`inf` -> `uint64`, 3 razy) i stb_vorbis (`lookup1_values`). Naprawione nasycającymi konwersjami (F16, F17); wejścia zapisane w `fuzz/regression/open/` i odtwarzane w każdym CI
(`make test-fuzz-replay`).

## Ograniczenia

- Korpus początkowy jest mały (fixtury AAC i prawdziwe pliki FLAC/Ogg/MP3/WAV/AIFF/VOC/Opus/ALAC); dłuższe sesje z rozbudowanym korpusem i słownikiem etykiet formatów zwiększą pokrycie.
- Dekodery są wolne (`fuzz_container` ≈ 100 wykonań/s): wartościowe są długie przebiegi nocne, nie 90 s w CI.
- Brak MSan (niezainicjowane odczyty): ASan ich nie widzi; zostaje `-ftrivial-auto-var-init=pattern` w audycie.
