# Atari Falcon DVSM: konwersja pełnego utworu do Sauti

Konwerter `tools/wav_to_dvsm.py` przyjmuje WAV, FLAC, MP3 lub inny plik audio
odczytywany przez FFmpeg. Zapisuje **WinRec-style DVSM** (`.dvs`):
nieskompresowane 16-bitowe PCM, stereo, big-endian, 49 170 Hz.

Nie modyfikuje oryginału i nie nadpisuje pliku wynikowego bez `--overwrite`.
Przesyła dane strumieniowo — może przetworzyć cały długi utwór.

## Fedora

```bash
sudo dnf install ffmpeg-free
cd "/home/marek/AYO Projekty/LAS/apps/sayari-sauti"
python3 tools/wav_to_dvsm.py "$HOME/Muzyka/utwor.wav" "$HOME/Muzyka/utwor_falcon.dvs"
bash ./Sauti
```

Otwórz `utwor_falcon.dvs` w Sauti i naciśnij Play. W konsoli powinno
pojawić się `codec=atari-falcon-dvsm-pcm, 49170 Hz, 2 ch`.

To **konwersja danych PCM do formatu Atari Falcon**, nie emulacja
przetworników analogowych Falcona, DSP lub oprogramowania studyjnego.
Nowoczesne nagranie przekonwertowane do DVSM nie jest historycznym nagraniem
pochodzącym z Atari.
