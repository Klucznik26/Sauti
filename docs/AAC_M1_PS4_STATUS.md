# AAC-M1 / PS-4a — most parsera prawdziwego SBR/PS do toru stereo

Status: kod zapisany na gałęzi `feature/aac-m1-ps-qmf`; testów
na Fedorze jeszcze nie uruchomiono.

Zbadany aktualny interfejs:
- `sauti_aac_he_adts_mono_decode_ex`: czyta prawdziwe ADTS SCE+FIL
  i zwraca 1024 PCM core oraz `SautiAacSbrPayloadFrame`, w tym
  `has_ps` i `ps` odczytane z EXTENSION_ID_PS.
- `sauti_aac_he_adts_qmf_decode_ex`: obecnie odrzuca
  `payload.has_ps`; zwraca tylko gotowe MONO PCM.
  Nie zwraca pośrednich, zrekonstruowanych 64 pasm HF QMF.
- `sauti_aac_ps_render_plan_build`: istniejące mapowanie PS,
  wygładzanie faz i zespolone macierze PS.
- `sauti_aac_ps_pcm_stereo_from_mono_qmf`: istniejący natywny
  tor QMF -> hybryda -> dekorrelacja -> miks -> stereo PCM.

Nowy `sauti_aac_he_ps_bridge_render` przyjmuje dokładnie strukturę
`SautiAacSbrPayloadFrame` zwróconą przez parser i gotowe, poprawnie
wyrównane QMF `[38][64]` z sześcioma prawdziwymi slotami lookahead.
Wymaga `has_ps==1`, buduje plan na bazie `payload.ps`,
następnie przetwarza ramkę na `4096` próbek interleaved float.
Na błędzie nie zatwierdza stanu faz, DSP ani PCM. Testy sprawdzają
obie topologie, wieloklatkowe stany i odrzucenie wejścia/PS.

**Krytyczne ograniczenie:** testy mostu mogą używać syntetycznej
ramki `SautiAacSbrPayloadFrame` i sztucznego QMF. Nie są
testem dekodowania rzeczywistego ADTS ani zgodności referencyjnej.

Do realnego HE-AAC v2 pozostaje:
1. Wystawić z dekodera SBR zrekonstruowane 64-pasmowe QMF
   ZANIM zostaną zamienione na mono PCM.
2. Zapewnić normatywną oś czasu HF, historię SBR,
   sześć przyszłych slotów QMF i granice ramek.
3. Zastosować PS z rzeczywistych bitów FIL we właściwej ramce,
   bez fallbacku i bez sztucznych wartości.
4. Porównać rzeczywiste L/R PCM z FFmpeg: opóźnienie,
   jakość, dynamikę, transjenty i odporność na uszkodzenia.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-he-ps-bridge PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```
