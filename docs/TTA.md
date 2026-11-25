# The True Audio (TTA 1.x) w MaktabaSauti

Stan: 2026-11-25. Moduł `src/decoders/modern/maktabasauti_tta.c` (kodek `tta`), napisany od zera w C. FFmpeg służył jako czarna skrzynka do porównań i producent strumieni testowych; algorytm (kolejność bitów, adaptacja Rice'a, filtr adaptacyjny, predyktor, dekorelacja) odtworzono z opisu formatu i własnej implementacji, bez przepisywania kodu.

## Zakres
* Plik `.tta` (format 1, TTA1), opcjonalny znacznik ID3v2 przed nagłówkiem. Nagłówek 22 B little endian (kanały 1 – 16, głębia 1 – 24 bity, częstotliwość do 1 MHz, liczba próbek, CRC32), tabela rozmiarów ramek, ramki (każda z CRC32 na końcu). Ramka = 256 · częstotliwość / 245 próbek na kanał, ostatnia krótsza.
* Dekoder ramki: kod Rice'a z dwoma adaptacyjnymi parametrami (k0 / k1), odwzorowanie parzysty / nieparzysty na znak, 8-współczynnikowy filtr adaptacyjny znak-znak (przesunięcie 10 / 9 / 10 dla 8 / 16 / 24 bit), predyktor pierwszego rzędu (15/16 dla 8 bit, 31/32 dla 16 i 24 bit), dekorelacja wielokanałowa. Ramki są niezależne (stan zerowany), więc seek jest dokładny.
* Wejście może być NIEPRZEWIJALNE i bez znanego rozmiaru (ramki leżą kolejno; nagłówek i tabela są czytane sekwencyjnie); seek wymaga wejścia przewijalnego. Pamięć: tabela ramek (12 B na ramkę) + jedna ramka skompresowana + PCM jednej ramki.
* Wyjście f32 jak w FFmpeg: 8 bit = (u8 − 128) / 128, 16 bit = s16 / 32768, 24 bit = wartość · 256 w s32 / 2^31.
* Rozpoznanie: sonda po `TTA1` (także za znacznikiem ID3v2), pewność 95.

## Weryfikacja
`make test-maktabasauti-tta` (ASan + UBSan): 15 plików z kodera FFmpeg `tta` z sygnałów syntetycznych (`tests/data/tta/`, `tests/data/make_tta_fixtures.py`: mono do 5.1, 8 / 16 / 24 bit, cisza, wartości skrajne, szum, 1 próbka, dokładnie jedna ramka, ramka + 1 próbka, 44,1 kHz); wzorzec = dekoder FFmpeg, wszystkie próbki IDENTYCZNE; odczyty po 1 / 3 B / 4097 / 100000 ramek, seek (granice ramek 8359 / 16718 / 46080, koniec, poza koniec), reset, wejście nieprzewijalne (pełne dekodowanie, seek = `E_NOT_SEEKABLE`), znacznik ID3v2, pliki urwane (ramki sprzed urwania poprawne, potem `E_TRUNCATED`), 1300 uszkodzonych plików. Poszerzona macierz poza testem (38 kombinacji: 8 – 192 kHz, 1 – 6 kanałów, 8 / 16 / 24 bit, długości 1 – 92161 próbek): zgodna co do bitu.

## Czego nie ma
* CRC32 (nagłówka, tabeli i ramek) nie jest sprawdzane (jak domyślnie w FFmpeg): uszkodzona ramka z poprawnym strumieniem bitów daje błędne próbki, a nie błąd; błędny strumień bitów daje `E_CORRUPT_DATA`.
* Format 2 (szyfrowany hasłem) i głębia powyżej 24 bit: `E_UNSUPPORTED_FEATURE`. TTA2 i TTA w kontenerze Matroska: brak.
* Prawdziwe pliki z oryginalnego kodera True Audio: brak pliku wzorcowego; zgodność wynika z opisu formatu i plików z kodera FFmpeg.
