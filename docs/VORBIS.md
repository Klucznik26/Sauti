# Ogg Vorbis I w MaktabaSauti (`maktabasauti_vorbis.c`)

Dekoder napisany od zera według specyfikacji Vorbis I (Xiph.Org, 4 lipca 2020); tablica `floor1_inverse_dB` pochodzi z sekcji 10.1 specyfikacji, ciał funkcji z stb_vorbis, libvorbis ani FFmpeg nie wklejono. Zastępuje w MaktabaSauti wpis `ogg-vorbis-stb` (stb_vorbis zostaje tylko w starym odtwarzaczu, `sauti_decoder_vorbis.c`).

## Zakres
Floor 0 i 1, residue 0 / 1 / 2, sprzężenie kanałów (kwadratowe), okna długie i krótkie z dowolnymi rozmiarami bloków, 1 – 32 kanały (maska WAVE dla 1 i 2 kanałów, od 3 w górę maska 0 i kolejność Vorbis I sekcja 4.3.9), łańcuchy (chained, także o różnych parametrach: `E_FORMAT_CHANGED` na granicy), przewijanie dokładne, przycięcie gapless z granulepos (początek tylko gdy pierwsza granula poprzedza ostatnią, koniec z ostatniej), komentarze (`get_tag`), ReplayGain / RTP / ID3 poza modułem. Cały plik jest czytany do pamięci (długość strumienia daje dopiero ostatnia strona); limit 1 GiB (`E_UNSUPPORTED_FEATURE`).

## Weryfikacja (2026-11-26)
Wzorcem jest **libvorbisfile** (`libvorbisfile.so.3`, referencyjny dekoder Xiph), nie FFmpeg: pliki `.f32` w `tests/data/vorbis/` zrobione wcześniej z FFmpeg miały złą długość (FFmpeg ucina 128 próbek z końca, w jednym pliku nie ucina nic) i zostały zastąpione wynikami libvorbisfile (`tools/vorbis_ref.py`).
* Pliki z libvorbis (kodera FFmpeg): 37 konfiguracji (8 – 96 kHz, 1 – 8 kanałów, jakość −1 … 10, 192 i 500 kb/s, sygnały: tony, szum, kliknięcia, cisza): różnica największa 2,4e−7, długość zawsze równa (`tools/vorbis_sweep.py`).
* Strumienie z ręcznie zrobionymi ustawieniami (`tools/vorbis_gen.py`): residue 0 / 1 / 2 × 1, 2, 3, 6 kanałów × sprzężenie × trzy pary rozmiarów bloków × floor 1: 90 plików, 0 rozbieżności (≤ 1e−4 względnie); floor 0: 90 plików w granicach 1e−3 względnie (libvorbis liczy krzywą z tablic, różnica ~1e−6).
* Fuzzing różnicowy (`tools/vorbis_fuzz.py`): zmutowane pakiety audio z przeliczonym CRC: 151 / 160 identycznych, reszta to szum zmiennoprzecinkowy przy wartościach ~1000 albo strumienie z niespójnymi flagami okien (zmieniony tryb pakietu), w których dekodery nie muszą się zgadzać.
* `tests/test_decoder_mks_vorbis.c` (`make test-vorbis`, ASan + UBSan): 18 plików wobec libvorbisfile (≤ 1e−4 bezwzględnie, w praktyce 3e−8 dla plików z libvorbis), przewijanie, łańcuchy, odporność (poniżej), 12 mutantów modułu zabitych (4 równoważne / nieosiągalne w danych testowych: zachowanie przy cięciu pakietu bez utraty strony).

## Błędy znalezione i naprawione w tej rundzie
1. Amplituda: IMDCT miał dodatkowe 1/N (wyjście ~1000 razy za ciche); wzór specyfikacji daje y = Σ X cos(…) bez skali.
2. Przycięcie początku: jedyna granula strumienia (ostatnia strona) oznacza koniec, nie początek; moduł obcinał początek o nadmiar (352 próbki w `click.ogg`).
3. Floor 1 bez partycji (`maximum_class = −1`, libvorbis pisze taki dla kanału LFE w 5.1): pętla klas szła po `uint32_t(−1)` i czytała śmieci: pliki 6-kanałowe się nie otwierały.
4. Floor 0: współczynniki w wektorze dostają ten sam offset (ostatnia wartość poprzedniego wektora), nie sumują się element po elemencie.
5. Residue 0: pakiet kończący się w środku partycji zostawia partycję nietkniętą (libvorbis czyta wszystkie słowa kodu przed dodaniem).
6. Floor 0: numer księgi spoza listy = floor nieużywany (libvorbis), nie błąd pakietu.
7. Szybkość: IMDCT O(N²) z `cos` w pętli (60 s dźwięku = 70 s dekodowania) zastąpiony DCT-IV przez FFT N/2 punktów z tablicami zapamiętanymi na rozmiar bloku, okna z pamięci podręcznej: 60 s w 0,09 s.
8. Odporność warstwy Ogg jak libogg + libvorbisfile (dotąd każdy błąd strony kończył otwarcie): plik urwany w środku strony gra do ostatniej całej strony (ostatni odczyt `E_TRUNCATED`), strona z błędnym CRC albo brakująca = dziura (upuszczony pakiet w trakcie składania i jego ciąg dalszy na następnej stronie), śmieci między stronami i za końcem pomijane, strony innych strumieni logicznych (Theora, Skeleton, Opus: grupowanie / multipleksowanie) ignorowane, pierwszy strumień Vorbis wybiera się po stronie BOS z pakietem identyfikacyjnym.
9. Wyjście zawsze skończone: skrajny floor 0 daje w libvorbis NaN (w pliku testowym 2880 próbek), tu NaN / inf → 0.

## Granice
* Zachowanie przy dziurze zgadza się z libvorbisfile co do liczby próbek we wszystkich pięciu testowych przypadkach, ale w jednym oknie nakładania (~360 próbek) przy samej dziurze próbki różnią się (specyfikacja tego nie określa).
* Floor 0 sprawdzony wobec libvorbis tylko na strumieniach syntetycznych (brak prawdziwych plików sprzed Vorbis 1.0).
* Plik w całości w pamięci (do 1 GiB); brak trybu strumieniowego bez znajomości długości.
* Nagłówki muszą być nienaruszone i zgodne ze specyfikacją (ident sam na stronie, setup kończy stronę, granula nagłówków 0).

## Narzędzia
`tools/vorbis_ref.py` (libvorbisfile przez ctypes), `tools/vorbis_gen.py` (generator ustawień + porównanie), `tools/vorbis_sweep.py`, `tools/vorbis_fuzz.py`; uruchamiać z katalogu głównego repozytorium przez `python3 -I`, potrzebne `ffmpeg` z libvorbis i `libvorbisfile.so.3`. Pliki testowe (własne syntezy, bez cudzych nagrań): `sine`, `mono`, `click`, `low`, `ch3`, `ch6` (44,1 kHz: floor bez partycji), `ch8`, `chain`, `chain-same`, `pages` (jedna paczka na stronę), `cont` (pakiety ciągnące się przez strony), `syn_*` (ustawienia z generatora), `syn_extreme` (NaN w libvorbis).
