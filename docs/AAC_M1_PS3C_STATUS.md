# AAC-M1 / PS-3c — 64-band QMF stereo -> interleaved PCM

Status: **implementacja wypchnięta; testy Fedorze jeszcze nieuruchomione**.
Nie jest to jeszcze kompletny dekoder bitstreamu HE-AAC v2.

Moduły:
- `include/sauti_aac_ps_pcm_stereo.h`
- `src/decoders/modern/aac/sauti_aac_ps_pcm_stereo.c`
- `tests/test_aac_ps_pcm_stereo.c`

Dwa nowe interfejsy:
- `sauti_aac_ps_pcm_stereo_from_qmf`: dwa wejścia
  `[32][64]` zespolone QMF -> `4096` próbek float
  `LRLR...`. Każdy kanał ma własny
  `SautiAacSbrQmfSynthWindowState`, trwale przechowywany
  pomiędzy ramkami.
- `sauti_aac_ps_pcm_stereo_from_mono_qmf`: jedno wejście
  `[38][64]` mono QMF wraz z harmonogramem/macierzami PS,
  łączące PS-3a -> PS-3b -> dwa istniejące syntezatory
  `sauti_aac_sbr_qmf_synth_64` -> interleaved stereo PCM.
  Zachowuje historię hybrydową, dekorrelatora oraz obu
  syntez QMF.

W obu wariantach wszystkie wyjścia i stany są zatwierdzane
dopiero po poprawnym wykonaniu OBU kanałów. Błędy QMF,
macierzy, rozmiarów, stanu syntezy lub nakładających się
buforów nie mogą opublikować częściowego wyniku.

Test `make test-aac-ps-pcm-stereo` porównuje nowy tor
z osobnymi wywołaniami PS-3a, PS-3b i dwóch istniejących
syntez QMF na wielu ramkach dla trybów 20/34,
weryfikuje kolejność LR, zachowanie historii i rollback.
Podłączony również do `test-aac-sanitize`.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps-pcm-stereo PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

**Pozostałe bramki AAC-M1:** podłączenie do prawdziwego
bitstreamu ADTS/HE-AAC v2 i SBR, rozwiązanie dokładnej
osi czasu i sześciu slotów lookahead na granicy ramek,
sprawdzenie referencyjnych PCM L/R, ataków/transjentów,
rozruchu, wygaszenia oraz nieprawidłowych danych.
Samo uzyskanie dwukanałowej fali float z syntetycznych
pasm QMF nie potwierdza jeszcze zgodności z normą.
