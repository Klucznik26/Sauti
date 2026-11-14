# AAC-M1 — PS-3b: odwrotna synteza hybrydowa 71/91 → 64 QMF

Status: **kod wypchnięty; testy na Fedorze nieuruchomione**.

Moduły:
- `include/sauti_aac_ps_hybrid_synth.h`
- `src/decoders/modern/aac/sauti_aac_ps_hybrid_synth.c`
- `tests/test_aac_ps_hybrid_synth.c`

Zgodnie z referencyjnym `hybrid_synthesis` FFmpeg n6.1:
- PS 20: zsumowanie `hybrid[0..5]` w QMF0,
  `[6..7]` w QMF1, `[8..9]` w QMF2; hybrydowe
  `[10..70]` są kopiowane do QMF3..63.
- PS 34: zsumowanie `[0..11]`, `[12..19]`,
  `[20..23]`, `[24..27]`, `[28..31]`
  w QMF0..4; hybrydowe `[32..90]` są kopiowane
  do QMF5..63.
- 32 sloty na ramkę, każda próbka zespolona `float re/im`.

Interfejsy jedno- oraz dwukanałowy odrzucają błędne wymiary,
schemat PS, NaN/Inf, sumę poza zakresem float, oraz nakładanie się
wejść i wyjść. Oba kanały publikuje się dopiero po poprawnym
obliczeniu całości. Synteza nie ma własnej historii — przechowuje
ją bank analizy PS-1.

Nowy `make test-aac-ps-hybrid-synth` obejmuje sumy niskich pasm,
identyczne kopiowanie wyższych pasm, dokładny routing L/R,
atomowy rollback również przy błędzie drugiego kanału,
niewystarczające bufory i aliasing. Test jest włączony do
`test-aac-native` oraz `test-aac-sanitize`.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps-hybrid-synth PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

**Granica etapu:** 64 zespolone pasma QMF w obu kanałach,
nie stereo PCM. PS-3c wymaga podłączenia do dwukanałowej
syntezy SBR QMF, z zachowaniem historii, opóźnienia i kolejności
ramek. Rzeczywisty bitstream HE-AAC v2 oraz zgodność
numeryczna z niezależnym dekoderem pozostają osobnym testem PS-4.
