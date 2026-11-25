# Apple Lossless (ALAC) w MaktabaSauti

Stan: 2026-11-25. Moduł `src/decoders/modern/maktabasauti_alac.c` (kodek `alac`), napisany od zera w C, bez kodu referencyjnego Apple ani FFmpeg (algorytm z opisu formatu; FFmpeg służył wyłącznie jako czarna skrzynka do porównań i producent strumieni testowych). Rejestr: `mks_legacy.c`, tuż po modułach AAC (sonda MP4 AAC ma pewność 60 i jej `open` odrzuca ALAC, sonda ALAC ma 50, a 85, gdy w prefiksie widać `alac`).

## Zakres
* Kontener MP4 / M4A / M4B z wpisem `alac` w `stsd` (magic cookie w pudełku `alac`, także w `wave`; wpis audio v0 i v1). Plik jest INDEKSOWANY, nie wczytywany: czytane jest tylko `moov`, w pamięci zostaje lista pakietów (12 B na pakiet, do 4 mln). Wejście musi być przewijalne (`moov` bywa na końcu): wejście nieprzewijalne daje czyste `E_NOT_SEEKABLE`.
* Kanały 1 – 8 (SCE / CPE / LFE; DSE i FIL pomijane), kolejność wyjściowa jak FFmpeg (FL FR FC, dalej tablica `map_tab`), głębia 16 / 20 / 24 / 32 bit (przesunięcie bajtowe `bytes shifted`, bity młodsze), ramka 1 – 65536 próbek, pakiety verbatim (escape), niepełny ostatni pakiet.
* Algorytm: adaptacyjny Golomb-Rice z trybem serii zer, predyktor adaptacyjny znak-znak LMS (rząd 0 – 31), macierz stereo `mixBits` / `mixRes`. Pakiety są niezależne, więc seek jest dokładny i tani.
* Wyjście f32 = próbka / 2^(głębia − 1) (dla 32 bitów f32 gubi najmłodsze bity).

## Weryfikacja
`make test-maktabasauti-alac` (ASan + UBSan): 18 plików z kodera FFmpeg `alac` z sygnałów syntetycznych (`tests/data/alac/`, `tests/data/make_alac_fixtures.py`: mono do 7.1, 16 i 24 bit, poziom kompresji 0 – 2, rzędy predykcji 1 i 30, cisza (serie zer), wartości skrajne, 1 ramka, 8 kHz – 192 kHz), wzorzec = dekoder FFmpeg, wszystkie próbki IDENTYCZNE; odczyty po 1 / 3 B / 4097 / 100000 ramek, seek (granice pakietów, koniec, poza koniec), reset, wejście nieprzewijalne, 1300 uszkodzonych plików (bez awarii, wycieków i nadmiernej pamięci). Poszerzona macierz (poza testem, ~40 kombinacji z różnymi długościami 1 – 8193 ramek, częstotliwościami i trzema poziomami kompresji): także zgodna.
Mutanty (15 zbadanych: mapowanie znaku, aktualizacja historii, seria zer, macierz stereo, znak współczynników, bity młodsze, mapowanie 5.1, zaokrąglenie mianownika, granica reszty, rozszerzenie znaku escape, kolejność długości niepełnej ramki, współczynnik pb, granica k, rozgrzewka): zabite.

## Czego nie ma
* Głębia 20 i 32 bit: kod jest, ale koder FFmpeg tego nie tworzy, więc brak strumienia i wzorca (tylko test kodu ścieżek wspólnych).
* Prawdziwy plik z iTunes / Apple Music (kodery Apple używają szerszego zestawu parametrów niż FFmpeg): brak pliku wzorcowego; zgodność wynika z opisu formatu i testów na plikach FFmpeg.
* Tryb predykcji ≠ 0 (nigdy nieużywany przez kodery), elementy CCE / PCE, CAF, fMP4, lista edycji i iTunSMPB (ALAC nie ma opóźnienia kodera).
