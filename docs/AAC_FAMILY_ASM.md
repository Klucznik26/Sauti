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


## Obserwacja testu 440 Hz: 2026-11-01

Na Fedorze, dla `aac_native_tone.aac`: 21 ramek, w tym
1× `unsupported_fil`, 19× `pns` i końcowe
`invalid_synthesis`. Żadna ramka nie dostarczyła PCM — porównanie
jakości z FFmpeg jest w tym przebiegu **niewykonalne**.
Oznaczenie ostatniej ramki jako błędnej było niejednoznaczne, ponieważ
skrypt po każdym odrzuceniu inicjalizował od zera overlap, udając
ciągłą historię. Poprawka oznacza historię jako nieznaną
(`state.initialized=0`); późniejsza syntaktycznie poprawna ramka
bez PNS i innych blokad raportuje `unsupported_history` (-1), a
nie `invalid_synthesis` (0). Po odrzuceniu dowolnej ramki PCM
z tego fragmentu nie może służyć do porównania z referencją.
Dopiero wsparcie PNS i świadome zarządzanie ciągłością pozwolą
zweryfikować pełny przebieg. Następny krok: normatywna synteza PNS
oraz kontrola FIL/SBR bez przyjmowania nieznanych rozszerzeń.


## AAC-19 i AAC-20 — PNS mono i szybsza IMDCT (2026-11-01)

- Dodano `sauti_aac_pns.c` i `sauti_aac_pns_apply`: generowanie
  deterministycznego pseudolosowego widma dla pasm codebook 13,
  z normalizacją sumy kwadratów szumu w każdej grupie/oknie do
  energii wskazanej przez noise scalefactor. Zakresy i kolejność SFB
  są kontrolowane, a na błędzie wynik i stan RNG są niezmienione.
- Stan `SautiAacLcSynthesis` przechowuje dodatkowo `pns_seed`,
  inicjowany przy rozpoczęciu strumienia. Nowa funkcja
  `sauti_aac_lc_synthesis_frame_pns` obsługuje PNS w mono;
  poprzednie `sauti_aac_lc_synthesis_frame` pozostaje jawnie
  zachowawczą wersją i nadal odrzuca PNS, chroniąc starsze kontrakty.
  Ścieżka mono ADTS korzysta z wersji PNS.
- `make test-aac-pns` sprawdza energię, powtarzalność, stan RNG,
  okna krótkie z grupami i niezapisanie wyniku po błędzie. Dodatkowy
  test syntezy sprawdza integrację i zachowanie starego API.
- Przyspieszono IMDCT przez rekurencję kosinusów obliczaną w double:
  dla każdego wiersza zamiast N wywołań `cos` używamy dwóch,
  a iloczyn skalarny nadal wykonuje NASM SSE2. Złożoność pozostaje
  O(N²), więc benchmark wciąż jest obowiązkowy; stary niezależny
  test bezpośredniego wzoru powinien wykrywać błędy numeryczne.
- **Ograniczenia:** PNS jest pierwszą wersją dla mono. Jej stan PRNG
  nie ma jeszcze potwierdzonej zgodności próbka-w-próbkę z FFmpeg;
  szum jest z natury zależny od generatora, więc korelacja PCM
  jest słabym kryterium w pasmach PNS. Brakuje TNS, CPE/MS/intensity,
  SBR, PS, MP4/M4A demux, oceny rzeczywistych nagrań i testów
  odsłuchowych. FIL nadal świadomie odrzucamy, gdyż może zawierać
  rozszerzenie SBR/PS. **Nie oznaczać dekodera jako kompletnego.**

## Definicja końca projektu AAC

Uznanie natywnego AAC za gotowy wymaga oddzielnych bramek:
pełnego AAC-LC mono/stereo z TNS/PNS/pulse/intensity/MS i
przełączaniem okien; poprawnego FIL/SBR (HE-AAC);
PS/QMF (HE-AAC v2); kontenerów ADTS i MP4/M4A;
porównań wyjścia PCM na nagraniach, skokach i seek;
testów błędnych ramek, ciągłości stanu i odpowiedniej wydajności
na docelowym sprzęcie. Dopiero po tych testach wolno rozważać
domyślne zastąpienie dotychczasowego adaptera FFmpeg.


## AAC-21 — dopuszczenie wyłącznie EXT_FILL przed SCE (2026-11-01)

Po uruchomieniu PNS 14/14 testów natywnych przeszło, ale próbka
440 Hz wciąż nie dawała PCM: pierwszy blok był odrzucany jako FIL,
a następne jako brakujące overlap history. W osobnym odtworzeniu
komendy kodującej początkowy ADTS miał ID_FIL (6), pole count=15 +
escape, extension_type=0 (EXT_FILL), po którym następował identyfikator
kodera ASCII `Lavc...`. To nie jest samo w sobie SBR.

Parser mono teraz sprawdza każde poprzedzające SCE ID_FIL i może
pominąć wyłącznie rozpoznane EXT_FILL (type 0) albo FIL z zerową
liczbą bajtów. EXT_SBR_DATA=13, EXT_SBR_DATA_CRC=14 i pozostałe
rozszerzenia nadal kończą się `unsupported_fil`, bez zmiany stanu.
Dopisano regresję EXT_FILL=0 / EXT_SBR_DATA=13. Wciąż nie obsługujemy
FIL po SCE i wszystkich payloadów MPEG-4. Nie wolno utożsamiać
tej poprawki z pełnym dekodowaniem rozszerzeń.


## AAC-22 — publiczna skala PCM (2026-11-01)

Pierwszy całkowicie zdekodowany test 440 Hz: 21/21 ramek,
korelacja raportowana 1.000000, lag=0, native/reference RMS=65536.4.
To niemal dokładnie 2^16, a więc zauważono spójne niedopasowanie
amplitudy, pomimo wysokiej korelacji kształtu sygnału.

Do `sauti_aac_lc_adts_mono_decode_ex` dodano **prowizoryczny**
współczynnik `ldexpf(result[i], -16)` na granicy wyjścia float PCM.
Niezależne moduły dequant, IMDCT i overlap-add pozostają w tej
samej wewnętrznej skali. W przypadku wartości nie-skończonej nie
zatwierdza się PCM ani stanu. Nie należy wprowadzać skali w kilku
miejscach ani uważać tego za walidację zgodności specyfikacyjnej.
Wymagane ponowne testy 440 Hz oraz inne częstotliwości, dynamika,
okna krótkie i stereo. PNS pozostaje pseudolosowe i nie musi
przechodzić porównania próbek 1:1 z FFmpeg.


## AAC-23 — testy PCM dla różnych sygnałów i ciągłości (2026-11-01)

Po AAC-22 użytkownik potwierdził test 440 Hz: 21/21 ramek mono,
korelacja 1.000000, RMS native/ref 1.00001, lag 0.
Dla dokładniejszej oceny dodano:

- `tools/generate_aac_fixtures.py`: pięć deterministycznych próbek
  WAV mono 44,1 kHz: 440 Hz, 1000 Hz, wieloton 220/880/3000 Hz,
  transjenty i chirp 150–7000 Hz.
- `scripts/inspect_aac_lc_matrix.sh`: każda próbka jest kodowana
  do prawdziwego AAC-LC ADTS przez program `ffmpeg`. Sauti
  dekoduje ten sam plik natywnie, a FFmpeg dostarcza PCM referencyjne.
  Obsługa stereo **nie** jest deklarowana.
- `tools/compare_aac_pcm.py`: jawna kontrola liczby ramek,
  niezależnych ciągłych odcinków i ramek bez historii,
  porównanie bez automatycznego dopasowywania gain:
  korelacja, native/ref RMS, RMSE, maksymalny błąd bezwzględny
  i znormalizowane RMSE. Dopasowanie opóźnienia szuka
  przesunięć do 2048 próbek (najpierw zgrubnie, potem co próbkę).
  Pierwsza ramka każdego ciągłego odcinka jest pomijana jako
  warm-up overlap. Wyniki nie są podawane jako conformance.
  `--lag N` umożliwia kontrolę przy zadanym przesunięciu.
- `tests/test_compare_aac_pcm.py`: samodzielne testy
  błędu amplitudy mimo korelacji 1.0, przerw w ciągłości,
  poprawnego sygnału oraz NaN/niepełnego PCM.
- Polecenie: `make inspect-aac-lc-matrix PKG_CONFIG=/bin/false`
  (wymaga `ffmpeg` i `python3`). W tym zadaniu wyniki
  NOT VERIFIED i INCONCLUSIVE pozostają raportami, a nie
  fałszywym PASS; awaria parsowania lub regresji przerywa test.

**Ważne:** próby na 440 Hz nie dowodzą jeszcze poprawnego
dekodowania muzyki i krótkich okien. Jeśli transjenty uruchomią
TNS albo inne niewspierane narzędzia, raport pokaże powód,
a test nie będzie udawał poprawnego odtworzenia. Zgodność
normatywna wymaga także osobnych przypadków stereo, impulsowych
okien i różnych częstotliwości próbkowania.


## AAC-23 wynik matrycy / AAC-24 parser TNS (2026-11-01)

Wynik pięciu nagrań AAC-LC mono na Fedorze:
- tone440: 23/23, korelacja 0.999999974, nRMSE 0.00022724;
- tone1000: 23/23, korelacja 0.999999914, nRMSE 0.00041431;
- multitone: tylko 1/23; ramka #1 `tns`, dalsze `unsupported_history`;
- transient: 23/23, korelacja 0.999986594, nRMSE 0.00517798;
- chirp: 23/23, korelacja 0.999991119, nRMSE 0.00421438.

Oznacza to, że TNS jest następną konkretną barierą.
Dodano `sauti_aac_tns_parse()` z ograniczeniami liczby filtrów,
rzędu i szerokości skwantowanych współczynników, zarówno dla
1 okna długiego jak i 8 krótkich. Wejściem są bity bezpośrednio po
`tns_data_present=1`, a wynik i pozycja bitowa są zatwierdzane tylko
po sukcesie. `make test-aac-tns` sprawdza długość, kolejność,
współczynniki ze znakiem, odcięcia oraz rollback.

**Świadome ograniczenie:** AAC-24 obejmuje wyłącznie strukturalny
parser TNS. Nie stosuje jeszcze odwrotnego filtru TNS w widmie,
nie aktywuje dekodowania ramek z TNS i NIE naprawia jeszcze
próby multitone. Przed aktywacją konieczne są normatywne
współczynniki, mapowanie pasm, kierunek predykcji i niezależne
porównanie PCM; brak filtru nie może być maskowany jako sukces.


## AAC-25 — odwrotna filtracja TNS AAC-LC mono (2026-11-01)

- Po zaliczeniu 15/15 testów AAC-24 dodano `sauti_aac_tns_apply()`.
  Wykorzystuje skalefaktorowe granice pasm 1024/128, tabele
  `tns_max_bands` dla indeksów częstotliwości 0–12, kierunek
  filtrowania oraz kilka filtrów na ramkę.
- Kwantowane PARCOR są mapowane przez funkcję sinus i algorytm
  step-up do współczynników predykcji LPC. Dekoder stosuje
  odwrotny filtr AR na zdekodowanym widmie przed IMDCT. Dla
  długich i krótkich okien sprawdza granice, skończoność
  próbek i nie nadpisuje wejścia, jeśli filtr zawiedzie.
