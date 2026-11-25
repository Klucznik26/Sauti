# Odtwarzacz MOD wobec libopenmpt (11-25)

`scripts/compare_mod_openmpt.py` (wymaga `openmpt123`, numpy, `build/maktabasauti_cli`): małe syntetyczne moduły, po jednej grupie efektów, grane przez `maktabasauti_mod.c` i przez libopenmpt (`--stereo 200` = twarda panorama Amigi, `--filter 1`, `--ramping 0`, bez resamplera Amigi). Miara: znormalizowana korelacja przy najlepszym przesunięciu (±400 ramek), bez wzmocnienia (libopenmpt skaluje wyjście 2×, stałe).

**Jak czytać wynik.** To niezależna miara, NIE wzorzec prawdy: libopenmpt i ProTracker różnią się szczegółami, a nasz mikser to czyste „najbliższa próbka” (`floor(i·zegar/(okres·częstotliwość))`, sprawdzone modelem w numpy: zgodność co do próbki, poza dwoma wyzerowanymi bajtami PT 1.1). Dlatego sample-dokładnie porównuje się tylko treść niskoczęstotliwościowa (próbka sinusoidalna); szerokopasmowe próbki (szum, piła, kwadrat) różnią się fazą kroków i dają korelację 0,84-0,99 mimo identycznego modelu.

| Grupa | Korelacja |
|---|---|
| arpeggio, porta w dół, tone porta, 5xy/6xy, slajd głośności, Cxx, Fxx (prędkość i tempo), 9xx, E1x/E2x, E5x finetune, EAx/EBx, ECx, E9x, EDx, E6x pętla, EEx opóźnienie wzorca | 0,9999 - 1,0000 |
| vibrato sinus / kwadrat | 0,9998 / 0,9992 |
| vibrato rampa (E41), „losowe” (E43), tremolo (sinus, kwadrat, rampa), 8xx | 0,83 - 0,995: różnice zamierzone, patrz niżej |

Różnice, które są wyborem odtwarzacza ProTrackera, nie błędem:
* **Tremolo i vibrato od taktu 1** (tak jak PT; w takcie 0 głośność / okres bez modulacji). libopenmpt przesuwa fazę, stąd 0,994 przy tremolo.
* **Rampa tremolo patrzy na bit połowy pozycji vibrato** (znany błąd ProTrackera, odtworzony celowo).
* **Fala 3 (losowa) gra kwadrat**, jak ProTracker; libopenmpt losuje.
* **8xx** to rozszerzenie spoza ProTrackera 2.3 (panorama); inne prawo panoramy niż libopenmpt.
* **Koniec utworu przy skoku na odegrany wiersz**: nasz odtwarzacz kończy utwór (udokumentowane), libopenmpt gra dalej.

**Czego to nie rozstrzyga.** Gdzie libopenmpt i nasz odtwarzacz się różnią, nie wiadomo bez prawdziwego ProTrackera, które z nich jest wierne. Do tego służyłby emulator Amigi: zainstalowano Amiberry (`flatpak install --user flathub com.blitterstudio.amiberry`, zawiera ROM AROS `aros-rom.bin`; wyjście dźwięku do pliku: `SDL_AUDIODRIVER=disk`), ale brakuje odtwarzacza modułów w wierszu poleceń dla OCS i kompilatora m68k do zbudowania go (kandydat: `ptplayer` Franka Wille'a, `aminet.net/package/mus/play/ptplayer`; licencja do sprawdzenia, kod w asemblerze). Nie zrobione.
