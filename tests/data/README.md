Syntetyczne fixtury strumieni AAC dla testów (2 s, ton + szum z `numpy default_rng(3)`; **nie są to dane użytkownika**). Wygenerowane przez `make_fixtures.sh`
(fdkaac i FFmpeg użyte jednorazowo; w repozytorium są ich wyniki). Używane przez `scripts/test_aac_threads.sh` i CI (bez fdkaac/FFmpeg).
`lc.aac` LC stereo 96 kb/s · `he1.aac` HE-AAC v1 · `he2.aac` HE-AAC v2 · `he1.m4a` HE v1 w M4A · `he1.loas` HE v1 w LOAS (konfiguracja w każdym elemencie) ·
`he1.adif` HE v1 w ADIF · `m51.aac` LC 5.1 (FFmpeg).
