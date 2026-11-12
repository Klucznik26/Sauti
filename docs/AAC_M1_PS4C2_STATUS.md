# AAC-M1 / PS-4c2 — eksperymentalny sterownik ADTS AAC-LC/SBR/PS

Status: kod zapisany na `feature/aac-m1-ps-qmf`, NIE uruchomiono
jeszcze testów na Fedorze.

Nowy sterownik:
- `include/sauti_aac_he_adts_ps.h`
- `src/decoders/modern/aac/sauti_aac_he_adts_ps.c`

`sauti_aac_he_adts_ps_decode_ex` wiąże:
1. `sauti_aac_he_adts_mono_decode_ex`: czyta rzeczywistą jedną
   ramkę ADTS (AAC-LC SCE + FIL SBR/PS) i zwraca 1024 PCM core,
   parametry SBR/PS oraz zatrzymuje całą historię parsera.
2. `sauti_aac_sbr_payload_make_hf` i
   `sauti_aac_sbr_qmf_frame_export_qmf_ex`: natywna analiza
   AAC core, rekonstrukcja HF, eksport 32×64 zespolonych pasm QMF.
3. `sauti_aac_he_ps_delay_push`: pierwsza ramka tylko
   w buforze; od drugiej kolejne sześć slotów QMF uzupełnia
   32 sloty poprzedniej ramki. Publikuje 4096 próbek stereo
   float, przeplatanych LR, dla POPRZEDNIEJ ramki.

Wszystkie stany AAC-LC, SBR, QMF, PS oraz wynik/raport
są atomowe przy błędach. Wspiera tylko dotychczasowy
ostrożny podzbiór SCE/SBR; brak PS oraz nieobsługiwany
wariant HF powodują odrzucenie bez fałszowania danych.
`sauti_aac_he_adts_ps_init` resetuje nowy strumień.

**Ważne: brak standardowej zgodności PCM.**
Ścieżka SBR-28 używa aktualnego strict/baseline transposer
(`lpc=NULL`) i jeszcze nie rozwiązuje wszystkich normatywnych
opóźnień HF, FIXVAR oraz źródeł zerowej energii. FIFO PS ma
sześć rzeczywistych slotów następnej ramki, ale poprawne
dopasowanie ich do normatywnej osi czasu SBR musi być
zweryfikowane względem FFmpeg. Koniec strumienia nadal
pozostawia jedną ramkę pending, bez wymyślonego flush.

Test `test-aac-he-adts-ps` na razie dowodzi **odrzucenia**
prawdziwego strumienia ADTS AAC-LC/SBR BEZ PS,
zachowania stanu/wyjść przy truncation, błędnych rozmiarach
i niespójnej historii. Nie istnieje tu jeszcze pozytywny
fixture rzeczywistego ADTS z PS ani PCM golden. Również
obecne inne testy PS używają sztucznych ramek QMF/PS.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-he-adts-ps PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

**Następny krok do AAC-M1**: rzeczywisty, mały
ADTS HE-AAC v2 z PS, co najmniej trzy poprawne
ramki, pozytywny decode (nie tylko reject), test
bitstreamu i kontroli międzyramkowej; następnie
porównanie PCM, opóźnień i korelacji L/R z FFmpeg,
oraz implementacja normatywnego rozwiązania granic.
