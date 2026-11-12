# AAC-M1 / PS-3a — zintegrowany miks w domenie hybrydowej

Status: kod wypchnięty do `feature/aac-m1-ps-qmf`, testy Fedorze
jeszcze nieuruchomione.

Moduł `sauti_aac_ps_stereo_hybrid_apply` łączy:
- analizę 38-slotowego QMF przez pełny bank PS-1 (71/91 hybrydowych pasm),
- PS-2c: dekorrelator all-pass z tłumieniem transjentów,
- dotychczasowy `sauti_aac_ps_mix_complex_apply` (NASM):
  macierze zespolone IID/ICC/IPD/OPD w ramach planu PS.

Wyniki: osobne, zespolone ramki hybrydowe L i R w układzie
`[32][71]` lub `[32][91]`. **To nie są jeszcze 64-pasmowe
ramki QMF ani dwukanałowy PCM**. Bufory QMF, wyjść i stanu
muszą być rozdzielone. W przypadku błędu nie zostanie
zapisana żadna ramka wyjścia ani stan historii.
Również błędne / niefinitywne współczynniki macierzy
powodują odrzucenie.

Test `make test-aac-ps-stereo` porównuje wynik z trzema
osobnymi etapami (QMF, dekorrelacja, miksowanie) przez
trzy kolejne ramki. Weryfikuje też atomowość przy
NaN, zbyt krótkich buforach i błędnej macierzy.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps-stereo PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

Gates do prawdziwego HE-AAC v2:
1. PS-3b: odwrotna synteza hybrydowa (suma 6+2+2 lub
   12+8+4+4+4 do 3/5 pasm QMF i przesłanie pozostałych 61/59),
   test odtwarzania pasm i zgodności kierunków L/R.
2. PS-3c: podłączenie dwóch strumieni QMF do syntezy SBR,
   z rzeczywistą ciągłością ramek i obsługą lookahead 6 slotów.
3. PS-4: prawdziwy plik HE-AAC v2 -> stereo PCM, porównanie
   z niezależnym dekoderem i testy uszkodzonych wejść.