- `sauti_aac_lc_mono_parse` odczytuje `tns_data()` po fladze
  zamiast automatycznie odrzucać ramkę. Dodatkowe pole
  `SautiAacLcMonoData.tns` przenosi parametry do nowego
  `sauti_aac_lc_synthesis_frame_tns()`, które zachowuje
  atomowość stanu i PCM; dotychczasowe wejścia syntezy nadal
  pozostają dostępne.
- `test_aac_tns` ma przypadki filtrów obu kierunków, obu
  rozmiarów okien, niezależne oczekiwane wyniki pierwszego
  rzędu, granice i rollback. `test_aac_lc_frame` obejmuje
  poprawny TNS order=0 oraz ucięty payload.
- **Kryterium integracji**: `make -B test-aac-native
  PKG_CONFIG=/bin/false` i `make inspect-aac-lc-matrix
  PKG_CONFIG=/bin/false`. Multitone może ujawnić kolejne
  różnice PCM lub narzędzia AAC. Do uzyskania rzeczywistego
  raportu z pięciu sygnałów nie deklarować osiągnięcia 23/23.
- Nadal brak natywnego dekodera AAC-LC stereo, SBR, PS i
  pełnej obsługi opcjonalnych narzędzi. Nie aktywować
  bezwarunkowego zastępowania FFmpeg w odtwarzaczu.


## AAC-26 — start natywnego CPE/stereo (2026-11-01)

- Dodano `sauti_aac_cpe.c`, `include/sauti_aac_cpe.h`,
  `sauti_aac_lc_cpe_parse` i
  `sauti_aac_lc_adts_stereo_decode_ex`.
- Obsługiwane podstawowe `ID_CPE` z indywidualnymi ICS oraz
  `common_window` bez aktywnych bitów Mid/Side; oba kanały
  mają osobne stany syntezy (IMDCT, overlap-add, PNS seed).
- Odczyt: global_gain, sections, scalefactors, PNS, TNS,
  spectral_data każdego kanału. Wyjście jest float32
  interleaved LRLR (2048 próbek na klatkę ADTS).
  Zarówno stan kanałów, jak i PCM, są zatwierdzane atomowo.
- `ms_mask_present=2` lub ustawione bity MS przy
  `ms_mask_present=1` są **jawnie odrzucane**, podobnie jak
  intensity stereo; nie wolno maskować braku rekonstrukcji
  stereo jako poprawnego dekodowania.
- `make test-aac-cpe PKG_CONFIG=/bin/false` bada CPE
  z niezależnymi oknami, wspólnym oknem i zerową maską MS,
  a także odmowę MS i cofnięcie po błędzie.
- `make inspect-aac-stereo PKG_CONFIG=/bin/false` tworzy
  zewnętrznie próbkowany strumień dwóch sinusoid 440/880 Hz
  przez FFmpeg CLI i analizuje, jakie narzędzia użył koder.
  **Nie oczekujemy jeszcze pełnego 23/23**: typowe stereo
  używa MS, które trzeba dodać osobno.

AAC-26 jest etapem przyrostowym, NIE ukończeniem AAC-LC stereo.
Dalsze zadania: dekodowanie i zastosowanie per-band maski MS
na wspólnych widmach, Intensity Stereo, stereo-PNS, transjenty,
pełne porównanie PCM lewego/prawego kanału z FFmpeg.

Diagnostyka odróżnia teraz `mid_side` od `intensity`; oba
narzędzia nie są jeszcze wykonywane w torze CPE. Próbki stereo
z `ms_mask_present=1/2` i aktywną maską muszą pozostać jawnie
odrzucone do czasu implementacji rekonstrukcji dwóch widm.


## AAC-27 — natywna rekonstrukcja Mid/Side dla stereo (2026-11-01)

W diagnostyce AAC-26 rzeczywiste stereo (440 Hz + 880 Hz)
zostało rozpoznane jako `mid_side`: 0 z 21 ramek PCM, zero
nieprawidłowych ramek. To brak narzędzia stereo, nie błąd formatu.

AAC-27 dodaje:
- Zachowanie `ms_used[group][sfb]` dla masek
  `ms_mask_present=0/1/2` wewnątrz CPE.
- Po odkwantowaniu obu kanałów, przed TNS i IMDCT,
  rekonstrukcję `L = M + S`, `R = M - S` na wskazanych
  pasmach, osobno dla każdego okna/grupy, bez sztucznego
  skalowania 1/sqrt(2).
- Oddzielny punkt wejścia syntezy z już przygotowanym
  widmem `sauti_aac_lc_synthesis_spectral`; istniejące
  API mono zachowują się jak wcześniej.
- Jawne odrzucenie MS na pasmach PNS/intensity
  (`mid_side`) do czasu poprawnej obsługi skorelowanego
  szumu stereo. Nie wyciszać takich pasm.
- Regresję z niezerowym widmem i maskami 0/1/2.
- `make inspect-aac-stereo PKG_CONFIG=/bin/false`
  generuje odtwarzalną próbkę AAC-LC ADTS stereo,
  wywołuje dekoder natywny, a następnie raportuje
  osobno lewy i prawy PCM względem FFmpeg CLI.

**Wynik rzeczywistego PCM i pełna ciągłość stereo weryfikujemy
dopiero po uruchomieniu matrycy.** AAC-27 nie oznacza jeszcze
obsługi Intensity Stereo, PNS skorelowanego z MS ani SBR/PS.


## AAC-28 — Intensity Stereo (2026-11-01)

W AAC-27 CPE stereo na rzeczywistym 440/880 Hz:
16/16 testów modułów PASS, ale dekoder osiągnął tylko 1/21
ramek (kolejne posiadały `intensity` lub odrzucano je
ze względu na brak historii po pominięciu).

AAC-28:
- Dopuszczono pseudo-codebooki 14 (odwrócona faza)
  i 15 (ta sama faza) **wyłącznie po prawej stronie**
  elementu CPE z `common_window`.
- PNS ignoruje pasma intensity (nie wytwarza dla nich szumu);
  zwykła ścieżka mono nadal odrzuca intensity.
- Po odkwantowaniu obu kanałów i Mid/Side, przed TNS,
  prawe widmo intensity jest rekonstruowane z lewego:
  `R = L * (cb==15 ? +1 : -1) *
  (ms_used ? -1 : +1) * 2^(-is_pos/4)`.
  `is_pos` pochodzi z dekodera delta skalefaktorów.
  Maski MS na pasmach intensity zmieniają wyłącznie fazę,
  nie uruchamiają zwykłego sumowania M/S na tych pasmach.
- Test CPE sprawdza 14/15, inwersję fazy maską MS 1/2,
  zerowe i niezerowe `is_pos` oraz niezerowy PCM.
- Zakazane wciąż połączenie intensity z nośnym pasmem
  PNS; brak prób przybliżania nieobsługiwanej
  korelacji szumu stereo.
- Po uruchomieniu:
  `make -B test-aac-native PKG_CONFIG=/bin/false &&
   make inspect-aac-stereo PKG_CONFIG=/bin/false`.
  Dopiero porównanie PCM kanału LEFT i RIGHT z referencją
  pozwala nazwać obsługę rzeczywistego stereo potwierdzoną.


## AAC-29 — reprodukowalna matryca stereo / rygorystyczna walidacja

Cel: odróżnić poprawne dekodowanie pełnego strumienia od
częściowej zgodności PCM. W AAC-28 pojedynczy strumień
440 Hz / 880 Hz osiągnął 21/21 ramek, korelacje
LEFT=0.999999967 i RIGHT=0.999999945, ale to
**nie jest jeszcze pełna zgodność AAC-LC**.

Nowe narzędzia:
- `tools/generate_aac_stereo_fixtures.py` generuje trzy
  **autorskie, syntetyczne** sygnały stereo: `tonal`,
  `transient`, `dense` (instrumenty, bas, impulsy, szum),
  zapisane jako stereo 16-bit WAV. Nie wymagają pobierania
  ani wykorzystywania chronionych nagrań.
- `scripts/inspect_aac_stereo_matrix.py` koduje z FFmpeg CLI
  3 rodzaje materiału x 3 częstotliwości (32000/44100/48000)
  x 4 bitrate (64/128/192/256 kb/s): **36 wariantów**.
  Nie korzysta z bibliotek FFmpeg podczas dekodowania Sauti.
- `tools/verify_aac_stereo_strict.py` **zwraca kod błędu**,
  jeśli pominięto choć jedną ramkę lub jeśli którykolwiek
  kanał nie spełnia metryk: korelacja >= 0.999,
  native/ref RMS w [0.99,1.01],
  normalized RMSE <= 0.03, minimum 1024 porównanych próbek.
  Test wymaga ciągłości dekodowania wszystkich ramek i
  zgodnych długości danych. Test nie dopasowuje wzmocnienia
  ani przesunięcia czasowego (`lag=0`). Pomija tylko
  pierwszą ramkę jako rozgrzewkę overlap.
- `tests/test_aac_stereo_strict.py` sprawdza, że brak
  ramki, błąd fazy prawego kanału, niewłaściwy poziom
  lewego, NaN lub niewłaściwa długość prowadzą do porażki.

Polecenia:

```sh
make -B test-aac-native PKG_CONFIG=/bin/false
make inspect-aac-29-smoke PKG_CONFIG=/bin/false
make inspect-aac-29-full PKG_CONFIG=/bin/false
```

`smoke` obejmuje trzy reprezentatywne przypadki,
`full` wszystkie 36. W obu trybach tworzony jest
`build/audio-fixtures/aac-29/summary_<mode>.json`
oraz log i maska dla każdego wariantu.
Każdy nieudany wariant jest raportowany, a całe polecenie
kończy się błędem. Oczekiwany sukces **nie jest
deklarowany przed wykonaniem rzeczywistej matrycy**.
Nadal wymagane testy uszkodzonych strumieni i
nieobsługiwanych kombinacji stereo PNS/MS.


### AAC-29 — analiza niezgodności transjentów

Pełna matryca z Fedory: **26/36 PASS**. Wszystkie 36
strumieni mają pełne dekodowanie ramek, ale 10 z 12
`transient` przekracza próg PCM (normalized RMSE 3%,
czasem także korelację 0.999). Wszystkie `tonal` i
`dense` mają 12/12 PASS. To **błąd jakości**, nie błąd
składni ani ucięta historia dekodowania. Nie luzować progu.

Do ustalenia przyczyny, bez zmian w torze PCM, dodano:
- `tools/diagnose_aac_stereo_frames.py native.f32 reference.f32 mask`
  — identyfikuje ramki, kanały i numery próbek
  odpowiadające największemu błędowi;
- opcjonalny `SAUTI_AAC_TRACE_WINDOWS=1` w
  `build/test_aac_real_stereo`, pokazujący ICS window_sequence
  i window_shape obu kanałów po każdej poprawnej ramce.

Do sprawdzenia po diagnostyce: czy błędy skupiają się
przy `EIGHT_SHORT_SEQUENCE` i przejściach
`LONG_START/LONG_STOP`, czy pojawiają się przy długich
oknach. Dopiero potem lokalizować zmianę w syntezie
okien/TNS; te dane nie dowodzą jeszcze źródła błędu.


### AAC-29 — zawężenie na realnym `transient_44100_128k`

