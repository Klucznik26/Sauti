# Amiga IFF 16SV (PCM 16-bit) — MaktabaSauti

Moduł strumieniowy `src/decoders/retro/amiga/maktabasauti_16sv.c` (kodek `amiga-16sv`, kontener `iff`). Pamięć nie zależy od długości pliku (bufor 4 KiB na kanał).

## Format
`FORM <rozmiar> 16SV` ma te same chunki co 8SVX. `BODY` to próbki ze znakiem, 16 bit, big-endian. `CHAN` 6 = stereo (cały lewy, potem cały prawy); 2 albo 4 = jeden kanał. Inne chunki są pomijane. Wyjście f32 = próbka / 32768.

`ctOctave` 0 albo 1: gra całe `BODY` jeden raz. `oneShotHiSamples` i `repeatHiSamples` są wtedy metadanymi pętli, nie cięciem. `ctOctave` > 1: ta suma jest długością najwyższej oktawy i tylko ona gra. Suma 0 przy więcej niż jednej oktawie: `E_UNSUPPORTED_FEATURE`.

`VHDR.volume` nie skaluje próbek (0x10000 to pełna głośność urządzenia).

## Odkładam
* `sCompression` 1 i 2 (`E_UNSUPPORTED_FEATURE`). SoundFX nazywa je FDPCM16_6 (Fibonacci, stosunek 8:3) i EDPCM16_5 (wykładnicza, 16:5). Nie publikuje tablicy delt ani układu bitów.
* `CHAN` poza 2, 4 i 6 (`E_UNSUPPORTED_FEATURE`). SoundFX wymienia quadro, a opublikowany `CHAN` nie podaje wartości ani układu czterech kanałów.

Brak `VHDR` albo `BODY`, częstotliwość 0, pusty `BODY`, plik inny niż 16SV: `E_UNSUPPORTED_FORMAT`. Stereo bez przewijania: `E_NOT_SEEKABLE`. Plik urwany: całe próbki sprzed urwania, potem lepki `E_TRUNCATED`.

## Weryfikacja
`tests/test_decoder_16sv.c` buduje pliki w pamięci (mono, stereo, nieparzysty `NAME`, oktawy, odmowa kompresji i obcego `CHAN`, urwane `BODY`, odczyt dłuższy niż bufor 4 KiB). Wzorzec to opis 16sv.datatype i SoundFX, nie zrzut cudzego dekodera.
