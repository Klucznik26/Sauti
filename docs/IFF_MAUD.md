# Amiga IFF MAUD — MaktabaSauti

Opis Richarda Koerbera (autor `multisnd.library`, MacroSystem), 8 sierpnia 1997. `FORM MAUD`. Wymagane chunki: `MHDR` (32 bajty) i `MDAT`. `ANNO`, `AUTH`, `(c)` i `NAME` są pomijane.

`MHDR`: `mhdr_Samples` (ULONG, liczba ramek; Samplitude zapisuje tu śmieci, więc długość bierze się z rozmiaru `MDAT`), `mhdr_SampleSizeC` (UWORD, bity w pliku), `mhdr_SampleSizeU` (UWORD, bity po dekompresji; tryb kompresji i tak to mówi), `mhdr_RateSource` (ULONG) / `mhdr_RateDevide` (UWORD) = Hz. Reszta z dzielenia, dzielnik 0, częstotliwość 0 albo powyżej 384 kHz: odmowa. Potem `mhdr_ChannelInfo`, `mhdr_Channels`, `mhdr_Compression` i trzy zarezerwowane ULONG, ignorowane przy czytaniu.

`MCOMP_NONE` (0): bity od najwyższego, bez dopełniania do bajtu. Dokładnie 8 bitów jest bez znaku, `(v − 128) / 128`. Każda inna szerokość od 1 do 32 jest ze znakiem (dopełnienie do dwóch), big-endian; 16 bit to `v / 32768`. Ramka to po jednej próbce z każdego kanału, po kolei. Niepełna ostatnia ramka w całym `MDAT` nie jest wydawana.

`MCI_MONO` (0) to jeden kanał, `MCI_STEREO` (1) dwa. `MCI_MULTIMONO` (2) to co najmniej dwa. `MCI_MULTISTEREO` (3) to parzysta liczba od 4. Powyżej 32 kanałów: `E_UNSUPPORTED_FEATURE`. Sprzeczność `ChannelInfo` z liczbą kanałów: `E_UNSUPPORTED_FORMAT`. Maska głośników jest ustawiona tylko dla mono i dla `MCI_STEREO`.

`MCOMP_8SIGNED` (11, nieoficjalne): 8 bitów **ze znakiem** zamiast bez znaku, `v / 128`. Wprowadza go program `ma2s` Lorence'a Lombardo (Aminet `mus/edit/ma2s`) jednym zdaniem: „8bit IFF-MAUD signed … a compression ID of 11”, więc inna szerokość kodu: `E_UNSUPPORTED_FEATURE`. Sprawdzone tylko testem jednostkowym (brak pliku z programu: patrz niżej).

`MCOMP_ALAW` (2) i `MCOMP_ULAW` (3): publiczny G.711, ten sam wzór co w AU, przy 8 bitach kodu w pliku. Wyjście to wartość liniowa / 32768. Inna szerokość kodu: `E_UNSUPPORTED_FEATURE`.

## Odkładam

* `MCI_MULTICHANNEL` (4) i chunk `MINF`, bo autor oznacza je jako przyszłe i nie podaje układu.
* `MCOMP_FLOAT` (9, nieoficjalne, `aw2m` tego samego autora): odmowa. Ani szerokość (32 / 64), ani kolejność bajtów, ani rodzaj liczby (IEEE czy FFP; ten sam autor ma też narzędzie `32toFFP`) nie są nigdzie podane; `aw2m` 1.1 zawiesza się pod AROS w emulatorze (nie zapisuje nic, nawet z `>NIL:`), więc nie dało się tego zmierzyć. Wymaga prawdziwego pliku albo działającego `aw2m`.
* `MCOMP_FIBDELTA` (1), `MCOMP_ADPCM2`..`MCOMP_ADPCM5` (4..7) i `MCOMP_LONGDAT` (8), bo autor pisze, że biblioteka ich nie obsługuje i nie zna programu, który je zapisuje. Układ bitów nie jest podany poza nazwą.
* Pętlę, bo ten opis nie ma chunka pętli. SoundFX mówi tylko, że pętla w przód istnieje.

Wejście przeplatane, więc wiele kanałów gra bez przewijania. Drugi `MHDR` albo drugi `MDAT`: `E_UNSUPPORTED_FORMAT`. `MDAT` przed `MHDR` na wejściu bez przewijania: `E_NOT_SEEKABLE`. Plik urwany: całe ramki sprzed urwania, potem lepki `E_TRUNCATED`. Sonda `FORM` + `MAUD` ma pewność 60.