Na 24/24 zdekodowanych ramkach największa rozbieżność PCM
występuje dla krótkich okien `EIGHT_SHORT_SEQUENCE`
w ramkach 1 i 22 (LEFT nRMSE odpowiednio 8,97% i 13,01%).
W obu ramkach TNS=0/0, natomiast obecny jest PNS,
Mid/Side i Intensity Stereo. Ramka 11 ma również krótkie
okna, lecz TNS=1/1, a nie jest jednym z największych
błędów. Zatem samo TNS nie może wyjaśnić obu głównych
odchyleń.

Dodano eksperyment 2x2 na **tym samym wejściowym WAV**
oraz bitrate i częstotliwości: przełączniki kodera FFmpeg
`-aac_pns 0/1` i `-aac_tns 0/1`. Każdy wariant tworzy
oddzielny AAC, dekoduje natywnie i w FFmpeg oraz
porównuje oba kanały według niezmienionych progów:

```sh
make inspect-aac-29-tools PKG_CONFIG=/bin/false
```

Wyniki: `build/audio-fixtures/aac-29/ablation/transient_44100_128k/`.
Warianty służą **identyfikacji źródła odchyleń**;
wyniki `-aac_pns 0` nie zastępują pełnej
matrycy z domyślnym koderem i nie stanowią
dowodu pełnej poprawności PNS. Ponieważ PNS generuje
szum pseudolosowy, różne ziarna generatora w dwóch
dekoderach mogą dać różnice próbka-po-próbce mimo
prawidłowej energii pasma.


### AAC-29 — korekta deterministycznej sekwencji PNS

Kontrolowany eksperyment `transient_44100_128k` z Fedory
pokazał: wszystkie 24/24 ramki dekodowane poprawnie;
oba warianty z włączonym PNS FAIL, oba z wyłączonym
PNS PASS, niezależnie od TNS. Zgodność dla `-aac_pns 0`
potwierdza, że różnica PCM dotyczy generacji szumu.

Po porównaniu z referencyjnym FFmpeg AAC (bez kopiowania
kodu źródłowego) wdrożono trzy małe zmiany:
- inicjalizacja ziarna generatora stanem `0x1f2e3d4c`,
  zamiast lokalnego `0x12345678`;
- szum pochodzi bezpośrednio z **signed 32-bit** wyniku
  LCG, bez wcześniejszego odwracania bitu znaku;
- stereo CPE konsumuje jeden ciąg RNG — najpierw pasma
  lewego, potem prawego kanału. Stan obu kanałów jest
  zatwierdzany atomowo po udanym dekodowaniu ramki.

Test syntetyczny sprawdza, czy ziarno rozwija się o
liczbę próbek PNS w **obu** kanałach i pozostaje ciągłe
między dwiema ramkami. Kod źródłowy samego DSP/SFB
pozostaje bez zmian. Wciąż wymagana weryfikacja
36 przypadków na Fedorze; nie deklarować poprawy
wyniku przed pomiarem. Szczegółowa zgodność PCM PNS
z referencją zależy też od normalizacji i narzędzi
stereo, więc ewentualny FAIL wymaga kolejnej analizy.


### AAC-29 — ponowna matryca po zmianach RNG (wynik rzeczywisty)

Ponowny przebieg `make inspect-aac-29-full` na Fedorze
dał ponownie **26/36 PASS**. Wszystkie moduły natywne
przeszły; nadal odpada 10 wariantów `transient`.
Zmiana ziarna generatora, reprezentacji znaku i
wspólnego RNG stereo **nie poprawiła korelacji** w tych
strumieniach. Nie należy twierdzić, że udało się
osiągnąć zgodność z FFmpeg tylko dzięki RNG.

Dodano osobną pełną matrycę kontrolną:

```sh
make inspect-aac-29-no-pns PKG_CONFIG=/bin/false
```

Korzysta ona z `-aac_pns 0` **wyłącznie podczas kodowania
próbek kontrolnych**, i zapisuje wyniki osobno w
`build/audio-fixtures/aac-29/encoder-pns-off/`.
Nie zastępuje domyślnej matrycy z włączonym PNS i nie
zmienia progów jakości ani sposobu oceniania PCM.
Jej cel: sprawdzić we wszystkich 36 konfiguracjach,
czy bez aktywnego narzędzia PNS natywne AAC-LC
stereo pozostaje zgodne z referencją.

Dla PNS standardowe normowanie energii i niezależna
realizacja widma losowego mogą powodować różnice
próbek PCM między poprawnymi dekoderami. Zgodność
próbek z jednym dekoderem nie jest wymogiem
normatywnym dla pseudolosowego szumu. Rygorystyczny
test PCM pozostaje przydatny diagnostycznie, ale
należy uzupełnić go niezależnymi testami energii PNS
i stereofonii — nie oznaczać automatycznie
rozbieżności losowego PCM jako niezgodności ze
standardem AAC.


## AAC-29B — niezależne testy PNS

Do dotychczasowego `test-aac-pns` dodano kontrolę:
- energii **każdego** z dwóch pasm PNS w kolejnych
  grupach okien i oddzielnie w każdym oknie;
  energia widma powinna wynosić
  `2^((scalefactor - 100)/2)`;
- różnych wartości skalefaktorów w pasmach,
  oraz braku modyfikacji pozostałych pasm;
- deterministycznej reprodukcji dla tego samego
  stanu LCG oraz różnego szumu dla różnych ziaren;
- zgodności z oczekiwaną liczbą kroków generatora
  przy przejściu przez pasma/okna;
- ciągłej sekwencji RNG przez dwa kanały stereo
  (bez ponownego inicjowania ziarna);
- atomowego wycofania zmian w widmie i stanie
  generatora po błędzie danych.

Uruchomienie:

```sh
make -B test-aac-pns test-aac-cpe PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granica tego etapu:** testy sprawdzają normowanie
energii, inicjalizację i ciągłość RNG dla PNS, ale
nie dowodzą jeszcze prawidłowego *skorelowanego PNS*
sterowanego maską MS w CPE. Obecny dekoder nadal
odrzuca ten przypadek; nie należy deklarować pełnej
obsługi wszystkich kombinacji AAC-LC.
Powtórzenie 36/36 PASS przy kodowaniu bez PNS jest
punktem kontrolnym niepodlegającym zmianie.


## AAC-29C — skorelowany PNS stereo

Po 36/36 PASS w matrycy AAC bez PNS oraz poprawnym
zestawie AAC-29B dodano obsługę wspólnego szumu PNS
sygnalizowanego flagą `ms_used` w CPE.

- Jeśli pasmo obu kanałów ma codebook `13` i `ms_used=1`,
  oznacza to korelację szumu, a **nie** klasyczną operację
  widmową `Mid/Side`. Widmo prawego kanału otrzymuje
  tę samą realizację szumu co kanał lewy, przeskalowaną
  proporcją zakodowanych energii
  `2^((noise_sf_right-noise_sf_left)/4)`.
- Dwa pasma kodowane PNS bez flagi `ms_used` pozostają
  niezależnymi realizacjami szumu.
- Mieszane PNS/spectral przy `ms_used=1` pozostają
  wyraźnie odrzucane jako nieobsługiwane; nie
  wykonywać na nich zwykłej rekonstrukcji M/S.
- Zmiany stanu dekodera i PCM nadal są atomowe; na
  błędzie nie wolno ich zatwierdzać.

Test `test-aac-cpe` zawiera syntetyczne rzeczywiste
ramki ADTS z maską `ms_mask_present=1` i `2`,
z różnicą skalefaktorów `0` i `4` oraz kontrolą
stosunku energii na dwóch kolejnych ramkach.

```sh
make -B test-aac-cpe test-aac-pns PKG_CONFIG=/bin/false
make inspect-aac-29-no-pns PKG_CONFIG=/bin/false
make inspect-aac-29-full PKG_CONFIG=/bin/false
```

**Uwaga:** testy nowych ramek są regresją funkcjonalną,
nie są jeszcze kompletną certyfikacją zgodności wszystkich
wariantów PNS stereo. Porównanie próbka-po-próbce z FFmpeg
przy włączonym PNS może różnić się z powodu generatora
pseudolosowego.


## AAC-29D — regresja uszkodzonych ramek i wznowienia

Do `tests/test_aac_cpe.c` dodano przypadki dla
**rzeczywistych ramek ADTS skonstruowanych testowo**:
ucięta ramka, uszkodzony syncword, niepoprawny indeks
częstotliwości, niezgodna konfiguracja kanałów,
zawyżona długość ramki, niepoprawne bity dopełnienia
oraz zarezerwowana wartość maski stereo. Sprawdzane
są też ucięte prefiksy surowego CPE.

Każdy nieudany dekod ma pozostawić bez zmian:
- oba stany syntezy (overlap, sequence/shape, PNS RNG);
- cały bufor PCM dostarczony przez użytkownika;
- zawartość struktury wyjściowej parsera CPE
  oraz wejściowy kursor bitowy.

Po każdym takim błędzie następuje dekodowanie
**poprawnej** ramki z zachowanego stanu, a wynik PCM
i stan końcowy są porównywane z niezaburzonym
przebiegiem referencyjnym tego samego Sauti.

```sh
make -B test-aac-cpe PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

Ten zestaw nie jest fuzzingiem ani kompletnym
audytorem bezpieczeństwa parsera. Dalsze testy
sanityzowane oraz realne uszkodzone pliki pozostają
oddzielnym zadaniem.


## AAC-29E — ASan/UBSan dla testów odporności CPE

Osobna kompilacja `build/test_aac_cpe_san` buduje
cały tor C dekodera CPE i testy AAC-29D z
`-fsanitize=address,undefined`, `-fno-omit-frame-pointer`,
`-O1 -g`. Obiekty NASM są linkowane bez zmian,
więc nie są instrumentowane sanitizerami.

```sh
make test-aac-sanitize PKG_CONFIG=/bin/false
```

Testy sprawdzają błędne nagłówki i długości ADTS,
maski stereo, ucięte CPE, atomowość zmian stanu i PCM
oraz poprawne wznowienie po błędzie. ASan/UBSan
uzupełniają asercje logiki dekodera, ale nie zastępują
fuzzingu ani pełnej analizy assemblera. Normalny
`test-aac-native` pozostaje osobną, niezmienioną
ścieżką.


### AAC-29E — deterministyczne mutacje bitów ADTS

Po pomyślnym uruchomieniu ASan/UBSan na Fedorze
rozszerzono `test_aac_cpe` o serię wszystkich
jednobitowych mutacji krótkiej, poprawnej ramki
ADTS stereo. Każda mutacja uruchamia dekoder od
tego samego stanu początkowego. Sprawdzane są:

- zwrot 1/-1/0 zamiast nieprawidłowej wartości;
- przy poprawnym dekodowaniu wszystkie 2048 próbek
  PCM są skończone, a oba stany pozostają aktywne;
- przy odrzuceniu ramki nie zmienia się ani bufor
  PCM, ani którykolwiek stan syntezy;
- test wykonuje się także w celu `test-aac-sanitize`.

Ten test **nie jest** fuzzingiem z pokryciem ani
formalnym dowodem bezpieczeństwa wszystkich strumieni.
```sh
make -B test-aac-cpe PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```


## AAC-LC — integracja z odtwarzaczem (pierwszy etap)

