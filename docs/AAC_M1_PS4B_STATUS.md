# AAC-M1 — PS-4b: natywny eksport QMF po rekonstrukcji HF

Status: implementacja zapisana na `feature/aac-m1-ps-qmf`;
testy na Fedorze jeszcze nieuruchomione.

Wcześniejszy `sauti_aac_sbr_qmf_frame_process_ex` kończył pipeline
analiza QMF AAC-LC -> transpose/predict HF -> HF ROM adjustment ->
złożenie 64 pasm QMF -> **mono PCM**. Pośrednia ramka QMF
`assembled[slots][64]` była ukryta.

Nowe `sauti_aac_sbr_qmf_frame_export_qmf_ex` wykorzystuje
**dokładnie ten sam kod** co wersja mono aż do złożenia
rzeczywistych pasm QMF z low/HF. Następnie publikuje zespolone
`[slots][64]` i zachowuje historię analizatora, HF, oraz
`low_prior`. Nie wywołuje syntezy mono i nie modyfikuje
`state.synthesis`.

Dotychczasowy mono PCM driver zachowuje swoją ścieżkę i API.
Eksport QMF zachowuje transakcyjne publikowanie raportu/stanu/
wyników. Test `test-aac-sbr-qmf-export` porównuje
trzy ramki: natywny mono PCM vs eksport QMF plus wcześniejsza
synteza `sauti_aac_sbr_qmf_synth_64`, historię i raport
limiterów. Zawiera też negatywne testy rozmiarów, NaN i rollback.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-sbr-qmf-export PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

**Ograniczenie krytyczne dla AAC-M1:**
- `export_qmf_ex` przyjmuje nadal *już zdekodowane* AAC-LC PCM
  i odpowiednią rzeczywistą konfigurację SBR. Nie jest sam
  czytnikiem ADTS ani bitstreamu.
- PS wymaga 38 slotów `[38][64]` na każde 32 przetwarzane:
  w szczególności prawdziwego lookahead 6 slotów z następnej
  ramki, a nie zer lub ponownie użytych starych próbek.
- Normatywne przesunięcia HF i transjenty/FIXVAR są nadal
  osobnymi warunkami zgodności. Aktualna eksperymentalna
  ścieżka SBR dopuszcza tylko własny restrykcyjny podzbiór
  ramek i nie gwarantuje idealnego PCM.

Kolejny etap PS-4c: połączyć prawdziwe ADTS SCE+FIL+PS,
stan AAC-LC, SBR HF, dwuramkowy bufor lookahead i
`sauti_aac_he_ps_bridge_render`; nie publikować stereo PCM
przed otrzymaniem niezbędnych przyszłych slotów.
