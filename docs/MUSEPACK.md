# Musepack SV7 i SV8 — MaktabaSauti

Dekoder jest oryginalny. Tablice Huffmana, współczynniki requantyzacji, okno syntezy, generator pasma −1 i liczby kombinatoryczne SV8 są z libmpcdec r475 (zespół Musepack). To jest opis formatu, na który jest zgoda. Ciała funkcji z libmpcdec i z FFmpeg nie są wklejone.

Wzorzec próbek to float z libmpcdec r475, nie synteza stałoprzecinkowa i nie dekoder `mpc7` ani `mpc8` z FFmpeg. Pliki SV7 zakodował mppenc 1.16. Pliki SV8 zakodował mpcenc z tego samego r475 (magia `MPCK`). `tests/data/mpc/*.f32` to surowe float32 z libmpcdec. `make test-mpc` porównuje je bitowo i nie potrzebuje ani kodera, ani libmpcdec. Na pięciu plikach SV7 i czterech SV8 maksymalny moduł różnicy wyniósł 0.

## SV7

Strumień `MP+`, wersja w dolnym półbajcie równa 7. SV7 jest tylko stereo. mppenc plik mono zapisuje jako dwa kanały. Górny półbajt magii (PNS) jest w nagłówku i ścieżka SV7 go nie używa. Bit intensity stereo jest czytany i pomijany: referencja nie ma dla niego osobnego wzoru.

Bity po czterech bajtach magii to słowa little-endian czytane od najwyższego bitu. Nagłówek ma 168 bitów. Dalej, bez wyrównania do bajtu, każda ramka ma 20-bitową długość, ciało i — tylko gdy demukser libmpcdec dolicza je z góry — 11 bitów liczby próbek ostatniej ramki. Długość 20-bitowa tych 11 bitów nie zawiera. Niezgodność liczby zużytych bitów z zapisaną długością to `E_CORRUPT_DATA`.

## SV8

Kontener `MPCK`. Po magii są pakiety: dwa bajty klucza `A`–`Z` i rozmiar w kawałkach po 7 bitów. Rozmiar obejmuje klucz i własne bajty. `SH` jest obowiązkowe. CRC-32 jest takie jak w PNG i liczone z ładunku `SH` bez czterech bajtów samej sumy. Zła suma to `E_CORRUPT_DATA`. Wersja w `SH` musi być 8. Częstotliwość to 44100, 48000, 37800 albo 32000. Najwyższe pasmo jest z zakresu 1..31. `block_pwr` to trzy bity razy dwa, a pakiet `AP` niesie `1 << block_pwr` ramek. Pierwsza ramka pakietu jest ramką kluczową.

`RG`, `EI`, `SO`, `ST` i każdy inny klucz `A`–`Z` są przeskakiwane po rozmiarze. Wartości ReplayGain nie są stosowane. Tablica seek nie jest potrzebna, bo seek dekoduje od początku z pamięci podręcznej. `SE` kończy strumień. `AP` jest czytany ponownie przy pierwszej ramce, tak jak w demukserze libmpcdec.

Ciało ramki czyta bity od najwyższego w bajcie. Rozdzielczości idą od najwyższego użytego pasma w dół, osobno lewy i prawy, także gdy wyjście jest mono: strumień i tak ma oba kanały, a synteza mono pisze tylko lewy ze skokiem 1. Mid-side jest przed obcięciem, tą samą funkcją co SV7. Współczynniki skali SV8 są inne niż drabina SV7 i trzymają się w poprzedniej ramce. Pasmo −1 używa tego samego generatora co SV7.

Wyjście mono to jedna próbka na ramkę. Maska kanału to środek. Stereo to lewy i prawy. Inna liczba kanałów (pole 4 bity plus jeden, inne niż 1 i 2) to `E_UNSUPPORTED_FEATURE`: libmpcdec taki strumień odrzuca, a mapy kanałów specyfikacja nie podaje.

## Długość i seek

Ramka ma 1152 próbek (36×32). Opóźnienie syntezy to 481 próbek, więc pierwsza wydana porcja ma 671, gdy `beg_silence` jest zerem. W SV8 `samples_to_skip` to 481 plus `beg_silence`. Cztery pliki testowe mają `beg_silence` równe zero. `total_frames` to liczba próbek słyszalnych. Koniec jest wtedy, gdy przed kolejną ramką nie zostało już próbek. SV8 nie czyta 11-bitowego pola ostatniej ramki.

Wyjście to `F32` w skali libmpcdec, bez dodatkowego mnożenia przez 4. Flaga stratna, VBR, przycięcie gapless. `priming_frames` wynosi 481 plus `beg_silence`. Profil w informacji o strumieniu to `SV7` albo `SV8`, kontener `mpc`.

Seek przewija stan i dekoduje od początku z pamięci podręcznej pliku. Nie woła `seek` hosta. `frame == total_frames` jest dozwolone. Większy cel to `E_OUT_OF_RANGE` i stan zostaje.

Sonda zwraca 90 dla `MP+` z wersją 7 i dla `MPCK`. Prefiks ID3v2 jest przeskakiwany, gdy cały tag mieści się w buforze sondy, flagi mają dolny półbajt 0, a rozmiar jest syncsafe.

## Odkładam

* SV4, SV5 i SV6 — inna magia wersji, `E_UNSUPPORTED_FORMAT`. Ta ścieżka ich nie zgaduje.
* Mapę kanałów poza mono i stereo.
* Wzmocnienie ReplayGain. Zapisane liczby, także zero „nie policzono”, nie są stosowane.
