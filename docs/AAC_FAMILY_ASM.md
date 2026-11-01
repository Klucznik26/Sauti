# Sauti — własny dekoder AAC w NASM (AAC-LC + HE-AAC + HE-AAC v2)

Status: **rozpoczęty, NIE odtwarza jeszcze PCM**. Gałąź \`feature/aac-family-asm\`.
Nie zastępuj obecnego działającego adaptera AAC/FFmpeg w rejestrze odtwarzacza
aż do przejścia testów poprawności i odsłuchu prawdziwych plików.

## Jeden rdzeń, trzy współpracujące warstwy

| Warstwa | Odpowiada za | Stan |
| --- | --- | --- |
| AAC-LC | ADTS/ASC, strumień bitowy, ICS, Huffman, odwrotna kwantyzacja, TNS, stereo, IMDCT/okna, PCM | Parser ADTS i czytnik bitów NASM: zaimplementowane. Dekodowanie: do wykonania |
| HE-AAC (SBR) | Parsowanie danych SBR, siatka czasowo-częstotliwościowa, QMF, rekonstrukcja wysokich pasm | Wykrywanie jawnego AOT 5 w ASC: zaimplementowane. DSP: do wykonania |
| HE-AAC v2 (PS) | Odczyt parametrów PS, dekorrelacja i rekonstrukcja stereo na wyjściu SBR | Wykrywanie jawnego AOT 29 w ASC: zaimplementowane. DSP: do wykonania |

PS wykorzystuje SBR; SBR wykorzystuje ten sam podstawowy rdzeń AAC-LC.
Nie implementować trzech niezależnych dekoderów ani trzech osobnych ścieżek PCM.

## Co testujemy w pierwszym kroku

- NASM: sprawdzanie ADTS AAC-LC, długości ramek, konfiguracji kanałów,
  częstotliwości, opcjonalnego CRC i bezpieczne odrzucanie uciętych danych.
- NASM: czytnik bitów MSB-first, także niebajtowo wyrównanych; kontrola granic.
- AAC AudioSpecificConfig: LC (AOT=2), HE-AAC (AOT=5, jawne SBR),
  HE-AAC v2 (AOT=29, jawne SBR+PS); mono rdzenia i stereo na wyjściu PS.
- **Samo ADTS nie wystarcza do oznaczenia strumienia jako HE-AAC lub HE-AAC v2.**
  Strumień może sygnalizować SBR/PS niejawnie w \`raw_data_block\`.
  AOT=2 w ASC też nie wyklucza tego rozszerzenia. Nie zgadywać profilu.
- Testy parserów **nie oznaczają**, że AAC-LC/HE-AAC/HE-AAC v2 jest już
  odtwarzany własnym dekoderem. \`make test-aac-headers\` bada metadane.

## Równoległe następne moduły

1. **AAC-LC**: składnia raw_data_block; elementy SCE/CPE/FIL; scalefactors,
   codebooki Huffmana, odwrotna kwantyzacja, PNS/TNS/stereo, IMDCT, overlap-add.
2. **HE-AAC SBR**: detekcja implicit/explicit, extension payload, siatki SBR,
   Huffman envelope/noise, QMF 32/64 i kontrola sample rate.
3. **HE-AAC v2 PS**: dekodowanie danych PS z rozszerzeń SBR, IID/ICC,
   decorrelation/mixing, przejście mono core -> stereo PCM.
4. **Kontenery**: osobny demultiplekser MP4/M4A podający ASC i próbki,
   nie łączony bezpośrednio z rdzeniem dekodera.
5. **Weryfikacja**: testy zgodności profilowej z rzeczywistymi próbkami,
   porównanie PCM z dekoderem referencyjnym tylko podczas testów,
   pomiar zniekształceń i poprawności seek. Nie używać FFmpeg w runtime
   własnego dekodera.

Wymagania: Ring 3, zero OS zależności w parserze/rdzeniu ASM, kontrola
rozmiarów i brak odczytów poza buforem. Docelowe SIMD SSE2/AVX2 po
osiągnięciu poprawności i pokrycia testami.

## Kolejna część: elementy syntaktyczne i DSP (2026-11-01)

- `sauti_aac_syntax.c`: czyta `ics_info` AAC-LC dla długich i ośmiu krótkich okien,
  grupowanie krótkich okien, z odrzuceniem nieobsługiwanej predykcji.
- `sauti_aac_fill_parse`: czyta długość `fill_element`, w tym escape count;
  odróżnia typy rozszerzeń 13/14 jako **kandydat SBR**, bez ogłaszania
  prawidłowego odtworzenia SBR ani wykrycia PS z samej obecności FIL.
- `sauti_aac_raw_prefix_parse`: identyfikuje ID_FIL / ID_END, ale ZATRZYMUJE
  się na SCE/CPE i pozostałych elementach audio. Nie skanuje Huffmana
  w poszukiwaniu pozornych nagłówków.
- `sauti_aac_sbr_header_parse`: czyta składnię nagłówka SBR (częstotliwości,
  parametry extra1/extra2), nie dekoduje obwiedni/QMF.
- `sauti_aac_ps_header_parse`: czyta nagłówek PS i tryby IID/ICC, odrzuca
  zarezerwowane wartości, bez dekodowania parametrów i rekonstrukcji stereo.
- `sauti_aac_dsp.asm`: autorski NASM SSE overlap-add okien 128/1024;
  nie jest jeszcze podłączony do transformacji IMDCT.
- Testy: `make test-aac-syntax`; `make test-container-formats` sprawdza
  także parser na prawdziwym zakodowanym pliku ADTS z testów.
- Stan nadal **bez własnego dekodowania AAC do PCM**; zachować dotychczasowy
  odtwarzacz FFmpeg do czasu zakończenia Huffmana, IMDCT, SBR, PS.


## AAC-LC — section_data() i rzeczywista ramka mono

- \`sauti_aac_lc_sections_parse\` interpretuje section_data() bez flag
  error resilience: 4-bitowy wybór codebook, długości 3/5 bitów,
  escape 7/31 i maksymalny zakres grupy scalefactor band.
- Zapamiętuje przypisanie codebook do każdej sekcji (SFB od/do),
  nie udaje, że już dekoduje współczynniki Huffmana.
- \`sauti_aac_lc_sce_prefix_parse\` czyta początek ID_SCE:
  element_instance_tag, global_gain, ics_info oraz section_data;
  kończy na wejściu scale_factor_data.
- Test \`make test-aac-lc-sections\`: sztuczne ramki mono, krótkie okna,
  wiele grup, escape, błędne codebooki, przekroczenia długości
  i niezmienność po błędzie.
- \`make test-container-formats\` dodatkowo generuje prawdziwy AAC-LC
  mono 44,1 kHz, pomija poprzedzające ID_FIL, czyta sekcje i sprawdza
  ich spójność. Stary test ADTS stereo pozostaje.
- Następny etap: rzeczywiste tablice scalefactor band zależne od sample
  rate, Huffman scalefactor/codebooks, PNS, IMDCT; dopiero wtedy PCM.



## Huffman: autorski, ograniczony dekoder symboli NASM

- \`sauti_aac_huffman.asm\`: odczyt pojedynczego symbolu Huffmana z
  walidowanego drzewa binarnego; MSB-first; kontrola końca bufora i indeksów
  tablicy. W przypadku błędu pozycja bitowa i wynik pozostają niezmienione.
- \`sauti_aac_huffman.c\`: buduje drzewo z zewnętrznej listy par
  kod/symbol, odrzuca duplikaty, niepełne długości, kolizje prefiksów,
  za mały bufor i niepoprawne tablice. Nie ma alokacji na stercie.
- \`make test-aac-huffman\`: testy syntetycznego słownika prefiksowego,
  wartości dodatnich/ujemnych, 32-bitowych kodów i uciętych danych.
- **Testowy słownik nie jest standardową tablicą AAC.** To ogólny silnik,
  nie dekodowanie współczynników widmowych. W kolejnych etapach należy
  dostarczyć zweryfikowane tablice Huffmana AAC scalefactor/spectral oraz
  dekodowanie krotek współczynników, znaków i escape codebook 11.
- Własne odtwarzanie AAC-LC/HE-AAC/HE-AAC v2 do PCM nadal niegotowe;
  działający adapter FFmpeg pozostaje w użyciu.



## AAC Spectral — indeksy kodbooków, znaki i ESCAPE w NASM

- \`sauti_aac_spectral.asm\` odwzorowuje indeksy symboli dla książek
  widmowych 1–11 na pary/czwórki wartości skwantowanych. Uwzględnia
  zakresy podpisane (1/2, 5/6) i bez znaku (3/4, 7–11), nie tworzy
  tablic Huffmana ISO/IEC w tym kroku.
- \`sauti_aac_spectral.c\` spina działający dekoder Huffmana w NASM,
  dekoder indeksów w NASM i istniejący czytnik bitów w NASM.
  Dla książek bez znaku odczytuje bity znaku; książka 11 odczytuje
  dodatkowo escape (do |x_quant|=8191) **po wszystkich bitach znaku**.
- \`make test-aac-spectral\` weryfikuje mapowanie indeksów we wszystkich
  grupach książek, sign/ESC, granice, błędne indexy, atomowe odrzucenie
  niekompletnych danych i ścieżkę Huffman → tuple → sign → ESC.
- Test Huffmana nadal korzysta z **syntetycznej** tablicy kodów.
  Nie można jeszcze dekodować standardowego strumienia AAC do widma:
  brak pełnych, zweryfikowanych tablic kodowych MPEG-4 (codebooks
  Huffmana 1–11 i skali). Nie interpretować przejścia testów
  jako obsługi samodzielnego odtwarzania AAC.


## Niezależny zestaw testów bez FFmpeg

`make test-aac-native` wykonuje testy całej dotychczas napisanej warstwy AAC
(parserów, sekcji, znaków, ESC, DSP i Huffmana). W CI jest uruchamiany
z `PKG_CONFIG=/bin/false`, co wyłącza wykrywanie zewnętrznych bibliotek
i wymusza osobną rekompilację z `-B`. Testy nie wymagają zainstalowania
bibliotek FFmpeg. **To nie oznacza jeszcze, że program odtwarza AAC
samodzielnie**: do tego potrzebne są normatywne tablice i dalsze etapy DSP.


## AAC-6 — normatywne tablice Huffmana (2026-11-01)

- `sauti_aac_tables.c`: 11 kompletnych standardowych codebooków widmowych
  i codebook scalefactor (121 symboli), bez wywołań FFmpeg.
- Wartości kodów/długości sprawdzone na podstawie tabel z FFmpeg
  `libavcodec/aactab.c` (Oded Shimon / Maxim Gavrilov; źródło LGPL-2.1+).
  Przed wydaniem dystrybucyjnym ustalić zasady redystrybucji tych danych,
  obowiązki licencyjne i przypisanie autorstwa.
- Drzewa budowane są własną funkcją `sauti_aac_huffman_tree_build`, a
  odczyt symboli pozostaje własnym kodem NASM. Największe drzewo ma 577
  węzłów, przy limicie 4096; bez alokacji na stercie.
- `make test-aac-tables` bada wszystkie 1362 kodów we wszystkich 12 tablicach, indeksy scalefactor -60..60, ścieżkę spectral 11 z ESC
  i brak zatwierdzenia wyniku dla uciętych danych. Ten test dołączono do
  `make test-aac-native`.
- To na razie warstwa kodowa Huffmana. Jeszcze nie ma kompletnej ścieżki
  scale_factor_data/spectral_data w ICS, odwrotnej kwantyzacji i IMDCT,
  więc Sauti NIE dekoduje samodzielnie AAC do PCM.


## AAC-7 — `scale_factor_data()` AAC-LC (2026-11-01)

- Nowy `sauti_aac_scalefactors.c` dekoduje standardowym scalefactor Huffman
  w NASM wartości dla codebooków spectral 1..11, PNS 13 i intensity 14/15,
  bez czytania danych dla ZERO_HCB 0.
- Pierwsza energia PNS jest pobierana jako podpisany offset 9-bitowy od
  `global_gain-90`; następne szumy korzystają z tablicy scalefactor.
  Pozycje intensity i spectral mają odrębne akumulatory, a spectral startuje
  od `global_gain`.
- Walidacja mapy sekcji obejmuje kolejność i pełne pokrycie pasm grup,
  a niepowodzenie pozostawia wyjście i offset wejściowy nietknięte.
- `make test-aac-scalefactors` obejmuje grupowanie, spectral, PNS,
  intensity, niebajtowy offset, ucięcie i błędne mapy sekcji.
- Jest to parser danych skali, **nie** gotowy dekoder widma/PCM:
  rzeczywiste scale factor band offsets, spectral_data, TNS/IMDCT i
  testy odkodowania realnych ramek pozostają do wykonania.


## AAC-8 — spectral_data i granice SFB (2026-11-01)

- `sauti_aac_bands.c` dostarcza standardowe tabele SFB dla 13 indeksów
  sample rate, w oknach 1024 (długie) i 128 (krótkie). Dane liczbowe
  zweryfikowane na podstawie FFmpeg `aactab.c` (LGPL-2.1+);
  przed dystrybucją przeanalizować licencję.
- `sauti_aac_spectral_data.c` przetwarza section_data w kolejności grupy →
  pasma → okna → krotki i odtwarza naturalny układ [okno][próbka] widma.
  Korzysta z istniejących tablic AAC i dekodera NASM. Sekcje ZERO/PNS/
  intensity nie mają kodowanych współczynników widmowych.
- Waliduje częstotliwość, strukturę sekcji i grupy, maksymalny SFB,
  szerokość pasm względem wymiaru krotek oraz odczyt poza końcem danych.
  Niepowodzenie nie zmienia ani widma wyjściowego, ani offsetu bitowego.
- `make test-aac-spectral-data` weryfikuje komplet tabel 1024/128,
  długie okno, zgrupowane krótkie okna, prawdziwe kody Huffmana,
  odrzucenie błędnych sekcji i uciętego bufora.
- To jeszcze **nie jest poprawny PCM**: przed odtworzeniem potrzebne
  są odwrotna kwantyzacja, PNS/TNS, IMDCT i okna. Ponadto nie wykonano
  jeszcze pełnego testu przejścia przez prawdziwą ramkę do końca widma.


## AAC-9 — odwrotna kwantyzacja (2026-11-01)

- `sauti_aac_dequant.c`: dla właściwych pasm spectral 1..11 oblicza
  `sign(q) * abs(q)^(4/3) * 2^((sf-100)/4)`. Skala jest liczona
  dla każdej grupy i pasma, z rozłożeniem okien [window][line].
- Nowa procedura `sauti_aac_dequant.asm` (SSE scalar) wykonuje
  znakowanie i mnożenie przez skalę. Referencyjna potęga 4/3 jest
  liczona przez `powf` z libm; w późniejszej optymalizacji można
  zastąpić ją własnym LUT/SIMD. Brak zależności runtime od FFmpeg.
- PNS, ZERO i intensity pozostają zerowe; PNS i intensity wymagają
  osobnych etapów syntezy. Walidacja kontroluje SFB, grupy, mapowanie
  codebooków, zakresy sf=0..255 i q=-8191..8191, a po błędzie
  bufor wyjściowy pozostaje niezmieniony.
- `make test-aac-dequant` dodaje testy znaku, skali, skrajnych
  wartości, krótkich grupowanych okien i atomowości błędów.
- Nadal nie ma kompletnej syntezy AAC do PCM; następne etapy:
  PNS/TNS, IMDCT, okna, testy referencyjne całych ramek.


## AAC-10 — niezależna IMDCT 128/1024 (2026-11-01)

- `sauti_aac_imdct.c`: transformuje N współczynników widmowych w 2N próbek
  w dziedzinie czasu, N=128 lub N=1024. W tym etapie stosuje jawną
  konwencję normalizacji `2/N` i klasyczną definicję IMDCT
  `cos(pi/N * (t+0.5+N/2)*(k+0.5))`.
- `sauti_aac_imdct.asm`: autorski wewnętrzny iloczyn skalarny SSE2.
  Współczynniki kosinusowe powstają w C (`cos`/libm); brak FFmpeg
  w linkowaniu i w runtime. Suma jest akumulowana w double.
- Jest to implementacja poprawnościowa O(N²), nie docelowa szybka
  transformacja FFT/rotated MDCT. Wymaga pomiarów wydajności przed
  włączeniem do odtwarzacza. Przed integracją z syntezą AAC należy także
  zweryfikować konwencję skali z referencyjnym PCM.
- `make test-aac-imdct` porównuje wyniki dla okien 128 i 1024 z
  niezależnie liczonym kosinusem (sygnały rzadkie i gęste), bada
  niepoprawne rozmiary, NaN/Inf i niezmienność bufora po błędzie.
- Sama IMDCT nie obsługuje przejść między długimi/krótkimi oknami,
  okna SIN/KBD, TNS ani PNS. Istniejący `window_overlap_asm` pozostaje
  osobną procedurą; pełnego dekodowania AAC do PCM nadal NIE ma.


## AAC-11 — okna SIN/KBD i synteza sekwencji (2026-11-01)

- `sauti_aac_window.c` generuje okna SIN i KBD (alpha 4 long,
  alpha 6 short), z poprawną normalizacją KBD względem N+1
  próbek Kaisera, zapewniającą komplementarność połówek.
- Obsługuje ONLY_LONG, LONG_START, EIGHT_SHORT i LONG_STOP.
  Osiem krótkich transformacji po 256 próbek jest składanych do
  jednego bloku 2048, zaczynając od offsetu 448 i kroku 128.
  Pierwsze lewe półokno wykorzystuje poprzedni kształt; kolejne
  oraz prawe półokna bieżący kształt.
- Końcowe 1024 próbek i stan overlap tworzy istniejąca procedura
  `sauti_aac_window_overlap_asm` w NASM, wywołana na wcześniej
  złożonym i okienkowanym buforze z jednostkowym oknem.
- Zmiany są atomowe na błędnych argumentach i niepoprawnych wartościach
  NaN/Inf. Nie ma alokacji ani zależności od FFmpeg.
- `make test-aac-window` sprawdza Princen-Bradley dla 128/1024,
  wszystkie cztery sekwencje z niezależnym wzorcem układu, powiązanie
  lewego/prawego kształtu oraz rollback po błędach.
- Kod pozostaje niezintegrowanym modułem syntezy. Do pełnego AAC-LC
  brakuje m.in. TNS/PNS, kontrolowanej kolejności przejść okien,
  obsługi ramek i testów PCM względem prawdziwych plików.


## AAC-12 — zintegrowana synteza AAC-LC kanału (2026-11-01)

- Nowy `sauti_aac_lc_synth.c` łączy wyjście wcześniejszego parsera
  widma: 1024 skwantowane współczynniki i współczynniki skali →
  `dequantize` → IMDCT 1024 albo 8×128 → okna SIN/KBD → overlap-add
  przez nasz moduł NASM. Zwraca 1024 próbek czasowych na kanał.
- `SautiAacLcSynthesis` przechowuje 1024 próbek historii, poprzedni
  kształt i sekwencję okien. Stan jest jawnie inicjalizowany i wymaga
  resetu przy nowym strumieniu albo seek. Przejścia: ONLY_LONG →
  ONLY_LONG/LONG_START; LONG_START → EIGHT_SHORT; EIGHT_SHORT →
  EIGHT_SHORT/LONG_STOP; LONG_STOP → ONLY_LONG/LONG_START.
- Zmiany PCM, historii i stanu są atomowe: brak częściowego zatwierdzenia
  w przypadku błędnych parametrów, widma lub niewspieranego przejścia.
  PNS i intensity są jawnie odrzucane zamiast ukrytego wyciszania.
- `make test-aac-lc-synth`: test przejść przez pięć ramek,
  porównanie z niezależnie złożonymi funkcjami etapowymi,
  testy niepoprawnych zakresów i niezmienności stanu po błędzie.
- **Nie jest to jeszcze dekoder pliku AAC do PCM**: wejście jest
  już sparsowanym strumieniem współczynników, brakuje pełnego parsera
  raw_data_block, obsługi TNS/PNS/elementów stereo i testów
  porównawczych na realnych ramkach. Wywołujący musi odrzucić TNS
  i inne nieobsługiwane narzędzia przed syntezą. IMDCT nadal O(N²)
  i wymaga optymalizacji/porównania poziomów PCM przed odsłuchem.


## AAC-13 i AAC-14 — strumień bitowy → PCM mono (2026-11-01)

- Nowy `sauti_aac_lc_frame.c` wiąże dekoder ograniczonej ramki AAC-LC:
  `ID_SCE` → `global_gain/ics_info/section_data` →
  `scale_factor_data` → flagi obecności pulse/TNS/gain-control →
  `spectral_data` → `ID_END` → dequant → IMDCT → okna → overlap-add.
  Przyjmuje jedną niechronioną CRC ramkę ADTS AAC-LC mono z jednym
  `raw_data_block`; wynikiem jest 1024 próbek float.
- Celowo odrzuca CPE/stereo, CRC, pulse_data, TNS, gain_control, PNS,
  intensity, FIL (potencjalnie zawiera implicit SBR/PS) i nieznane
  elementy syntaktyczne. Kod nie skanuje Huffmana dla pozornych znaczników.
  Kod wynikowy: `1` sukces, `0` błąd, `-1` niespierane narzędzie.
- Waliduje indeks częstotliwości, długość ramki, kolejność flag i
  zakończenie ID_END / byte alignment. Na niepowodzeniu ani PCM,
  ani historia syntezy nie zmieniają się.
- `make test-aac-lc-frame` testuje syntetyczne pełne ADTS od składni SCE
  do PCM, realne kody Huffmana, wycofanie stanu i odrzucenie flag narzędzi.
  Ten test wchodzi do `test-aac-native`.
- `make test-aac-real-mono` (wymaga programu `ffmpeg`, ale nie bibliotek
  FFmpeg) koduje prawdziwy krótki plik ciszy mono AAC-LC ADTS i przepuszcza
  wszystkie ramki przez nasz dekoder. Test raportuje *oddzielnie*
  liczbę ramek odkodowanych, niewspieranych i błędnych. Gdy żaden blok
  nie jest obsługiwany, wypisuje SKIP zamiast fałszywego PASS.
- Natywny tor nie zastępuje jeszcze działającego dekodera FFmpeg.
  Brakuje wielu legalnych narzędzi AAC, wsparcia stereo, porównania
  numerycznego PCM z referencją na muzyce i optymalizacji IMDCT.
  Nie należy traktować testu ciszy jako dowodu jakości dźwięku.


## AAC-15–18 — rzeczywiste audio, powody odrzuceń, pomiar DSP (2026-11-01)

- **AAC-15:** `make test-aac-tone` tworzy za pomocą `ffmpeg` prawdziwe
  440 Hz AAC-LC mono w ADTS oraz odpowiedź referencyjną jako float32 PCM.
  `test_aac_real_mono` zapisuje własne 1024-próbkowe bloki do pliku
  `aac_native_tone.f32`, a osobno maskę `aac_native_tone.mask`.
  Niewspierane ramki mają w eksporcie dokładnie zerowy placeholder; nie
  są uznawane za udane dekodowanie i wyklucza się je z porównań.
- **AAC-16:** wariant `sauti_aac_lc_adts_mono_decode_ex` zwraca kod statusu
  oraz `SautiAacReason`. W logach widać konkretne powody odmowy,
  np. `tns`, `pulse`, `gain_control`, `pns`, `intensity`,
  `unsupported_fil`, `unsupported_adts`, `invalid_spectral`.
  Pomaga to ustalić, co blokuje rzeczywistą muzykę.
- **AAC-17:** `tools/compare_aac_pcm.py` analizuje zgodność z FFmpeg:
  przeszukuje względne przesunięcia o krok 16 próbek w zakresie ±2048,
  wyznacza korelację i stosunek RMS w natywnych ramkach zaakceptowanych
  przez dekoder. To analiza diagnostyczna, nie dowód zgodności ze
  specyfikacją; raport `NOT VERIFIED` oznacza wykrytą niezgodność.
  Dodatkowe testy sprawdzają szczegółowe powody odmowy i atomowość stanu.
- **AAC-18:** `make bench-aac-imdct` mierzy czas obecnej referencyjnej
  transformacji 128/1024 z `clock_gettime(CLOCK_MONOTONIC)`, podaje czas
  bloku i relację względem nominalnego budżetu przy 44,1 kHz.
  To oddzielny pomiar, nie automatyczny PASS real-time.
- `make inspect-aac-15-18` uruchamia test tonu oraz benchmark; pierwszy
  wymaga programu `ffmpeg` i `python3`. Do standardowego
  `make test-aac-native` wchodzą tylko deterministyczne testy regresji,
  aby brak programu ffmpeg nie podszywał się pod błąd naszego kodu.
- **Ograniczenia:** porównanie korelacyjne bazuje na ramkach bez
  niewspieranych narzędzi; nie wykonuje pełnego rozwiązania opóźnienia
  kodera, nie kompensuje poprawnie wszystkich przypadków utraty stanu
  historycznego, nie bada SBR/PS. Obecna skala IMDCT i zgodność PCM
  wymagają jeszcze weryfikacji w nagraniach niezerowych.
