# Przegląd kodeków poza AAC (2026-11-26)

| Kodek | Moduł | Wynik | Uwagi |
|---|---|---|---|
| Ogg Vorbis I | `maktabasauti_vorbis.c` (własny) | zgodny z libvorbisfile do 3e-7 na 37 plikach z libvorbis, 90 + 90 strumieniach syntetycznych | patrz `docs/VORBIS.md` |
| MP3 | minimp3 (`sauti_decoder_mp3.c`) | 13 konfiguracji libmp3lame (8 – 48 kHz, mono / stereo, CBR / VBR / ABR, joint): różnica z FFmpeg `mp3float` ≤ 4,6e−5 (granica precyzji minimp3), długość równa | próbki powyżej pełnej skali, które FFmpeg zwraca bez obcięcia (do 1,28), Sauti obcina do ±1 (3 – 13 próbek na plik szumu): to jedyna różnica (`tools/mp3_sweep.py`) |
| FLAC | dr_flac (`maktabasauti_flac.c`) | 70 / 70 plików bit w bit (1 – 8 kanałów, 8 – 192 kHz, 16 / 24 b, kompresja 0 – 12); **luka: >2 kanały były odrzucane, naprawione** | patrz niżej, `tools/flac_sweep.py` |
| **Opus** | **brak** | plik libopus jest odrzucany („no codec recognised”) | plan: `docs/OPUS_PLAN.md` |


## FLAC: wielokanałowość (2026-11-26, popołudnie)
**Znaleziona luka:** moduł `maktabasauti_flac.c` odrzucał pliki FLAC o więcej niż 2 kanałach (`E_UNSUPPORTED_FORMAT`, w CLI „no codec recognised the data”), choć dr_flac i format dopuszczają 1 – 8. Dwa miejsca (`ch > 2u` w sondzie STREAMINFO i po otwarciu dr_flac) podniesione do 8, maski kanałów wg specyfikacji FLAC (3: L R C; 4: FL FR BL BR; 5: FL FR FC BL BR; 6: 5.1; 7: FL FR FC LFE BC SL SR; 8: 7.1).
**Weryfikacja:** `tools/flac_sweep.py` (FLAC bezstratny, więc wzorcem jest sygnał źródłowy): 70 plików (1 – 8 kanałów, 8 – 192 kHz, 16 i 24 bity, poziomy kompresji 0 – 12, bloki 256 – 16384, sygnały: szum, tony, cisza, prostokąt, niskie bity, impulsy, chirp) = **70 / 70 identycznych co do bitu** (przed zmianą 20 / 70: wszystkie pliki 3+ kanałowe odrzucone). Test pamięci `test_maktabasauti_flac_stream` ma teraz limit 8 MiB (najgorszy przypadek 8 kanałów × 65535 próbek: 5,9 MB).
**Granice:** maska kanałów z tagu `WAVEFORMATEXTENSIBLE_CHANNEL_MASK` (komentarz Vorbis) nie jest czytana (nietypowe układy dostają układ domyślny wg liczby kanałów); 32-bitowy FLAC nie sprawdzony (FFmpeg koduje go tylko eksperymentalnie).


## WAV / AIFF: wielokanałowy AIFF i ADPCM (2026-11-26, popołudnie)
`tools/wav_aiff_sweep.py` (wobec FFmpeg, 103 pliki: u8 / s16 / s24 / s32 / f32 / f64, A-law, mu-law, MS ADPCM, IMA ADPCM, warianty AIFF / AIFF-C, 1 – 8 kanałów, 8 – 96 kHz). Przed zmianą 44 z 103 odrzucone:
* **AIFF / AIFF-C z więcej niż 2 kanałami** (moduł `maktabasauti_pcm.c` miał `channels > 2` tylko w AIFF; WAV i AU do 32): limit 32 jak w WAV; wszystkie warianty zgodne z FFmpeg do 1e-6.
* **ADPCM w WAV** (Microsoft 0x0002, IMA / DVI 0x0011, także w WAVE_FORMAT_EXTENSIBLE): nowy moduł `maktabasauti_adpcm.c` (plik w pamięci do 256 MiB, blok dekodowany na żądanie, seek dokładny); 1 – 8 kanałów, bit w bit względem FFmpeg (do 1e-6).
* **Otwarte:** AIFF-C `ima4` (QuickTime IMA4): pierwszy pakiet 64 próbek zgodny z FFmpeg co do bitu, kolejne się rozjeżdżają (stan predyktora między pakietami), dlatego moduł go na razie odmawia (`E_UNSUPPORTED_FORMAT`); maska kanałów z `CHAN` / `WAVE_FORMAT_EXTENSIBLE` powyżej 2 kanałów nie jest czytana (układ domyślny wg liczby kanałów).
