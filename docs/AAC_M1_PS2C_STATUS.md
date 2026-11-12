# AAC-M1 — PS-2c: zintegrowany dekorrelator

Status: **kod wypchnięty, oczekuje na wykonanie testów na Fedorze**.

Moduły:
- `include/sauti_aac_ps_decorrelator.h`
- `src/decoders/modern/aac/sauti_aac_ps_decorrelator.c`
- `tests/test_aac_ps_decorrelator.c`

`sauti_aac_ps_decorrelator_apply` łączy PS-2a i PS-2b:
1. PS-2a oblicza wzmocnienie tłumienia transjentów dla każdego slotu
   QMF i parametru PS (20 albo 34).
2. PS-2b tworzy zespolony sygnał zdekorrelowany na 71/91 pasmach.
3. Wynik PS-2b jest mnożony przez wzmocnienie odpowiadającego
   parametru z `schedule.hybrid_to_parameter[k]`.

Stan filtrów all-pass i statystyk energii jest aktualizowany atomowo.
Niepowodzenie PS-2b po pozytywnym PS-2a nie aktualizuje żadnego z dwóch
stanów. Odrzucane są błędne rozmiary i nakładające się bufory.
Warianty 20/34 są obsługiwane przez istniejący harmonogram hybrydowy.

`make test-aac-ps-decorrelator` porównuje z niezależnym wywołaniem
dwóch wcześniejszych etapów przez trzy kolejne ramki w obu układach.
Testuje także awarię w drugiej fazie i rollback całości.
Cel `test-aac-sanitize` zawiera odpowiedni wariant ASan/UBSan.

Komendy odbiorcze:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps-decorrelator PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

Uwaga: PASS testów integracji nie dowodzi jeszcze numerycznej
zgodności z normą na rzeczywistym strumieniu HE-AAC v2.
Przed oznaczeniem całego PS-2 jako zakończonego potrzebny jest
niezależny test wektorów fazowych, odpowiedzi impulsowych oraz
poprawności opóźnień i konwencji czasowych między ramkami.

Następny etap: podłączyć wyniki PS-1 i PS-2c do
`sauti_aac_ps_mix_complex_apply`, dodać odwrotną syntezę hybrydową,
a następnie wpiąć tor stereo do SBR QMF/PCM.
