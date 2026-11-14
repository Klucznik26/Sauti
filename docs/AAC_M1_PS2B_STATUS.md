# AAC-M1 — PS-2b: dekorrelator all-pass (2026-11-12)

Gałąź: `feature/aac-m1-ps-qmf`. **Kod wypchnięty; wymagane uruchomienie testów Fedorze.**

Nowe moduły:
- `include/sauti_aac_ps_decorrelate.h`
- `src/decoders/modern/aac/sauti_aac_ps_decorrelate.c`
- `tests/test_aac_ps_decorrelate.c`

Dla układu 20 parametrów przetwarzane jest 71 pasm hybrydowych,
w tym all-pass 0–29, delay 14 na 30–41 oraz delay 1 na 42–70.
Dla 34 parametrów: 91 pasm, all-pass 0–49, delay 14 na
50–61, delay 1 na 62–90. Każdy link all-pass zawiera
rotację fazową i sprzężenie o odpowiednio 3, 4, 5 slotach;
wejście all-pass ma opóźnienie 2 slotów. Współczynniki
oparte są na tabelach i algorytmie FFmpeg n6.1
(`aacps_tablegen.h`, `aacpsdsp_template.c`, `aacps.c`).

Historia 14 próbek sygnału i po pięć próbek na link all-pass
przechodzi między ramkami. Przy zmianie 20↔34 jest zerowana.
Przetwarzanie jest atomowe; wejście, stan i wyjście nie mogą
zachodzić na siebie. Ten etap **nie stosuje jeszcze**
współczynników tłumienia transjentów z PS-2a.

Testy:
```sh
make test-aac-ps-decorrelate PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

**Warunek odbioru:** PASS obu zestawów, testy impulsowe opóźnień,
kontynuacja między ramkami, testy porównawcze fazy i amplitudy
względem niezależnego dekodera. Sam test impulsowy nie stanowi
potwierdzenia pełnej zgodności numerycznej.

**Następny etap PS-2c:** połączyć wyjście PS-1 (71/91), historię
dekorrelatora PS-2b i tłumienie PS-2a, potem podać wynik do
istniejącego miksera macierzy PS. Przed rzeczywistym stereo PCM
wymagane pozostają dalsza integracja i testy bitstreamowe.
