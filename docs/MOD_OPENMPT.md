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

**Emulator Amigi (próba 11-25).** Zainstalowano Amiberry (`flatpak install --user flathub com.blitterstudio.amiberry`, w pakiecie ROM AROS). Kompilator m68k okazał się zbędny: z Aminet pobrano `BastyPlayer 1.00` (Sebastian Vater, m68k, wiersz poleceń, 100 % asembler, gra na gołym Paula; prawa zastrzeżone, więc tylko lokalnie), umieszczono go w katalogu będącym dyskiem startowym AROS z `S/Startup-Sequence` (`Utils/BastyPlayer -lt.mod -p -w1`). AROS startuje, program gra, a dźwięk trafia do pliku (`SDL_AUDIODRIVER=disk`). IPC emulatora (`/run/user/UID/amiberry.sock`, argumenty rozdzielane TABULATOREM) pozwala na `RESET`, `SCREENSHOT` samego okna emulatora i `QUIT`: narzędzie `scripts/amiberry_render_mod.py` robi cały cykl. Pułapka: wcześniej zrobiłem zrzut CAŁEGO pulpitu `import -window root` (złapał prywatne okna użytkownika; skasowany, nie powtarzać) — używać wyłącznie `SCREENSHOT` emulatora.

Wyniki pomiaru (moduł z jedną nutą, sinus):
* Wysokość dźwięku z Amiberry jest stale o **0,35 % niższa** niż z naszego odtwarzacza (stosunek 0,9964–0,9974 dla okresów 856 / 214 / 113 / 428), czyli to stała emulatora lub zegara, nie błąd zaokrąglenia okresu; nasza wartość zgadza się z wzorem `zegar/(okres·256)` (stosunek 0,999996).
* Czas między wierszami jest o **0,69 % dłuższy** (3,866 s zamiast 3,840 s dla 32 wierszy przy 125 BPM): BastyPlayer liczy takt po swojemu (nie ProTracker), więc skala czasu i wysokości różnią się i nie da się ich skorygować jednym przepróbkowaniem.
* Wniosek: BastyPlayer na emulowanym Paula jest TRZECIM niezależnym odtwarzaczem, ale nie ProTrackerem i nie wzorcem prawdy. Do rozstrzygnięcia spornych efektów (tremolo, rampa vibrato) potrzebny byłby prawdziwy ProTracker albo wiernie odtwarzający jego replayer (np. `ptplayer` Franka Wille'a) z własnym programem startowym; nie zrobione.