Nowy `sauti_decoder_aac_native_ops` wykorzystuje
**bezpośrednio natywny C/NASM AAC-LC stereo** w
interfejsie `SautiDecoderOps` (bez bibliotek FFmpeg).
Wymaga strumienia ADTS z jednym blokiem raw AAC
na ramkę, AAC-LC, 2 kanały, bez CRC. Nie dekoduje
kontenera MP4/M4A ani profili SBR/PS.

- `probe/open`: waliduje nagłówki ADTS wszystkich ramek i
  dekoduje **tylko pierwszą ramkę PCM**. Odrzuca strumień
  z błędnym nagłówkiem lub pierwszą nieobsługiwaną ramką.
  Pozostałe ładunki AAC dekoduje dopiero podczas odczytu.
  Jeśli natrafi na nieobsługiwaną lub uszkodzoną późniejszą
  ramkę, zatrzymuje odczyt (aktualne API nie rozróżnia
  takiego błędu od EOF — ograniczenie do rozwiązania).
- `read`: generuje PCM 32-bit float LRLR po 1024
  próbki na kanał, buforując tylko jedną ramkę;
  nie trzyma w pamięci całego PCM.
- `seek`: odtwarza historię overlap, okien i PNS
  od początku pliku do żądanej pozycji. Jest poprawne
  semantycznie, ale może być wolne w długich plikach;
  przyszły indeks checkpointów przyspieszy seek.
- `close`: zwalnia indeks ramek; wejściowe bajty
  pozostają własnością wywołującego.
- Odtwarzacz SDL próbuje natywnego AAC-LC ADTS
  **przed** dotychczasowym adapterem libavcodec,
  który pozostaje fallbackiem dla innych profili
  i kontenerów. Gdy native zawiedzie, ta ścieżka
  nadal może użyć FFmpeg — nie deklarować wtedy
  odtwarzania bez FFmpeg.

Pierwszy test `test-aac-native-adapter` buduje
syntetyczny dwu-ramkowy strumień ADTS i sprawdza
otwieranie, metadane, odczyt, przejście przez granicę
ramek, przewijanie do próbki wewnątrz ramki,
EOF, granice zakresu oraz odrzucenie uszkodzeń.

```sh
make test-aac-native-adapter PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Nie zamyka to jeszcze AAC-LC.** Wymagane jest
potwierdzenie kompilacji, realne wielominutowe
pliki stereo, test odsłuchowy, przewijanie i dalsza
walidacja przypadków PNS. Natywny dekoder nie jest
jeszcze deklarowany jako kompletny MPEG-4 AAC-LC.


### Odtwarzanie prawdziwego strumienia AAC-LC przez adapter

Rozszerzony `test_decoder_aac_native` może przyjąć
ścieżkę do istniejącego pliku ADTS AAC-LC stereo.
Otwiera go **wyłącznie natywnym SautiDecoderOps**,
sprawdza PCM oraz powtarzalność przewijania do próbki
wewnątrz ramki (identyczny wynik po odczycie
pośredniego fragmentu).

Po wygenerowaniu dotychczasowej matrycy:

```sh
./build/test_decoder_aac_native build/audio-fixtures/aac-29/encoder-pns-off/transient_44100_128k.aac
```

Podana ścieżka zależy od układu plików fixture
w danej wersji skryptu; test nie tworzy własnego
pliku audio i nie uruchamia FFmpeg.


### AAC-LC — usunięcie blokującego predekodowania (długie pliki)

Przy testowaniu nagrania 90,87 s wyszło na jaw, że
poprzednia implementacja `open` synchronicznie
dekodowała cały strumień AAC-LC przed pierwszym
odtworzeniem dźwięku, a potem ponownie podczas
`read`. Dla tysięcy ramek blokowało to obsługę UI.

Teraz `open` buduje szybki indeks nagłówków ADTS,
dekoduje tylko pierwszą ramkę i pozostawia ją
w cache. Kolejne ramki dekodowane są na żądanie.
`seek` nadal odtwarza historię od początku i może
być powolny przy dużym przesunięciu; dla długich
nagrań potrzebne będą checkpointy stanu i
odseparowanie dekodowania od głównej pętli UI.

```sh
make test-aac-native-adapter PKG_CONFIG=/bin/false
make window-pipewire
./build/sauti_window_pw --source monitor
```


### AAC-LC — checkpointy historii przy przewijaniu

Po zgłoszeniu zawieszenia i zrzutu pamięci podczas
przewijania 90-sekundowego utworu dodano zapisy
**pełnego stanu obu kanałów** co 64 zdekodowane
ramki ADTS (overlap, okna, PNS RNG). Przy przewinięciu
do obszaru już wcześniej odtworzonego dekoder
odtwarza stan z najbliższego checkpointu i dekoduje
najwyżej 63 wcześniejsze pełne ramki; test obejmuje
granice 64 i 128 ramek.

**Ograniczenie:** pierwsze przewinięcie daleko naprzód,
poza zdekodowany zakres, nadal wykonuje synchroniczny
replay od najbliższego checkpointu i może blokować UI.
Nie zidentyfikowano jeszcze przyczyny samego zrzutu
pamięci — wymagany jest backtrace z coredumpctl/GDB.
Checkpointy są optymalizacją, nie potwierdzoną naprawą
crasha. W przyszłości niezbędne będą asynchroniczne
seek i propagacja błędów czytania PCM.


### AAC-LC — poprawa IMDCT i asynchroniczne przewijanie

Przeanalizowany coredump z Fedora 44 (2026-11-02,
SIGABRT) wskazał stos `sauti_mp3_sdl_seek → seek_native →
decode_next → sauti_aac_lc_synthesis_spectral → sauti_aac_imdct`.
Nie przesądza to jeszcze, dlaczego proces wykonał `abort`,
ale wyraźnie lokalizuje kosztowną ścieżkę przewijania.

- Stara referencyjna IMDCT wykonywała O(N²) obliczeń.
  Nowy kod używa natywnego radix-2 FFT długości 8N
  i wyznacza składowe IMDCT bez zmiany skalowania 2/N:
  `Re(FFT[2*t+N+1])` dla wejścia w indeksach
  nieparzystych `2*k+1`. Złożoność O(N log N).
  Dotychczasowe testy kosinusowej referencji weryfikują
  N=128 i N=1024, także gęste widma.
- Przewijanie natywnego AAC zostało przeniesione do
  osobnego wątku SDL. Pętla interfejsu nie wykonuje
  już synchronicznego `sauti_decoder_seek` dla tego kodeka.
  Kolejne przesunięcia suwaka są kolejkowane do ostatniej
  żądanej pozycji. Podczas działania wątku `pump`
  nie czyta równocześnie stanu dekodera.
  `destroy` oraz `stop` dołączają wątek, zanim
  zwolnią stan lub bufor wejściowy.
- Nadal wymagamy praktycznego potwierdzenia szybkiego
  przewijania i braku SIGABRT na długim nagraniu.
  Odczyt PCM po przewinięciu odbywa się wciąż
  w pętli UI; na przyszłość potrzebny może być
  oddzielny worker odtwarzania.

Test:
```sh
make test-aac-imdct test-aac-native-adapter PKG_CONFIG=/bin/false
make bench-aac-imdct PKG_CONFIG=/bin/false
make window-pipewire
./build/sauti_window_pw --source monitor
```


### AAC-LC — ciągłość dźwięku podczas przewijania

Po teście na 90,91-sekundowym `1.aac` na Fedorze
potwierdzono brak zrzutu pamięci i możliwość
przewijania, ale użytkownik zgłosił około 0,5–1 s
ciszy po przesunięciu paska.

Poprawiono logikę kolejki SDL: przy żądaniu
asynchronicznego seek **nie usuwa się od razu**
już zakolejkowanych próbek. Wątek rekonstrukcji
historii AAC pracuje, gdy SDL nadal odtwarza
wcześniejsze PCM. Dopiero gdy ostatnia żądana
pozycja zostanie przygotowana, wątek usuwa
starą kolejkę SDL i ustawia nową pozycję odczytu;
`pump` następnie uzupełnia PCM. Dla natywnego
AAC kolejka ma bufor docelowo ok. 2 s zamiast
0,5 s, co zwiększa margines przy długich seek.

To kompromis: podczas przygotowania nowej
pozycji słychać przez krótki czas **stary fragment**
utworu; przełączenie nie musi być natychmiastowe.
Dla odległych seek dłuższych niż czas bufora
wciąż może wystąpić przerwa; mechanizm nie
gwarantuje bezprzerwowości przy każdej pozycji.
Inne kodeki zachowują dotychczasową kolejkę.


### AAC-LC — stabilna pozycja suwaka przy asynchronicznym seek

Test użytkownika potwierdził brak przerwy w audio,
ale pasek po kliknięciu pokazywał przez moment
nowy czas, wracał do starego i następnie skakał
ponownie na nowy. Przyczyną był interfejs:
`sauti_mp3_sdl_position` raportował pozycję
starego odtwarzanego PCM do czasu zakończenia
seek, mimo że użytkownik wskazał już nową pozycję.

Podczas asynchronicznego seek funkcja pozycji
raportuje teraz ostatnią żądaną próbkę
(`seek_request`) pod mutexem. Po zakończeniu
handoff ponownie używa rzeczywistej pozycji SDL
(`submitted - queued`). Inne kodeki bez wątku
seek zachowują poprzednią ścieżkę.


### AAC-29 — diagnoza 10/36 FAIL przy aktywnym PNS

Na Fedorze matryca 36/36 przechodzi z `-aac_pns 0`,
natomiast domyślny enkoder FFmpeg daje 26/36,
z 10 niezgodnościami PCM tylko w materiale transient.
Nie wolno osłabiać progów strict gate.

Przegląd ścieżki FFmpeg `aacdec_proc_template.c` oraz
`aacdec_dsp_template.c` wykazał różnicę znaku:
dla `NOISE_BT` FFmpeg ustawia ujemny skalefaktor
w dekoderze float i skaluje nim próbki generatora
LCG. Sauti przed poprawką używał dodatniego
`gain/sqrt(energy)`, przez co odwracał znak pasm
PNS względem FFmpeg, choć energia, stan RNG i
testy normalizacji były poprawne. Zmieniono
skalowanie na `-gain/sqrt(energy)` i dodano
asercję znaku dla deterministycznego pierwszego
prążka PNS zarówno dla długich, jak krótkich okien.
To hipoteza przyczynowa do weryfikacji pełną
matrycą (wyniki sprzed poprawki: 26/36).

Dodano także diagnostykę:
```sh
make test-aac-native test-aac-sanitize PKG_CONFIG=/bin/false
make inspect-aac-29-full
python3 scripts/diagnose_aac_pns_frames.py --case transient_44100_128k
```
Raport diagnostyczny zestawia nRMSE każdej ramki
z jej licznikami PNS/TNS/MS/intensity i typem okna,
bez zmiany walidatora ani progów testowych.


### AAC-29 — wynik po zmianie znaku i dalsza izolacja

Po zmianie znaku skalowania PNS ponowna pełna matryca
nadal daje 26/36 PASS, a testy natywne i sanitizery PASS.
Nie ma potwierdzenia, że znak był przyczyną. Diagnostyka
`transient_44100_128k` (24 ramki) wykazała PNS w 23
ramkach, zerowy błąd jedynej ramki bez PNS oraz
duże błędy szczególnie w ramkach 0 (nRMSE 0,944 L),
22 (0,130 L) i 1 (0,090 L). Nie należy mylić
tej lokalizacji z dowodem przyczyny.

Rozszerzono diagnostykę o przejścia 32-bitowego
stanu PNS RNG przed/po każdej ramce, aby sprawdzić
deterministyczną konsumpcję pseudolosowych próbek
przez wspólne stereo i zmiany okien.

```sh
make test-aac-native PKG_CONFIG=/bin/false
python3 scripts/diagnose_aac_pns_frames.py --case transient_44100_128k --top 24
```

Stan RNG jest lokalnie deterministyczny; docelowo
trzeba go porównać z niezależną implementacją
lub śladem FFmpeg, zanim zmienimy kolejność
pasm, współdzielenie szumu czy resztę syntezy.


### AAC-29 — izolacja stereo MS/PNS bez zmiany produkcyjnego dekodera

Z diagnostyki `transient_44100_128k` wynika, że stan LCG
jest ciągły, a liczba wywołań generatora odpowiada
faktycznym szerokościom pasm PNS w obu kanałach:
ramka 0: 892, ramki 1 i 22: po 136, ramka 23: 0.
Nie potwierdza to jeszcze kolejności wartości
ani poprawności wynikowego PCM.

Porównanie z referencyjną ścieżką float FFmpeg
(`aacdec_dsp_template.c`) pokazało, że jej
mid/side butterfly pomija pasma NOISE_BT,
podczas gdy Sauti dodatkowo koreluje prawe
pasmo PNS z lewym. To różnica do zbadania,
nie wystarczający dowód błędu Sauti.

Dla rozstrzygnięcia dodano **wyłącznie
diagnostyczną** kompilację
`build/test_aac_real_stereo_pns_independent`
z makrem `SAUTI_AAC_DIAG_INDEPENDENT_PNS`.
Porównuje ona wynik bez korelacji PNS z
produkcyjnym algorytmem Sauti, używając
niezmienionego walidatora i FFmpeg PCM:
```sh
python3 scripts/compare_aac_pns_stereo_modes.py --case transient_44100_128k
```
Drugi wariant nie jest używany przez odtwarzacz,
nie zmienia domyślnego testu AAC-29 i nie stanowi
obejścia testów zgodności. Jeśli korelacja w Sauti
jest przyczyną, wariant powinien zmniejszyć
nRMSE; jeśli nie, należy zbadać próbki PNS i
ścieżkę syntezy niezależnie.

## HE-AAC v1 / SBR-01 — natywna siatka czasowa obwiedni

Pierwszy samodzielny etap HE-AAC v1 w gałęzi `feature/aac-family-asm`.
Dodano `sauti_aac_sbr_grid_parse` bez FFmpeg i bez alokacji.
Parser korzysta z istniejącego czytnika bitów NASM i obsługuje
klasy `FIXFIX`, `FIXVAR`, `VARFIX`, `VARVAR` klasycznego SBR:
- 1–5 obwiedni oraz granice czasowe dla 15/16 szczelin,
- flagi rozdzielczości częstotliwości, `bs_pointer`,
- granice 1 lub 2 obwiedni szumowych,
- odrzucenie niepoprawnych, niemonotonicznych granic,
  niedozwolonej liczby obwiedni i uciętych danych,
- brak zmiany kursora lub wyniku po błędzie (fail-closed).

Testy syntetyczne obejmują wszystkie cztery klasy,
niebajtowe pozycje startowe, niepoprawny wskaźnik,
kolizję granic i ucięcie bitstreamu.

```sh
make test-aac-sbr-grid PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Zakres:** rozpoznawanie siatki SBR to dopiero etap składniowy.
Nie implementuje jeszcze dekodowania obwiedni Huffmana,
częstotliwościowej tabeli pasm, filtrów QMF ani syntezy
wysokich częstotliwości. Nie uruchamia HE-AAC w odtwarzaczu;
nie zmienia wyboru dekodera AAC-LC ani jego progów testów.
Wyniki powyższych testów na Fedorze wymagają potwierdzenia;
nie deklarować PASS przed rzeczywistym uruchomieniem.

