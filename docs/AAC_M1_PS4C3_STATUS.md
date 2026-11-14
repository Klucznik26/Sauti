# AAC-M1 PS-4c3 — pierwszy dodatni test z pełnymi ramkami ADTS+SBR+PS

Status: test wypchnięty na gałąź `feature/aac-m1-ps-qmf`.
**Nie został jeszcze uruchomiony na Fedorze; wynik nieznany.**

W istniejącym `tests/test_aac_he_adts_ps.c` rozbudowano
test negatywny PS-4c2 o **trzy kolejne ramki prawdziwej składni
bitowej**, tworzone niezależnym, dotychczasowym generatorem
SBR-30/SBR-31:
- ADTS 24 kHz mono AAC-LC SCE + FIL typu 13
  zawierający entropijnie kodowane SBR 48 kHz;
- `bs_extended_data=1`, 3 bajty `EXTENSION_ID_PS=2`,
  nagłówek IID mode0, jeden env, 10 pasm IID;
- pierwszy nagłówek SBR jawny, kolejne ramki korzystają z
  ponownego użycia nagłówka i entropijnej historii;
- geometria HF z kompletnym pokryciem pasm, bez sztucznego
  dopisywania niezrekonstruowanych danych QMF;
- sprawdzenie 32+6 lookahead: pierwsza ramka musi być
  zachowana bez PCM, dwie następne powinny dać
  po 4096 próbek L/R z poprzedniej ramki;
- sprawdzenie skończoności float, nieprzekroczenia bufora
  i zachowania wszystkich historii po uszkodzonym ADTS.

Test używa **prawdziwej składni bitstreamu**, ale jest
wygenerowany wewnętrznie, a nie przez niezależny enkoder.
SCE jest syntetycznie cichy, przy jawnej SBR noise-floor.
Nie udowadnia jakości PCM ani normatywnego opóźnienia.

Komendy:
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-he-adts-ps PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

Następna bramka:
- po dodatnim PASS: niezależny strumień HE-AAC v2
  zakodowany np. FDK-AAC z wieloma ramkami, sygnałem
  stereo o różnych kanałach, zmianami parametrów PS;
- porównać z FFmpeg po świadomym wyrównaniu opóźnień,
  sprawdzić kanały, energię, korelację, artefakty,
  ataki i zakończenie strumienia;
- rozwiązać szczegółową normatywną oś czasu SBR/FIXVAR
  oraz poprawny EOS drain i inne warianty sygnału.

**Uwaga:** ani PS-4c2, ani ten dodany test nie mogą być
nazwane 'zweryfikowanym dekoderem HE-AAC v2' przed
pozytywnym wykonaniem i niezależną kontrolą jakości.
