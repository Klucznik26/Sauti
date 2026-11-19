# Sauti — notatka przekazania prac (sesja 14.11.2026)

## Gdzie zatrzymaliśmy się

- **Repozytorium:** `Klucznik26/Sauti`.
- **Gałąź robocza:** `feature/sbr74-qmf-avx2-dispatch`.
- **Ostatni sprawdzony commit przed tą notatką:** `16c079e66b420f42cb195ed870c709e1d212aaf2`.
- **PR #6:** https://github.com/Klucznik26/Sauti/pull/6
- **Gałąź docelowa PR:** `feature/aac-family-asm` (nie `main`).
- **Status:** PR otwarty, **niescalony**. Przerwaliśmy pracę po udanym teście migracji P/E na laptopie i po zaliczeniu CI. Nie wprowadzamy kolejnych zmian w tej sesji.

**Zakres zamkniętego etapu:** SBR-74 — optymalizacja 64-pasmowej syntezy QMF. Nie oznacza to zakończenia całego kodeka AAC-LC/HE-AAC ani jego zgodności ze wszystkimi plikami.

## Co zostało wykonane

1. Dodano jądra QMF **AVX2** dla modulacji zespolonej oraz okna polifazowego; zachowano **SSE2** i przenośny **C11**. Nie używamy FMA, aby ograniczyć różnice numeryczne.
2. Naprawiono odczyt niewyrównanych współczynników w oknie SSE2 (`movups` zamiast niebezpiecznego pamięciowego `mulps`). Dodano testy niewyrównanych buforów i różnicowe SSE2/AVX2.
3. Dla Intel hybrid wprowadzono wybór na podstawie CPUID **bieżącego rdzenia**: P → AVX2, E → SSE2; nieznany typ hybrydowy → SSE2. Na innych CPU obsługujących AVX2 zachowujemy dotychczasową politykę. Działają też opcje wymuszające wariant.
4. Dodano testy polityki wyboru, probe aktualnej ścieżki, benchmark jednej 40-slotowej ramki, porównanie 2560 próbek PCM oraz **16-ramkowy test ciągłości przy migracji P/E** (40 960 próbek).
5. Dodano do GitHub Actions testy GCC i Clang. Eksperyment z pełnym rozwinięciem pętli AVX2 odrzucono: zysk około 1% na runnerze CI nie uzasadniał dodatkowego kodu.
6. W Makefile wyeliminowano także podwójne linkowanie obiektów AAC w integracji SDL2/PipeWire.

## Potwierdzone wyniki

- **GitHub Actions dla `16c079e`:** `Sauti ASM core` **PASS** (run `43650240229`), `Native AAC-29 conformance` **PASS** (run `43650240231`). Obejmują m.in. testy C11/SSE2/AVX2, zgodności PCM, sanitizery, SDL2 i PipeWire.
- **CI, 16 ramek:** w runnerze bez dostępnej pary P/E migracja została oznaczona jako **SKIP**, ale test parytetu 40 960 próbek **PASS** (RMS `1.389946e-08`, maksimum `1.192093e-07`). To nie jest potwierdzenie migracji na runnerze.
- **Laptop, Intel Core i7-12700H, Fedora:** probe potwierdził P-Core `0x40` → AVX2, E-Core `0x20` → SSE2. Testy czterech wariantów wyboru **PASS**.
- **Laptop, pojedynczy pomiar pełnej ramki:** P-Core CPU 4: auto/AVX2 `26.8 µs`, SSE2 `28.6 µs`. E-Core CPU 12: auto/SSE2 `48.4 µs`, SSE2 `48.1 µs`. Nie uogólniać wyników na wszystkie CPU.
- **Laptop, faktyczna migracja — PASS:** `make test-aac-qmf-migration` wykonał 8 ramek na P-Core CPU 0 i 8 na E-Core CPU 12, zarówno dla auto, jak i dla wymuszonego SSE2. Porównanie **40 960 próbek PASS**, RMS `9.88395e-09`, maksimum `1.192093e-07` (próbka 2647).

## Od czego zacząć następnym razem

1. Sprawdzić aktualny stan gałęzi i PR #6; nie zakładać, że prace zostały już scalone.
2. Następnie zweryfikować **rzeczywiste pliki HE-AAC**, najlepiej na reprezentatywnych próbkach mono/stereo i z PS. Porównać PCM przed PS z referencją oraz odporność na uszkodzone lub ucięte dane. Dotychczasowy test QMF nie potwierdza jakości i zgodności całego dekodera.
3. Zmierzyć pełny tor HE-AAC, nie tylko syntezę QMF, na sprzęcie docelowym. W szczególności pozostaje pomiar na **Ryzenie 5 7600X**; dotychczasowe pomiary użytkownika wykonano na laptopie i7-12700H.
4. Dopiero po ocenie zgodności pełnego dekodera i przeglądzie diffu zdecydować o scaleniu PR #6 do `feature/aac-family-asm`. **Nie scalać automatycznie**.

## Polecenia do wznowienia

```bash
cd ~/Sauti
git status --short
git branch --show-current
git fetch origin
git switch feature/sbr74-qmf-avx2-dispatch
git pull --ff-only
make test-aac-qmf-dispatch test-aac-qmf-simd test-aac-qmf-frame-parity
make test-aac-qmf-migration      # na laptopie P/E: BEZ taskset
make bench-aac-qmf-frame         # tylko lokalny mikrobenchmark syntezy
```

**Ważne:** przed `git switch` należy sprawdzić lokalne zmiany. Nie nadpisywać niezacommitowanych plików. Dokumentacja techniczna szczegółów: `docs/SBR74_QMF_SIMD.md`, a historia bieżącej pracy: PR #6.