## HE-AAC v1 / SBR-02 — liniowa tabela pasm master

Zaimplementowano niezależny generator `sauti_aac_sbr_master_linear`
dla przypadku `bs_freq_scale=0` i `bs_alter_scale=0/1`.
Generator przyjmuje wcześniej obliczone poprawne `k0` i `k2`,
buduje rosnące granice pasm QMF, oblicza `kx` z `bs_xover_band`
i gwarantuje brak modyfikacji wyniku po niepowodzeniu.
Arytmetyka jest całkowitoliczbowa; nie wymaga FFmpeg, QMF ani
zewnętrznych bibliotek. Testy weryfikują parzyste/nieparzyste
szerokości, korekty -2/-1/+1, granice, crossover i fail-closed.

```sh
make test-aac-sbr-master PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Ograniczenia:** to nie jest pełna tabela częstotliwości SBR.
Jeszcze nie obliczamy `k0` i `k2` z częstotliwości próbkowania
oraz `bs_start_freq`/`bs_stop_freq`; ten obowiązek pozostaje
po stronie przyszłego etapu. Brak logarytmicznych trybów
`bs_freq_scale=1..3` i wyznaczania tabel high/low/noise.
Nie podłączać jeszcze HE-AAC do odtwarzacza ani nie ogłaszać
dekodowania SBR. CI i test na Fedorze wymagają potwierdzenia.


## HE-AAC v1 / SBR-03 — logarytmiczna tabela pasm master

Nowy, własny generator `sauti_aac_sbr_master_log` implementuje
logarytmiczne tryby `bs_freq_scale=1,2,3` i `bs_alter_scale=0,1`.
Buduje jedną lub dwie sekcje częstotliwości, zgodnie z progiem
podziału zakresu, i wyznacza rosnące granice pasm master
z wyrównaniem szerokości i opcjonalnym warping.
Nie korzysta z FFmpeg ani innych dekoderów; wymaga `libm`
wyłącznie do operacji matematycznych (`powf`, `log2f`, `lrintf`).

Testy obejmują wektory o ustalonych granicach, oba warianty
liczby sekcji, wszystkie skale, obie wartości warping, crossover
i przegląd 72 960 kombinacji k0=1..32, k2=k0+1..64, xover=0..7.
Odrzucone dane nie modyfikują wyjścia (fail-closed).
Testy przeszły lokalnie z `-O2 -Wall -Wextra -Werror` oraz
`-fsanitize=address,undefined` w środowisku roboczym.
Wyniki Fedory i CI należy potwierdzić osobno.

```sh
make test-aac-sbr-master-log PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Zakres:** moduł nie oblicza jeszcze `k0`/`k2` z nagłówków
`bs_start_freq`/`bs_stop_freq` i częstotliwości próbkowania
ani nie tworzy tabel high/low/noise. HE-AAC nadal nie produkuje
własnego PCM SBR; filtr QMF, odwzorowanie pasm i obwiednie
pozostają kolejnymi etapami. Nie zmieniać progów AAC-LC.

## HE-AAC v1 / SBR-04 — parametry start/stop QMF (k0/k2)

Zaimplementowano `sauti_aac_sbr_limits_compute` z niezależnymi tablicami
przesunięć `bs_start_freq` i obliczaniem `bs_stop_freq` z 13
posortowanych przyrostów geometrycznych; wartości 14/15 oznaczają
odpowiednio 2*k0 / 3*k0 z ograniczeniem do 64 pasm QMF.
Parametrem jest **wyjściowa** częstotliwość próbkowania SBR,
nie częstotliwość rdzenia AAC-LC. Dopuszczalne częstości:
16000, 22050, 24000, 32000, 44100, 48000, 64000,
88200, 96000, 128000, 176400 i 192000 Hz.
Ograniczenia szerokości SBR: maks. 48/35/32 pasm QMF w
zależności od częstotliwości. Błędne argumenty i nieobsługiwane
częstotliwości nie modyfikują struktury wyniku (fail-closed).

Testy porównują wybrane punkty z niezależnymi tablicami
częstotliwości i ich posortowanymi przyrostami oraz wykonują
3468 konfiguracji (w tym niepoprawne 4-bitowe parametry).
Wynik lokalny w środowisku roboczym GCC i ASan/UBSan:
2376 zaakceptowanych, 1092 poprawnie odrzuconych. Osobnego
potwierdzenia wymaga Fedora oraz test CI.

```sh
make test-aac-sbr-limits PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

Wzorce standardowych wartości parametrów sprawdzono z implementacjami
referencyjnymi FFmpeg (`aacsbr_template.c`, `aacsbrdata.h`) i FDK-AAC
(`sbrenc_freq_sca.cpp`), bez wykorzystania żadnej biblioteki
dekodującej w Sauti. Surowa tablica stop-frequency FDK nie jest
ostateczną tabelą `k2`: przed sumowaniem jej różnice są sortowane.

**Zakres:** nadal bez QMF i PCM, bez wyznaczania high/low/noise
z tabel master; parametry k0/k2 nie są jeszcze podłączone
do pełnego parsera/odtwarzacza HE-AAC. Nie deklarować pełnego
dekodowania SBR ani niezależności wszystkich profili AAC od FFmpeg.


## HE-AAC v1 / SBR-05 — tabele high/low/noise

Nowy moduł `sauti_aac_sbr_tables_build` wykorzystuje zatwierdzoną
tablicę `SautiAacSbrMaster` do utworzenia trzech tabel granicznych:
- `high` (N_High pasm po `bs_xover_band`, do 48 pasm),
- `low` (N_Low = ceil(N_High/2), do 24 pasm) z oddzielnym
  odwzorowaniem indeksów dla parzystej i nieparzystej liczby pasm,
- `noise` (1..5 pasm) z `bs_noise_bands * log2(k2/kx)`
  i kolejno rozdzielanymi pasmami low.

Parser kontroluje spójność wejściowej tabeli `master`, dodatnie
szerokości, `kx <= 32`, zakres QMF `1..64`, liczbę pasm i brak
kolizji granic. Zbyt wiele pasm noise **odrzuca** zamiast je
niejawnie obcinać. W żadnym przypadku błędu nie zmienia wyjścia
(fail-closed); alokacje nie są wykonywane. Jedyne dodatkowe
wywołania matematyczne to `log2f` / `lrintf` z `libm`.

Testy obejmują rozdzielne złote wektory even/odd, pojedynczą
obwiednię noise, błędne i ucięte zakresy, a także pełny łańcuch
limits -> master (liniowy/logarytmiczny) -> high/low/noise
na wielu kombinacjach częstotliwości, crossover i noise.

```sh
make test-aac-sbr-tables PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Nie jest to dekoder HE-AAC.** Tablice nie są jeszcze podłączone
do parsowania ramki SBR ani do obsługi envelope Huffman/QMF/PCM.
Po zmianach wymagane są pomyślne testy na Fedorze i CI;
nie zastępują one testów jakości dźwięku AAC-LC/PNS.


## HE-AAC v1 / SBR-06 — sterowanie obwiedniami dtdf i inverse filtering

