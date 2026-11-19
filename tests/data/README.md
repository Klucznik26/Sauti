Syntetyczne fixtury strumieni AAC dla testów (2 s, ton + szum z `numpy default_rng(3)`; **nie są to dane użytkownika**). Wygenerowane przez `make_fixtures.sh`
(fdkaac i FFmpeg użyte jednorazowo; w repozytorium są ich wyniki). Używane przez `scripts/test_aac_threads.sh` i CI (bez fdkaac/FFmpeg).
`lc.aac` LC stereo 96 kb/s · `he1.aac` HE-AAC v1 · `he2.aac` HE-AAC v2 · `he1.m4a` HE v1 w M4A · `he1.loas` HE v1 w LOAS (konfiguracja w każdym elemencie) ·
`he1.adif` HE v1 w ADIF · `m51.aac` LC 5.1 (FFmpeg).

`probe_real.*` — małe (0,3 s, 22,05 kHz, dwa tony z generatora FFmpeg `lavfi sine`; **nie są to dane użytkownika**) prawdziwe pliki pozostałych formatów: FLAC, Ogg Vorbis,
MP3, WAV, AIFF, VOC, Opus, ALAC/M4A. Wygenerowane przez `make_probe_fixtures.sh` (jednorazowo; w repozytorium są wyniki). Używane przez audyt sond
(`tests/test_maktabasauti_probe_audit.c`, cel `make test-maktabasauti-probe-audit`) i test równoległego rozpoznawania (`scripts/test_maktabasauti_threads.sh`).
