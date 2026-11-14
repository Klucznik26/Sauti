# AAC-M1 — PS-2a: tłumienie transjentów (12.11.2026)

Status: **kod wypchnięty, oczekuje na testy Fedorze**. To jeden
z elementów dekorrelatora, NIE gotowy dekorrelator ani stereo PCM.

Moduł: `src/decoders/modern/aac/sauti_aac_ps_transient.c`
API: `include/sauti_aac_ps_transient.h`
Testy: `tests/test_aac_ps_transient.c`

Realizuje pętlę detekcji transjentów dla układu 20/34 pasm:
- Energia: suma kwadratów części rzeczywistej i urojonej próbek
  hybrydowych mapowanych na jedno pasmo parametrów.
- Wygaszany szczyt: `peak=max(0.76592833836465*peak,energy)`.
- Wygładzanie mocy i różnicy szczytu: współczynnik 0.25.
- Tłumienie: `min(1,power_smooth/(1.5*diff_smooth))`.
- Historia trzech wielkości przechodzi między ramkami i resetuje się
  przy zmianie trybu 20/34.
- Błędy, NaN/Inf i wadliwa mapa nie publikują częściowych wyników.

Równania i ich znaczenie kontrolowano względem FFmpeg n6.1
`libavcodec/aacps.c`. Własne testy nie potwierdzają jeszcze
numerycznej zgodności z prawdziwym dekoderem HE-AAC v2.
Przed dystrybucją sprawdzić pochodzenie danych/warunki licencyjne.

Test na Fedorze:

```sh
make test-aac-ps-transient PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

Następny etap PS-2b: normatywne opóźnienia 2/14/1 slotu,
rotacje fazowe i trzy łańcuchy all-pass, wraz z historią i
testami liczbowymi. PS-2c: połączenie detekcji transjentów
z dekorrelacją i pełnym bankiem 71/91.

Nie przełączać odtwarzacza na własny HE-AAC v2 przed
referencyjną weryfikacją stereo PCM.