Dodano `sauti_aac_sbr_control_parse` wykorzystujący natywny
czytnik bitów NASM, bez zewnętrznych dekoderów.
Parser konsumuje **dwie kolejne** klasyczne składnie SBR:
`sbr_dtdf()` (po jednym bicie `bs_df_env` na obwiednię i
`bs_df_noise` na obwiednię szumu) oraz `sbr_invf()`
(po dwa bity `bs_invf_mode` na **pasmo częstotliwościowe noise**).
Liczby elementów pochodzą z już zweryfikowanej siatki czasowej
`SautiAacSbrGrid` i tabeli częstotliwości `SautiAacSbrTables`.

Wspierane są 1–5 obwiedni, 1–2 obwiednie szumu, 1–5 pasm
noise i dowolny początkowy offset bitowy. Walidowane są
liczniki, rozdzielczość częstotliwości `freq_res`, podstawowa
spójność tabel oraz dostępność bitów. Po błędzie `*out` i
`*bit_offset` pozostają bez zmian (fail-closed).

Test `test_aac_sbr_control.c` sprawdza ręcznie zdefiniowane
wartości bitów, przesunięcia niebajtowe, przerwania wejścia
na wszystkich granicach bajtowych, błędne liczniki, oraz
integrację z rzeczywistymi funkcjami parsowania siatki SBR
i wyznaczania tablic high/low/noise.

```sh
make test-aac-sbr-control PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Zakres etapu:** wynik zawiera wyłącznie flagi przełączające
sposób kodowania obwiedni i tryby inverse filtering.
Nie dekoduje Huffmana, nie odtwarza skalefaktorów, nie generuje
filtrów QMF ani PCM, nie zmienia implementacji AAC-LC.
Do potwierdzenia: testy CI i test na Fedorze.


## HE-AAC v1 / SBR-07 — komplet 10 natywnych tablic Huffmana

Dodano `sauti_aac_sbr_huffman_decode` z własną implementacją
przechodzenia po kompaktowych drzewach kodowych.
Przy każdym bicie korzysta wyłącznie z czytnika NASM Sauti.
Nie wykorzystuje FFmpeg, FDK-AAC ani zewnętrznego kodeka
w czasie odtwarzania. Kod jest bez alokacji i fail-closed:
na błędzie ani `*bit_offset`, ani `*out_delta` nie zmieniają się.

Zaimplementowano wszystkie **10** klasycznych tablic SBR:
- envelope 1.5 dB: time/freq, level/balance (4),
- envelope 3.0 dB: time/freq, level/balance (4),
- noise 3.0 dB: time, level/balance (2).
Przy szumie kodowanym w kierunku częstotliwości używa się
odpowiedniej tablicy `env_3.0dB_F` (level/balance), zamiast
osobnej jedenastej tablicy noise.

Najpierw porównano **604/604** pełne słowa kodowe (wartości,
długości, bity) między dwoma niezależnymi źródłami:
- FFmpeg n6.1 `libavcodec/aacsbrdata.h` (długości i kody),
- FDK-AAC `libSBRdec/src/sbr_rom.cpp` (drzewa).
Nie stwierdzono rozbieżności. W Sauti na podstawie sprawdzonych
wartości zbudowano nowy, własny układ drzew indeksowanych
wskaźnikami `int8_t`, zamiast przenosić kod dekodera.

Test syntetyczny dekoduje każdy z 604 kodów na niebajtowych
pozycjach, sprawdza prawidłowe wartości i zużycie bitów,
następnie ponawia każdy kod przy braku ostatniego bajtu,
kontrolując brak zmian stanu. Jest też test mieszanego
strumienia 30 słów kodowych i błędnych argumentów.

```sh
make test-aac-sbr-huffman PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granica SBR-07:** wynik to podpisane delty Huffmana, nie
zrekonstruowane poziomy obwiedni i nie dźwięk PCM.
Osobnym etapem będzie `sbr_envelope()` / `sbr_noise()`
z obsługą wartości początkowych, czasowych różnic,
kwantyzacji i zachowania stanu między ramkami.
Weryfikację `PASS` na Fedorze i w CI potwierdzać osobno.


## HE-AAC v1 / SBR-08 — kwantowane obwiednie i poziomy szumu

Zaimplementowano `sauti_aac_sbr_quant_decode` — natywny etap
rekonstrukcji **całkowitoliczbowych poziomów** obwiedni i szumu
z bitstreamu przy użyciu sprawdzonych 10 tablic Huffmana SBR-07
i czytnika bitów NASM. Nie ma zależności od FFmpeg ani alokacji.

Rozpoznaje kierunek różnicowania `bs_df_env`/`bs_df_noise`:
- kierunek częstotliwości: zakodowana wartość początkowa
  i kolejne podpisane delty z właściwych tablic F;
- kierunek czasu: delty T dodawane do poprzedniej obwiedni
  lub zachowanego stanu poprzedniej ramki;
- poprawną zmianę rozdzielczości między high i low,
  także dla nieparzystego `N_high`;
- wymuszenie 1,5 dB dla FIXFIX z jedną obwiednią (classic SBR);
- zwykłe poziomy oraz podwojone delty i wartości początkowe
  prawego kanału w trybie sprzężonego stereo (balance);
- osobne tablice czasu i częstotliwości dla szumu.

Zakres liczb: 0..127 dla obwiedni i 0..30 dla szumu.
Stan kanału zapamiętuje ostatnią obwiednię i szum wraz
z geometrią pasm. Dla pierwszej obwiedni w trybie czasu
wymaga poprzedniej poprawnej ramki oraz identycznych
tabel pasm i trybu. Nieudana ramka nigdy nie zmienia
bufora wyjściowego, kursora ani historii (atomic fail-closed).

Testy syntetyczne obejmują: dwa kolejne bloki AAC,
zależności między obwiedniami, high/low (parzyste
i nieparzyste), balanced stereo, wymuszoną rozdzielczość,
obcięte strumienie, niezgodne tabele, przepełnienie
i brak historii.

```sh
make test-aac-sbr-quant PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Ograniczenia:** to nadal kwantowane wartości sterujące
bez konwersji do liniowych energii SBR, unmapping sprzężonego
stereo i filtrów QMF. Nowy moduł nie jest jeszcze podłączony
do odtwarzacza HE-AAC. Ścieżka pełnych ramek CPE wymaga
osobnego spięcia składni envelope/noise obu kanałów.
Przy zmianie geometrii częstotliwości między ramkami
moduł świadomie odrzuca time-delta zamiast próbować
niezweryfikowanego mapowania. Na tę chwilę nie deklarować
pełnego odtwarzania HE-AAC bez FFmpeg.

Testy CI i testy na Fedorze wymagają osobnego potwierdzenia.


## HE-AAC v1 / SBR-09 — liniowe energie obwiedni i współczynniki szumu

Dodano `sauti_aac_sbr_energy_dequant` — własny, bezalokacyjny
przelicznik wyjścia SBR-08 na **liniowe** wartości sterujące
wysokoczęstotliwościową syntezą. Zgodnie z MPEG-4 SBR:

- Energia niezależnego kanału: `E = 64 * 2^(alpha * q_env)`,
  gdzie `alpha = 0.5` dla `bs_amp_res=0` oraz `1.0` dla
  `bs_amp_res=1`.
- Poziom szumu: `Q = 2^(6 - q_noise)`.
- Dla `FIXFIX` z jedną obwiednią wymuszane jest 1,5 dB
  niezależnie od flagi nagłówka.

Arytmetyka `double` i `ldexp` zachowuje pełny przedział
kwantowanych wartości `q_env=0..127`, `q_noise=0..30`
bez niejawnego przepełnienia typów float. Wartości dla
nieużywanych pasm i obwiedni są zerowane. Wynik jest
zapisywany dopiero po pełnej walidacji tablic `high/low/noise`,
rozdzielczości, liczby obwiedni i wszystkich poziomów;
po błędzie pozostaje bez zmian (fail-closed).

**Ważne ograniczenie stereo:** `balanced=1` oznacza prawy
kanał sprzężonego CPE. Musi być przeliczany wspólnie
z lewym kanałem (pan/unmapping); SBR-09 jawnie go
odrzuca, zamiast niepoprawnie udawać niezależny kanał.
Mono i kanały stereo niesprzężonego są obsługiwane.

Testy: wartości charakterystyczne (w tym współczynnik 64),
287 wyczerpujących sprawdzeń kwantyzacji (2x128 + 31),
odrzucanie nieprawidłowych danych bez mutacji wyjścia,
nieparzyste pasma high/low, tryb FIXFIX oraz prawdziwy
łańcuch `quant_decode -> energy_dequant` z kodami NASM/Huffman.

```sh
make test-aac-sbr-energy PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

Potwierdzenie ostatecznych testów CI i Fedory nastąpi
po wykonaniu powyższych poleceń. Moduł nie jest jeszcze
syntezą QMF i nie uruchamia odtwarzania HE-AAC.

## HE-AAC v1 / SBR-10 — wspólne energie sprzężonego stereo

Dodano `sauti_aac_sbr_coupled_dequant` do modułu
`sauti_aac_sbr_energy.c`. Przyjmuje **dwie** poprawnie zdekodowane
ramki kwantowane SBR-08: poziom (`balanced=0`) i proporcję
kanałów (`balanced=1`) w standardowym CPE z `bs_coupling=1`.
Wspólnie wyznacza `left` i `right`; nie wolno zamienić kanałów
ani traktować prawego strumienia balance jako niezależnego PCM.

Dla obwiedni:
- `E_sum = 128 * 2^(alpha * q_level)`,
- `ratio = 2^(alpha * (pan_offset - q_balance))`,
- `E_left = E_sum / (1 + ratio)`,
- `E_right = E_sum * ratio / (1 + ratio)`.

`alpha=0.5` i `pan_offset=24` w rozdzielczości 1.5 dB;
`alpha=1` i `pan_offset=12` w rozdzielczości 3.0 dB.
FIXFIX z jedną obwiednią wymusza rozdzielczość 1.5 dB.
Dla szumu: `Q_sum=2^(7-q_noise_level)` oraz
`ratio_noise=2^(12-q_noise_balance)` i analogiczny podział.

Wzory sprawdzono na podstawie niezależnych implementacji
FFmpeg n6.1 (`aacsbr.c`, `sbr_dequant`) i FDK-AAC
(`env_dec.cpp`, `sbr_envelope_unmapping`). Wynik używa `double`,
`ldexp` i `exp2`, bez FFmpeg i bez alokacji w czasie działania.

Spójność pary sprawdzana jest przez identyczne liczniki
obwiedni, rozdzielczości, tablice pasm i dozwolone zakresy
kwantowania. W razie jakiegokolwiek błędu **oba** bufory wyjściowe
pozostają niezmienione; przekazanie tego samego bufora dla
lewego i prawego kanału jest zabronione.

Test `test_aac_sbr_coupled.c` weryfikuje wartości golden
(pan center/lewo/prawo, energia i szum), wymuszony FIXFIX,
33 729 kombinacji energii i szumu z kontrolą zachowania sumy
oraz proporcji kanałów, błędy i atomowość obu wyjść.
Dodatkowo sprawdza autentyczny łańcuch:
`SBR-08 Huffman -> quant level/balance -> SBR-10 unmapping`.

