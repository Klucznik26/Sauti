# Opus w MaktabaSauti: stan i plan (2026-11-26)

## Stan
**MaktabaSauti nie ma dekodera Opus.** Rejestr (`mks_legacy.c`) go nie zawiera; jedyny ślad to `sauti_decoder_opus.c` w starym odtwarzaczu, który działa wyłącznie po zbudowaniu z `SAUTI_HAVE_OPUSFILE` (libopusfile + libopus + libogg, nie dołączone do repozytorium). Plik `.opus` zrobiony libopusem jest przez CLI odrzucany („no codec recognised the data”). Opus jest dziś najważniejszym kodekiem po AAC (rozmowy, strumienie, YouTube / WebM, Discord, Signal), a licencja (BSD-3, RFC 6716 z pełnym kodem referencyjnym) pozwala na własną implementację z zachowaniem noty.

## Zakres (RFC 6716, RFC 7845 = Ogg Opus)
1. **Ogg Opus:** `OpusHead` (wersja, kanały, pre-skip, częstotliwość wejściowa, output gain Q7.8, mapping family 0 / 1 / 255 z tablicą kanałów), `OpusTags`, przycięcie początku o pre-skip, koniec z granulepos (jak Vorbis: ostatnia strona skraca), łańcuchy, przewijanie z 80 ms preroll; wyjście zawsze 48 kHz.
2. **Dekoder zakresowy** (RFC 6716 4.1), jedna wspólna warstwa dla SILK i CELT.
3. **CELT** (4.3): energia pasm (gruba / drobna, predykcja czasowa i międzypasmowa), alokacja bitów (tablice `band_allocation`, `cache_caps`, trim, skip, intensity, dual stereo), PVQ (`cwrs` odtwarzanie wektorów), rozpraszanie (spreading) i składanie widma (folding), anti-collapse, filtr tonowy (pitch pre/post filter, comb), preemfaza, MDCT 120 – 960 z oknem niskonakładkowym i krótkimi blokami (interleave).
4. **SILK** (4.2): zakresy NB / MB / WB (8 / 12 / 16 kHz), LSF (NLSF) z książkami kodowymi i stabilizacją, LTP, szum ekscytacji (pulse shell coding, LCG), stereo przez predykcję M/S, interpolacja NLSF, resampler do 48 kHz (FIR wymagany przez normę tylko co do wyniku, nie struktury).
5. **Hybryda** SILK + CELT (SWB / FB 10 / 20 ms), przełączanie trybu z redundancją (transition frames), PLC (utrata pakietu).
6. Kody pakietów 0 – 3 (jedna / dwie ramki, VBR / CBR, padding), długości ramek 2,5 – 60 ms.

## Wzorzec i kontrola
Dostępny w systemie: **FFmpeg z dekoderem libopus** (`-c:a libopus`, referencyjny libopus; kodery `libopus` do generowania plików: tryby `-application voip | audio | lowdelay`, `-frame_duration`, `-b:a`, `-vbr`), więc wszystkie ścieżki (CELT-only, SILK-only, hybryda, mono / stereo, 2,5 – 60 ms, 8 – 48 kHz) da się wygenerować i porównać dokładnie jak Vorbis z libvorbisfile. Cel: ≤ 1e-3 względnie (norma dopuszcza różnice zmiennoprzecinkowe; libopus ma wersję stałoprzecinkową i zmiennoprzecinkową, wektory testowe RFC 6716 / 8251 do porównania w 16 bitach z tolerancją).

## Dane (tablice)
Z RFC 6716 (kod referencyjny jest częścią normy; licencja BSD) potrzebne są tablice: `eband5ms`, `band_allocation`, `cache_index / cache_bits / cache_caps`, `log2_frac_table`, `trim_icdf`, `spread_icdf`, `tapset_icdf`, współczynniki predykcji energii, parametry Laplace'a, okno `window120`, tablice SILK (`silk_NLSF_CB_*`, `silk_pitch_*`, `silk_LTP_*`, `silk_shell_*`, `silk_rate_levels_*`, tablice stereo, `silk_resampler_*`). Zasada jak przy tablicach libxaac: dane, nie ciała funkcji; skrypt generujący, wpis w rejestrze danych i NOTICE (BSD-3 Xiph.Org / Skype / Broadcom).

## Kolejność i szacunek pracy
1. Ogg Opus + zakres + pakiety TOC (1 – 2 dni): rozpoznanie i parsowanie bez dźwięku, testy na plikach libopus (liczba ramek, pre-skip, długość).
2. CELT-only stereo / mono (3 – 5 dni): dopiero to daje dźwięk w trybie `-application lowdelay` i przy wysokich przepływnościach.
3. SILK-only (4 – 6 dni), potem hybryda i redundancja (2 dni).
4. PLC, przewijanie, dokładność (2 dni), wektory RFC 8251.
Razem rząd 2 – 3 tygodni pracy; pierwszy użyteczny etap (CELT-only) po tygodniu. Do decyzji użytkownika, czy zaczynać od razu po domknięciu AAC.
