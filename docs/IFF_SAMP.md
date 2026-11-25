# Amiga IFF SAMP — MaktabaSauti

Opis: Jim Fiore i Jeff Glatt (dissidents), 3 grudnia 1989. `FORM SAMP`, wymagany `MHDR`, fale w `BODY`. Kodek `amiga-samp`.

`NumOfChans` 0 znaczy, że nie ma `PlayMap`. Fale są grane po kolei, każda raz, na jednym kanale, z polem `Rate` (próbki na sekundę). `NumOfChans` inne niż 0: `E_UNSUPPORTED_FEATURE`, bo mapa wymaga nuty MIDI, a plik jej nie niesie.

`Format` to liczba istotnych bitów, od 8 do 28. 8 bitów to bajt ze znakiem (przykład w opisie ma wartość −90), wyjście /128. 9–16 bitów to słowo big-endian dosunięte w lewo, /32768. 17–28 bitów to długie słowo tak samo, /2147483648. Inny `Format`, `Rate` 0 albo powyżej 384 kHz, nieparzysty `WaveSize`: `E_UNSUPPORTED_FORMAT`. Kolejna fala z innym `Rate` gra w swoim tempie: odczyt staje na granicy, następny zwraca `E_FORMAT_CHANGED` i zero ramek, `get_info` podaje już nowe `Rate`, a kolejny odczyt wydaje próbki. Przewinięcie na falę o innym `Rate` robi to samo przed pierwszą próbką. Dekoder nie przelicza próbek na pierwszą częstotliwość.

`VelStart` 64 i 128: `E_UNSUPPORTED_FEATURE` (start zależy od prędkości nuty). Inna wartość niż 0, 64 i 128: `E_UNSUPPORTED_FORMAT`.

Pętla (`LoopStart` / `LoopEnd`) trwa do puszczenia nuty. Nuty nie ma, więc grane są zapisane próbki, jeden raz. `ATAK` i `RLSE` to głośność nuty, `FATK` i `FRLS` to filtr, którego opis nie przypisuje Amidze, a `USER` (`VOLMOD` / `PERMOD`) to modulacja sprzętowa bez wzoru PCM: te bajty są pomijane. Bit 0 pola `Flags` oznacza kolejną dyskietkę; ten plik gra fale, które ma.

Drugi `MHDR` albo drugi `BODY`: `E_UNSUPPORTED_FORMAT`. `BODY` przed `MHDR` bez przewijania: `E_NOT_SEEKABLE`. Plik urwany: całe próbki, potem lepki `E_TRUNCATED`. Sonda 60 tylko dla `FORM` oraz `SAMP`.