```sh
make test-aac-sbr-coupled PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Ograniczenie:** moduł wytwarza liniowe energie i poziomy
szumu, nie sygnał PCM. Nie jest jeszcze podłączony do
bitstreamowego parsera CPE i syntezy QMF. Brak pełnego
odtwarzania HE-AAC bez etapów HF/QMF i integracji.
Przed zamknięciem etapu wymagane są testy Fedory i CI.


## HE-AAC v1 / SBR-11 — konstrukcja łatek HF (indeksy QMF)

Pierwszy etap generatora wysokich częstotliwości: samodzielny
`sauti_aac_sbr_patch_build` tworzy **mapę indeksów pasm**
z istniejących `k0`, `kx`, `k2`, tabel `master` i `high/low/noise`.
Na podstawie wyjściowej częstotliwości próbkowania SBR
wyznacza częstotliwość docelową algorytmu (`goalSb`),
ustala kolejne zakresy źródłowe/wyjściowe QMF oraz zapisuje
tablicę `source_for_target[64]` (`-1` dla pasm niemapowanych).

Pozostawiono dokładny warunek parzystości i końcowego
odrzucenia ostatniej łatki, gdy ma mniej niż trzy pasma.
Przewidziano do 6 łatek (klasyczne wymagania przewidują
maksimum pięć, lecz znane strumienie testowe zawierają sześć).
Weryfikowana jest cała geometria, brak nakładania zakresów,
źródła poniżej `k0`, zgodność pasm oraz postęp algorytmu.
Wynik jest zatwierdzany wyłącznie po sukcesie (fail-closed).

Testy zawierają wzorce przy 48 i 96 kHz dla pasm 16..32
oraz 16..48, w tym łączone łatki, pomijany końcowy
ogon dwóch pasm, błędne częstotliwości i zmienione
granice. Dodatkowo wykonują łańcuch:
`limits -> master linear/log -> high/low/noise -> patches`.

```sh
make test-aac-sbr-patch PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Ważne:** SBR-11 jedynie wyznacza skąd pobrać dolne
pasma QMF dla generatora HF. Nie generuje danych
zespolonych QMF, nie implementuje predykcji/chirp,
nie nakłada obwiedni, nie posiada polifazowej
analizy/syntezy QMF i **nie daje jeszcze PCM HE-AAC**.
Rzeczywisty tor syntezy pozostaje dalszą pracą.
PASS dla CI i Fedory należy potwierdzić osobno.


## HE-AAC v1 / SBR-12 — operacje na zespolonych próbkach QMF (baza HF)

Dodano niezależny moduł `sauti_aac_sbr_hf_transpose`,
który przyjmuje gotowe **zespolone** próbki subpasm QMF
`{re, im}` i mapę transpozycji z SBR-11.

Układ wejścia: `low[time][32]`, a wyjścia:
`high[time][64]`. Używa `source_for_target[k]` i przenosi
dokładne wartości obu składowych zespolonych do
pasm wysokich. Wszystkie pozostałe częstotliwości
otrzymują zero, w tym dopuszczony, pominięty
ogon ostatniej łatki. Obsługuje 1..40 przedziałów QMF.

Wyjście jest zatwierdzane **dopiero po walidacji wszystkich
pasm i próbek**: kontroluje liczby elementów buforów,
przedziały źródłowe i docelowe, brak nakładania łatek,
związek mapy z tabelami `high/low/noise`,
NaN i Inf w rzeczywiście wykorzystywanych danych.
Na błędzie wyjście pozostaje niezmienione; dzięki
lokalnemu stagingowi nawet nakładanie wejścia i wyjścia
jest bezpieczne. Brak alokacji i zewnętrznych kodeków.

Testy `test_aac_sbr_hf.c` obejmują:
- 40 przedziałów z mapą przy 48 kHz i krótkim ogonem,
- 32 przedziały i wielokrotne łatki przy 96 kHz,
- aliasowanie wejścia i wyjścia, a także 1 przedział,
- ucięte bufory, błędną geometrię, NaN/Inf w końcowym
  przedziale i brak częściowego zapisu,
- rzeczywisty łańcuch `limits -> master -> tables -> patches
  -> zespolone QMF` dla różnych częstotliwości i ustawień.

```sh
make test-aac-sbr-hf PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granica SBR-12:** to tylko **składnik bezpośredniego
przeniesienia** z generatora HF. W standardowym HE-AAC
generacja wysokich pasm obejmuje również zespoloną
predykcję liniową, chirp/inverse filtering, dostrajanie
energii i szumu oraz polifazowe filtry QMF analizy
i syntezy. Ten moduł nie wyznacza próbek PCM i nie
jest jeszcze podłączony do odtwarzacza.
PASS na Fedorze i w CI musi być potwierdzony osobno.


## HE-AAC v1 / SBR-13 — chirp oraz zespolona predykcja drugiego rzędu

Dodano `sauti_aac_sbr_chirp_update` i
`sauti_aac_sbr_hf_predict`. To własny moduł Sauti,
bez kodeków zewnętrznych i alokacji.

Pierwsza funkcja utrzymuje poprzednie tryby `bs_invf_mode`
z SBR-06 oraz współczynniki wygładzania pasm:
- podstawowe poziomy: `0, 0.75, 0.9, 0.98`,
- przy przejściu OFF↔LOW: `0.6`,
- przy zmniejszeniu celu: `0.75*cel + 0.25*poprzedni`,
- przy wzroście: `0.90625*cel + 0.09375*poprzedni`,
- wartości poniżej `1/64` są zerowane.

Druga funkcja implementuje *zastosowanie* zespolonej
predykcji liniowej rzędu drugiego do częstotliwości
zmapowanych przez SBR-11:
`Xhi[t,k] = Xlo[t,p] + bw[g]*alpha0[p]*Xlo[t-1,p]`
` + bw[g]^2*alpha1[p]*Xlo[t-2,p]`.
Mnożenia to pełne działania na liczbach zespolonych,
a wejściowy bufor zawiera dwa wcześniejsze sloty QMF.
Każde pasmo HF dostaje `bw[g]` ze swojej tablicy noise.

Testy obejmują sekwencje zmian trybów invf, wygaszanie
współczynnika, pełne mnożenie zespolone, 40 slotów,
przenikanie buforów, wadliwe mapy, NaN/Inf, wycofanie
całego wyniku po błędzie oraz łańcuch wielu konfiguracji
`limits → master → tables → patches → chirp/prediction`.

```sh
make test-aac-sbr-hf-predict PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granica funkcjonalna:** SBR-13 przyjmuje od wywołującego
gotowe `alpha0/alpha1`. Nie oblicza ich jeszcze
z autokorelacji próbek QMF (etap LPC), nie dokonuje
regulacji obwiedni/szumu i nie zawiera polifazowych
banków filtrów QMF ani końcowego PCM. Parametry
poprzedniego stanu `chirp` wymagają inicjalizacji zerami.
Sprawdzenie CI i Fedory jest odrębnym warunkiem zakończenia.


## HE-AAC v1 / SBR-14 — natywna autokorelacja i współczynniki LPC

Nowy moduł `sauti_aac_sbr_lpc_estimate` oblicza zespolone
współczynniki `alpha0/alpha1` **z samych próbek QMF**.
Wejście obejmuje **40 kolejnych slotów** po 32 pasma QMF
(dwa sloty historii i 38 pozostałych), uporządkowane
`low[time][32]`. Liczba rzeczywiście używanych pasm
1..32 przekazywana jest jawnie.

Algorytm wykorzystuje wielkości autokorelacji zespolonej
z opóźnieniami 0, 1 i 2: energię, iloczyny
`conj(x[t]) * x[t+1]` i `conj(x[t]) * x[t+2]`,
z właściwymi oknami klasycznego HE-AAC. Z tych wielkości
wyznaczany jest wyznacznik z regularyzacją `1.000001`,
następnie dwa współczynniki zespolone z układu
predykcji drugiego rzędu.

Obliczenia pośrednie w `double` oraz normalizacja
każdego pasma względem szczytowego modułu składowej
zabezpieczają przed przepełnieniem/szkodliwym
skalowaniem amplitudy. Cisza, układ niemal osobliwy
i współczynniki o normie >=4 powodują zerowanie
predykcji w danym paśmie. Niepoprawne dane lub
za małe bufory pozostawiają **cały wynik bez zmian**.
Brak alokacji i FFmpeg/FDK podczas dekodowania.

Test `test_aac_sbr_lpc.c` obejmuje analityczne
wzorce dla dwóch impulsów 1/1 i 1/i, skalowanie
amplitudy, ciszę, przypadki szumowe, NaN/Inf i
fail-closed. Test integracyjny wykorzystuje
rzeczywisty łańcuch `LPC → SBR-13 chirp/HF predict`
i porównuje wyjście z ręcznie wyliczonym wzorem.

```sh
make test-aac-sbr-lpc PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Nie jest to jeszcze gotowy HE-AAC:** LPC i
transpozycja pracują na wcześniej obliczonych
zespolonych subpasmach. Do PCM potrzebne będą
jeszcze analiza i synteza QMF, regulacja obwiedni
i szumu oraz integracja dekodera ramki.
Walidację CI i Fedory trzeba potwierdzić osobno.


## HE-AAC v1 / SBR-15 — dopasowanie energii obwiedni (podzbiór)

Dodano `sauti_aac_sbr_hf_adjust`: natywny, bezalokacyjny
etap dopasowania amplitudy zespolonych próbek QMF do
docelowych **liniowych energii obwiedni** z SBR-09/10.
Interfejs przyjmuje granice obwiedni przeliczone przez
wywołującego na sloty QMF, dane `low/high` z SBR-05
oraz `energy->freq_res`.

Dla każdego przedziału czasowego i częstotliwościowego
liczy uśrednioną moc `mean(re*re+im*im)`, a następnie
mnoży obie składowe zespolone przez
`sqrt(E_target/E_measured)`. Dwie opcje:
- `interpolation=0`: średnia moc całego pasma low/high;
- `interpolation=1`: osobna moc i wzmocnienie subpasma QMF.

Obsługa 1..40 slotów i 1..5 obwiedni. Wyniki poza
wysokoczęstotliwościowym zakresem SBR są kopiowane
bez zmian. Ujemne, niefinitywne energie, wadliwe
granice czy NaN/Inf w używanych próbkach powodują
odrzucenie całego wyniku bez częściowego zapisu.
Nie da się samym wzmocnieniem odtworzyć dodatniej
energii z zerowego sygnału, dlatego takie wejście
jest bezpiecznie odrzucane. Bufory mogą się nakładać.

Testy obejmują:
- dwa typy rozdzielczości częstotliwości,
- rozróżnienie interpolacji pasmowej i subpasmowej,
- 40 slotów, 5 obwiedni i zachowanie granic,
- zerowy sygnał, NaN/Inf, błędne rozmiary i rollback,
- kompletne przemiatanie geometrii `limits → master →
  tables → energy → adjust` dla wielu parametrów SBR.

```sh
make test-aac-sbr-hf-adjust PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Ważna granica:** SBR-15 nie jest jeszcze pełnym
normatywnym `envelope adjustment`. Pozostają limiter
wzmocnienia/boost, wprowadzanie szumu i harmonicznych,
wygładzanie granic oraz dopasowanie skali energii do
przyszłego banku analizy QMF. Potrzebne są również
synteza QMF i pełne połączenie z dekoderem ramek.
Obecne wzory są sprawdzane w ramach testów syntetycznych,
nie należy ogłaszać ukończenia HE-AAC.


