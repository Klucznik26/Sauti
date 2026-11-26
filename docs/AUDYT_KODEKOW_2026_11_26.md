# Przegląd kodeków poza AAC (2026-11-26)

| Kodek | Moduł | Wynik | Uwagi |
|---|---|---|---|
| Ogg Vorbis I | `maktabasauti_vorbis.c` (własny) | zgodny z libvorbisfile do 3e-7 na 37 plikach z libvorbis, 90 + 90 strumieniach syntetycznych | patrz `docs/VORBIS.md` |
| MP3 | minimp3 (`sauti_decoder_mp3.c`) | 13 konfiguracji libmp3lame (8 – 48 kHz, mono / stereo, CBR / VBR / ABR, joint): różnica z FFmpeg `mp3float` ≤ 4,6e−5 (granica precyzji minimp3), długość równa | próbki powyżej pełnej skali, które FFmpeg zwraca bez obcięcia (do 1,28), Sauti obcina do ±1 (3 – 13 próbek na plik szumu): to jedyna różnica (`tools/mp3_sweep.py`) |
| FLAC | dr_flac (`maktabasauti_flac.c`) | plik 44,1 kHz stereo z libFLAC (przez FFmpeg): długość i próbki zgodne | libFLAC 14 jest w systemie jako wzorzec do dalszej kontroli (wielokanałowość, 24 bity, zmienne rozmiary bloków) |
| **Opus** | **brak** | plik libopus jest odrzucany („no codec recognised”) | plan: `docs/OPUS_PLAN.md` |
