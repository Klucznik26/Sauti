Syntetyczne fixtury strumieni AAC dla testów (2 s, ton + szum z `numpy default_rng(3)`; **nie są to dane użytkownika**). Wygenerowane przez `make_fixtures.sh`
(fdkaac i FFmpeg użyte jednorazowo; w repozytorium są ich wyniki). Używane przez `scripts/test_aac_threads.sh` i CI (bez fdkaac/FFmpeg).
`lc.aac` LC stereo 96 kb/s · `he1.aac` HE-AAC v1 · `he2.aac` HE-AAC v2 · `he1.m4a` HE v1 w M4A · `he1.loas` HE v1 w LOAS (konfiguracja w każdym elemencie) ·
`he1.adif` HE v1 w ADIF · `m51.aac` LC 5.1 (FFmpeg).

`probe_real.*` — małe (0,3 s, 22,05 kHz, dwa tony z generatora FFmpeg `lavfi sine`; **nie są to dane użytkownika**) prawdziwe pliki pozostałych formatów: FLAC, Ogg Vorbis,
MP3, WAV, AIFF, VOC, Opus, ALAC/M4A. Wygenerowane przez `make_probe_fixtures.sh` (jednorazowo; w repozytorium są wyniki). Używane przez audyt sond
(`tests/test_maktabasauti_probe_audit.c`, cel `make test-maktabasauti-probe-audit`) i test równoległego rozpoznawania (`scripts/test_maktabasauti_threads.sh`).

`lc_mono.aac` AAC-LC mono 64 kb/s · `he1_mono.aac` HE-AAC v1 mono (rdzeń mono + SBR, bez PS), dodane przy etapie 2 (strumieniowy ADTS), te same ziarno i generator (`make_fixtures.sh`).

Etap 2 (strumieniowe kontenery AAC): `lc.loas` / `lc_mono.loas` (konfiguracja tylko okresowo, elementy przed pierwszą są pomijane) · `he2.loas` · `lc.adif` / `lc_mono.adif` (z ramek ADTS, `make_fixtures.sh`) · `lc.m4a` / `lc_mono.m4a` / `he2.m4a` (fdkaac, z listą edycji) · `lc_frag.m4a` (fragmentowany MP4 zbudowany z `lc.m4a` przez `make_fragmented.py`: moov bez tablic próbek, mvex/trex, moof/trun po 24 próbki) · `lc_fast.m4a` / `lc_mono_fast.m4a` / `he1_fast.m4a` / `he2_fast.m4a` (`moov` przed `mdat`, `make_faststart.py`: moov przesunięty, offsety w stco/co64 poprawione; czytelne z wejścia nieprzewijalnego) · `lc_frag_mdatfirst.m4a` (`make_mdat_first.py`: każde mdat przed swoim moof, ujemny data_offset; z wejścia nieprzewijalnego `E_UNSUPPORTED_FEATURE`).

`lc44.aac` / `lc44.loas` — AAC-LC stereo 44,1 kHz (ADTS; LOAS z konfiguracją w każdym elemencie), do testów zmiany parametrów w środku strumienia (pary sklejane w `test_format_change`).