## HE-AAC v1 / SBR-16 — ograniczniki wzmocnienia i gain boost

Nowy, bezalokacyjny moduł `sauti_aac_sbr_hf_limit`
oblicza **wzmocnienia pasm QMF przed dopasowaniem amplitudy**.
Nie jest nakładką na gotowe wyjście SBR-15: dostaje
pierwotne zespolone QMF, więc nie dopuszcza do
podwójnego wzmocnienia sygnału.

Interfejs otrzymuje docelowe energie SBR-09/10,
granicę czasową każdej obwiedni i jawne granice
*limiter bands* tworzące partycję `[kx, k2]`.
Wyznaczanie normatywnej tablicy `f_tablelim` z
`bs_limiter_bands`, granic łatek i tabel częstotliwości
jest jeszcze **niezaimplementowane**; testy używają
poprawnych granic low-resolution z SBR-05.

Dla każdego pasma QMF i obwiedni liczona jest średnia
moc `|Re+jIm|²`, następnie limit na grupę:

- `Gmax = min(100000, limGain * sqrt(sum(Eorig)/sum(Ecurr)))`.
- `limGain`: `0.70795`, `1`, `1.41254`, `1e10` dla
  trybów `-3 dB / 0 dB / +3 dB / unlimited`.
- `G[k] = min(sqrt(Eorig[k]/Ecurr[k]), Gmax)`.
- `Gboost = min(1.584893192, sqrt(sum(Eorig)/sum(EafterCap)))`.
- Końcowe `G[k] = Gboost * G[k]`, wspólne dla
  rzeczywistej i urojonej składowej QMF.

Zwracany jest także `SautiAacSbrLimiterReport`:
limit wzmocnienia i boost każdej grupy oraz
końcowe wzmocnienia każdego QMF. Pozwala to sprawdzić
algorytm niezależnie od wyjściowych próbek audio.

Moduł obsługuje 1–40 slotów i 1–5 obwiedni,
pasywne pasma spoza `[kx,k2]` zachowuje bez zmian.
Wyjście może współdzielić pamięć z wejściem.
Na uszkodzone dane, puste dodatnio docelowe pasmo,
overflow, NaN/Inf i złe granice obydwa wyniki
pozostają bez zmian (pełna atomowość).

Test `test_aac_sbr_hf_limiter.c` sprawdza cztery
tryby ogranicznika, niezależnie policzone wartości
`Gmax/Gboost`, 40 slotów i pięć obwiedni,
aliasowanie, zero target, bardzo duże docelowe
energie, rollbacks oraz sweep łańcucha tabel SBR.

```sh
make test-aac-sbr-hf-limiter PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granice SBR-16:** ten etap nie wstrzykuje szumu
ani składowych sinusoidalnych, nie wygładza obwiedni
i nie wyznacza limiter bands z flag SBR. Parametry
noise/harmonics i filtry QMF analizy/syntezy nadal
wymagają implementacji. Nie oznacza pełnej zgodności
odtwarzania HE-AAC.


## HE-AAC v1 / SBR-17 — automatyczna tablica limiter frequency bands

Nowy moduł `sauti_aac_sbr_limiter_bands_build` oblicza
`f_tablelim` na podstawie `bs_limiter_bands` (0..3),
tablicy niskiej rozdzielczości SBR-05 oraz styku łatek
z SBR-11. To brakujące wejście pasmowe dla SBR-16.

- Tryb 0: jedno pasmo ogranicznika `[kx,k2]`.
- Tryb 1: próg bliskości `2^(0.49/1.2)`.
- Tryb 2: próg `2^(0.49/2)`.
- Tryb 3: próg `2^(0.49/3)`.

Kandydaci są sortowani i przerzedzani według stosunku
częstotliwości. Granice łatek oraz oba końce `[kx,k2]`
pozostają chronione przed usunięciem, nawet jeśli
ostatnia łatka została skrócona o 1–2 pasma.
Nie są generowane zduplikowane ani zerowej długości
przedziały. Geometria przechodzi również niezależną
walidację mapy SBR-11/12. Wszelkie błędy pozostawiają
całą tablicę wyjściową bez zmian (fail-closed).

Funkcja `sauti_aac_sbr_hf_limit_auto` łączy nową tablicę
z istniejącym SBR-16 w jednym wywołaniu, bez ręcznie
dostarczonych granic. Nie modyfikuje wejścia; na błędzie
nie zapisuje też próbek ani statystyk ogranicznika.

Testy obejmują dokładne zestawy granic dla 48 i 96 kHz,
przypadki 1/2/3 łatek, zachowanie wewnętrznych styków,
porównanie wszystkich 16 kombinacji automatycznego
i ręcznego ogranicznika, błędy argumentów i geometrii,
oraz szeroki sweep od `limits/master/tables/patch`.

```sh
make test-aac-sbr-limiter-bands PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granice:** SBR-17 nadal nie generuje szumu,
sinusoid ani nie wygładza obwiedni. Nie implementuje
banków polifazowych QMF ani wyjścia PCM. Zgodność
z realnymi strumieniami wymaga integracji końcowej
i osobnych testów międzyimplementacyjnych.


## HE-AAC v1 / SBR-18 — natywny szum zespolony HF i udział energii

Dodano `sauti_aac_sbr_hf_noise_mix` — etap wprowadzający
deterministyczny szum do wysokich subpasm zespolonych QMF,
po wcześniejszym wygenerowaniu i dostrojeniu sygnału
przez SBR-13–17. Otrzymuje energie obwiedni i
współczynniki Q z SBR-09/10, osobne granice slotów
obwiedni i szumu oraz stan kanału `SautiAacSbrNoiseState`.

Podstawowy podział oczekiwanej energii w każdym paśmie:

- `E_signal = E_target / (1+Q)`;
- `E_noise = E_target * Q / (1+Q)`;
- próbka zespolona sygnału jest mnożona przez
  `1/sqrt(1+Q)`, a wektor szumu jednostkowego przez
  `sqrt(E_noise)`.

Dla wejścia o mocy zgodnej z `E_target` oczekiwana
moc wyniku wynosi `E_target`, ponieważ szum ma
zerową średnią i jednostkową moc. Nie oznacza to
**dokładnej sumy energii** dla każdej skończonej ramki:
występują rzeczywiste losowe składniki mieszane.

Generator szumu jest autorski: `xorshift32` i osiem
zespolonych faz o jednostkowym module. Zachowuje
stan między wywołaniami i pozwala rozdzielić
ziarna dla różnych kanałów. To obecnie **celowo
nie jest normatywna tablica 512 wektorów MPEG-4 SBR**;
do uzyskania bitowo zgodnego HE-AAC trzeba ją dodać
i sprawdzić zgodność sekwencji faz oraz indeksów.

Przedziały szumu mogą być szersze od obwiedni, ale
każda wewnętrzna granica czasowa szumu musi być
również granicą obwiedni. Poziomy Q dobierane są
oddzielnie w każdej grupie częstotliwości z
`tables->noise[]` i dla aktualnego przedziału czasu.

Moduł obsługuje 1–40 slotów, 1–5 obwiedni, 1–2
okresy szumu i prawidłowe 1–5 pasm noise.
Zachowuje pasma spoza zakresu HF. Gdy docelowa
energia wynosi zero, zwraca zero dla danego
pasma; dla dodatniej energii i całkowicie pustego
sygnału źródłowego zgłasza błąd, zamiast
udawać zgodność proporcji sygnał/szum.
Weryfikuje NaN/Inf, zakresy, geometrię, stan i
pojemność buforów. **Wyjście i stan są atomowe**;
bufory wejściowe i wyjściowe mogą się pokrywać.

Testy:
- niezależny pomiar obu udziałów energii,
- identyczny przebieg dla tych samych ziaren;
- dokładna ciągłość po podziale jednej ramki na dwie;
- rozdzielone ziarna dla stereo i aliasing pamięci;
- rzeczywisty łańcuch kwantyzacji SBR-09 do szumu QMF;
- cisza docelowa, Q=0, błędne pasma, brak danych,
  niefinitywne liczby, overflow, rollback oraz sweep
  poprawnych konfiguracji SBR-05.

```sh
make test-aac-sbr-hf-noise PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granice:** SBR-18 nie zawiera jeszcze normatywnej
tablicy pseudo-szumu, wygładzania gain/noise,
sinusoidalnych harmonicznych, obsługi transientów,
syntezy/analizy QMF ani pełnego dekodowania ramek
HE-AAC do PCM. Poziomy szumu zakładają wejście
wcześniej dopasowane do energii obwiedni; ogranicznik
może celowo obniżyć jego końcową moc.


## HE-AAC v1 / SBR-19 — harmoniczne zespolone w pasmach QMF

Dodano `sauti_aac_sbr_hf_sine_mix`, samodzielny, bezalokacyjny
moduł generowania dodatkowych harmonicznych w domenie QMF.
Każda flaga `bs_add_harmonic[i]` (0/1) dotyczy pasma
wysokiej rozdzielczości `tables->high[i..i+1]`; składnik
sinusoidalny trafia do jego środkowego subpasma
`(high[i]+high[i+1])/2`.

Stan kanału `SautiAacSbrSineState` zapamiętuje fazę
`indexsine` w cyklu 0..3 i mapę aktywnych harmonicznych
poprzedniej ramki. Nowe harmoniczne wchodzą dopiero od
wskazanej obwiedni `transient_env`, podczas gdy
harmoniczne kontynuowane z wcześniejszej ramki mogą
być obecne już w pierwszej obwiedni. Faza przesuwa się
o jeden na każdy slot QMF, również gdy flaga harmonicznej
nie jest aktywna. Dla 4 faz obowiązuje naprzemiennie
real + / imaginacyjna ze znakiem wynikającym z parzystości
subpasma / real − / imaginacyjna z odwróconym znakiem.

Amplituda pojedynczej sinusoidy jest liczona z
docelowych energii `E` (SBR-09/10) i noise factor `Q`:
`sqrt(E/(1+Q))`. Tablice czasowe obwiedni i szumu
wskazują obowiązującą wartość E/Q; zawartość QMF
poza odpowiednimi harmonicznymi pozostaje niezmieniona.

Testy `test_aac_sbr_hf_sine.c` pokrywają:
- dokładną kolejność czterech faz dla dwóch harmonicznych;
- nowy transient, kontynuację i wygaszenie między ramkami;
- idealną zgodność całości z dwiema kolejnymi ramkami;
- działanie przy aliasingu wejścia i wyjścia;
- integrację z liniowymi energiami z SBR-09;
- zakresy, brakujące wejścia, NaN/Inf, uszkodzone flagi,
  wycofanie całego wyniku i stanu w przypadku błędu;
- szeroki sweep konfiguracji tablic pasm SBR.

```sh
make test-aac-sbr-hf-sine PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granica funkcjonalna:** to osobny, testowalny moduł
sinusoid. Pełna, normatywna mieszanka harmonicznych,
szumu i regulacji obwiedni wymaga wspólnej obsługi
limiter boost, tłumienia szumu w tonalnych pasmach,
wygładzania i transientów oraz ostatecznie banków
QMF analizy/syntezy i integracji z dekoderem AAC.
Generator SBR-18 ma nadal niezgodną z normatywną
tablicą 512-wektorową autorską sekwencję szumu.
