# Sauti — własny dekoder AAC w NASM (AAC-LC + HE-AAC + HE-AAC v2)

Status: AAC-LC oraz eksperymentalne HE-AAC/SBR odtwarzają PCM w restrykcyjnych
ścieżkach ADTS; HE-AAC v2/PS nie ma jeszcze własnej rekonstrukcji stereo.
Gałąź \`feature/aac-family-asm\`.
Nie zastępuj obecnego działającego adaptera AAC/FFmpeg w rejestrze odtwarzacza
aż do przejścia testów poprawności i odsłuchu prawdziwych plików.

## Jeden rdzeń, trzy współpracujące warstwy

| Warstwa | Odpowiada za | Stan |
| --- | --- | --- |
| AAC-LC | ADTS/ASC, strumień bitowy, ICS, Huffman, odwrotna kwantyzacja, TNS, stereo, IMDCT/okna, PCM | Działający dekoder ADTS mono/stereo AAC-LC z NASM w gorących prymitywach; nadal ograniczony do obsługiwanych elementów i jedno-RDB. |
| HE-AAC (SBR) | Parsowanie danych SBR, siatka czasowo-częstotliwościowa, QMF, rekonstrukcja wysokich pasm | Eksperymentalny ADTS SCE/CPE type-13/type-14 z QMF i PCM 2×; wymagane dalsze porównanie z niezależnym PCM. |
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
- Własne dekodowanie AAC-LC i restrykcyjne HE-AAC/SBR do PCM jest dostępne;
  zachować ostrożność przy wyborze go zamiast FFmpeg, dopóki HE-AAC nie ma
  porównania zgodności z niezależnym PCM, a PS pozostaje niezaimplementowane.

### PS — stan nagłówka i ramek między ramkami (2026-11-07)

Dodano `SautiAacPsHeaderState`. Modeluje semantykę `ps_header_flag`: pierwsza
ramka bez nagłówka jest odrzucana, późniejsza dziedziczy ostatnie IID/ICC,
a aktualizacja stanu i kursora jest atomowa. To nadal warstwa składni, bez
hybrydowego QMF i stereo PCM.

`sauti_aac_ps_frame_parse` rozszerza ten stan o normatywny szkielet
`ps_data()`: `frame_class`, `num_env_idx`, tabelę liczby obwiedni (w tym
legalne `num_env==0`), wyliczone granice klasy stałej oraz monotonicznie
zweryfikowane granice klasy zmiennej, a także ściśle ograniczony długością
blok `ps_extension`. Dekoduje dziesięć książek Huffmana: częstotliwościowe i
czasowe dla obu rozdzielczości IID, obie książki ICC oraz cztery książki
IPD/OPD. Parametry przechodzą weryfikację zakresu, a ostatnia obwiednia
jest zapamiętana wyłącznie po udanym parsowaniu. IPD/OPD używa 5, 11 albo 17
parametrów fazy modulo 8 (według trybu IID), niezależnie od 10/20/34 pasm
samych parametrów IID. Tabele kodów zostały
liczbowo sprawdzone względem FFmpeg n6.1 `aacpsdata.c` (Alex Converse,
LGPL-2.1+); nie ma zależności runtime od FFmpeg, ale dane wymagają przeglądu
licencyjnego przed dystrybucją pakietową.

Parser wystawia osobno liczbę obwiedni sygnalizowaną przez bitstream oraz
`resolved_num_env`: do tej drugiej dołącza syntetyczną końcową obwiednię
z historią parametrów, jeśli legalna ramka nie obejmuje jeszcze slotu QMF 31.
To jest wymagane przez późniejszą interpolację parametrów stereo i nie zmienia
ani kursora, ani stanu różnicowego składni.

`sauti_aac_ps_map_frame` normalizuje już parametry każdej rozwiązanej
obwiedni do normatywnego układu 20 albo 34 pasm, zachowując osobne reguły
5/11/17 dla faz IPD/OPD. Jest to czysty etap przygotowania danych, bez QMF i
bez PCM; `make test-aac-ps-map` sprawdza przejścia 5/10/11/17/20/34 oraz
atomowość błędnego wejścia.

`sauti_aac_ps_hybrid_schedule_build` dokłada do tego sprawdzoną topologię
warstwy hybrydowej: 32 sloty QMF są przypisane do obwiedni, a 71 albo 91
hybrydowych pasm do właściwych 20 albo 34 parametrów (w tym znaki specjalnych
pasów IPD/OPD). Nie wykonuje filtracji ani miksowania PCM. `make
test-aac-ps-hybrid` obejmuje oba układy pasm, granice obwiedni i odrzucenie
niepełnego harmonogramu, zanim stan może zostać opublikowany.

`sauti_aac_ps_matrix_build` dekwantyzuje IID i ICC, a następnie buduje
rzeczywiste współczynniki macierzy PS A/B dla każdej obwiedni i pasma.
Obliczenie konfiguracji jest celowo skalarne; przyszła gorąca pętla mnożenia
zespolonych slotów hybrydowych będzie kandydatem do NASM. IPD/OPD pozostaje
odrębną rotacją zespoloną, więc ten etap nie deklaruje jeszcze stereo PCM.
`make test-aac-ps-matrix` sprawdza dekwantyzację, obie macierze i rollback.
Stałe dekwantyzacji i równania zostały liczbowo porównane z generatorem
`aacps_tablegen.h` z FFmpeg n6.1 (LGPL-2.1+); implementacja Sauti nie ma od
niego zależności runtime, lecz przed dystrybucją należy objąć ten materiał
tym samym przeglądem licencyjnym co wcześniej dodane tabele PS.

`sauti_aac_ps_mix_real_apply` ma już gorącą część w NASM/SSE: miesza 32
sloty zespolonych pasm hybrydowych przez macierz IID/ICC, z routingiem
obwiedni oraz pełną atomowością wyjścia zapewnianą przez warstwę C. Moduł
jest świadomie ograniczony do części rzeczywistej macierzy — nie udaje
dekorrelatora, IPD/OPD ani hybrydowej analizy. `make test-aac-ps-mix`
porównuje oba kanały dla różnych obwiedni i bada rollback przy NaN.
Ten sam rdzeń ma też wariant pełny zespolony, który stosuje IPD/OPD oraz
normatywne odwrócenie znaku części urojonej na wybranych pasmach hybrydowych.

`sauti_aac_ps_phase_apply` uzupełnia macierz o IPD/OPD: utrzymuje
trójsymbolową historię fazy, normalizuje filtr wygładzający i tworzy
zespolone współczynniki dla obu kanałów. Przejście 20/34 pasm resetuje tę
historię, a błąd fazy nie publikuje ani wyniku, ani nowego stanu. Test
`make test-aac-ps-phase` obejmuje IPD, OPD, brak rozszerzenia i rollback.

`sauti_aac_ps_render_plan_build` scala powyższe cztery kroki w jeden
atomowy, bezpróbkowy plan dla przyszłego renderera PS: znormalizowane
parametry, routing 71/91 pasm hybrydowych, rzeczywistą macierz IID/ICC i jej
zespoloną postać po IPD/OPD. Stan obejmuje wyłącznie historię wygładzania
fazy; różnicowa historia składni nadal należy do parsera SBR. Przejście
20→34 pasm oraz błąd dowolnego podetapu nie publikuje częściowego planu ani
historii. `make test-aac-ps-render` i jego odpowiednik ASan/UBSan sprawdzają
oba układy pasm, granicę obwiedni i rollback. To jest granica kontrolna,
nie hybrydowa analiza/dekorrelacja ani PCM HE-AAC v2.

Regresja render-planu obejmuje też niezmienniki trwałego stanu: `frames==0`
nie może współistnieć z gotową historią fazy, dodatni licznik wymaga tej
historii, `ready` ma tylko wartości boolowskie, gotowa historia ma wyłącznie
topologię 20 albo 34 pasm i mieści sześciobitowe symbole IPD/OPD, a
`ready==0` wymaga kanonicznego, zerowego stanu bez historii. `UINT64_MAX` nie
może zostać zwiększone. Każdy taki uszkodzony stan jest odrzucany bez zmiany
ani stanu, ani wcześniej przygotowanego planu. To wzmacnia granicę fail-closed przed
przyszłymi etapami hybrydowego QMF i dekorrelacji.

`test-aac-syntax` dekoduje każde z 242 słów wszystkich dziesięciu tabel tym samym
czytnikiem MSB-first, a także bada dziedziczenie, oba klasy ramek, IID/ICC
różnicowe w czasie i częstotliwości, reset stanu po wyłączeniu parametrów,
blok rozszerzeń, fazowe różnice w czasie modulo 8 oraz rollback uciętego
payloadu. Zarezerwowany bit oraz
nieznany identyfikator rozszerzenia są fail-closed odrzucane. Następne
konieczne etapy to hybrydowy QMF, dekorrelator i macierz stereo.

Po tej zmianie przeszło pełne, niezależne od bibliotek FFmpeg:

```text
make test-aac-native PKG_CONFIG=/bin/false
```

Obejmuje ono parser PS razem z AAC-LC, mono/stereo SBR, CPE, QMF oraz CRC
transportowym. Parser SBR przekazuje też atomowo `EXTENSION_ID_PS=2` jako
side-data, łącznie z ramkami zaczynającymi się pod niebajtowym offsetem.
Wynik testu nie jest deklaracją obsługi HE-AAC v2 PCM: publiczna ścieżka
HE-AAC/QMF rozpoznaje i odrzuca payload PS zamiast publikować go jako
monofoniczne lub fałszywe stereo, dopóki nie powstanie hybrydowy QMF,
dekorrelator i macierz stereo.

Włączono także te parsery do `test-aac-sanitize`, więc ASan/UBSan wykonuje
test CPE, pełne graniczne testy syntaktyczne PS, mapowanie pasm oraz zagnieżdżony
parser SBR→PS. Kod NASM jest linkowany, lecz — zgodnie z ograniczeniem sanitizerów
C — nie jest przez nie instrumentowany.


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


## HE-AAC v1 / SBR-20 — wspólne dostrajanie HF (limiter + szum + sinusoidy)

Dodano `sauti_aac_sbr_hf_joint_process`: pierwszy
**wspólny**, bezalokacyjny łańcuch SBR pracujący
na uprzednio wygenerowanych zespolonych subpasmach QMF.
Wywołanie łączy automatyczny podział pasm `f_tablelim`
(SBR-17), ogranicznik i gain boost (SBR-16),
szum (SBR-18) oraz harmoniczne (SBR-19).

**Ważne:** naiwne dodawanie szumu i sinusoidy
nadwyższałoby energię tonalnych subpasm. Dlatego
SBR-18 ma teraz dodatkowy, opcjonalny interfejs
`sauti_aac_sbr_hf_noise_mix_tonal`, który przyjmuje
maskę aktywnych harmonicznych `[slot][64]` i:

- nadal przesuwa generator losowy we **wszystkich**
  pasmach HF (indeksy szumu nie uciekają po zmianie flag);
- w paśmie z sinusoidą wyłącza dodawanie szumu
  losowego i skaluje sygnał o `sqrt(Q/(1+Q))`;
- pozostawia `sqrt(E/(1+Q))` mocy amplitudowej
  dla czterofazowej harmonicznej SBR-19;
- poza maską działa jak istniejący interfejs SBR-18:
  sygnał `sqrt(1/(1+Q))`, szum `sqrt(E*Q/(1+Q))`.

Maska jest budowana z flag `bs_add_harmonic`,
środkowych pasm wysokiej rozdzielczości, numeru
obwiedni pojawienia się nowych harmonicznych oraz
stanu poprzedniej ramki. **Obie historie kanału**
(`SautiAacSbrNoiseState` i `SautiAacSbrSineState`),
statystyka ogranicznika i bufor wyjściowy zostają
zatwierdzone wyłącznie po sukcesie wszystkich
etapów. W przypadku błędu wszystko pozostaje
bitowo niezmienione (fail-closed).

Testy `test_aac_sbr_hf_joint.c`:
- analityczny test podziału energii 3:1 w paśmie
  harmonicznym oraz braku dodatkowego szumu;
- nowy transient w środku ramki i kontynuacja fazy;
- zgodność modułu bez harmonicznych z historycznym
  sekwencyjnym SBR-16/18/19;
- porównanie całej i podzielonej ramki z identycznymi
  stanami końcowymi oraz działanie in-place;
- Q=0: całkowicie tonalna składowa bez szumu;
- rollback po błędach każdego etapu, także po
  udanym ograniczniku, gdy zawiedzie kolejny etap;
- 40 slotów, 5 obwiedni, 2 przedziały szumu
  i sweep poprawnych geometrii częstotliwości.

```sh
make test-aac-sbr-hf-joint PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Stan zgodności:** jest to własny, testowalny model
wspólnego przetwarzania HF, **nie** pełny normatywny
algorytm HE-AAC. Ogranicznik nadal liczy boost przed
ostatecznym uwzględnieniem harmonicznych i szumu;
brak wygładzania, transient-aware noise gating,
normatywnej tablicy 512 wektorów, polifazowych
banków QMF i połączenia z wyjściem PCM AAC.
Ponadto moc sumy zespolonych sygnałów w krótkiej
ramce zawiera składniki mieszane; podział E oznacza
udział składowych, a nie gwarancję dokładnej mocy
każdej ramki. Testy syntetyczne nie zastępują
weryfikacji rzeczywistymi strumieniami.


## HE-AAC v1 / SBR-21 — wygładzanie wzmocnienia i amplitudy szumu

Dodano własny moduł `sauti_aac_sbr_smooth_coefficients` dla
**rzeczywistych współczynników wzmocnienia i amplitudy szumu QMF**.
Wygładzane są parametry *przed* składaniem sygnału zespolonego,
a nie gotowe próbki dźwięku.

W trybie `bs_smoothing_mode=0` używany jest pięciopunktowy
filtr MPEG-4 SBR o współczynnikach (od najnowszej próbki):
`0.33333333333333, 0.30150283239582, 0.21816949906249,`
`0.11516383427084, 0.03183050093751`.

W trybie `bs_smoothing_mode=1` filtr jest wyłączony. Obwiednie
oznaczone jako atak (*transient*) również pomijają filtr,
ale zawsze zapisują bieżące wartości w historii na kolejne
sloty. Przy pierwszym uruchomieniu cztery wcześniejsze sloty
są inicjalizowane pierwszym poziomem obwiedni.

`SautiAacSbrSmoothState` zapamiętuje cztery sloty dla każdego
subpasma oraz geometrię `[kx,k2]`. Rozbieżność geometrii wymaga
jawnego resetu stanu (wyzerowania obiektu). Obsługiwanych jest
1–40 slotów, 1–5 obwiedni, 64 subpasma w układzie czasowym.
Dane ujemne, NaN/Inf, błędne granice i stan oznaczają
**pełne wycofanie wyniku i historii**.

Test `test_aac_sbr_smooth.c` weryfikuje:
- analityczny wynik pięciu wag i stopniowej zmiany obwiedni;
- identyczność pojedynczej ramki i dwóch kolejnych wywołań;
- ominięcie wygładzania podczas ataku oraz przełączanie trybów;
- 40 slotów, 5 obwiedni, błędne dane i atomowość stanu.

```sh
make test-aac-sbr-smooth PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Zakres:** SBR-21 generuje na razie szereg wygładzonych
współczynników, ale nie jest jeszcze podłączony do SBR-20.
Włączenie ich do wspólnej regulacji HF będzie kolejnym
etapem, po sprawdzeniu numeryki. Normatywna tablica
512 wektorów szumu, synteza i analiza QMF oraz łączenie
ramek HE-AAC do PCM również pozostają do wykonania.


## HE-AAC v1 / SBR-22 — integracja wygładzania w torze HF

Dodano `sauti_aac_sbr_hf_joint_process_smoothed` — nowy
punkt wejścia, który łączy SBR-16/17 (limiter/gain),
SBR-21 (pięciopunktowe wygładzanie współczynników),
SBR-18 (renderowanie szumu) i SBR-19 (harmoniczne).
Dotychczasowy SBR-20 pozostaje **niezmieniony** jako
punkt odniesienia oraz ścieżka bez wygładzania.

Po ustaleniu docelowych wzmocnień limiterem obliczana
jest pełna amplituda sygnału na podpasmo:
`G_total = G_limiter * sqrt(signal_share)`, oraz
amplituda szumu `A_noise = sqrt(E*noise_share)`.
W subpasmach harmonicznych (od transient albo z
poprzedniej ramki) szum losowy jest zerowany,
a jego udział rezerwowany dla sinusoidy. SBR-21
wygładza **te współczynniki** w czasie QMF, a nie
gotowe próbki zespolone. Dopiero po tym SBR-18
wykonuje `X*G_smooth + N*A_smooth` z identycznym
generatorem faz co wcześniej, zaś SBR-19 dokłada
czterofazowe harmoniczne.

Zastosowano nowy interfejs
`sauti_aac_sbr_hf_noise_mix_levels`, który przyjmuje
wprost `[slots][64]` wartości G i A szumu i utrzymuje
nieprzerwaną sekwencję generatora nawet przy A=0.

Stan `SautiAacSbrHfSmoothState` zawiera obie historie
generatorów oraz cztery ostatnie sloty współczynników.
Wyjście, statystyki ogranicznika oraz cały stan są
zatwierdzane **łącznie** wyłącznie po sukcesie.

Test `test_aac_sbr_hf_joint_smooth.c` sprawdza:
- zgodność wyniku bez wygładzania z SBR-20 oraz
  identyczność stanów noise/sine;
- analityczną rampę wzmocnienia, amplitudy szumu,
  pomijanie filtra na ataku i historię między ramkami;
- wywołanie na 40 slotach, 5 obwiedniach i 2 przedziałach
  szumu oraz aliasowanie wejścia i wyjścia;
- pełny rollback po błędzie także w końcowym etapie.

```sh
make test-aac-sbr-hf-joint-smooth PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granice:** to nadal nie jest pełna zgodność HE-AAC.
W torze pozostaje własna, nienormatywna sekwencja
szumu ośmiu faz; obsługa transjentów, kalkulacja
końcowego ogranicznika uwzględniającego jednocześnie
harmoniczne i szum, analiza/synteza QMF oraz połączenie
z dekoderem AAC do PCM są nieukończone.


## HE-AAC v1 / SBR-23 — normatywna 512-wektorowa tablica szumu

Dodano osobny moduł `sauti_aac_sbr_noise_rom` z kompletem
**512 zespolonych wektorów szumu SBR**. Są to numeryczne
wartości referencyjne MPEG-4 AAC SBR; zgodność pozycji
zweryfikowano z publiczną tablicą FFmpeg n6.1
`libavcodec/sbrdsp_template.c` (LGPL-2.1-or-later).
Dane te są statyczne i nie wymagają zewnętrznego dekodera
ani żadnych bibliotek kodeka w czasie wykonania.
Przed publiczną redystrybucją projektu należy potwierdzić
obowiązki licencyjne związane z wykorzystaniem wartości
tablicy referencyjnej.

Wartości szumu nie są znormalizowane do modułu 1.
Różni to je zasadniczo od wcześniejszego, autorskiego
generatora ośmiu faz z SBR-18. Poprzedni generator
pozostaje nienaruszony i nadal obsługuje SBR-20/SBR-22.

Indeks w MPEG-4 SBR **rośnie przed odczytem**:
z początkowego `index=0` odczytywane są kolejno
`ROM[1]`, `ROM[2]`, ..., `ROM[511]`, `ROM[0]`.
Indeks wraca na początek przez maskę `& 511`.
`SautiAacSbrNoiseRomState` przechowuje ostatni indeks
i liczbę wyemitowanych próbek. Stan jest niezależny
na każdy kanał i zachowywany między ramkami.

Interfejsy:
- `sauti_aac_sbr_noise_rom_lookup`: odczyt dowolnego
  wektora bez przesuwania stanu;
- `sauti_aac_sbr_noise_rom_next` i `_sequence`:
  atomowe pobieranie kolejnych wektorów;
- `sauti_aac_sbr_noise_rom_apply_levels`: natywne
  `Y=X*gain + ROM[index]*noise_amplitude` na zespolonych
  subpasmach QMF (1–40 slotów). Amplitudy są przekazywane
  bezpośrednio z poziomu współczynników, dzięki czemu
  można później wykorzystać je po SBR-21.

Moduł przetwarza pasma poza `[kx,k2]` bez zmian.
Indeks przesuwa się po każdym paśmie HF nawet dla
zerowego współczynnika szumu. Wejście/wyjście mogą
wskazywać ten sam bufor. Błędne parametry, geometria,
NaN/Inf, overflow czy skrócone bufory nie zmieniają
wyjścia **ani stanu**.

Test `test_aac_sbr_noise_rom.c`: dokładne wartości
wybranych pozycji referencyjnych (0, 1, 2, 7, 31,
127, 255, 256, 511), niejednostkowe moduły, pełne
512-próbkowe zawinięcie, 2560 wektorów w jednym i
wielu wywołaniach, 40-slotowe QMF, pracę in-place,
maskowanie szumu i testy fail-closed.

```sh
make test-aac-sbr-noise-rom PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granica:** sam moduł SBR-23 jest osobną ścieżką.
Włączenie normatywnej ROM w SBR-22, zastąpienie
przestarzałego generatora faz i testy rzeczywistych
strumieni będą osobnymi etapami. Nadal brakuje
banków analizy/syntezy QMF oraz kompletnego toru PCM.


## HE-AAC v1 / SBR-24 — 512-wektorowa ROM we wspólnym torze HF

Nowa ścieżka `sauti_aac_sbr_hf_joint_process_rom` łączy:

- automatyczne granice limitera SBR-17 i ograniczanie/boost SBR-16;
- pięciopunktowe wygładzanie współczynników SBR-21;
- **normatywną 512-wektorową tablicę szumu SBR-23**;
- czterofazowe dodatkowe harmoniczne SBR-19.

Przetwarzanie QMF następuje w kolejności:
`limiter → wyznaczenie gain/noise dla każdej obwiedni
→ smoothing QMF slotów → ROM szumu → sine → atomowy commit`.

`SautiAacSbrHfRomState` przechowuje niezależnie:
index/emitted ROM (modulo 512), stan czterofazowy
harmonicznych, a także historię czterech slotów
wygładzanych współczynników. Pierwsza próbka
szumu po starcie z `index=0` to `ROM[1]`;
kolejne rosną po każdym subpaśmie HF, również
gdy harmoniczna blokuje dodawanie szumu.
Indeks i pozostałe stany pozostają zachowane
między ramkami i w pełni odtwarzają się po
podziale jednej operacji na kolejne wywołania.

Poprzedni SBR-20/22 z autorską ośmiofazową
sekwencją pozostaje bez zmian i stanowi
punkt odniesienia do testów. SBR-24 ma oddzielny
interfejs i stan kanału, bez współdzielenia
generatora z tymczasową implementacją.

Test `test_aac_sbr_hf_joint_rom.c`:
- porównanie pierwszych wyników QMF z
  konkretnymi wektorami ROM[1] i ROM[2];
- harmoniczna uruchamiana od drugiej obwiedni;
  szum jest wtedy wyłączony, ale indeks rośnie;
- analityczna rampa pięciopunktowego wygładzania;
- identyczność pełnej i podzielonej ramki,
  aliasowanie wejścia i wyjścia;
- cztery 40-slotowe ramki z wielokrotnym
  zawijaniem ROM modulo 512;
- brak szumu — bitowa zgodność z SBR-22;
- pełny rollback przy błędach geometrii,
  współczynników, ROM, sine i smoothing.

```sh
make test-aac-sbr-hf-joint-rom PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granica:** włączenie normatywnej tablicy usuwa
dotychczasową niezgodność generatora szumu,
ale nie oznacza gotowego, zgodnego HE-AAC.
Wciąż wymagane są banki analizy/syntezy QMF,
normatywny wspólny rachunek energii limiter/noise/sine
oraz integracja strumieni AAC i wyjścia PCM.


## HE-AAC v1 / SBR-25 — 640-tap polifazowe okno syntezy QMF

Zaimplementowano samodzielny moduł
`sauti_aac_sbr_qmf_synth_window_64`, pierwszy
etap **banku syntezy QMF**. Przetwarza każdy
blok **128 już zmodulowanych wartości rzeczywistych V**
na 64 wynikowe próbki za pomocą 640-współczynnikowego
okna prototypowego MPEG-4 SBR.

Filtr dla wyjścia `n=0..63` sumuje 10 wkładów:

`out[n] = sum(j=0..9, C[64*j+n] * V[128*j + 64*(j&1) + n])`.

`V` to bieżący blok 128 wartości i dziewięć
poprzednich bloków. Historię przechowujemy w
10-elementowym pierścieniu (`head`, `ready`,
`history[10][128]`) między kolejnymi ramkami.
Bez kosztownego przesuwania 1280 próbek przy
każdym slocie. Nowy kanał wymaga stanu wyzerowanego.

640 współczynników prototypu zweryfikowano względem
publicznej tablicy FFmpeg n6.1
`libavcodec/aacsbrdata.h` (LGPL-2.1-or-later).
Nie występuje zależność od FFmpeg ani jakiegokolwiek
innego kodeka w czasie wykonania. Tak jak przy
SBR-23, przed dystrybucją należy rozstrzygnąć
obowiązki licencyjne dotyczące użytej tablicy.

Moduł wspiera 1–40 slotów na wywołanie i
sprawdza wszystkie wejściowe wartości, pamięć
historii, granice, NaN/Inf, przepełnienia i
nakładanie się buforów. W razie błędu **cały
wynik i historia pozostają niezmienione**.

Test `test_aac_sbr_qmf_synth_window.c`:
- normatywne wartości wybranych współczynników
  i symetrię całej 640-elementowej tablicy;
- odpowiedź na impuls przechodzący przez
  dziesięć bloków polifazowego nakładania;
- niezależnie policzone sumy filtra dla 40 slotów;
- zgodność ramki pełnej i podzielonej na 17+23 sloty;
- cztery kolejne 40-slotowe ramki i ciągłość historii;
- sprawdzanie granic, NaN/Inf, nakładania buforów
  oraz rollback przy błędach późnego slotu.

```sh
make test-aac-sbr-qmf-synth-window PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Ważne ograniczenie:** SBR-25 nie przyjmuje
bezpośrednio zespolonych podpasem QMF z SBR-24.
Brakuje transformacji/modulacji QMF z 64
zespolonych pasm do 128 wartości rzeczywistych V.
Dopiero po jej implementacji i integracji można
przeprowadzić syntetyczne testy toru QMF → próbki
i następnie zbudować pełny HE-AAC → PCM.


## HE-AAC v1 / SBR-26 — zespolona modulacja syntezy QMF i pierwszy pełny tor filtracyjny

Zaimplementowano `sauti_aac_sbr_qmf_synth_modulate_64`
(wersja referencyjna O(64 × 128) na slot, bez FFT/DCT/ASM).
Przetwarza 64 zespolone pasma SBR w danym slocie QMF
na **128 rzeczywistych wartości modulacji V**.

Dla numeru pasma `k=0..63` i próbki modulacji `n=0..127`:

`phase = π*(k+0.5)*(2*n+1)/128`

`V[n] = -(1/64) * sum_k (Re(X[k])*cos(phase) - Im(X[k])*sin(phase))`.

To bezpośrednia postać zespolonego 64-pasmowego
modulatora syntezy SBR, bez optymalizacji szybką
transformacją DCT-IV/MDCT. Zastosowany współczynnik
`1/64` odpowiada skali syntezy 64-pasmowej.
Moduł używa obliczeń pośrednich double, kontroluje
wszystkie zespolone wejścia, NaN/Inf, przepełnienia
i granice buforów. Wynik jest zapisywany atomowo.
Ponieważ cała ramka powstaje w buforze lokalnym,
dopuszcza także nakładanie pamięci wejścia i wyjścia.

**Połączenie z SBR-25:**
`sauti_aac_sbr_qmf_synth_64` realizuje łącznie:

`64 zespolone pasma QMF → 128 V na slot →
640-współczynnikowe okno polifazowe → 64 próbki / slot`.

Zachowuje stan dziesięciu bloków SBR-25 między
kolejnymi wywołaniami. Dwie części muszą zakończyć
się powodzeniem, zanim zostaną zapisane próbki
oraz aktualizacja stanu okna (*fail-closed*).
Nie są wymagane zewnętrzne biblioteki kodeków.

Test `test_aac_sbr_qmf_synth_mod.c` pokrywa:
- dokładne wzorcowe wartości dla czysto rzeczywistych
  i urojonych zespolonych podpasm 0, 17 i 63;
- symetrię faz, liniowość i niezależną referencję
  obliczoną z wysoką precyzją long double;
- 40 slotów, zgodność pojedynczego i podzielonego
  wywołania oraz przetwarzanie in-place;
- porównanie pełnego toru z osobno uruchomionymi
  modułami SBR-26 i SBR-25;
- pierwszy slot liczony z jedynego niezerowego
  bloku historii i znanego współczynnika filtra;
- błędy późnych próbek, NaN/Inf i atomowość stanu.

```sh
make test-aac-sbr-qmf-synth-mod PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Zastrzeżenie:** jest to pierwsza pełna matematyczna
ścieżka *syntezy QMF* zespolone pasma → próbki rzeczywiste,
ale jeszcze nie gotowy dekoder HE-AAC. Nadal potrzebne są
bank analizy QMF dla sygnału AAC-LC, zsynchronizowanie
opóźnień i łączenie całych ramek, dopracowanie algorytmu
wspólnej regulacji energii oraz weryfikacja numeryczna
z niezależnym dekoderem i rzeczywistymi bitstreamami.
Przed optymalizacją ASM trzeba także uzgodnić skalowanie
i kolejność próbek z referencyjnym torem całej ramki.


## HE-AAC v1 / SBR-27 — 32-pasmowy zespolony bank analizy QMF

Dodano własny moduł `sauti_aac_sbr_qmf_analyze_32`.
Przetwarza `32` kolejne próbki rzeczywiste sygnału
AAC-LC (jeden slot) na `32` zespolone podpasma QMF
w formacie używanym przez SBR-12/13. Nie korzysta
z zewnętrznych bibliotek kodeków, nie alokuje pamięci
i przechowuje osobną historię dla każdego kanału.

Moduł korzysta z **tej samej 640-elementowej tablicy
prototypu co SBR-25**, lecz pobiera współczynniki
o **parzystych indeksach** — łącznie 320.
Historia `x[320]` jest ułożona od najnowszych próbek:
nowy blok PCM zapisuje `x[31-n] = pcm[n]`,
a wcześniejsze 288 wartości przesuwane są o 32.

Okno i polifazowa suma (`n=0..63`):

`u[n] = sum(j=0..4, x[n+64*j] * C[2*(n+64*j)])`.

Bezpośrednia (nieoptymalizowana) modulacja zespolona
na każde `k=0..31`:

`X[k] = 2*sum(n=0..63, u[n] *
exp(i*pi/64*(k+0.5)*(2*n-0.5)))`.

Wzór i numeracja odpowiadają bankowi analizy SBR
MPEG-4, Figure 4.42. Obliczenia pośrednie korzystają
z `double`, a wynik ma postać `{float re, float im}`.
Opcjonalny parametr `active_bands` (1..32) pozwala
wyzerować pasma poza aktualnym progiem `kx`,
przy zachowaniu pełnego 32-próbkowego postępu historii.

Obsługiwane jest 1–40 slotów na wywołanie, również
po podziale jednej ramki na wiele wywołań.
Bufory wejścia i wyjścia mogą na siebie nachodzić
(przy wystarczającej pojemności). W razie
błędu danych lub stanu wyjście i historia pozostają
**bitowo niezmienione**. Po każdej udanej ramce
aktualizowana jest liczba skonsumowanych próbek PCM.

Test `test_aac_sbr_qmf_analysis.c` weryfikuje:
- wzorcowe próbki zespolone z impulsu `x[1]`
  w podpasmach 0, 1, 7 i 31 (faza, znak, skala);
- rozchodzenie się impulsu przez 10 slotów
  i wygaśnięcie po opuszczeniu 320-próbkowej historii;
- pełne 40 slotów względem niezależnego obliczenia
  `long double`, z funkcjami `cosl` i `sinl`;
- identyczny wynik po podziale na 17+23 sloty
  i identyczny końcowy stan kanału;
- maskowanie wyższych pasm `kx`, ciszę,
  niezależność kanałów, nakładanie buforów;
- fail-closed przy NaN/Inf, złej pojemności,
  uszkodzonej historii i liczniku próbek.

```sh
make test-aac-sbr-qmf-analysis PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Stan toru:** mamy teraz oddzielnie bank analizy
`32 PCM -> 32 zespolone QMF` (SBR-27), rekonstrukcję
HF (SBR-12–24) i bank syntezy `64 zespolone QMF ->
64 próbek` (SBR-25/26). Następnym etapem będzie
zsynchronizowanie `XLow`/`XHigh`, opóźnień
i pełnej ramki, następnie weryfikacja realnego
HE-AAC względem niezależnej implementacji.

**Zastrzeżenie:** powodzenie testów syntetycznych
nie oznacza jeszcze zgodności całego dekodera.
Połączenie z AAC-LC i testy faktycznych strumieni
HE-AAC oraz poprawności wyjściowego PCM są nieukończone.


## HE-AAC v1 / SBR-28 — pierwsza integracja całej ścieżki QMF jednego kanału

Dodano `sauti_aac_sbr_qmf_frame_process` i stan
`SautiAacSbrQmfFrameState`, który przechowuje
historię analizy QMF, dwa poprzednie sloty zespolone
dla predykcji HF, stan regulacji ROM i bank syntezy.

Wspólne wywołanie przetwarza na każdy slot:

1. 32 próbki PCM AAC-LC → 32 zespolone pasma (SBR-27);
2. generowanie pasm HF z łatki SBR-12 **lub** predyktor
   SBR-13 z dostarczonymi przez wywołującego
   współczynnikami LPC/chirpem;
3. limiter, wygładzanie, szum z normatywnej ROM i
   harmoniczne (SBR-24);
4. połączenie dolnych pasm `[0,kx)` z odtworzonymi
   górnymi `[kx,k2)`, wyzerowanie pasm `[k2,64)`;
5. modulacja i synteza QMF 64-pasmowa (SBR-26/25),
   64 próbki wynikowe na slot.

Konfiguracja zawiera wcześniej zdekodowane dane
sterujące i energetyczne SBR; `slots=1..40`.
Predykcja LPC nie jest tu szacowana automatycznie.
Jeśli obie opcjonalne struktury LPC i chirp są
nieobecne, włączana jest prosta transpozycja.

Stan kanału jest niezależny dla stereo. Wyjście
może nakładać się na wejście PCM, lecz nie na
stan czy diagnostykę. Wszystkie etapy korzystają
z tymczasowych buforów; **nawet błąd ostatniego
etapu pozostawia PCM, raport i wszystkie historie
bitowo niezmienione**.

Test `test_aac_sbr_qmf_frame.c` pokrywa:
- zgodność próbki po próbce z ręcznie połączonym
  łańcuchem SBR-27 → 12/13 → 24 → 26/25;
- zgodność ramki 8-slotowej i dwóch połówek 4+4
  (wynik, ROM, faza sinusoidy, analiza i synteza);
- nakładanie buforów, ciągłość historii dwóch
  slotów dla predyktora;
- osobne wyniki podstawowej transpozycji i LPC;
- pełną ciszę oraz ściśle atomowe błędy w późnym
  etapie banku syntezy QMF;
- odrzucenie dodatniej energii przypisanej do
  niezałatanych najwyższych podpasm HF.

```sh
make test-aac-sbr-qmf-frame PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Zastrzeżenia:** SBR-28 jest testowalnym mostem QMF,
**nie** gotowym, normatywnie zgodnym dekoderem HE-AAC.
Na wejściu wymaga już zdekodowanego PCM AAC-LC,
siatki SBR, energii i flag. Czasowe przesunięcia
analizy/syntezy i pełnej ramki SBR nie zostały
jeszcze zweryfikowane na strumieniach zgodności.
Moduł SBR-16 odmawia odtworzenia dodatniej energii
z pustego pasma źródłowego; dotyczy to m.in.
ostatnich 1–2 pasm pominiętych przez mapę łatek.
Kolejne etapy muszą dopracować obsługę takich
konfiguracji, automatyczną estymację LPC i
integrację realnego bitstreamu HE-AAC do PCM.


## HE-AAC v1 / SBR-29 — parsowanie rzeczywistego ładunku SBR z FIL

Po połączeniu syntezy QMF (SBR-28) praca przeniosła się z
syntetycznych parametrów DSP na **rzeczywistą składnię bitową**.
Nowy `sauti_aac_sbr_payload_sce_parse` przyjmuje cały
element `EXT_SBR_DATA` (`extension_type=13`) wraz z jego
długością i pozycją bitową pochodzącą z `ID_FIL`.
Może startować na **niebajtowej** pozycji w buforze
i ogranicza wszystkie odczyty do długości elementu FIL.

Parser wywołuje w kolejności wcześniejsze moduły:
- opcjonalny nagłówek SBR lub nagłówek zapamiętany
  z poprzedniej ramki (`bs_header_flag`);
- obliczanie `k0/k2`, master, high/low/noise i patch;
- `bs_data_extra`, siatkę czasu SBR-01, dtdf/invf SBR-06;
- kodowane Huffmanem obwiednie i poziomy szumu SBR-08;
- dekwantyzację energii/noise SBR-09;
- `bs_add_harmonic_flag` i wektor flag wysokich pasm;
- flagę `bs_extended_data` oraz wyrównanie FIL.

Wyjściowa struktura `SautiAacSbrPayloadFrame` zawiera
tabele, energie, siatki czasowe przeliczone z 16
jednostek SBR na **32 sloty QMF**, harmoniczne,
parametry limitera i wygładzania. Pomocnicze API
`sauti_aac_sbr_payload_make_hf` tworzy widok
`SautiAacSbrHfJointConfig`, który może być przekazany
bezpośrednio do toru SBR-28.

Stan `SautiAacSbrPayloadState` zapamiętuje nagłówek
i historię entropijną Huffmana między ramkami.
Na błędzie, w tym ucięciu FIL, niezmienione pozostają
historia, wyjście i parametry; nie ma odczytów poza
deklarowaną granicą ładunku. Nie jest potrzebna
żadna zewnętrzna biblioteka kodeków.

**Świadomie ograniczony zakres pierwszej wersji:**
pojedynczy kanał SCE, AAC-LC core 1024/32 QMF,
typ 13 bez CRC, bez rozszerzeń PS oraz tylko
granice obwiedni w przedziale 0..16. Typ 14
(SBR CRC), CPE, PS i dodatkowe payloady nie są
jeszcze obsługiwane — parser je odrzuca.
Przesunięcia czasowe wymagające danych z sąsiednich
ramek również czekają na buforowanie/rozszerzenie DSP.

Test `test_aac_sbr_payload.c` buduje pełne skompresowane
ładunki zgodne ze składnią SBR, w tym niezależne
kodowanie Huffmana, nagłówek, siatkę, poziomy noise
i harmoniczne. Sprawdza rozpakowanie payloadu
od pozycji niebajtowej, powtórne użycie nagłówka
oraz kodowanie różnicowe względem poprzedniej ramki.
Osobno weryfikuje obsługę błędnego typu rozszerzenia,
CRC/PS, obcięcia, przepełnień i niezmienność historii.

```sh
make test-aac-sbr-payload PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Następny obowiązkowy krok:** pełny demultiplekser
ADTS raw_data_block (SCE/CPE + ID_FIL), który dostarczy
prawdziwy payload do SBR-29 i PCM AAC-LC do SBR-28.
To otworzy drogę do porównania pierwszych plików
HE-AAC v1 z niezależnym dekoderem. Sam SBR-29
nie oznacza jeszcze pełnego dekodowania muzyki.


## HE-AAC v1 / SBR-30 — rzeczywista ramka ADTS, rdzeń PCM i SBR z ID_FIL

Nowy `sauti_aac_he_adts_mono_decode_ex` przyjmuje
**całą ramkę ADTS**: pojedynczy AAC-LC mono `ID_SCE`,
jego zakodowane widmo, `ID_FIL/EXT_SBR_DATA` typu 13,
`ID_END` i wyrównanie do bajtu. Pozycja rozszerzenia
SBR wynika z faktycznego parsowania SCE, a nie
z wyszukiwania wzorca bajtów lub heurystyki.

Pierwsze wyniki:
- 1024 próbek PCM rdzenia AAC-LC przy core_sample_rate,
  z IMDCT, oknami, overlap, PNS i TNS;
- `SautiAacSbrPayloadFrame` z faktycznie zdekodowanymi
  parametrami SBR: nagłówkiem, energią, siatką,
  częstotliwościami, flagami harmonicznych i łatkami;
- trwały `SautiAacHeAdtsMonoState`, przechowujący
  osobno historię AAC-LC i stan nagłówków/Huffmana SBR.

Stan kanału inicjuje się jednorazowo przez
`sauti_aac_he_adts_mono_init`, ponownie po seeku
lub przerwaniu ciągłości strumienia.

Obsługiwany pierwszy wariant: ADTS mono, AAC-LC 1024,
jedno raw_data_block, brak CRC, jeden element SBR
typu 13 i ewentualnie nieszkodliwy EXT_FILL.
Odrzucamy CPE, inne rozszerzenia, podwójne SBR,
brak SBR, błędny padding i składnię wykraczającą
poza zakres SBR-29. Dotychczasowy dekoder AAC-LC
nadal konserwatywnie odrzuca HE-AAC zamiast
ignorować rozszerzenie.

Cały proces jest transakcyjny: niepoprawny FIL,
ucinanie danych czy awaria syntezy nie zmienia
PCM, nagłówków, historii IMDCT ani Huffmana.

Test `test_aac_sbr_adts_mono.c` buduje kompletną
ramkę ADTS bit po bicie: 24 kHz rdzeń AAC-LC,
48 kHz SBR, niebajtowy FIL, realne kody Huffmana.
Sprawdza dwa kolejne ADTS z powtórnym użyciem
nagłówka SBR, historię entropijną, próbki PCM
i rollback po błędach CRC, ucięciu, braku lub
nieprawidłowym typie rozszerzenia.

```sh
make test-aac-sbr-adts-mono PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Granica:** SBR-30 zwraca 1024 próbki rdzenia,
nie 2048 próbek końcowego HE-AAC. Integracja
z pełnym torrem SBR-28, opóźnienia analizy/syntezy
oraz porównanie faktycznych nagrań z dekoderem
referencyjnym pozostają do wykonania.


## HE-AAC v1 / SBR-31 — pełna ścieżka ADTS do 2048 próbek QMF

Nowa, **eksperymentalna** funkcja
`sauti_aac_he_adts_qmf_decode_ex` integruje:

- SBR-30: prawdziwą ramkę ADTS AAC-LC SCE + FIL
  i 1024 próbek rdzenia PCM;
- SBR-29: rzeczywiste dane entropijne rozszerzenia SBR,
  siatkę, energie, szum, flagi harmonicznych i tabelę łatek;
- SBR-28: 32-pasmową analizę QMF, rekonstrukcję HF
  (w tej wersji bazowa transpozycja), limitery, ROM szumu
  i harmoniczne, a następnie syntezę 64-pasmową;
- wynik: **2048 float** na jedną 1024-próbkową
  ramkę AAC-LC, o podwojonej częstotliwości próbkowania.

Stany AAC-LC (IMDCT/overlap), parsowania SBR
(nagłówek i entropia), analizy QMF, fazy ROM,
harmonicznych i syntezy QMF są aktualizowane
**jedną transakcją**. W każdej ścieżce błędu wynik
i cały stan kanału pozostają bitowo niezmienione.

Test `test_aac_sbr_adts_qmf.c` nie podmienia
rdzenia sztucznym buforem PCM: konstruuje kompletną
ramkę AAC-LC z **niezerowym zakodowanym widmem**
i prawdziwym payloadem SBR w ID_FIL. Odtwarza
dwie kolejne ramki (header reuse i time Huffman),
a wynik porównuje próbka po próbce z osobnym
wywołaniem SBR-30 + SBR-28. Sprawdza także
zgodność stanów i liczników oraz wycofywanie
przy obciętym ADTS, błędnym CRC, uszkodzonym QMF
i braku źródła HF dla dodatniej energii SBR.

```sh
make test-aac-sbr-adts-qmf PKG_CONFIG=/bin/false
make test-aac-native PKG_CONFIG=/bin/false
```

**Bardzo ważne:** to nadal eksperymentalna ścieżka QMF,
nie deklaracja poprawności dekodowania HE-AAC.
Nie znamy jeszcze zgodności czasowej analizy
i syntezy z referencyjnym dekoderem, a zerowe
źródła dla dodatnich obwiedni SBR są odrzucane
(nie generujemy sztucznego HF). Brakuje estymacji
LPC na żywo, obsługi PS/CRC/CPE, elastycznych siatek
przechodzących przez granice ramek oraz próby
z **niezależnie zakodowanym nagraniem**. Testy
konstruowanych ADTS są konieczne, ale niewystarczające
do stwierdzenia zgodności bitowej czy brzmieniowej.


## HE-AAC v1 / SBR-32 — diagnostyka niezależnie zakodowanych plików ADTS

Powstało narzędzie `tools/sauti_he_aac_probe.c`
(kompilowane jako `build/sauti_he_aac_probe`).
To **pierwszy test rzeczywistych plików wejściowych**
bez konstruowania zawartości ADTS w testach Sauti.

Pierwszy przebieg skanuje wszystkie nagłówki ADTS:
liczbę ramek, rozmiary, CRC, kanały i częstotliwość rdzenia.
Drugi przebieg wywołuje SBR-31 na kolejnych ramkach
z **jednym, nieprzerwanym stanem**. Przy pierwszym
niepowodzeniu przeprowadza osobne, niezatwierdzane
parsowanie SBR-30 na kopii stanu i pokazuje,
czy przyczyna dotyczy AAC-LC / ID_FIL / Huffmana
czy późniejszej syntezy HF/QMF.

**Ważna gwarancja:** po błędzie nie wstawiamy ciszy,
nie resetujemy automatycznie historii i nie zgłaszamy
późniejszych ramek jako poprawnie zdekodowanych.
Każde powodzenie oznacza tylko uzyskanie 2048
skończonych próbek z eksperymentalnego toru,
**nie normatywną zgodność HE-AAC**.

```sh
make probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe /sciezka/do/nagrania.aac 12
```

### Przygotowanie niezależnej próbki HE-AAC v1 mono

Można użyć aktualnego programu **FAAC** z obsługą
`--object-type he-aac-v1` i `-a` (ADTS).
Starsze systemowe wydania FAAC mogą jeszcze nie
obsługiwać tego trybu. Weryfikacja:

```sh
faac --help-qual
```

Przykład z FFmpeg wyłącznie jako generatorem
**wejściowego WAV**, nigdy jako biblioteką dekodera Sauti:

```sh
ffmpeg -hide_banner -loglevel error -y -f lavfi \
  -i 'sine=frequency=1000:sample_rate=48000:duration=2' \
  -ac 1 -c:a pcm_s16le /tmp/sauti_he_mono.wav
faac --object-type he-aac-v1 -b 32 -a \
  -o /tmp/sauti_he_mono.aac /tmp/sauti_he_mono.wav
./build/sauti_he_aac_probe /tmp/sauti_he_mono.aac 12
ffprobe -v error -show_entries stream=profile,sample_rate,channels \
  -of default=nw=1 /tmp/sauti_he_mono.aac
```

FAAC jest **tylko niezależnym enkoderem testowym**.
FFprobe pełni jedynie funkcję referencyjnego
rozpoznania profilu. Kompilacja i przetwarzanie
Sauti nie wymagają FAAC ani FFmpeg.

Publiczne oficjalne testy porównawcze FFmpeg FATE
można znaleźć pod
`https://fate-suite.ffmpeg.org/aac/`;
wiele ma jednak kanał **stereo** lub format MP4,
więc aktualny parser mono ADTS prawidłowo je
odrzuci. Nie traktujemy takiej odmowy jako błędu
poprawności algorytmu QMF.

**Kolejny etap:** zebrać dokładny raport
z prawdziwego pliku mono, poprawić pierwszą
rzeczywistą niezgodność parsera lub HF/QMF,
a następnie skonfrontować amplitudę, opóźnienie
i widmo PCM z niezależnym dekoderem.


## Punkt kontrolny 2026-11-04 — SBR-52 zakończony, przerwa przed SBR-53

**Status na koniec sesji:** zamknięty etap *dekodowania rdzenia AAC-LC,
parsowania SBR i przygotowania historii czasowej SBR* dla przebadanego
strumienia mono HE-AAC v1 ADTS. Kolejny etap — **SBR-53: synteza HF/QMF
z parametrami międzyramkowymi** — **nie został rozpoczęty**. Nie
utożsamiać poprawnego parsera lub mapy slotów z gotowym dekoderem PCM.

### Potwierdzone wyniki

- Gałąź: `feature/aac-family-asm`; ostatnia sprawdzona rewizja
  `754e2de` (SBR-52). Niniejszy wpis jest checkpointem dokumentacyjnym.
- Rzeczywisty, niezależnie zakodowany plik testowy na komputerze Fedora:
  `/tmp/sauti_he_v1_64k.aac`, **50 ramek ADTS**, **17066 bajtów**,
  mono, bez CRC, rdzeń AAC-LC **24 kHz**, wyjściowe HE-AAC **48 kHz**.
  Plik testowy znajduje się lokalnie u użytkownika, nie w repozytorium.
- **50/50** ramek przeszło ciągłe dekodowanie 1024 próbek rdzenia
  AAC-LC oraz **temporalne parsowanie** SBR (bez końcowego PCM HE-AAC).
  Obsługiwane są powtarzane nagłówki i historia entropijna.
- Produkcyjna, **eksperymentalna** ścieżka
  `sauti_aac_he_adts_qmf_decode_ex` wygenerowała **2048 float PCM**
  dla ramek **0 i 1**. Ramka **2** nadal jest prawidłowo odrzucana:
  `unsupported_fil` przez restrykcyjny parser (SBR FIXVAR kończy
  obwiednię po granicy ramki). **Nie ma sukcesu 50/50 dla syntezy.**
- SBR-46: w pierwszej ramce zerowego rdzenia AAC-LC stwierdzono
  `harmoniczne=0/14`, dodatni parametr szumu SBR
  `Q=0.00390625`; jawna, noise-aware ścieżka HF
  pozwoliła na prawidłowy rozruch bez zgadywania sygnału źródłowego.
  Pierwotne, restrykcyjne API limitera pozostaje dostępne.
- SBR-47–52: temporalny parser i plan dzielą obwiednie na sloty
  bieżące oraz odroczone, historia zachowuje kompletny poprzedni
  `SautiAacSbrPayloadFrame`, a resolver przypisuje 32 sloty QMF
  właściwej ramce i indeksom obwiedni energii/szumu.
  **Weryfikacja na rzeczywistym strumieniu:**
  - ramka **2 → 3**: 2 odroczone sloty QMF;
    w ramce 3 sloty **0–1** należą do ramki 2
    (`env=3, noise=1`), sloty **2–31** do ramki 3;
  - ramka **48 → 49**: 6 odroczonych slotów;
    w ramce 49 sloty **0–5** należą do ramki 48
    (`env=1, noise=1`), sloty **6–31** do ramki 49;
  - obydwa przejścia mają dokładnie stykające się granice,
    bez luk i nakładania się. Historia: **50 zapisanych ramek,
    2 przekazania, najdłuższy ogon 6 slotów**.
- Ostatnie testy na Fedorze: `test-aac-sbr-payload` PASS,
  `test-aac-sbr-adts-qmf` PASS i kompilacja `probe-he-aac` PASS;
  diagnostyka realnego pliku potwierdziła przypisania QMF i 50/50
  rdzenia. **Nie są to testy zgodności brzmieniowej PCM.**
- Cała ścieżka natywna w tych testach nie potrzebuje
  zewnętrznej biblioteki dekodującej AAC.

### Co pozostało — rozpocząć od SBR-53

1. **Przebudować driver HF/QMF do pracy na granicach ramek**:
   zachować ciągły stan analizy QMF, transpozycji/predykcji,
   wygładzania współczynników, fazy ROM szumu, harmonicznych
   i 64-pasmowej syntezy.
2. **Podłączyć resolver SBR-52 do DSP**, zamiast traktować każdą
   ramkę jako jeden zestaw obwiedni. Przetwarzać odroczone sloty
   według oryginalnych parametrów poprzedniej ramki, a pozostałe
   według bieżącej, bez sztucznego przycinania obwiedni, dopisywania
   ciszy lub wymyślania energii HF. Dbać o właściwe przesunięcia
   czasowe i opóźnienia banków filtrów.
3. **Zachować transakcyjność**: błędna składnia lub nieukończona
   synteza nie może zmieniać PCM ani stanu AAC-LC, SBR, QMF,
   ROM i limiterów. Dodać regresje dla granic 2→3 i 48→49,
   błędnych/uciętych danych, ciągłości i restartu po seeku.
4. **Pierwszy cel mierzalny:** ramka 2 poprawnie przechodzi
   pełną ścieżkę i produkuje 2048 próbek PCM; następnie ramka 3,
   kompletne **50 ramek** i pozostałe przypadki siatek SBR.
5. **Walidacja zgodności audio**: porównać natywne PCM 48 kHz
   z niezależnym dekoderem, uwzględnić delay/priming,
   długość, fazę, poziomy, widmo i odsłuch. Nie ogłaszać
   zgodności HE-AAC na podstawie samych testów syntetycznych.
6. Dalsze, poza tym checkpointem: automatyczna estymacja LPC/chirp
   tam, gdzie wymaga jej standard, pozostałe warianty SBR, CRC,
   stereo/CPE, HE-AAC v2/Parametric Stereo i integracja
   natywnego dekodera z odtwarzaczem. Obecny zakres testowany
   jest mono SCE, ADTS bez CRC i SBR `EXT_SBR_DATA=13`.

### Polecenia do wznowienia prac

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-payload PKG_CONFIG=/bin/false
make test-aac-sbr-adts-qmf PKG_CONFIG=/bin/false
make probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe /tmp/sauti_he_v1_64k.aac 50
```

**Decyzja na dziś:** koniec prac po SBR-52; SBR-53 dopiero w
następnej sesji. Nie zmieniamy teraz toru dekodera.


## SBR-53a/b/c — wznowienie prac 2026-11-05

**SBR-53a i SBR-53b potwierdzone na Fedorze przez użytkownika** na
`~/Sauti-fixtures/he_test.aac`: poprawny odczyt parametrów HF i ciągła
analiza QMF/transpozycja HF dla 50/50 ramek, także przekazanie
FIXVAR 2 slotów 2→3. Syntetyczne regresje rollback PASS. W dalszym
ciągu tylko ramki 0 i 1 mają eksperymentalne wyjście PCM 2048 próbek;
obecny produkcyjny tor QMF nadal odrzuca ramkę 2.

**SBR-53c (nowy kod, oczekuje na kompilację i wyniki testów Fedory):**
`sauti_aac_sbr_temporal_power_measure` gromadzi średnią moc
nieskorygowanego HF QMF *całej* obwiedni, zamiast uśredniać jedynie
fragment do granicy AAC. Kiedy FIXVAR wychodzi 2 lub 6 slotów poza
ramkę, zachowuje sumę energii i liczbę slotów i kończy pomiar dopiero
po otrzymaniu wymaganych slotów następnej ramki. Raport zwraca wyłącznie
kompletne obwiednie, bez udawania gotowego PCM. API nie modyfikuje
istniejącego toru SBR-31. Testy sprawdzają średnią policzoną z obu
ramek, odrzucenie NaN, rollback oraz zakaz podwójnego przetworzenia
tej samej ramki. Diagnostyka realnego strumienia liczy ukończone
obwiednie i przekazania, ale nie porównuje jeszcze PCM z wzorcem.

Weryfikacja:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-power PKG_CONFIG=/bin/false
make test-aac-sbr-temporal-qmf test-aac-sbr-payload test-aac-sbr-adts-qmf probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 | grep -E 'SBR-53|Rdzen:|odrzucona'
```

**Kolejny krok po potwierdzeniu SBR-53c:** limiter i współczynniki
HF obliczane z całej prawidłowej obwiedni (w tym z ogonem poprzedniej
ramki), zgodna czasowo historia wygładzania/ROM/sinusoid oraz
transakcyjna synteza 64 QMF. Bez fałszywego sukcesu PCM, bez obcinania
brzegów i bez podmiany części nieobsługiwanych na ciszę.


## SBR-53c potwierdzone, SBR-53d do weryfikacji (2026-11-05)

Użytkownik uruchomił Fedora `make test-aac-sbr-temporal-power`
i regresje SBR-53b/a/31: **PASS**. Realny plik
`~/Sauti-fixtures/he_test.aac`: 50/50 ramek analizowanych,
57 pełnych obwiedni HF, dwa poprawne domknięcia odroczonych
obwiedni (ramka 3: 12 slotów; ramka 49: 16 slotów), brak
niedokończonego ogona przy końcu. Pełny PCM HE-AAC nadal
zatrzymuje się na ramce 2: nie zmieniać tego wyniku w raporcie.

Dodano SBR-53d jako **izolowany etap diagnostyczny**:
`sauti_aac_sbr_temporal_gain_calculate` pobiera kompletne
średnie moce HF z SBR-53c, sprawdza ich oryginalną tożsamość
obwiedni/ramki i liczy ograniczenie wzmocnienia z podziałem
na rzeczywiste pasma limitera SBR-17. Stosuje model cap/boost
SBR-16 i toleruje pasmo o zerowej energii źródłowej wyłącznie
przy dodatnim dekodowanym czynniku szumu, nie wymyśla źródła.
Nie wykonuje jeszcze stosowania gain, smoothing, noise ROM,
sinusoid ani syntezy 64-QMF; nie publikuje PCM. Oryginalne
API odtwarzania nie jest zmienione. Testy dla ogonów 2/6,
odmiennych targetów z obu ramek, noise-only i rollback
oczekują **pierwszej kompilacji/testu na Fedorze**.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-gain PKG_CONFIG=/bin/false
make test-aac-sbr-temporal-power test-aac-sbr-temporal-qmf test-aac-sbr-payload test-aac-sbr-adts-qmf probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 | grep -E 'SBR-53|Rdzen:|odrzucona'
```

Następny krok po PASS: zaprojektować opóźnione stosowanie współczynników
gain do właściwych slotów QMF z ciągłą historią smoothing/noise/sine
i pełnym rollback. Nie dopuszczać niezależnego zysku dla fragmentu
obwiedni przed otrzymaniem jej końca z kolejnej ramki.


## SBR-53d Fedora PASS, SBR-53e buforowany HF do weryfikacji (2026-11-05)

Użytkownik potwierdził kompilację i PASS `test-aac-sbr-temporal-gain`,
`test-aac-sbr-temporal-power`, `test-aac-sbr-temporal-qmf`,
`test-aac-sbr-payload`, `test-aac-sbr-adts-qmf`. Na realnym
`~/Sauti-fixtures/he_test.aac` SBR-53a/b/c/d: 50/50 ramek,
57/57 ukończonych obwiedni, 27 pasm noise-only, dwa
przekazania (ramka 3 i 49). **To nie jest 50 ramek PCM!**
Stary tor HE-AAC wciąż zatrzymuje się na ramce 2.

SBR-53e to izolowana **aplikacja wzmocnień do PRE-adjustment HF QMF**:
`sauti_aac_sbr_temporal_gain_apply` przechowuje 32 sloty
nieskorygowanego HF i ich wzmocnienia; jeśli ostatnia obwiednia
przekracza koniec ramki, czeka z emisją jej fragmentu aż
`SBR-53c/d` policzą pełne parametry także z następnej ramki.
Następnie wydaje kompletną *poprzednią* ramkę współczynników HF
z opóźnieniem jednej ramki. `_flush` wydaje ostatnią ramkę
wyłącznie gdy nie ma niedokończonej obwiedni; nie obcina
ogonów ani nie wstawia ciszy. Przed publikacją zapewnia
finite float i transakcyjny rollback. Nowa diagnostyka
liczy wejściowe i wydane ramki HF. Dopóki nie ma testów
z Fedory, nie deklarować SBR-53e PASS.

SBR-53e **nie wykonuje** normatywnego dopasowania szumu,
sinusoid, smoothing ani końcowej 64-QMF syntezy PCM.
Regresje: 2 i 6 slotów w odziedziczonej obwiedni,
odrzucenie NaN, nadmiarowego gain powodującego overflow,
wielokrotnego flush i flush przy otwartym ogonie.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-apply PKG_CONFIG=/bin/false
make test-aac-sbr-temporal-gain test-aac-sbr-temporal-power test-aac-sbr-temporal-qmf test-aac-sbr-payload test-aac-sbr-adts-qmf probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 | grep -E 'SBR-53|Rdzen:|odrzucona'
```

Po pomyślnych testach: smoothing oraz zgodny w czasie
generator ROM noise i sine dla już skorygowanych slotów;
w żadnym razie nie integrować przedwcześnie z wyjściowym PCM.


## SBR-53e potwierdzone na Fedorze; SBR-53f do testów (2026-11-05)

**Wynik lokalny użytkownika** z `~/Sauti-fixtures/he_test.aac`:
`test-aac-sbr-temporal-apply` PASS (lookahead 2/6 slotów,
flush, overflow i rollback). Regresje 53a–d, 31, 46 PASS.
Próba rzeczywista: 50 wejściowych ramek, 50 ukończonych
buforowanych ramek **HF QMF po gain**, 0 zaległych slotów;
57 obwiedni i 27 pasm noise-only. **Nadal tylko 2 ramki
eksperymentalnego HE-AAC PCM**, ramka 2 odrzucona przez
pierwotny ograniczony dekoder. Nie utożsamiać HF QMF z PCM.

Nowy **SBR-53f** to osobny diagnostyczny moduł synchronizacji
normatywnej 512-elementowej tablicy zespolonych wektorów
`sauti_aac_sbr_noise_rom_next` z wydaniem opóźnionej o jedną
ramkę HF QMF z SBR-53e. Przechowuje pełny `SBR-53a` plan
źródłowych parametrów na każdy z 32 slotów, aby przy
wydaniu ramki N–1 używać właśnie jej geometrii,
energii i czynnika szumu, a nie parametrów ramki N.
Wynikiem są WEKTORY ROM + **surowa, niewygładzona**
amplituda `sqrt(E)*sqrt(Q/(1+Q))`. Indeks ROM
przesuwa się również gdy amplituda jest zerowa.
Nie uwzględnia jeszcze opóźnionych bramek harmonicznych,
wygładzania, szumu końcowego, modulacji ani syntezy PCM.
To nie jest sygnał do bezpośredniego odtworzenia.

Testy 53f dodane, ale **nie były jeszcze uruchomione na Fedorze**:
ogon 2/6 slotów, poprawne współczynniki z oryginalnych
ramek, ciągłość indeksu ROM w całym 512-cyklu,
brak przesuwania indeksu podczas samego buforowania,
odrzucenie błędnego frame_index i NaN z zachowaniem
stanu i output, przesuwanie indeksu przy zerowej
amplitudzie oraz flush bez duplikowania ramki.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-noise PKG_CONFIG=/bin/false
make test-aac-sbr-temporal-apply test-aac-sbr-temporal-gain test-aac-sbr-temporal-power test-aac-sbr-temporal-qmf test-aac-sbr-payload test-aac-sbr-adts-qmf probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 | grep -E 'SBR-53|Rdzen:|odrzucona'
```

Po PASS 53f: kontrolowana integracja pełnych poziomów
szumu z wygładzaniem, tłumieniem w pasmach harmonicznych,
historią 4-slotowej fazy i normatywną syntezą w testowym
torze, nie w obecnym produkcyjnym torze PCM.


## SBR-53f potwierdzone na Fedorze; SBR-53g podgląd do testów (2026-11-05)

**Potwierdzone przez użytkownika:** `test-aac-sbr-temporal-noise`
PASS (ogon 2/6, indeks ROM, rollback, zerowa amplituda), regresje
`test-aac-sbr-temporal-apply` i `test-aac-sbr-adts-qmf` PASS.
Realny `~/Sauti-fixtures/he_test.aac` — 50/50 diagnostycznych
ramek ROM, 43 200 wektorów, wszystkie z dodatnią amplitudą,
końcowy indeks modulo 512 = 192. Wciąż stary tor PCM
odrzuca trzecią ramkę (indeks 2). Nie mylić diagnostyki
HF QMF ze zdekodowanym audio.

Nowe **SBR-53g**: stateless `sauti_aac_sbr_temporal_noise_mix_preview`
oblicza podgląd zespolonych wartości
`gain-adjusted HF QMF + ROM vector * raw noise amplitude`
wyłącznie dla ramek już wydanych przez SBR-53e i z
pasującym indeksem ramki SBR-53f. Rozszerzono diagnostyczny
format SBR-53f o jawną `active[32*64]` mapę — nawet dla
zerowej amplitudy. Podgląd weryfikuje każde aktywne
wyjście ROM z oryginalną 512-elementową tablicą,
wszystkie liczby, licznik oraz indeks modulo 512, a
na dowolny błąd pozostawia bufor wynikowy bez zmian.
To **surowa demonstracja złożenia torów**, nie normatywny
HE-AAC: NIE ma jeszcze wygładzania 5-slotowego,
bramkowania ataku/harmonicznych, dodatkowych sinusoid,
stabilizacji energii szumu czy syntezy 64-QMF/PCM.
SBR-53g nie zmienia istniejącego toru PCM.

Testy dodane, **nieuruchomione na Fedorze**:
przekazanie 2/6 slotów, rozróżnienie zmiany amplitudy
z poprzedniej i bieżącej ramki, ciągłość 512-elementowej
tablicy ROM, zerowa amplituda z aktywnym indeksem,
niepoprawny frame_index/ROM/NaN/overflow i rollback.
Ważne: docelowy mixer nie może dodawać surowych
amplitud bez docelowego smoothing i bramkowania.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-mix test-aac-sbr-temporal-noise test-aac-sbr-temporal-apply test-aac-sbr-adts-qmf probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 | grep -E 'SBR-53|Rdzen:|odrzucona'
```


## SBR-53g PASS Fedora; SBR-53h 5-tap noise coefficient smoothing pending (2026-11-05)

Na Fedorze potwierdzono `test-aac-sbr-temporal-mix`:
2-slot, 6-slot, pusty aktywny mask, niezmienność po błędzie PASS.
Regresje SBR-53f, e, SBR-31/46 PASS. Realny `he_test.aac`:
50/50 ramek HF/QMF, 43 200 aktywnych wektorów ROM, ROM index
końcowy 192, 57 gotowych obwiedni, 0 nieukończonych slotów.
SBR-53g to nadal NIE finalny PCM HE-AAC; stara synteza
odrzuca ramkę 2. Mieszanie SBR-53g używa surowej amplitudy,
bez normatywnego wygładzania i harmonicznego bramkowania.

Wprowadzono izolowany etap **SBR-53h** (do pierwszych
testów na Fedorze): `sauti_aac_sbr_temporal_noise_smooth`
stosuje **ten sam pięciowspółczynnikowy filtr poziomów
szumu co SBR-21**, utrzymując 4 poprzednie sloty dla
każdego absolutnego pasma QMF. Przy ataku obwiedni lub
`bs_smoothing_mode=1` pomija filtr, ale aktualizuje
historię bieżącym surowym poziomem. Metadane bypass
pochodzą z ORYGINALNEJ ramki SBR poprzez SBR-53a i
SBR-53f (w tym odziedziczone sloty FIXVAR). Zmiany
sygnatur dozwolone tylko po sprawdzeniu regresji.
Pasma nieaktywne resetują historię konkretnego pasma
zamiast przejmować poprzednią geometrię bez kontroli.
Dokładny wariant normatywnej obsługi zmiany geometrii
i harmonicznych pozostaje do przyszłych testów.

Nowa struktura `SautiAacSbrTemporalSmoothFrame` zawiera
wyłącznie wygładzone amplitudy, bez dodawania szumu do
sygnału HF, sinusoid ani końcowej syntezy PCM. Dotychczasowy
SBR-53g nadal pracuje na surowych amplitudach i pozostaje
niezmieniony. Diagnostyka wypisuje liczbę obliczonych ramek,
pasm-slotów i slotów bez filtru. Testy 2 i 6 slotów,
attack/bypass, startup, NaN/replay oraz rollback wymagają
uruchomienia na maszynie użytkownika.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-smooth test-aac-sbr-temporal-noise test-aac-sbr-temporal-mix test-aac-sbr-temporal-apply test-aac-sbr-adts-qmf probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 | grep -E 'SBR-53|Rdzen:|odrzucona'
```

Po SBR-53h PASS: osobno zweryfikować gating i sinusoids
w historii oryginalnego źródła oraz dopiero potem użyć
wygładzonych poziomów do normatywnej mieszanki HF QMF.


## SBR-53h PASS na Fedorze; audyt rzeczywistego bypass (2026-11-05)

Użytkownik potwierdził kompilację `test-aac-sbr-temporal-smooth`
oraz **PASS** dla ogonów 2/6 slotów, 5-tap smoothing,
bypass ataku, startup pierwszego aktywnego pasma i rollback.
Regresje SBR-53f/g/e oraz SBR-31/46 również PASS.
Na rzeczywistym `~/Sauti-fixtures/he_test.aac`:
50 ramkowych diagnoz, 43 200 aktywnych pasm-slotów,
ale 1 600/1 600 slotów zostało pominiętych przez filtr
(`bypass transient=1600` w dotychczasowej diagnostyce).
**To nie jest dowód, że FIR wygładził tę próbkę.**

Dodano **SBR-53h-audit** do `sauti_he_aac_probe`, bez zmian
w dekoderze PCM: zlicza nagłówki `bs_smoothing_mode=0/1`,
obwiednie `attack`, sloty bypass z nagłówka,
sloty bypass WYŁĄCZNIE z ataku, ich przecięcie i sloty
rzeczywiście filtrowane FIR. Wszystko liczy według
oryginalnego właściciela SBR każdego slotu, również
podczas FIXVAR handoff. Sprawdza zgodność sum bypass
z raportem SBR-53h. Dodano regresję bezpośredniego
mapowania `smooth_bypass` dla 2 slotów wcześniejszej
ramki i kolejnych obwiedni; błędna flaga ataku ma
spowodować rollback. Nowa diagnostyka wymaga uruchomienia
na Fedorze, więc nie twierdzić z góry, co powoduje
1 600 bypass — możliwe, że nagłówek kodeka wskazuje
`smoothing_mode=1` w całym strumieniu.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-payload test-aac-sbr-temporal-smooth test-aac-sbr-temporal-noise probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 | grep -E 'SBR-53h|SBR-53h-audit|Rdzen:'
```

Po analizie rozkładu bypass: utworzyć testowy
realny strumień HE-AAC z nagłówkiem `smoothing_mode=0`
(bez zgadywania i przepisywania bitów na żywym strumieniu),
dopiero potem przechodzić do normatywnego
gatingu harmonicznych i syntezy 64-QMF.


## SBR-54a — most z opóźnionych pasm QMF do PCM PREVIEW (2026-11-05)

Punkt konstrukcyjny po SBR-53a–h: w branchu feature/aac-family-asm
zapisano eksperymentalne API
sauti_aac_sbr_temporal_pcm_preview_push/flush. Wcześniej
przygotowywaliśmy HF QMF i szum, ale nie mieliśmy pełnych
próbek czasu po ich połączeniu z właściwymi dolnymi pasmami.

SBR-54a:
- przechowuje 32*32 dolnych pasm QMF z bieżącej ramki AAC-LC;
- gdy SBR-53e/g wyda poprzednią ramkę HF, łączy pasma z
  właściwymi, wcześniej przechowanymi dolnymi pasmami;
- wykorzystuje maskę aktywnych binów z SBR-53f w każdym
  slocie, także przy FIXVAR i dwóch źródłach SBR;
- wykonuje już istniejącą 64-pasmową syntezę QMF SBR-25/26,
  z ciągłą historią, generując 2048 próbek float na ramkę;
- flush na końcu nie uzupełnia brakujących danych ciszą,
  a nieprawidłowy indeks, mapa HF, liczby i etap syntezy
  pozostawiają stan i wynik niezmienione.

SBR-54a jest WYŁĄCZNIE eksperymentalnym podglądem przebiegu!
NIE stanowi poprawnego końcowego dekodera HE-AAC.
Nie mamy jeszcze normatywnego harmonic/sine processing,
zintegrowanych wyników wygładzania SBR-53h, LPC/chirp,
ani zgodności delay/priming i porównania PCM z FFmpeg.
Oryginalny tor produkcyjny SBR-31 pozostaje bez zmian
i nadal zatrzymuje się na ramce indeks 2.

Nowy test test-aac-sbr-temporal-pcm-preview porównuje
bitowo trzy ramki wyjściowe z ręcznie złożonym referencyjnym
łańcuchem 64-QMF, dla 2 i 6 slotów odziedziczonych.
Dodatkowo testuje opóźnienie jednej ramki, numerację,
NaN, nieciągłą mapę, końcowy flush i atomowy rollback.
NIE JEST JESZCZE POTWIERDZONY W KOMPILACJI NA FEDORZE.

Test w katalogu ~/Sauti:
  git pull --ff-only origin feature/aac-family-asm
  make test-aac-sbr-temporal-pcm-preview test-aac-sbr-temporal-mix test-aac-sbr-temporal-noise test-aac-sbr-temporal-apply test-aac-sbr-adts-qmf probe-he-aac PKG_CONFIG=/bin/false
  ./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 | grep -E 'SBR-54a|SBR-53e|SBR-53g|Rdzen:|odrzucona'

CEL nr 1 po testach: RAMKA 2, 2048 niezerowych próbek
w torze PCM PREVIEW. CEL nr 2: 50 * 2048 = 102400
próbek bez utraty slotów i z ciągłością QMF.
OBA CELE SĄ TYLKO PODGLĄDEM, NIE WALIDACJĄ DŹWIĘKU.
Dalej: 54b harmonics/sine/gating oraz smoothed noise;
54c LPC/chirp/delay i transakcja całego dekodera;
54d porównanie z niezależnym dekoderem i odsłuch;
potem stereo, PS/HE-AACv2 i integracja w playerze.


## SBR-54a potwierdzone, SBR-54b walidacja amplitudy i opóźnienia (2026-11-05)

**Raport Fedora użytkownika**:
- test-aac-sbr-temporal-pcm-preview: PASS (2-slot, 6-slot,
  3*2048 próbek, ciągłość syntezy 64-QMF, rollback);
- testy regresyjne SBR-53e/g i SBR-31/46: PASS;
- rzeczywisty plik ~/Sauti-fixtures/he_test.aac:
  **50/50 RAMKI PCM PREVIEW**, 102400 próbek f32,
  102399 próbek niezerowych, brak brakujących slotów,
  pierwsza niezerowa ramka 0;
- kluczowa ramka indeks 2 ze starego toru nadal jest
  odrzucana, ale nowy tor dał 2048/2048 niezerowych
  próbek przy RMS 3.08089898 i peak 33.2961998.

**Ważna interpretacja:** uzyskaliśmy pierwszy pełny
przebieg czasowy z modułów SBR-53 i syntezy SBR-25/26,
ale wartości RMS/peak są niepokojąco duże wobec
znormalizowanego PCM f32. Nie wolno normalizować,
przycinać ani uznać tych próbek za poprawny
HE-AAC przed porównaniem z niezależnym dekoderem.

**SBR-54b** (dodany kod, oczekuje na Fedora PASS):
- próbnik obsługuje opcjonalny 3. argument ścieżki
  zapisu raw f32le (mono 48kHz). Buforuje PCM PREVIEW
  w pliku tymczasowym i zapisuje wskazany plik dopiero
  po kompletnym procesie 54a, końcowym flush i
  zgodności liczników próbek. Nie zastępuje ścieżki AAC.
- tools/sauti_he_aac_compare_pcm.py porównuje
  eksport z referencyjnym plikiem float32 z FFmpeg:
  korelacja próbek, najlepsze przesunięcie w granicach
  +/-8192, względny błąd RMS po dopasowaniu skali/DC,
  statystyki peak/RMS. NIE uznawać wysokiej korelacji
  za pełną zgodność normatywną, a niskiej interpretować
  jako błąd skali bez kolejnych badań.
- make test-aac-he-preview-compare uruchamia
  syntetyczny self-test porównywarki; nadal należy
  potwierdzić go lokalnie na Fedorze.

Komendy:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-he-preview-compare probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le
ffmpeg -hide_banner -loglevel error -y \
  -i ~/Sauti-fixtures/he_test.aac -map 0:a:0 -ac 1 -ar 48000 \
  -f f32le -acodec pcm_f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
python3 tools/sauti_he_aac_compare_pcm.py \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
```

UWAGA: dawny status programu probe może pozostać 1,
ponieważ jego pierwotny tor decoder SBR-31 celowo
zwraca ograniczenie na ramce 2. Weryfikować osobny
wiersz SBR-54b potwierdzający rzeczywisty zapis.

**Dokładny stan:** SBR-54a — sukces konstrukcji
2048*50 PRÓBEK PCM PREVIEW. SBR-54b — oczekuje na
pierwszy wynik skali/opóźnienia/korelacji; normatywne
HE-AAC PCM jeszcze nie jest skończone.
Po pomiarze wybierzemy zmianę o największym znaczeniu:
prawdopodobnie wyjaśnienie nadmiarowego RMS/peak,
a dopiero potem harmoniczne/chirp/smoothing i
integrację transakcyjną oraz stereo/HE-AACv2.


## SBR-54b: naprawa walidatora korelacji — błąd testu na Fedorze (2026-11-05)

Użytkownik uruchomił `make test-aac-he-preview-compare probe-he-aac`
na HEAD b76fc31. Self-test walidatora Pythona zatrzymał się na
`AssertionError: (-3685, 384)`: algorytm miał znaleźć przesunięcie
384 próbek. Ponieważ pierwszy target `make` nie przeszedł,
**próbnik C nie został odbudowany**, więc wywołanie 3.
argumentu eksportu pokazało starą składnię CLI.
Ten wynik nie jest regresją dekodera AAC/SBR-54a.

Poprawiono `tools/sauti_he_aac_compare_pcm.py`:
- siatka wstępnego wyszukiwania jest teraz zakotwiczona
  w zerowym opóźnieniu (wcześniej zaczynała się w
  `-maxlag`; dla maxlag=4128 nie próbkowała lag=384);
- krok wyszukiwania zmieniono z 64 na 8 próbek, aby
  nie wybierać odległego maksimum korelacji z
  okresowego sygnału testowego;
- test obejmuje teraz przesunięcia +384, +385, +471,
  -423 próbek oraz odwróconą polaryzację sygnału.
- poprawkę przeanalizowano na analogicznym kodzie
  lokalnie, ale nadal wymagana jest kompilacja oraz
  wykonanie aktualnego pliku na Fedorze.

Po `git pull` należy osobno potwierdzić:
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-he-preview-compare PKG_CONFIG=/bin/false
make probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 ~/Sauti-fixtures/he_test_sauti_preview.f32le
```
Następnie użyć FFmpeg wyłącznie do utworzenia
referencji i uruchomić porównanie; dopóki nie ma
wyników korelacji/skali, **SBR-54b pozostaje WIP**.


## SBR-54b porównane z FFmpeg: duża niezgodność; SBR-54c izolacja przyczyny (2026-11-05)

**Wynik na Fedorze** po prawidłowym eksporcie f32le:
- PREVIEW: 102400 próbek, RMS 0.900186297, peak 36.0775452;
- FFmpeg: 102400 próbek, RMS 0.120547656, peak 0.22226046;
- znaleziony przez korelator lag -4332 próbek, ale
  korelacja zaledwie 0.069288; po optymalnym
  skalowaniu signed 0.00794001 nadal pozostaje
  błąd RMS 0.12098383 (~99.818% referencji).
To NIE jest sam problem poziomu. Zmienianie globalnego
gain, clipping albo potwierdzanie HE-AAC jest zabronione.
Tak niska korelacja nie uprawnia do uznania -4332 za
rzeczywisty delay/priming.

**Nowy SBR-54c (diagnostyka, wymaga pierwszego testu Fedora):**
Rozdzielenie diagnostycznej pełnej syntezy 64-QMF na
trzy niezależne ciągłe banki, z tym samym źródłowym
indeksem ramki i geometrią HF:
1. low: tylko oryginalne dolne pasma AAC-LC QMF
   dla właściwej, opóźnionej ramki;
2. hf: tylko wysokie pasma po gain z SBR-53e,
   BEZ dodatkowego szumu;
3. noise: tylko różnica zespolonych współczynników
   po miksie SBR-53g i przed nim, czyli surowy
   szum ROM, z tym samym indeksem fazowym.

Przed publikacją danych każda próbka jest sprawdzana:
low_pcm + hf_pcm + noise_pcm musi odtworzyć pełny
PCM PREVIEW w granicy tolerancji float32. Każdy stem
zachowuje swój stan syntezy między ramkami, a
eksport wymaga 50/50 ramek i poprawnego flush.
Eksport jest WYŁĄCZNIE opcjonalny, podany jako
czwarty ARGUMENT po nazwie pliku AAC, liczbie
ramek oraz ścieżce głównego eksportu.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le \
  ~/Sauti-fixtures/he_test_parts
for part in low hf noise; do
  echo "=== $part ==="
  python3 tools/sauti_he_aac_compare_pcm.py \
    "$HOME/Sauti-fixtures/he_test_parts.$part.f32le" \
    "$HOME/Sauti-fixtures/he_test_ffmpeg_reference.f32le" |
    grep -E 'PREVIEW:|REFERENCE:|Best alignment:|Correlation:|After best scale'
done
```

Kryterium: 50/50 stem frames, każdy plik
409600 bajtów, brak dekompozycji niespójnej
ze znanym PREVIEW. Dopiero RMS/correlation poszczególnych
stemów pozwoli zdecydować, czy priorytetem jest
poprawienie analizy/syntezy 32/64-QMF, wzmocnienia
HF, czy surowego szumu. NIE dodawać następnego
modułu harmonicznych zanim nie rozdzielimy źródła błędu.


## SBR-54c Fedora PASS; SBR-54d wspólna regresja składników (2026-11-05)

**Wynik użytkownika SBR-54c** na prawdziwym
`~/Sauti-fixtures/he_test.aac`: kompilacja i
eksport trzech składników 50/50 ramek PASS;
rekombinacja LOW+HF+NOISE zgodna z PREVIEW.

Po porównaniu z niezależnym FFmpeg (102400 próbek):
- LOW: RMS 0.0826822439, peak 0.159414411;
  najlepszy lag -6 próbek, korelacja 0.991765,
  dopasowany gain 1.4459565, reszta RMS 0.015439245
  (12.807% RMS referencji).
- HF gain: RMS 0.828072518, peak 33.193573;
  korelacja z CAŁYM FFmpeg 0.004770 przy lag -4350;
  nie wolno interpretować -4350 jako opóźnienia.
- Noise raw: RMS 0.317338204, peak 23.1169834;
  korelacja z CAŁYM FFmpeg -0.000195, lag -458;
  równie niewiarygodny jako rzeczywiste opóźnienie.

Interpretacja: **LOW wygląda bardzo obiecująco**,
ale sam korelator całego sygnału nie ocenia
pasma wysokiego poprawnie, gdy referencja ma silny
rdzeń niskoczęstotliwościowy. Ponadto duży HF/NOISE
RMS pokazuje, że obecny bezpośredni mixer jest
błędny w poziomie, normatywności lub fazie, ale
NIE rozstrzyga jeszcze pojedynczej przyczyny.
W kodzie widać jawne `sqrt(decoded_energy)`
dla ROM noise oraz osobne `sqrt(target/current)`
dla HF, więc przed zmianą normatywnego skalowania
trzeba najpierw potwierdzić, czy te składowe
zawierają jakikolwiek poprawny przebieg poza LOW.

**Nowy SBR-54d**, tylko narzędzie Python:
`tools/sauti_he_aac_stem_ablation.py`
- ustala JEDEN lag względem FFmpeg na podstawie LOW
  (oczekiwane -6 próbek dla tej próbki);
- dopasowuje w tej samej osi czasu trzy modele
  LOW, LOW+HF, LOW+HF+NOISE (łącznie z DC);
- wypisuje zmniejszenie relatywnego RMS błędu przez
  każdy kolejny składnik oraz 3 współczynniki
  dopasowania, które są diagnostyczne, a NIE
  poprawkami gain dekodera;
- opcjonalnie mierzy różnicę sumy STEM-ów od
  zapisanego pełnego PCM PREVIEW;
- `make test-aac-he-stem-ablation` uruchamia
  niezależny self-test z sygnałami o znanych
  wagach, przesunięciu +37 i reszcie ~0.
Do pierwszego uruchomienia na Fedorze:
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-he-stem-ablation PKG_CONFIG=/bin/false
python3 tools/sauti_he_aac_stem_ablation.py \
  ~/Sauti-fixtures/he_test_parts.low.f32le \
  ~/Sauti-fixtures/he_test_parts.hf.f32le \
  ~/Sauti-fixtures/he_test_parts.noise.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le
```

**Najbliższy punkt decyzyjny:** czy błąd względny
po LOW (12.807%) zauważalnie maleje po HF i NOISE?
Jeśli NIE, trzeba naprawiać źródłową rekonstrukcję
HF / domenę energii / fazę / kontrolę sinusoid i
noise, a nie dobierać globalny gain. Jeśli TAK,
najpierw sprawdzić normatywne wzmocnienie i opóźnienie.
Stary tor odrzucający ramkę 2 pozostaje niezmieniony;
nowe 54d nie tworzy PCM i nie integruje się z playerem.


## SBR-54d wyniki Fedora: HF/NOISE nie wyjaśniają referencji (2026-11-05)

Na `he_test.aac` użytkownik potwierdził przebiegi 50 ramek,
102400 próbek i wspólne wyrównanie -6 próbek na podstawie LOW
(korelacja LOW = 0.991765). Rekombinacja trzech osobno
syntezowanych składników PCM zgadza się z 54a:
**diff RMS = 4.36321317e-08, max = 2.55869236e-06**.
Zatem test ablation mierzy składniki tego samego toru,
nie przypadkowo rozjechane ścieżki eksportu.

Least-squares residual przy jednym, wspólnym lag:
- LOW: waga 1.44595651, RMS error 0.015439245,
  relative 12.807%;
- LOW + HF: wagi 1.44596558 i 0.000169065579,
  RMS error 0.0154386103, relative 12.807%;
  przyrost jakości zaledwie **0.0005 p.p.**;
- LOW + HF + NOISE: wagi 1.44596559,
  0.000169252947, -0.0000144148608,
  RMS error 0.0154386096, relative 12.807%;
  przyrost od NOISE **~0.0000 p.p.**.
Wszystkie wagi są TYLKO diagnostyczne: NIE wolno
wpisywać ich jako stałych gain do dekodera.

**Ocena: sukces SBR-54d jako ablation diagnostycznej,
ale nie jako HE-AAC conformance.** HF i NOISE mają
nieprawidłowy poziom, a nawet swobodny dobór amplitudy
nie odtwarza brakującego pasma referencji. Pierwszym
obszarem weryfikacji jest ścieżka SBR-09 dequant energii
→ SBR-53a target HF/noise → SBR-53c pomiar mocy
→ SBR-53d limiter i gain → SBR-53f RAW amplitude,
oraz osobno sygnatura fazy QMF SBR-27/26.
Nie wprowadzać globalnego scalingu, clippingu ani
kolejnego modułu harmonicznych na podstawie samych
liczników PASS.

**Błąd syntetycznego testu SBR-54d:**
`make test-aac-he-stem-ablation` na Fedorze dał
`AssertionError: 2.3295012311199514e-10`
dla progu `1e-10`. Syntetyczny fit ma umyślne
ridge regularization `1e-9`, które pozostawia
residual rzędu `2.33e-10`, mimo prawidłowych wag.
Poprawiono TYLKO test (próg na `1e-8`, nadal
ściśle bliski zeru względem sygnału); nie zmieniono
modelu, dekodera ani rzeczywistych wyników regresji.
Niezbędne ponowne wykonanie syntetycznego testu
na Fedorze.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-he-stem-ablation PKG_CONFIG=/bin/false
```
Następny etap: konkretny audyt jednostek energii,
faz, limiter boost i harmonics/gating w HF. Punkt
kontrolny: mierzyć istotny spadek error RMS
po dołączeniu HF, nie tylko generację 50 ramek.


## SBR-54d Fedora PASS; SBR-54e audyt rozdziału energii HF/NOISE (2026-11-05)

**Faktycznie potwierdzone:**
- `make test-aac-he-stem-ablation` po zmianie progu
  `1e-10 -> 1e-8` przeszedł PASS na Fedorze.
  To kończy SBR-54d: osobna diagnostyka korelacji,
  wspólny lag -6 próbek, 3 składniki i raport residual.
- SBR-54d realny pomiar: tylko LOW daje 0.991765
  korelacji z FFmpeg oraz błąd 12.807% po dopasowaniu.
  Dodanie HF zmniejsza błąd o 0.0005 punktu procentowego;
  NOISE nie obniża go w mierzalny sposób.
- Nie wolno interpretować współczynników regresji
  jako dozwolonych poprawek DSP.

**SBR-54e — nowy wyizolowany, tylko-odczytowy
moduł** `sauti_aac_sbr_temporal_energy_audit`:
- dla oryginalnej, opóźnionej ramki i każdego pasma-slotu
  analizuje deklarowaną energię E z SBR-53a,
  aktualne |HF QMF|² po SBR-53e i nominalny przydział
  NOISE = raw_amplitude² z SBR-53f;
- zlicza biny o (HF_power + nominal_noise_power)/E > 1.0001
  i maksymalny stosunek, energię sumaryczną oraz
  sygnalizowane pasma harmonicznych;
- sprawdza zgodność geometrii HF, ramki oraz matematycznej
  zależności nominalnej energii NOISE=E*Q/(1+Q);
- NIE zastępuje normatywnego obliczania szumu,
  nie wymusza ograniczeń amplitudy, nie zmienia
  dekodera ani wyjściowych próbek audio;
- nie utożsamia bezpośrednio energii zespolonej QMF
  z normatywnym PCM, bo do tego potrzebna jest pełna
  analiza normalizacji, fazy, harmonics/gating i
  cross-term między HF a noise.

**Podejrzenie sprawdzane, a nie jeszcze udowodniona
przyczyna**: w obecnym kodzie SBR-53d HF gain celuje
w całą energię E, a SBR-53f dodaje osobno noise
`sqrt(E)*sqrt(Q/(1+Q))`. Syntetyczny przykład E=4,
Q=0.25, HF_amp=2 daje 4 + 0.8 = 4.8
(1.2x nominalnego budżetu). Na realnych danych
trzeba sprawdzić, czy i jak często występuje
podobny nadmiar oraz ile wynosi jego rzeczywista skala.

Nowy test `test-aac-sbr-temporal-energy-audit`
sprawdza nadbudżet 1.2x, noise-only poniżej E,
błędny indeks ramki, geometrię, niezgodny target
i końcowy NaN przy atomowym rollback.
Nowa diagnostyka w `sauti_he_aac_probe`
wypisuje SBR-54e ramka 2 oraz SBR-54e audit bilansu
sumaryczny dla 50 ramek, wraz z finalnym flush.

**Wymaga pierwszej kompilacji i walidacji na Fedorze.**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-energy-audit test-aac-sbr-temporal-mix test-aac-sbr-temporal-pcm-preview probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 |
  grep -E 'SBR-54e|SBR-54a|SBR-53e|Rdzen:|odrzucony'
```

**Kamień milowy SBR-54e:** konkretny pomiar nadmiaru
mocy HF+NOISE względem oryginalnych E dla 50 ramek.
Jeśli hipoteza nadalokacji okaże się prawdziwa,
dopiero osobno testować normatywny podział E
w zależności od Q/limiter i harmonicznych. Jeśli
nie — skoncentrować się na QMF phase/aliasing/
LPC-chirp i porównaniu fazowym. Nadal NIE mamy
HE-AAC conformance-quality PCM; nie zmieniać
starego dekodera obsługującego 2 pierwsze ramki.


## SBR-54e Fedora PASS i wyniki — prawdziwy problem: MEAN vs slot (2026-11-05)

Użytkownik uruchomił na Fedorze testy `test-aac-sbr-temporal-energy-audit`,
`test-aac-sbr-temporal-mix`, `test-aac-sbr-temporal-pcm-preview`
i przebadał realne 50 ramek `he_test.aac`. Wszystkie testy PASS.
SBR-54e PASS na 50/50 ramek:
- ramka indeks 2: sum(target E)=2620672,
  sum(|HF|²)=2086390, sum(noise_nom)=5767.42014;
  437/864 lokalnych pasm-slotów powyżej E;
- wszystkie ramki: target=8567296, HF=6826699.94,
  nominalny noise=1092346.11; sum(HF+noise)/target=0.924334359;
  23725/43200 lokalnych pasm-slotów powyżej E;
  harmonic_bins=3020; max lokalny stosunek=75.4060352.
- tor 54a pozostaje 50/50 PCM PREVIEW 102400 próbek.

**WAŻNA KOREKTA INTERPRETACJI:** raport 54e porównuje
**energię jednego slotu** `|X[t,k]|²` do `E[envelope,k]`,
a E jest energią **średnią w całej oryginalnej obwiedni**.
Lokalne przekroczenie (nawet 75x) może być całkowicie
prawidłowym krótkim transientem; samo w sobie NIE
dowodzi błędu limitera, podwójnego przydziału energii
ani nieprawidłowego zdekodowania HE-AAC. Dodatkowo
całkowity budżet 92.43% nie potwierdza globalnego
nadmiaru energii. Mimo to inne pomiary (PCM correlation
54d) jednoznacznie pokazują niską jakość odbudowy HF.

**SBR-54f** przygotowano właśnie do właściwej skali
statystycznej:
- `sauti_aac_sbr_temporal_envelope_audit_push` grupuje
  wyniki `54e` po numerze ORYGINALNEJ ramki,
  oryginalnej obwiedni i pasmie k;
- `source=1` -> emitted-frame, `source=0` ->
  emitted-frame minus jeden, więc FIXVAR 2/6 slotów
  w następnej ramce nadal należy do pełnej obwiedni
  poprzedniej ramki;
- sumuje E, HF² i nominalny noise² przez wszystkie
  sloty każdej oryginalnej obwiedni i dopiero
  wtedy porównuje ich budżety (progi 1.0001/2/10);
- wypisuje maksymalny *średni* stosunek oraz numer
  oryginalnej ramki, obwiedni, pasma i liczbę slotów;
- weryfikuje sumę 43200 pasm-slotów z audytem 54e;
  nie wpływa na PCM, amplitudę ani historię kodeka;
- w teście syntetycznym rozlicza 32 sloty + 2 sloty
  FIXVAR w tej samej obwiedni; sprawdza rollback
  dla niespójnego E, NaN i indeksu ramki.

**Kod SBR-54f dodany do GitHub, jeszcze NIE zaliczony
kompilacją ani testem na Fedorze.**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-envelope-audit test-aac-sbr-temporal-energy-audit probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 |
  grep -E 'SBR-54f|SBR-54e|SBR-54a|Rdzen:|odrzucony'
```

**Cel:** odpowiedzieć, czy nadmiar mocy HF/NOISE
utrzymuje się na prawidłowych, pełnych obwiedniach.
Nawet taki wynik będzie wciąż tylko diagnostyką:
normatywny HF wymaga sinusoids, noise gating,
chirp/LPC i zgodnej syntezy fazowej oraz porównania
z niezależnym dekoderem. Bez potwierdzenia nie
zmieniać globalnego gain ani nie dopisywać sztucznego
skalowania obwiedni.


## SBR-54f Fedora PASS; SBR-54g LPC(2) + rzeczywisty chirp (2026-11-05)

**Potwierdzona Fedora SBR-54f:**
- `test-aac-sbr-temporal-envelope-audit`: PASS dla
  32+2 slotów FIXVAR, pełnej średniej energii,
  wycofywania błędnych danych.
- `test-aac-sbr-temporal-energy-audit`: PASS.
- realny plik `~/Sauti-fixtures/he_test.aac`:
  50 ramek PCM PREVIEW i 57 pełnych obwiedni,
  1539 pasm-obwiedni, 43200 slotów-pasm,
  zgodność sum 54f z 54e = TAK.
  Średni bilans HF+noise/target = 0.924334359.
  Nadbudżet na 112/1539 obwiedni-pasm (~7.28%),
  ponad 2x dla 62/1539 (~4.03%),
  ponad 10x dla 0,
  maksimum ŚREDNIE 2.8452198 dla źródłowej
  ramki 2, obwiedni 0, pasma 18 (10 slotów).
- Ta informacja koryguje błędną intuicję, że
  lokalne 75.4x z 54e oznacza globalne przesterowanie.
  Nie wprowadzać skali globalnej ani clippingu.
- Wcześniejsze 54d wykazało, że dodanie dotychczasowych
  HF i NOISE niemal nie zmienia błędu (12.807%)
  referencji. Teraz priorytetem jest JAKOŚĆ HF
  i zgodność fazy, nie liczniki energii.

**Przegląd kodu przyniósł konkretny trop:**
`sauti_aac_sbr_temporal_qmf_prepare` do SBR-53b
korzysta tylko z `sauti_aac_sbr_hf_transpose`,
a więc kopiuje dolne pasmo QMF na odpowiednie
wysokie pasmo, bez LPC/chirp. Istniejący SBR-13
`sauti_aac_sbr_hf_predict` realizuje matematyczny
predyktor i `sauti_aac_sbr_chirp_update`, ale
potrzebował rzeczywistych współczynników alpha0/1.

**SBR-54g (nowy kod, wymaga Fedora PASS):**
- `sauti_aac_sbr_lpc_estimate_2` — niezależna,
  atomowa, autorska estymacja zespolonego
  AR(2) z 2 slotów historycznych oraz 32
  bieżących. Solves Hermitian covariance with
  diagonal loading, rejects non-finite inputs
  and unstable coefficients. No external codec.
  Jest to estymator eksperymentalny, jeszcze
  NIE potwierdzony jako dokładny wariant ISO.
- Test syntetyczny generuje sygnał z zadanymi
  zespolonymi alpha0=-0.55+0.14i,
  alpha1=0.13-0.06i i porównuje odzyskane
  współczynniki; waliduje rollback NaN,
  krótkie bufory oraz ciszę.
- Realny próbnik przyjmuje zaktualizowane
  `invf_mode` z ramki SBR, oblicza czasowe
  współczynniki i wywołuje istniejący
  SBR-13 dla każdego z 32 slotów,
  z poprawnymi 2 historycznymi slotami
  i oryginalnym właścicielem FIXVAR.
- Wypisuje `SBR-54g: LPC(2)+chirp alternatywa`
  z liczbą niezerowych współczynników,
  chirp i RMS różnicy między uzyskanym QMF
  a dotychczasowym transpose QMF.
- **Nie podłącza się jeszcze do 53c/d/e/g
  ani do PCM PREVIEW i nie zmienia starego
  toru AAC.** Realne A/B wyjścia PCM jest
  kolejnym, osobnym kamieniem milowym,
  dopiero po walidacji nowego estymatora.

Komendy:
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-lpc-estimate test-aac-sbr-hf-predict probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 |
  grep -E 'SBR-54g|SBR-54f|SBR-54a|Rdzen:|odrzucona|odrzucony'
```
Oczekujemy syntetycznego PASS oraz 50 ramek
LPC(2) analizy bez regresji 54f/54a.
**Nie zakładać z góry**, czy alternatywa LPC
poprawi dźwięk; zmiana QMF nie świadczy o
korelacji z niezależnym dekoderem. O tym
rozstrzygnie następne A/B PCM vs FFmpeg.


## SBR-54g Fedora PASS: LPC działa, ale chirp wyłącza predykcję na he_test.aac (2026-11-05)

**Potwierdzone w logach Fedory:**
- `test-aac-sbr-lpc-estimate`: PASS (znane współczynniki
  zespolone AR(2), cisza, błędne dane, rollback).
- `test-aac-sbr-hf-predict`: PASS
  (11438 predykcji zespolonych, 151 odrzuconych map).
- Próba na 50 klatkach rzeczywistego
  `~/Sauti-fixtures/he_test.aac`: 1549 niezerowych
  zestawów współczynników alpha, ale
  `dodatni_chirp_slot-band=0`,
  `roznice_z_transpose=0/43200`,
  `RMS_roznicy_QMF=0`.
- SBR-54f, SBR-54a nadal PASS, PCM PREVIEW 50/50;
  produkcyjny SBR-31 nadal zatrzymuje się na ramce 2.

**Interpretacja:**
Wzór SBR-13 jest `HF = X0 + bw*alpha0*X1 +
bw²*alpha1*X2`. Przy `bw=0` alternatywa LPC
MUSI być identyczna z prostą transpozycją
bez względu na niezerowe alpha. Nie osiągnięto
poprawy jakości PCM. Nie wolno sztucznie ustawiać
`bw>0` wbrew zakodowanym `invf_mode`.

**Audyt źródła chirp:**
`sauti_aac_sbr_control_parse` bezpośrednio
pobiera 2-bitowe `sbr_invf()` i zapisuje w
`payload.control.invf_mode`. `54g` przekazuje
to do `sauti_aac_sbr_chirp_update`.
Dopisano rozkład źródłowych czterech trybów
`OFF/LOW/MID/HIGH` i liczbę ramek z niezerowym
INVF w 50 ramkach. Dzięki temu rozstrzygniemy,
czy `bw=0` jest wprost sygnalizowane przez
koder, czy warto śledzić wadliwy stan/chirp.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 |
  grep -E 'SBR-54g|SBR-54a|SBR-54f|Rdzen:'
```

**Główna blokada HE-AAC:**
Nadal tylko LOW ma wysoką korelację z FFmpeg
(0.991765), a dotychczasowe HF i noise nie
zmniejszają błędu 12.807%. SBR-53a wykrył
aktywne harmoniczne, ale SBR-53g + 54a nie
mają jeszcze sinusoid, tonal gating ani
właściwego rozdziału energii HF/noise/harmonics.
Jeśli zakodowane INVF rzeczywiście jest OFF,
to dalsze liczenie LPC na tym samym strumieniu
nie poprawi PCM. Następny konstrukcyjny krok:
osobna opóźniona ścieżka sinusoid harmonicznych
i gate zgodny z oryginalnym owner, bez naruszania
54a/31, a potem bezpośredni odsłuch/PCM A/B
wobec FFmpeg. Nie przyznawać 54g poprawy audio.


## SBR-54g — invf_mode OFF jest prawidłowe; SBR-54h harmonic A/B PCM (2026-11-05)

**Fedora SBR-54g — po bezpośrednim sprawdzeniu parsera bitstreamu:**
```
SBR-54g invf_mode bitstream:
  OFF=150, LOW=0, MID=0, HIGH=0
  pasma=150, ramki_z_aktywnym_INVF=0/50
SBR-54g LPC(2)+chirp:
  ramki=50, niezerowe_zestawy_alpha=1549,
  dodatni_chirp_slot-band=0,
  roznice_z_transpose=0/43200,
  RMS_roznicy_QMF=0
```
Jest to **rozstrzygnięte**: kod `sauti_aac_sbr_control_parse`
czyta z rzeczywistego FIL 2-bit `sbr_invf()`,
a wszystkie wartości dla tego pliku są OFF.
Normatywna reguła `bw=0` wyłącza dodatkową
predykcję nawet przy poprawnym estymatorze LPC.
W żadnym wypadku nie włączać sztucznego chirp.
SBR-54g zakończony konstrukcyjnie, bez poprawy PCM.

**SBR-54h — nowy KANDYDAT PCM A/B z sinusoidami i bramką szumu**:
- moduł `sauti_aac_sbr_temporal_harmonic_preview` czyta
  wcześniej zdekodowane `SBR-53a` harmonic flags,
  E i Q, bieżące `SBR-53g` HF+ROM i niezależną
  sekwencję ROM SBR-53f. Przy aktywnej sinusoidzie
  dodaje znormalizowany *eksperymentalny* składnik
  amplitudy `sqrt(E/(1+Q))` zgodnie z SBR-19,
  z czterofazowym licznikiem QMF i parzystością k.
- w oznaczonym paśmie k odejmuje RAW ROM noise
  przed dodaniem sinusoidy; to eksperymentalna
  bramka tonalna, NIE pełna norma MPEG-4 SBR.
- odróżnia nową harmoniczną (wymaga zaliczonego
  oryginalnego transient_env) od kontynuowanej
  harmonicznej z poprzedniej ramki; uwzględnia
  `source=0/1` podczas FIXVAR, a odpowiednia
  maska onset jest buforowana na jedną ramkę
  razem z opóźnionym QMF.
- wykorzystuje DRUGĄ, niezależną ciągłą historię
  syntezy 64-QMF; oryginalny PCM PREVIEW 54a,
  SBR-31, gain, parser i wskaźnik ROM niezmienione.
- mierzy `marked_slot_bands`, `active_tones`,
  `gated_noise`, RMS i peak alternatywnego PCM
  i RMS różnicy względem starego 54a.
- gdy próbnik otrzyma ścieżkę zapisu PCM 54a
  jako 3. argument CLI, automatycznie publikuje
  nowy plik `<nazwa_f32le>.sine.f32le` TYLKO
  po prawidłowych 50/50 ramkach, końcowym flush
  i pełnym zapisie w buforze tymczasowym. Pierwszy
  plik `<nazwa_f32le>` pozostaje bazowy.
- test syntetyczny sprawdza 3-slot onset,
  czterofazowy znak QMF, kontynuację, wyzerowanie,
  odjęcie ROM i rollback przy NaN/indexie ramki.

**STATUS SBR-54h: ZAPISANY, NIE TESTOWANY na Fedorze**.
Także po pomyślnym teście **NIE będzie to jeszcze
normatywny HE-AAC**, ponieważ brakuje m.in. korekt
wzmocnienia harmonics/noise, envelope boost,
transient gating, gain smoothing, QMF delay
i konformancji w stosunku do referencji.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-harmonic-preview test-aac-sbr-hf-sine test-aac-sbr-temporal-pcm-preview probe-he-aac PKG_CONFIG=/bin/false

./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le |
  grep -E 'SBR-54h|SBR-54g|SBR-54a|Rdzen:'

python3 tools/sauti_he_aac_compare_pcm.py \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le

python3 tools/sauti_he_aac_compare_pcm.py \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.sine.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
```

**Kryterium realnego postępu audio:** spadek
błędu PCM RMS po odpowiednim wyrównaniu z
FFmpeg / poprawa korelacji. Sama liczba
`active_tones>0` nie oznacza sukcesu.
Jeżeli włączone sinusoidy nie zmienią PCM,
sprawdzić oryginalne `transient_env` i
ciągłość flag tonalnych bez wymuszania onset.
Wynik A/B pozwoli ocenić dalszy priorytet
normatywnego składania HF/sine/noise.

## SBR-54h — poprawka kompilacji (-Werror), 2026-11-05

Pierwsza kompilacja nowego `test-aac-sbr-temporal-harmonic-preview` na
Fedorze zatrzymała się na nieużywanej lokalnej zmiennej `active`
w `sauti_aac_sbr_temporal_harmonic_preview.c:38`. Kompilator używa
`-Wall -Wextra -Werror`, więc to fatalny błąd kompilacji,
**nie** błąd działania harmonicznych ani testu PCM. Usunięto
wyłącznie nieużywaną deklarację bez zmiany algorytmu.
Status SBR-54h nadal WIP: po `git pull` wykonać wszystkie testy,
próbkę 50 ramek oraz porównanie pliku `*.sine.f32le`
z FFmpeg. Nie ogłaszać PASS przed wynikiem z Fedory.


## SBR-54h: Fedora PASS testów jednostkowych i kompilacji (2026-11-05)

Użytkownik potwierdził na Fedorze po pobraniu HEAD `97136e9`:
- `make test-aac-sbr-temporal-harmonic-preview`: PASS,
  3-slot transient onset, 4-phase/parity, ROM noise gating,
  continued tone, flag removal, bypass i rollback;
- `make test-aac-sbr-hf-sine`: PASS, w tym 11589
  prawidłowych geometrii pasm QMF;
- `make test-aac-sbr-temporal-pcm-preview`: PASS
  dla 2- i 6-slotowego przekazania między ramkami;
- `make probe-he-aac`: kompilacja bez błędów
  przy `-Wall -Wextra -Werror`.

**Status**: SBR-54h zaliczony jednostkowo, lecz
NIE zweryfikowany jeszcze na rzeczywistych 50 ramkach
PCM ani pod względem brzmienia. Nie ogłaszać sukcesu
HE-AAC. Najbliższy krok to eksport dwóch równoległych
plików:
```sh
./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le
```
Sprawdzić log `SBR-54h: tonal A/B PCM` i
`SBR-54h: zapisano A/B tonal PCM`.
Drugi plik `he_test_sauti_preview.f32le.sine.f32le`
porównać z `he_test_ffmpeg_reference.f32le`
przez `tools/sauti_he_aac_compare_pcm.py`, a wynik
zestawić z bazowym 54a. Stary produkcyjny SBR-31
nadal może skończyć się kodem 1 na ramce 2;
to nie przekreśla poprawnego 50/50 eksportu
diagnostycznego z późniejszego przebiegu.


## SBR-54h realny A/B bez poprawy; SBR-54i audyt domeny QMF (2026-11-05)

**Potwierdzenie użytkownika na Fedorze** dla realnego
`~/Sauti-fixtures/he_test.aac`:
- SBR-54a: 50/50 ramek, 102400 PCM samples,
  w ramce 2 RMS 3.08089898, peak 33.2961998.
- SBR-54h: 50/50 ramek QMF i PCM, 3020 marked
  slot-bands, 3000 aktywnych sinusoid i 3000
  operacji bramkowania ROM noise.
- PCM z harmonicznymi: RMS 0.893832395,
  peak 36.0773697; RMS różnicy od bazowego
  54a = 0.2581988. Eksport pełnych 102400
  próbek do `he_test_sauti_preview.f32le.sine.f32le` PASS.
- Porównanie z FFmpeg: korelacja **0.067763**,
  względny błąd RMS **99.816%**, najlepszy
  znaleziony lag -4332 próbek (niewiarygodny
  fizycznie przy tak słabej korelacji).
  Bazowy 54a: korelacja 0.069288, błąd 99.818%.
  Zatem dodanie SBR-54h NIE poprawiło
  dźwięku w istotny sposób; nie twierdzić
  że uzyskano conformant PCM.

**Audyt przyczyny — jednostki i skala amplitudy:**
`sauti_aac_sbr_energy_dequant` rekonstruuje
`E=2^(6 + quant*alpha)`, tak samo jak FFT/SBR
referencji. Natomiast `sauti_aac_sbr_qmf_analyze_32`
przyjmuje native float PCM AAC-LC około ±1.
Dla porównania, implementacja float FFmpeg
skalibruje SBR na PCM około ±32768 i
normalizuje odwrotnie w syntezie. Por. źródło
`libavcodec/aacsbr_template.c` w FFmpeg,
komentarz w `ff_aac_sbr_ctx_alloc_init`:
https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/aacsbr_template.c

**Hipoteza, nie jeszcze zweryfikowany bug:**
W Sauti wysokie pasma są dopasowywane do
`E` zapisanej w jednostkach 16-bit PCM, ale
po przetworzeniu są przekazywane bez
przeliczenia do 64-QMF, która operuje na
naturalnych float. To może tłumaczyć
nieproporcjonalnie wysokie RMS i peak
HF/NOISE z poprzednich audytów.

**SBR-54i — eksperyment A/B bez naruszania
starej ścieżki**:
- czysty atomowy
  `sauti_aac_sbr_temporal_hf_units_float`
  dzieli tylko AKTYWNE wysokie pasma zespolone
  przez **32768**; dolne pasma pozostają
  nietknięte. Normalizacja dotyczy wariantu
  z harmonicznymi SBR-54h; nie zmienia
  SBR-53d, SBR-09, ROM, QMF LOW, starego
  54a, 54h ani starego dekodera SBR-31;
- oddzielne ciągłe 64-QMF PCM i buforowanie
  50 ramek plus final flush, output
  `he_test_sauti_preview.f32le.units.f32le`;
- test jednostkowy wylicza
  16384/32768=0.5 i -32768/32768=-1
  oraz weryfikuje rollback NaN/mask/length;
- **nie** jest to normatywne rozwiązanie:
  poprawne SBR powinno mieć jednorodne
  jednostki także WEWNĄTRZ energy/gain i
  normalizację QMF oraz zgodne opóźnienia.
  Wynik tego wariantu pokaże jednak,
  czy niezgodność domen tłumaczy różnicę.

**Status: SBR-54i zapisany w GitHub, czeka na
pierwszy test Fedora i realne A/B względem FFmpeg.**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-hf-units probe-he-aac PKG_CONFIG=/bin/false

./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le |
  grep -E 'SBR-54i|SBR-54h|SBR-54a|Rdzen:'

python3 tools/sauti_he_aac_compare_pcm.py \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.units.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
```
Nie oceniać sukcesu po samej zmianie skali:
sprawdzić korelację i relatywny RMS błędu,
a także czy część HIGH faktycznie wnosi
informację przy LOW-aligned lag -6.


## SBR-54i Fedora PASS bez poprawy HE-AAC; nowy SBR-54j pre-limiter (2026-11-05)

**Wynik na Fedorze, 50 ramek realnego he_test.aac:**
- `make test-aac-sbr-temporal-hf-units` PASS;
  `make probe-he-aac` kompiluje się poprawnie z `-Werror`.
- diagnostyczny SBR-54i QMF+PCM: 50/50 ramek,
  102400 f32le, HF dzielony przez 32768 wyłącznie
  w OSOBNEJ ścieżce; RMS 0.0826821164,
  peak 0.159405291.
- porównanie z FFmpeg: lag -6 próbek, korelacja
  0.991765, dopasowany gain 1.4459592,
  error RMS 0.015438979 (12.807% RMS referencji).
- Dla porównania wcześniejsze tylko LOW:
  RMS 0.0826822439, korelacja 0.991765,
  błąd względny 12.807%.
**Interpretacja:** wariant `HF/32768` sprowadził
wyjście praktycznie do samego LOW. Nie dowodzi to,
że 32768 jest właściwą poprawką po syntezie; zniknął
wpływ HF, ale nie odzyskano zgodności wysokich pasm.
Nie wdrażać dzielnika do produkcyjnego odtwarzania.

**Konkretny, źródłowo potwierdzony defekt modelu:**
W `sauti_aac_sbr_temporal_gain.c:gain_one`
dotychczasowy SBR-53d wylicza podstawowe
`sqrt(E/E_curr)`, a potem limiter i boost,
bez pełnego podziału `Q`, harmonicznych i
`delta` początku obwiedni. W referencyjnym
FFmpeg `libavcodec/aacsbr.c:sbr_gain_calc`
są inne wyrażenia:
- `Q_M = sqrt(E * Q/(1+Q))`;
- `S_M = sqrt(E/(1+Q) * active_sine)`;
- bez harmonicznej:
  `G = sqrt(E / ((1 + E_curr) * (1 + Q*delta)))`;
- z harmoniczną:
  `G = sqrt(E*Q / ((1 + E_curr)*(1 + Q)))`.
Źródło porównania:
https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/aacsbr.c
oraz skala 16-bit/QMF (inny osobny problem):
https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/aacsbr_template.c
To potwierdza *niezgodność wzorów* w bieżącym
modelu Sauti, ale NIE przesądza jeszcze o
konkretnej ilości błędu PCM.

**SBR-54j przygotowany**: nowa, odizolowana
funkcja `sauti_aac_sbr_gain_partition`
w `src/decoders/modern/aac/sauti_aac_sbr_gain_partition.c`,
wraz z nagłówkiem i jednostkowym testem.
Odtwarza powyższe wzory w double, sprawdza
E/E_curr/Q>=0, delta/tonal=0 lub 1, nie
zapisuje nic przy błędnych danych. Bezpiecznie
liczy Q/(1+Q) także dla dużych wartości.
Test syntetyczny obejmuje E=100, E_curr=24,
Q=3, delta=0/1, harmonic=0/1, przypadki
Q=0, E=0, NaN, Inf, overflow i rollback.
To jest PRE-LIMITER, jeszcze bez normatywnego
ograniczania mocy q/sine, boost, fazy i
smoothing. **Na razie nie jest podłączony
do głównego 53d/53e/54a ani do odtwarzacza**.

Kolejny cel inżynierski (po test PASS):
zintegrować poprawny `G/Q_M/S_M` z pełnym
limiterem i boostem na całych oryginalnych
obwiedniach, uwzględniając FIXVAR/attack
i wspólną domenę QMF; wtedy porównać nowe
PCM z FFmpeg na jednym lag oraz residual
pozostały po LOW. Nie modyfikować
produkcyjnego SBR-31, dopóki nie będzie
weryfikacji 50/50 ramek i poprawy PCM.

Pierwszy test na Fedorze:
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-gain-partition PKG_CONFIG=/bin/false
```
**SBR-54i PASS (konstrukcyjny A/B), brak
poprawy HE-AAC. SBR-54j WIP, czeka na
kompilację na Fedorze.**


## SBR-54j Fedora PASS; SBR-54k limiter/boost trzech składników WIP (2026-11-05)

**Potwierdzony Fedora SBR-54j:**
`make test-aac-sbr-gain-partition PKG_CONFIG=/bin/false`
przeszedł wszystkie cztery grupy testów przy
`-O2 -std=c11 -Wall -Wextra -Werror`:
`non-tonal steady`, `non-tonal transient delta=0`,
`tonal harmonic gain and sine`, `bounded big inputs
and atomic rejection`. PASS dotyczy tylko
matematyki pre-limiter, bez PCM czy gotowego HE-AAC.

**SBR-54k — nowy kod, NIE SPRAWDZONY jeszcze na Fedorze.**
Nowa funkcja `sauti_aac_sbr_gain_limit_partition`
oblicza na kompletnym oryginalnym envelope i w
granicach częstotliwości SBR-17 trzy wzajemnie
zależne poziomy:
- dla każdego pasma wywołuje przetestowany SBR-54j
  i liczy wejściowe amplitudy `G,Q_M,S_M`;
- w każdej grupie limitera używa
  `gain_max=min(100000,limGain*sqrt((FLT_EPSILON+sumE)/
  (FLT_EPSILON+sumEcurr)))`;
- ogranicza `Q_M` proporcjonalnie do `gain_max/G`,
  następnie ogranicza `G`;
- liczy wspólną energię wyjściową
  `sum(Ecurr*G² + S_M² + (delta && S_M==0)*Q_M²)`;
- `boost=min(1.584893192,sqrt((FLT_EPSILON+sumE)/
  (FLT_EPSILON+after)))` wspólnie mnoży `G,Q_M,S_M`;
- zachowuje atomowy rollback i sprawdza `NaN`,
  ujemne `Q`, flagi sinusoid, granice grup
  oraz `gain_mode` i `delta`.

Implementacja jest osobnym, czystym modułem
`src/decoders/modern/aac/sauti_aac_sbr_gain_limit_partition.c`
oraz testem `tests/test_aac_sbr_gain_limit_partition.c`.
Nie zmienia SBR-53d ani istniejącego PCM.
Ścieżka bin-group przyjmuje granice SBR-17
jako parametr, dlatego w przyszłości można
jej użyć dla rzeczywistych obwiedni i FIXVAR.

Wzorcem jest referencyjna funkcja FFmpeg
`libavcodec/aacsbr.c:sbr_gain_calc`:
https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/aacsbr.c

**Do wykonania na Fedorze:**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-gain-limit-partition test-aac-sbr-gain-partition PKG_CONFIG=/bin/false
```

Po PASS: osobno podłączyć SBR-54k do kompletnych
opóźnionych obwiedni wraz ze źródłowymi mapami
harmonicznych / attack i następnie porównać
alternatywny 64-QMF PCM z FFmpeg. Przede wszystkim
nie mylić PASS jednostkowego z korelacją audio;
realny SBR-54i nadal zaledwie odtwarza LOW
(z korelacją 0.991765, ale nierozwiązanym błędem
12.807%), a działający HF wciąż nie ma
potwierdzonej poprawnej amplitudy, fazy i jakości.


## SBR-54k Fedora PASS; SBR-54l realne kompletne obwiednie (2026-11-05)

**Potwierdzony PASS na Fedorze dla SBR-54k:**
`make test-aac-sbr-gain-limit-partition test-aac-sbr-gain-partition PKG_CONFIG=/bin/false`
zbudował oba testy przy `-O2 -std=c11 -Wall -Wextra -Werror`.
SBR-54k PASS: osobne grupy z boost, delta transient,
jednoczesne ograniczenie HF i szumu, maksymalny boost
oraz atomowy rollback dla NaN, Q, sin i geometrii.
SBR-54j PASS ponownie. Są to **testy matematyki,
nie gotowego HE-AAC**.

**SBR-54l — adapter na realne dane, jeszcze nieprzetestowany
na Fedorze:**
- nowy kod `sauti_aac_sbr_temporal_reference_gain_calculate`
  czyta `SBR-53c` full original envelope power i
  oryginalny `SautiAacSbrTemporalHistory`, zachowując
  `source=0` (poprzednia ramka z FIXVAR) i `source=1`;
- najpierw waliduje tę samą strukturę
  istniejącym `SBR-53d`, którego wartości posłużą
  jako punkt porównania (NIE są modyfikowane);
- z oryginalnego payloadu odtwarza E, Q i sinusoidy
  z midpoint high-frequency harmonic flags,
  generuje lim-bands poprzez SBR-17, uruchamia
  przetestowany 54j+54k i porównuje HF gains;
- dla delta=0 używa aktualnie oznaczonej lokalnej
  `transient_env`, a dla pozostałych delta=1.
  **Ważna niedokończona część normy**: FFmpeg
  uwzględnia także delayed `e_a` oraz aktywną,
  historyczną bramkę sinusoid. Dlatego jest to
  obserwacyjny model z poprawną matematyką 54j/54k,
  NIE normatywna synteza HE-AAC;
- porównuje liczbę pasm o zmienionym HF gain,
  liczbę aktualnie oznaczonych harmonicznych i
  pasm szumu, ograniczenia szumu, liczność grup,
  sumy HF power `sum(Ecurr * G²)` w obu modelach,
  liczbę ukończonych obwiedni FIXVAR;
- test `test-aac-sbr-temporal-reference-gain`
  obejmuje 2- i 6-slotowe FIXVAR z prawdziwym
  SBR-53c pomiarem, rozróżnia właścicieli,
  transient/no-transient oraz harmonics,
  zawiera późny NaN, ujemny Q, zły rozmiar
  obwiedni i atomic rollback;
- **żadnych zmian** w oryginalnym SBR-53d,
  SBR-53e, SBR-54a, SBR-31 ani wyjściowym PCM.
  Brak odrębnej kandydatury PCM na tym etapie;
  przed zastąpieniem starego toru wymagane jest
  bezpieczne przeniesienie opóźnionych
  osobnych amplitud HF/noise/sine do
  niezależnego QMF i pomiar korelacji z FFmpeg.

**Komendy pierwszego testu:**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-reference-gain test-aac-sbr-gain-limit-partition probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 |
  grep -E 'SBR-54l|SBR-53d|SBR-54f|Rdzen:|odrzucony'
```
Sukcesem diagnostycznym będzie 50 ramek obliczeń
i zgodna liczba kompletnych obwiedni 53c/53d/54l,
bez regresji dotychczasowego PCM. Dalszy etap:
zintegrować **poprawne wyjście audio w osobnym
torze**, nie przeklejać samych znormalizowanych
współczynników 54k do starego SBR-53d.


## SBR-54l Fedora PASS; SBR-54m osobny PCM z nowym gain HF (2026-11-05)

**Rzeczywisty przebieg Fedory SBR-54l — PASS**:
- test `test-aac-sbr-temporal-reference-gain`: oba FIXVAR 2/6,
  właściciele previous/current, harmoniczne, attack, rollback PASS;
- test `test-aac-sbr-gain-limit-partition` PASS;
- `make probe-he-aac` skompilował się przy `-Werror`;
- 50 ramek prawdziwego `he_test.aac`, 57 pełnych obwiedni
  (tak jak 53d i 54f), 2 odziedziczone FIXVAR,
  171 grup limitera, 1335 zmienionych gainów pasm-obwiedni,
  1539 pasm z dodatnim docelowym noise, 104 oznaczone
  harmoniczne, 207 ograniczonych szumów;
- porównanie tylko sygnałowej składowej HF, bez szumu i sine:
  `old_HF_power=1177579.6` i `new_HF_power=47361.1999`
  (około 24.9 razy niżej w nowym modelu).
  Te sumy są energią z raportowanych obwiedni, NIE
  bezpośrednim RMS ani sumą realnego PCM.
- **To nie dowodzi poprawy HE-AAC**: w nowym modelu energia
  jest rozdzielana pomiędzy HF, szum i harmoniczne;
  pozostaje brak właściwej skali, bramkowania sine/delta
  i przesunięcia fazowego.

**SBR-54m: pierwszy eksperyment A/B z rzeczywistą nową
składową HF (G only), bez zmian produkcyjnych**:
- `sauti_aac_sbr_temporal_gain_bridge` mapuje tylko G
  z pełnych oryginalnych obwiedni SBR-54l do struktury
  już przetestowanego SBR-53e, zachowując komplet
  `source,env,slots,kx,k2`, cap i boost.
  Czysta, atomowa funkcja. Osobny test syntetyczny
  sprawdza previous/current, 34-slotową obwiednię,
  zero poza HF, NaN, niepoprawną geometrię i rollback;
- istniejący SBR-53e stosuje nowy gain przez DRUGI,
  całkowicie niezależny stan `ref_hf_apply_state`,
  zachowując własne opóźnienie 32 slotów i FIXVAR;
- istniejąca synteza 64-QMF dostaje nowy G-adjusted HF
  oraz NIEZMIENIONE LOW w trzecim niezależnym
  `ref_hf_pcm_state`. W tym wariancie **nie dodaje się
  żadnego NOISE ani SINE**: celem jest izolacja
  samego wpływu nowego ograniczania HF. Maska aktywnych
  wysokich pasm pochodzi z tego samego poprawnie
  wyrównanego, oryginalnego noise-frame;
- zachowuje ciągłość historii od 1. do 50. ramki,
  końcowy `flush`, pomiar RMS i peak
  oraz dodatkowy eksport `*.refhf.f32le` po 50/50
  ramkach; bazowe PREVIEW, .sine, .units i cały
  dekoder SBR-31 pozostają nietknięte;
- **UWAGA: 54m nadal nie jest kompletnym SBR ani
  właściwym brzmieniem.** Pomyślny test ma odpowiedzieć,
  czy same nowe ograniczone G poprawia korelację PCM
  z FFmpeg w stosunku do samego LOW. Następny etap to
  niezależna synteza całości HF+Q_M+S_M po odpowiedniej
  bramce opóźnionych transientów i sin/carry oraz
  poprawienie jednostek QMF.

**STATUS: SBR-54m WIP, zapisany w GitHub i oczekuje
na pierwsze testy Fedory.**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-gain-bridge test-aac-sbr-temporal-reference-gain test-aac-sbr-temporal-apply probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le |
  grep -E 'SBR-54m|SBR-54l|SBR-54a|Rdzen:|odrzucon'
python3 tools/sauti_he_aac_compare_pcm.py \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.refhf.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
```
Nie zastępować 53d/53e produkcyjnie nawet jeśli
korelacja lub RMS będą wyglądały obiecująco.


## SBR-54m Fedora real PCM: wysoki HF pozostaje błędny; SBR-54n diagnostyka stałego lag (2026-11-05)

**Rzeczywisty wynik SBR-54m (użytkownik, Fedora)**:
- pełne 50/50 ramek G-only HF i PCM, 102400 próbek,
  eksport `he_test_sauti_preview.f32le.refhf.f32le` PASS;
- PCM: RMS 0.143121202, peak 7.11286831;
  FFmpeg: RMS 0.120547656, peak 0.22226046;
- automatyczny wybór lag -4424 próbek (−92.167 ms),
  korelacja 0.673192, optymalny fitted scale
  0.44203216, względny błąd 84.572%.
- wcześniej wariant prawie LOW (SBR-54i, HF/32768)
  miał lag −6 próbek, korelację 0.991765,
  optymalny fitted scale 1.4459592 i względny
  błąd 12.807%.
**Istotne zastrzeżenie:** automatyczna selekcja
maksymalnej korelacji przy różnych lag może wybierać
odległe, fałszywe maksimum; nie porównywać 0.673
i 0.991 jako bezpośredniego kryterium dopóki nie
zastosujemy JEDNEGO, fizycznie uzasadnionego lag.
Gain fitting w zewnętrznym narzędziu jest jedynie
diagnostyką, a nie poprawką dekodera. Duży peak
przy prawie LOW-only RMS i niski residual nie
potwierdzają jeszcze zgodnej syntezy HF.

**SBR-54n: dodano niezależną, czysto diagnostyczną
analizę przy ustalonym przesunięciu**:
- `tools/sauti_he_aac_fixed_lag_audit.py` czyta
  trzy istniejące raw f32le: baseline LOW/proxy,
  kandydat SBR-54m, referencję FFmpeg.
  Podstawowe wyrównanie = −6 próbek; opcjonalne
  `--lag` pozwala jawnie zmienić przesunięcie.
  Nie wyszukuje nowego maksimum korelacji.
- osobno pokazuje baseline i HF-only RMS/peak,
  korelację i residual RMS po diagnostycznym
  LS fit gain+DC na dokładnie tym SAMYM oknie.
  Raportuje również `HF = candidate - baseline`
  jako źródło nadmiernych wartości.
- dodatkowo ortogonalizuje HF względem LOW i DC,
  mierzy korelację pozostałej składowej HF z
  residualem `FFmpeg - fitted_LOWo`.
  Wylicza maksymalny udział resztowej wariancji,
  który mógłby zostać wyjaśniony przez tę
  składową HF przy jednym dodatkowym LS
  współczynniku; **nie stosuje** tej wartości
  w dekoderze i nie twierdzi zgodności.
- `--self-test` generuje deterministyczny
  przypadek, w którym HF istotnie wyjaśnia
  resztę, oraz test zerowego unikalnego HF
  i zbyt małego overlap. Makefile:
  `test-aac-sbr-fixed-lag-audit`.
- `units.f32le` jako baseline jest **proxy LOW**,
  ponieważ zawiera bardzo małe HF/32768.
  Preferować czysty LOW-only stem, gdy będzie
  dostępny, i oznaczać przybliżenie.

**Status SBR-54n: zapisany w GitHub, test Fedora PENDING.**
Nie zmienia kodu AAC, SBR-53d, SBR-54m ani
żadnego bitu eksportowanego PCM; tylko analiza
trzech plików z poprzedniej próby.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-fixed-lag-audit

python3 tools/sauti_he_aac_fixed_lag_audit.py \
  --lag -6 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.units.f32le \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.refhf.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
```

**Decyzja dopiero po raporcie:** jeżeli HF wnosi
mało informacji residual-correlated, nie dodawać
kolejnego globalnego mnożnika. Zamiast tego
szukać niezgodności fazy, QMF delay, mapowania
pasm i domen skalowania. Jeśli unikalny HF
jest silnie zgodny z residualem, zaprojektować
osobne, prawidłowe wzmocnienie HF/noise/sine
w wewnętrznej domenie SBR i zbadać normatywny
end-to-end PCM. Bez zmian produkcyjnych w ciemno.


## SBR-54n Fedora PASS: HF bez informacji residual; SBR-54o przesunięcie fazowo-czasowe (2026-11-05)

**Wynik Fedory dla rzeczywistego SBR-54n (50 ramek)**:
- `make test-aac-sbr-fixed-lag-audit`: PASS wszystkich
  trzech przypadków syntetycznych;
- wymuszone JEDNO wyrównanie LOW vs FFmpeg: `lag=-6`,
  wspólne `overlap=102394` próbek;
- prawie LOW (54i): `corr=0.991765`,
  `relative_error=12.807%`, `RMS=0.0826845388`;
- referencyjny G-only HF (54m): `corr=0.568249`,
  `relative_error=82.286%`, `RMS=0.143125395`,
  `peak=7.11286831` (na tym samym opóźnieniu);
- wyizolowana różnica `54m-54i`: `RMS=0.117423672`,
  `peak=7.20016`;
- dodany HF po ortogonalizacji LOW+DC do resztowego
  błędu FFmpeg: `corr=0.011160`,
  `max_residual_variance_explained=0.012%`,
  `optimal_ADDITIONAL_coefficient=0.0014673358`,
  `theoretical_best_error_RMS=0.0154380181`
  w porównaniu z baseline `0.0154389795`.

**Wniosek:** sama zmiana gain/limiter w HF nie naprawia
brzmienia; nawet po optymalnym dodatkowym globalnym
mnożniku HF nie ma istotnej informacji o brakującej
części FFmpeg przy fizycznie uzasadnionym lag=-6.
Nie wprowadzać globalnego gain ani clippingu do PCM.
Pomiar nie odróżnia jednak błędnej fazy lub osobnego
opóźnienia HF od niepoprawnego źródła widma i
niewłaściwego mapowania subbandów.

**SBR-54o — kolejne zawężenie błędu bez mutacji kodu
dekodera**:
- dodano `tools/sauti_he_aac_hf_shift_audit.py`
  i target `make test-aac-sbr-hf-shift-audit`;
- LOW i FFmpeg pozostają wyrównane na `lag=-6`;
  przesuwane są WYŁĄCZNIE próbki składowej
  `HF=candidate-LOW_proxy`, w zakresie ±512 próbek
  (opcjonalnie `--max-shift`);
- skrypt używa tego samego wewnętrznego okna
  dla wszystkich shiftów; po odjęciu rzutu na LOW/DC
  maksymalizuje wyłącznie korelację z resztą FFmpeg.
  Pierwszy przebieg decymowany testuje KAŻDY
  shift; finał liczy najlepsze pełnopróbkowo.
- raportuje `HF shift=0` i najlepszy osobny shift,
  korelacje i **kwadrat** korelacji, czyli maksymalny
  odsetek resztowej wariancji możliwy do wyjaśnienia
  przez liniowe dopasowanie przesuniętego HF;
- test syntetyczny: znane osobne przesunięcia HF
  +37 i -48 próbek z prawidłowo wyrównanym LOW,
  i pełne odtworzenie sygnału resztowego.
- **Status: SBR-54o zapisany w GitHub, pierwsze
  testy na Fedorze PENDING.** Nie zmienia
  żadnego PCM, QMF, gain, ROM ani historii dekodera.
  Jest to poszukiwanie maksimum, więc zwycięski
  shift jest optymistycznym wskaźnikiem
  diagnostycznym, NIE normatywnym opóźnieniem.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-shift-audit
python3 tools/sauti_he_aac_hf_shift_audit.py \
  --lag -6 --max-shift 512 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.units.f32le \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.refhf.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
```

**Decyzja po pomiarze:**
- znaczna poprawa przy małym shift -> badać
  opóźnienie QMF, timing X_low/X_high i bank syntezy;
- minimalna poprawa nawet po najlepszym shift
  -> badać fazę zespoloną QMF, znaki Im/syntezy,
  mapowanie źródło/destynacja HF i domenę
  poziomów energii;
- nie uruchamiać równoległej przebudowy produkcyjnego
  dekodera ani nie traktować korelacji fit jako dowodu
  normatywnej zgodności.


## SBR-54o Fedora PASS, realny peak shift=+288; SBR-54p niezależna walidacja (2026-11-05)

**SBR-54o na Fedorze**:
`make test-aac-sbr-hf-shift-audit` PASS:
syntetyczny sygnał przesunięty o +37 i −48 próbek.
W rzeczywistym `he_test.aac` z ustalonym
`LOW/FFmpeg lag=-6` i wspólnym overlap
`101370` próbek:
- HF `shift=0`: `residual corr=0.011160`,
  `variance explained=0.012%`;
- najlepsze z przeszukanych przesunięć HF
  `shift=+288`: `residual corr=−0.188268`,
  `variance explained=3.544%`.
- **+288 = 9 × 32 próbek banku QMF = 6 ms przy
  48 kHz.** Ujemny znak sugeruje dodatkową
  polaryzację/fazę, ale nie dowodzi, że
  poprawką powinno być odwrócenie znaku.
- 3.544% jest bardzo małą poprawą, dodatkowo
  zawyżoną przez wyszukiwanie najlepszego
  przesunięcia wśród 1025 kandydatów.
  Nie wdrażać żadnego offsetu ani inversion
  do produkcyjnego PCM na podstawie tego wyniku.

**SBR-54p**: dodano `tools/sauti_he_aac_hf_shift_validation.py`
oraz `make test-aac-sbr-hf-shift-validation`.
Narzędzie nie modyfikuje Sauti, tylko trzech już
wyeksportowanych f32le. Funkcje:
- LOW/FFmpeg nadal przesunięte **tylko -6 próbek**;
- szuka shift HF z pierwszej połowy nagrania i
  ocenia BEZ ponownego dopasowania przesunięcia
  na niewidzianej drugiej połowie;
- powtarza eksperyment z zamienionymi połowami;
- osobno w każdej połowie ocenia
  wcześniej wskazane, jawne `shift=+288` i `shift=0`;
- raportuje **podpisaną** korelację i
  `corr²` na walidacji, bez optymalizacji
  gain PCM i bez clippingu;
- porównuje tylko z dopasowanym resztowym
  FFmpeg po usunięciu LOW+DC, w stałym
  wspólnym oknie umożliwiającym przesunięcia
  ±512; liczba próbek w obu połowach pozostaje
  taka sama pomiędzy kandydatami;
- `--self-test`: rozpoznanie stałych
  przesunięć +37/-48 na niezależnych połowach
  oraz odrzucenie złudnego maksimum z części
  treningowej, które nie występuje w walidacji.

**Status SBR-54p: kod jest w GitHub, test na
Fedorze jeszcze NIE WYKONANY.** Nadal nie mamy
potwierdzonej zgodnej generacji HF ani
normatywnego dekodera HE-AAC.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-shift-validation PKG_CONFIG=/bin/false

python3 tools/sauti_he_aac_hf_shift_validation.py \
  --lag -6 --max-shift 512 --test-shift 288 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.units.f32le \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.refhf.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
```

Następny krok po rzeczywistej walidacji:
- jeśli niezależne połówki potwierdzą stałe
  +288 próbek i znak korelacji, sprawdzić
  opóźnienie 32-QMF vs 64-QMF, fazę Im i
  oryginalne bufory HF;
- jeśli wynik jest niestabilny lub nadal
  wyjaśnia tylko kilka procent residualu,
  szukać problemu mapowania/phase/normalizacji
  w zespolonych pasmach, zamiast doklejać
  arbitralny PCM sample-shift.


## SBR-54p Fedora PASS, ale naprawa aliasingu selekcji (+288 vs +295), 2026-11-05

**Nowe realne dane na Fedorze (stara wersja selektora):**
`test-aac-sbr-hf-shift-validation` syntetyczne przesunięcia
+37 i -48 oraz test złudnego optimum: wszystkie PASS.
Dla `he_test.aac` (LOW vs FFmpeg lag=-6):
- trenowanie na pierwszej połowie: wybrano +288,
  korelacja TRAIN **-0.157254**, nieznana wcześniej
  druga połowa **-0.263842**, wyjaśniono 6.961%
  jej wariancji residualu;
- trenowanie na drugiej połowie: wybrano +295,
  lecz korelacja TRAIN wyniosła zaledwie **+0.048175**,
  a walidacja na pierwszej części **+0.027005**,
  wyjaśniono jedynie 0.073%;
- jednocześnie w tej samej DRUGIEJ połowie
  znane z SBR-54o **+288** dawało TRAIN
  **-0.263842**, znacząco lepiej od +295.
  W pierwszej połowie +288 dawało **-0.157254**.
- zatem +295 jest **artefaktem narzędzia**, a nie
  dowodem zmiennego opóźnienia QMF.

**Znaleziony błąd w selekcji SBR-54p**:
stary `select_shift` brał tylko 16 najlepszych
z jednej fazy `positions[::16]`, a potem oceniał
pełnopróbkowo TYLKO bliskich kandydatów.
Takie decymowanie aliasuje składowe HF; globalnie
silniejszy kandydat może w ogóle nie wejść na
krótką listę. Sztuczny przypadek 8192 próbek
ze szkodliwą fazą co 16 próbek dowiódł:
stara procedura omija prawidłowe przesunięcie
+37, mimo że pełnopróbkowa korelacja sięga >0.8.

**Poprawka zapisana w GitHub (wymaga testu Fedora):**
- zgrubny skan osobno na gęstszej siatce
  `positions[::4]` i na czterech niezależnych
  fazach kroku 16: offset 0,4,8,12;
- do dokładnej selekcji trafiają szerokie
  shortlisty z każdej fazy, ich sąsiedzi,
  `shift=0` i **obowiązkowo jawnie wskazany +288**;
- pełnopróbkowy scoring na tym samym oknie
  treningowym; jawny błąd, jeżeli wybrany shift
  wypada słabiej niż +288 albo zero na tych
  samych próbkach;
- nowa regresja `stride-16 alias`, która wymaga
  odnalezienia prawidłowego +37 na obu połowach.
- nadal jest to przeszukiwanie shortlisty,
  nie dowód globalnego optimum dla wszystkich
  możliwych shiftów. Również nawet stabilne +288
  z korelacjami -0.157/-0.264 tłumaczy tylko
  część błędu, nie potwierdza normatywnego
  opóźnienia ani polaryzacji SBR.

**Wyłącznie narzędzie Python i dokumentacja.**
Brak zmian Sauti QMF/AAC/HE-AAC, ROM, gain lub PCM.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-shift-validation PKG_CONFIG=/bin/false
python3 tools/sauti_he_aac_hf_shift_validation.py \
  --lag -6 --max-shift 512 --test-shift 288 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.units.f32le \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.refhf.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
```

**Kryterium:** żadne optymalne przesunięcie
z podzbioru nie może mieć mniejszej bezwzględnej
korelacji na pełnym train niż wcześniej
zweryfikowane +288. Dopiero z wynikami
walidacji obu połówek przechodzimy do
izolowanego porównania faz zespolonych QMF,
mapowania pasm oraz bilansu skali.


## SBR-54p poprawiony PASS; +288 stabilne, ale HF nadal niezgodny. SBR-54q faza Im A/B (2026-11-05)

**Realny test użytkownika, Fedora — SBR-54p**:
- `make test-aac-sbr-hf-shift-validation`: PASS
  przesunięć +37/-48, złudnego maksimum oraz
  testu regresyjnego stride-16 alias.
- Prawidłowy skan z kilku faz na dwóch niezależnych
  połowach (po 50 685 próbek, LOW/FFmpeg lag=-6):
  - trening pierwszej: shift +288, train corr
    **-0.157254**, held-out druga corr **-0.263842**,
    resztowa wariancja **6.961%**;
  - trening drugiej: shift +288, train corr
    **-0.263842**, held-out pierwsza corr **-0.157254**,
    resztowa wariancja **2.473%**;
  - przy HF shift=0 dwie corr tylko **+0.009846**
    i **+0.014691**.
- **Interpretacja**: +288 jest odtwarzalnym
  maksimum spośród przesunięć objętych skanem,
  odpowiada 9 slotom QMF po 32 próbki (6 ms
  przy 48 kHz), z UJEMNYM znakiem korelacji.
  To nie jest dowód normatywnego opóźnienia;
  2.5–7.0% wyjaśnionej wariancji nadal jest niskie.
  Znak przy przesunięciu nie dowodzi sam przez
  się, że trzeba odwrócić część urojoną X[k].

**SBR-54q — nowy kontrolowany eksperyment fazy
zespolonej HF**:
- nowa atomowa funkcja
  `sauti_aac_sbr_hf_conjugate_probe` zamienia
  `Im(X[k]) -> -Im(X[k])` wyłącznie dla aktywnych
  wysokich pasm. `Re` i LOW nie ulegają
  zmianie; puste pasma nadal są zerowe.
  Nie zmienia produkcyjnego SBR-31/53e ani QMF.
- oddzielne testy: przeliczenie wszystkich aktywnych
  zespolonych HF, zachowanie LOW i wejścia,
  nieprawidłowe NaN/maska/rozmiar oraz rollback;
  target `test-aac-sbr-hf-phase-probe`;
- eksperymentalny `sauti_he_aac_probe`
  używa tych samych opóźnionych SBR-54m HF
  z nowym `G`, tych samych LOW i slot-masek,
  ale z conjugated Im w OSOBNEJ ciągłej
  64-QMF syntezie `conj_pcm_state`;
- cały 50-ramkowy zapis wraz z finalnym flush
  do `he_test_sauti_preview.f32le.refhfconj.f32le`.
  Oddzielny bufor i blokada eksportu niepełnego
  przebiegu, bez modyfikacji starych plików
  `.refhf.f32le`, `.sine.f32le`, `.units.f32le`
  ani głównego PCM preview;
- **konieczna walidacja Fedora**: wyraźna poprawa
  residualu przy tej samej osi czasu `lag=-6`
  przemawiałaby za problemem fazy/znaku Im.
  Brak poprawy kieruje audyt do mapowania pasm,
  szumu/sinusoid i czasu wewnątrz QMF.
  Nawet dobry wynik nie upoważnia do wdrożenia
  conjugation jako normatywnej poprawki.

Test:
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-phase-probe probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le |
  grep -E 'SBR-54q|SBR-54m|Rdzen:|odrzucon'
python3 tools/sauti_he_aac_fixed_lag_audit.py \
  --lag -6 \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.units.f32le \
  ~/Sauti-fixtures/he_test_sauti_preview.f32le.refhfconj.f32le \
  ~/Sauti-fixtures/he_test_ffmpeg_reference.f32le
```

**SBR-54p PASS i diagnoza zapisana; SBR-54q
w kodzie WIP, oczekuje na test Fedory.**


## SBR-54q Fedora PASS, ale conjugation NIE NAPRAWIA HF; SBR-54r indeksy źródła QMF (2026-11-05)

**Wyniki z realnego pliku `he_test.aac` na Fedorze**:
- `test-aac-sbr-hf-phase-probe` PASS i
  `make probe-he-aac` kompiluje się bez błędów `-Werror`.
- SBR-54q: odwrócony wyłącznie `Im(HF)`,
  50/50 ramek, 102400 PCM, RMS **0.193644462**,
  peak **9.64282417**.
- dla porównania nieodwrócony SBR-54m:
  RMS **0.143121202**, peak **7.11286831**;
  referencja FFmpeg RMS **0.120547656**,
  peak **0.22226046**.
- przy tym samym `lag=-6` i overlap=102394:
  nowy SBR-54q `corr=0.420329`,
  `relative_error=90.737%`,
  dodany HF vs LOW-fit residual `corr=0.012603`,
  wyjaśniona wariancja **0.016%**.
- poprzedni SBR-54m `corr=0.568249`,
  `relative_error=82.286%`,
  `HF-residual explained=0.012%`.
**Wniosek**: conjugation nie poprawia brzmienia
i globalne odwrócenie `Im(HF)` nie może trafić
do produkcyjnego dekodera. Minimalny wzrost
`0.012% -> 0.016%` residual jest bez znaczenia;
zbieżność PCM spadła, amplituda dodatkowo wzrosła.

**Weryfikacja źródeł FFmpeg** (master, 2026-11-05):
https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/aacsbr_template.c
- `sbr_lf_gen` używa `t_HFGen=8` i
  `X_low[i] <- W[i-8]`, także z poprzedniej
  ramki dla pierwszych 8 slotów.
- `sbr_hf_gen` korzysta z offsetu
  `ENVELOPE_ADJUSTMENT_OFFSET=2`:
https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/aacsbr.h
- `sbr_hf_assemble` w
https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/aacsbr.c
  pobiera `X_high[i+2]`; `sbr_x_gen` pobiera
  analogicznie LOW `X_low[i+2]`.
  Dlatego z samego indeksowania źródła
  bez LPC, przy output slot `i`, pochodzi
  ono z analizy `W[i+2-8] = W[i-6]`.
- Sauti `sauti_aac_sbr_temporal_qmf_prepare`
  analizuje 32 QMF na bieżącym `core_pcm` i
  robi prostą transpozycję `low[slot][src]`
  do `high[slot][target]` bez tego buforowania.
  Stan 53e odkłada gotowe HF o ramkę dla
  rozliczenia pełnej obwiedni, ale to NIE jest
  identyczne z referencyjną kolejnością
  `W[i-8]`, `Y[i]` i `X[i]`.
  Do poprawnego odwzorowania konieczne jest
  osobne rozróżnienie czasu LOW i HF,
  tail-FIXVAR, analizy QMF i czasu syntezy;
  nie wolno dodać globalnego przesunięcia
  samego HF na podstawie tych indeksów.

**KOREKTA WCZEŚNIEJSZEJ POMYŁKI**:
wcześniejsze wyjaśnienie `+288 próbek = 9 slotów
QMF` pomieszało 32 próbki *rdzenia wejściowego*
z 64 próbkami *wyjściowego PCM przy 48 kHz*.
Poprawnie +288 wyjściowych próbek = **4.5 slotu
64-QMF**, natomiast 9 slotów analizy
32 próbek rdzenia (24 kHz) stanowi 576
wyjściowych próbek po 2x upsampling, przed
opóźnieniami filtrów. Referencyjne `8-2=6`
slotów oznacza 6×32=192 wejściowe próbki
rdzenia i teoretycznie 6×64=384
próbki wyjściowego zegara; NIE jest to
równoznaczne z konkretnym przesunięciem
porównywanych PCM. Wyrównanie LOW/FFmpeg
`lag=-6` (próbek wyjściowego PCM) również
nie jest tym samym, co `t_HFGen=8` (sloty QMF).

**SBR-54r**: w `tools/sauti_he_aac_qmf_index_audit.py`
zapisano jawny, deterministyczny model
indeksowania na 32-slotowym frame:
- `FFmpeg source global W-slot = 32*frame+slot-6`;
- `Sauti current source global W-slot = 32*frame+slot`;
- testy sprawdzają pierwszych sześć
  slotów z poprzedniej ramki, slot 6 jako
  początek bieżącej oraz przejścia na końcu.
- ważne: jest to **model indeksów zweryfikowany
  na źródłach**, nie pomiar amplitudy,
  fazy, numerycznych wartości FFmpeg QMF,
  ani dowód konkretnego opóźnienia PCM.
- nie modyfikuje AAC/SBR, QMF, PCM ani ścieżki
  produkcyjnej.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-index-audit
```

**Następna sensowna implementacja po potwierdzeniu
SBR-54r**: osobny, stanowy bufor LOW QMF
8 slotów z poprawnym `+2` indeksem w
eksperymentalnym torze; test przejścia
między ramkami, porównanie LOW/HF PCM
na niezmienionej osi czasu, z referencyjnymi
wektorami QMF/FFmpeg. Żadnej zmiany
produkcyjnego dekodera bez poprawy
rzeczywistej korelacji i bilansu.


## SBR-54r Fedora PASS; nowy SBR-54s six-slot source-QMF diagnostic (2026-11-05)

**Potwierdzony test Fedory SBR-54r**:
`make test-aac-sbr-qmf-index-audit` PASS.
Pierwsze sześć slotów ramki N korzysta w modelu
indeksów FFmpeg z analizy `W[N-1][26..31]`, a
slot 6 odpowiada `W[N][0]`. Dotychczasowe
Sauti-53b bierze wprost `W[N][slot]`.
Jest to **porównanie pochodzenia źródła**, nie
numeryczna walidacja zespolonych współczynników.
Utrzymujemy również korektę rozmiarów:
1 slot analizy = 32 próbki rdzenia, 1 slot
syntezy = 64 próbki wyjściowego PCM.

**SBR-54s (diagnostyka QMF w C, jeszcze NIE
przetestowana na Fedorze)**:
- `sauti_aac_sbr_temporal_source6_prepare`
  bierze istniejące **rzeczywiste** 32x32 zespolone
  LOW QMF z SBR-53b, zachowuje ostatnie
  sześć slotów poprzedniej ramki i zwraca
  `aligned_low[t] = W[t-6]`, łącznie z
  oryginalnymi `Re` i `Im`;
- pierwsza ramka ma wyzerowany preroll w
  slotach 0..5, który raportowany jest
  jako *nieznany*, a nie jako prawidłowa
  rekonstrukcja źródła;
- niezależnie od źródłowej historii 6 slotów
  wybiera oryginalnego właściciela geometrii
  HF przez `SBR-52/53a`. FIXVAR ogon
  długości 2/6 slotów **nie jest tożsamy**
  z historią źródłowego QMF; nie wolno
  łączyć tych dwóch mechanizmów;
- na tak przesuniętym źródle wykonuje tylko
  `sauti_aac_sbr_hf_transpose`, w drugim
  buforze, bez zmiany zasilania SBR-53c,
  wzmacniaczy, szumu, sinusoid, ani PCM;
- nowy target
  `make test-aac-sbr-temporal-source6`
  sprawdza: 2/6 FIXVAR, pierwszy preroll,
  skopiowane Re/Im na granicy ramek,
  mapowanie oryginalnego właściciela
  docelowego HF, zgodność wektorów z
  per-slot transpose, NaN, replay i atomowy
  rollback;
- w `sauti_he_aac_probe` nowa linia
  `SBR-54s: realne HF QMF W[i-6] audit`
  z liczbą ukończonych ramek, skopiowanych
  slotów poprzedniego LOW, preroll,
  osobną liczbą FIXVAR owner-slot,
  liczbą zmienionych zespolonych slot-binów,
  `complex_corr` między prostym transponowaniem
  SBR-53b a źródłem 6-slotowym i RMS różnicy QMF.
- ta korelacja porównuje DWA WARIANTY SAUTI,
  **nie jest korelacją z FFmpeg**. Nie daje
  prawa do podmiany indeksów w produkcji.
  Gdy uzyskamy pomiar, należy zbudować
  niezależne referencyjne wektory pośrednie
  W/X z FFmpeg lub zestaw normatywny, zanim
  zmienimy kolejność HF/LOW w dekoderze.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-temporal-source6 probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 |
  grep -E 'SBR-54s|SBR-53b|Rdzen:|odrzucon'
```

**Status:** SBR-54r PASS na Fedorze. SBR-54s
w repo, jeszcze bez Fedora PASS. Oryginalny
PCM HE-AAC i SBR-31 są bez zmian.


## SBR-54s Fedora PASS; SBR-54t lokalizacja różnic według pasm HF (2026-11-05)

**Rzeczywisty raport Fedory SBR-54s na `he_test.aac`:**
- `make test-aac-sbr-temporal-source6 probe-he-aac` z `-Werror`: PASS;
- 2-/6-slotowy FIXVAR, carry, preroll, replay i rollback PASS;
- wszystkie 50 ramek przeszło diagnostykę, 294 sloty
  źródła QMF zostały przejęte z poprzedniej ramki,
  6 slotów brakującego preroll tylko w pierwszej,
  8 slotów należało do wcześniejszego właściciela FIXVAR;
- `aktywne_slot-biny=41634`, `zmienione=41634`,
  `complex_corr=0.995847548`,
  `RMS_roznicy_QMF=0.0968231162`.

**Co to mówi:** dwie rekonstrukcje Sauti mają bardzo podobne
zespolone widmo (corr ~0.996) pomimo zmian wszystkich
współczynników. Sama wysoka korelacja nie oznacza
zgodności z FFmpeg ani małej różnicy amplitudowej.
Nie zastępujemy w ten sposób SBR-53b ani HF w PCM.
Zwrócić uwagę, że globalna korelacja może ukrywać
kilka bardzo odmiennych pasm i lokalnych pików QMF.

**SBR-54t — nowy read-only per-band HF audit:**
- `sauti_aac_sbr_hf_band_audit_add` zbiera
  `old_energy`, `new_energy`, `cross`,
  `difference` i liczbę aktywnych/zmienionych
  komórek QMF w każdym z 64 pasm,
  wyłącznie na wcześniej porównanych slotach
  (pierwsze 6 bez źródła wykluczone);
- na końcu w `sauti_he_aac_probe` drukuje
  cztery pasma o największej łącznej
  różnicy zespolonej, ich `corr`,
  `diff_RMS`, `diff_rel` (względem
  energii starego HF) i maksimum różnicy
  z numerem ramki, slotu i pasma;
- dodatkowo potwierdza, że łączna liczba
  aktywnych/zmienionych zgadza się z SBR-54s;
  liczy metryki osobno dla pasm, bez
  projektowania arbitralnych mnożników HF;
- oddzielny target
  `make test-aac-sbr-hf-band-audit` z
  syntetyczną różnicą fazy w paśmie 21
  oraz mocą w paśmie 20, testem preroll,
  dwóch kolejnych ramek i atomowego
  odrzucenia NaN/replay/złej geometrii;
- **status: zapisane w repo, brak potwierdzenia Fedora**.
  To tylko wariant Sauti-vs-Sauti, nie FFT
  referencji FFmpeg ani zgodny HE-AAC PCM.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-band-audit test-aac-sbr-temporal-source6 probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 |
  grep -E 'SBR-54s|SBR-54t|Rdzen:|odrzucon'
```

**Kolejny krok zależy od wyniku:** jeżeli błędy koncentrują
się w określonych pasmach, prześwietlić ich dokładną
mapę patch-source i energię QMF / rozkład
faz. Jeżeli są równomierne, priorytetem staje
się konstrukcja niezależnego testu
32->64 QMF end-to-end względem referencji
i sprawdzenie oddzielnie chirp/LPC, noise/sine
i konwencji jednostek. Nie dotykać produkcyjnego
PCM bez dowodów numerycznych.


## SBR-54t Fedora PASS: HF differences concentrated at bands 18–21; SBR-54u patch provenance (2026-11-05)

**Actual Fedora test (SBR-54t)**: `make test-aac-sbr-hf-band-audit test-aac-sbr-temporal-source6 probe-he-aac` passed `-Wall -Wextra -Werror` and synthetic rollback tests. Real 50-frame ADTS `he_test.aac`:
- SBR-54s: 294 previous-frame QMF source slots carried, 6 unavailable first-frame preroll, 8 previous FIXVAR-owner slots, 41,634/41,634 nonzero compared complex slot-bins changed, complex correlation 0.995847548, diff RMS 0.0968231162.
- SBR-54t: largest instantaneous complex HF difference = **6.01597239**, **frame 49, slot 1, destination band 18** (comparing two SAUTI QMF streams).
- Top destination subbands by cumulative squared complex difference:
  - **k=18**: N=1542, corr 0.996160921, diff RMS 0.47681007, diff relative to baseline power 0.0876250985;
  - **k=19**: N=1542, corr 0.979896113, diff RMS 0.140171764, relative 0.200518761;
  - **k=20**: N=1542, corr 0.962206361, diff RMS 0.0344076027, relative 0.274931407;
  - **k=21**: N=1542, corr 0.98116133, diff RMS 0.0240620618, relative 0.194106497.
**Interpretation**: k18 dominates ABSOLUTE complex QMF difference but k20 has a larger RELATIVE difference. This is *not* evidence that these bands are numerically wrong vs FFmpeg, or that the 6-slot source history produces reference-conformant PCM. The baseline is direct SBR-53b, not the independent decoder.

**SBR-54u: targeted read-only original-patch-route provenance**:
- `sauti_aac_sbr_hf_patch_route_audit_add` consumes the existing real SBR temporal history and original owner-specific `source_for_target`; separately counts, for destinations k18..21, which LOW source subband 0..31 was selected in each output slot, whether target is outside kx..k2, intentionally unmapped, or owned by previous FIXVAR envelope.
- Tested independent synthetic FIXVAR 2/6 sequences, source-route counts, malformed patch maps and replay/atomic rollback in `test-aac-sbr-hf-patch-route-audit`. **These Fedora tests are still PENDING.**
- Real probe prints `SBR-54u: band=18 ... srcX=...` through k21, allowing us to tell whether the high-energy bins all originate from a single low subband, or the bitstream changes patch routes. It only counts source mapping; no low PCM, noise/sine, gain, phase, or QMF state modifications.
- All 50-frame side-channel outputs remain *non-normative*. Do not feed source6 or source-route metrics into production decoder based on these results.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-patch-route-audit test-aac-sbr-hf-band-audit probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 |
  grep -E 'SBR-54s|SBR-54t|SBR-54u|Rdzen:|odrzucon'
```

**Next decision**: inspect the source LOW bins and original patch geometry. A stable patch source but poor audio fit should shift investigation toward source-QMF amplitude, LPC/chirp, phase alignment, envelope/noise/sine and a reference intermediate QMF vector, not another arbitrary HF scale.


## SBR-54v — LOW source complex energy/phase audit (2026-11-05)

After SBR-54u Fedora PASS, a read-only audit now measures source LOW QMF subbands 2–5 from the existing `source6_low[32][32]`. Metrics: Re/Im/total energy, RMS, peak and location, and adjacent-slot phase product normalized across each band. First-frame missing 6-slot preroll is excluded, including from phase products. No comparison to independent FFmpeg QMF vectors is claimed. No decoder or PCM change.

**Fedora validation pending:**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-low-source-audit probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 | grep -E 'SBR-54v|Rdzen:|odrzucon'
```

Next: independently extract FFmpeg or normative complex W/X vectors before using any 54v metric as a conformance claim.


### SBR-54v Fedora PASS — real 50-frame result (2026-11-05)

User's Fedora: `make test-aac-sbr-low-source-audit probe-he-aac PKG_CONFIG=/bin/false` built with `-Wall -Wextra -Werror`; unit regression reported PASS for amplitude/complex phase/preroll/replay/atomic rollback. Real `~/Sauti-fixtures/he_test.aac`, 50-frame probe: `Rdzen: udane=50, pierwsza niezerowa=1`; separate probe also prints `Pierwsza odrzucona ramka: 2` (do not conflate with successful core frame count; inspect original reporting semantics if needed).

All four LOW bands have `count=1594` (first-frame 6-slot missing-preroll excluded, i.e. 26 + 49*32). Values:

| LOW band | Total complex energy | complex RMS | Re energy | Im energy | normalized adjacent Re | normalized adjacent Im | peak (frame,slot) |
|---|---:|---:|---:|---:|---:|---:|---|
| 2 | 45658.1355 | 5.3519855 | 22144.557 | 23513.5785 | -0.499691465 | 0.865358747 | 6.23377305 (49,1) |
| 3 | 753.521114 | 0.687548841 | 462.30102 | 291.220094 | -0.502609025 | 0.823481393 | 1.78564342 (49,6) |
| 4 | 24.1515213 | 0.123091507 | 23.3182234 | 0.833297899 | -0.479297585 | 0.0153657012 | 0.467032571 (2,10) |
| 5 | 23.6957111 | 0.121924425 | 23.0733444 | 0.622366744 | -0.496826019 | -0.00864669635 | 0.369335345 (49,6) |

**Interpretation:** LOW 2 energy is about 60.6x LOW 3; the SBR-54u route LOW 2 -> HF 18 explains why HF 18 dominates absolute Sauti-vs-Sauti differences. This is **not** evidence that LOW 2 values or amplification are incorrect. LOW 2/3 show strong adjacent complex rotation whereas LOW 4/5's normalized adjacent complex coherence is weaker. Since the metric is aggregate, it does not directly establish a single phase step or reveal FFmpeg agreement. SBR-54v is validated as a diagnostic only, not an HE-AAC conformance or PCM improvement.

**Next gated action (SBR-54w):** obtain comparable *independent*, time-aligned intermediate complex QMF source vectors W for LOW 2–5 from FFmpeg (or standard vectors) and compare Re/Im, energy and lag, respecting filter normalization and delay. Verify source sampling and geometry first. Avoid modifying production HF/PCM or arbitrary scale factors until this cross-implementation evidence exists.


## SBR-54w — independent QMF-reference comparison gate, phase I (2026-11-05)

An initial **strict comparator** was added in `tools/sauti_he_aac_qmf_reference_audit.py`, with `make test-aac-sbr-qmf-reference-audit` to exercise synthetic complete-frame matching and rejection of mismatched core identity or truncated data.

The comparator accepts interleaved little-endian complex float32 samples with layout `[frame][slot=0..31][LOW band=2..5][Re,Im]`, and two JSON manifests containing:

```json
{
  "format": "sauti-sbr54w-low-qmf-complex32-v1",
  "producer": "Sauti",
  "independent": false,
  "core_sha256": "64 lowercase hexadecimal digits for the exact shared AAC-LC PCM",
  "sample_rate_core": 24000,
  "frame_count": 50,
  "qmf_source": "W[i-6]",
  "first_frame_preroll": 6,
  "bands": [2, 3, 4, 5],
  "layout": "frame-slot-band-re-im-f32le"
}
```

The independent producer must set a distinct producer label and `independent: true`. Labels are *user-supplied declarations*, not cryptographic proof of a reference implementation; source capture and comparison still require manual verification. The tool checks source input SHA identity as declared in the manifests, sample rate, frame count, geometry, exact binary size and finite values before measuring per-band complex coherence, energy ratio and differences. It excludes six missing first-frame source slots.

**Important status:** This is only a Phase-I comparison gate. **No FFmpeg intermediate W/X extractor exists in this change, and the Sauti probe does not yet export the required complex W tensors.** Therefore this phase is NOT an independent FFmpeg numeric comparison, nor evidence of HE-AAC conformity. No production decoder paths changed.

Fedora self-test:
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-reference-audit
```

Next phase must instrument a compatible reference QMF analysis and export Sauti W with a common verified AAC-LC core input, matching normalization/filter history and index conventions before using this comparator on real data.


## SBR-54w phase II — Sauti shared-core QMF export (2026-11-05)

This change provides an isolated **Sauti** diagnostic exporter (not a decoder alteration):
- `tools/sauti_qmf_low_export.c` uses the production 32-band QMF analysis directly on **shared external mono AAC-LC core PCM f32le**. It processes consecutive 1024-sample frames, keeps the QMF analysis filter state, preserves the previous frame's final six 32-band W slots, and exports W[i-6] for bands LOW 2..5.
- Output `frame-major/slot-major/band-major` 32 slots x 4 bands x interleaved Re,Im float32 little-endian per frame (1024 bytes/frame). First six slots of frame zero contain placeholders; comparison scripts exclude them.
- `tools/sauti_qmf_low_manifest.py` hashes *exact input PCM bytes* using SHA256, validates output byte count, records sample rate and geometry. The independent reference must consume **the exact same PCM** and emit the same geometry, independently, before the comparator may be used.
- `tools/test_sauti_qmf_low_export.py` exercises the compiled C exporter, missing preroll, carry between two frames, output shape, manifest and incomplete input rejection.
- No change to AAC-LC decoding, HE-AAC PCM, SBR synth, gain or QMF library logic. Production files remain untouched.

**Fedora build and smoke:**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-export test-aac-sbr-qmf-reference-audit PKG_CONFIG=/bin/false
```

**For subsequent real exports**, supply a byte-identical 24 kHz mono AAC-LC core stream with complete 1024-sample f32le frames:
```sh
make export-aac-sbr-low-qmf PKG_CONFIG=/bin/false
./build/sauti_qmf_low_export /tmp/shared_core.f32le /tmp/sauti_low.qmf.f32le
python3 tools/sauti_qmf_low_manifest.py --core-rate 24000 \
  /tmp/shared_core.f32le /tmp/sauti_low.qmf.f32le /tmp/sauti_low.json
```

**Critical unfinished dependency:** there is still no instrumented FFmpeg analysis-QMF W exporter fed by these identical core PCM frames. The `he_test_ffmpeg_reference.f32le` audio output is *not* complex QMF and is not valid input for the independent comparator. Do not claim numerical reference agreement or change production audio until the independent W tensor is available. The manifest's producer label is a declaration, not cryptographic proof of independent processing.


## SBR-54w phase III — FFmpeg float QMF instrumentation, not yet validated (2026-11-05)

Added:
- `tools/sauti_ffmpeg_qmf_instrument.py`: fail-closed patcher for **disposable FFmpeg checkout** `libavcodec/aacsbr_template.c`. The patch targets the float 32-subband `sbr_qmf_analysis` routine. An opt-in instrumented binary (environment variables `SAUTI_SBR54W_CORE` and `SAUTI_SBR54W_QMF`) overrides only the analysis input with **the byte-identical shared 1024-float core PCM** and captures complex W in FFmpeg's **own** filterbank at the point *before HF generation*. Carries six W slots to match the W[i-6] diagnostic contract. It aborts if wrong slot count, incomplete PCM or failed binary output.
- `tools/sauti_ffmpeg_qmf_manifest.py`: records exact output size and source core SHA256; marks the output as declared independent. This flag is a declaration, **not** provenance proof.
- `make test-aac-sbr-ffmpeg-qmf-instrument`: tests injection anchors and repeat-patch rejection on a synthetic function; it does not compile a real FFmpeg checkout.

**Safeguards:** Run only against a disposable copy and a separate local FFmpeg build, never system-installed FFmpeg. Run mono, one thread, fixed 1024-sample frames, float decoder path. The instrumented process is strictly a diagnostic, not for playback. Review generated C diff before building. Reference extraction is **not yet Fedora-validated**. Upstream FFmpeg code layout may differ, and this patch intentionally rejects unsupported layouts instead of guessing. Licensing and copyright of FFmpeg code remain FFmpeg's own; no FFmpeg QMF code has been incorporated into production Sauti.

**Fedora phase III validation (first step):**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-ffmpeg-qmf-instrument
```

**Then on a disposable upstream checkout**:
```sh
python3 tools/sauti_ffmpeg_qmf_instrument.py --dry-run /path/to/ffmpeg-source
python3 tools/sauti_ffmpeg_qmf_instrument.py /path/to/ffmpeg-source
```

No independent QMF values have yet been compared. Next gate: build the modified float FFmpeg binary separately, decode the target mono HE-AAC while setting both environment paths, ensure exact output frame counts, generate the independent JSON manifest, and invoke `tools/sauti_he_aac_qmf_reference_audit.py`. No production decoder changes until numerical comparisons are credible.


### SBR-54w III: shared-input test and real FFmpeg build instructions (2026-11-05)

The real FFmpeg reference is **still unvalidated**. A deterministic 24-kHz mono float32 QMF input generator `tools/sauti_sbr54w_shared_pcm.py` supplies the **exact same 1024-sample frames** to Sauti and the instrumented FFmpeg analyzer. This avoids confounding differences in AAC-LC decoder output. A binary validator `tools/sauti_sbr54w_verify_reference.py` rejects truncated/nonfinite/silent/misframed files.

**First, Fedora safe tests:**
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-shared-input test-aac-sbr-qmf-export test-aac-sbr-qmf-reference-audit PKG_CONFIG=/bin/false
python3 tools/sauti_sbr54w_shared_pcm.py /tmp/sbr54w-shared-core.f32le --frames 50
./build/sauti_qmf_low_export /tmp/sbr54w-shared-core.f32le /tmp/sbr54w-sauti.qmf.f32le
python3 tools/sauti_qmf_low_manifest.py --core-rate 24000 /tmp/sbr54w-shared-core.f32le /tmp/sbr54w-sauti.qmf.f32le /tmp/sbr54w-sauti.json
```

**External reference build** (separate disposable checkout; never replace system FFmpeg):
```sh
mkdir -p ~/Sauti-tools
git clone --depth 1 https://github.com/FFmpeg/FFmpeg.git ~/Sauti-tools/ffmpeg-sbr54w
cd ~/Sauti
python3 tools/sauti_ffmpeg_qmf_instrument.py --dry-run ~/Sauti-tools/ffmpeg-sbr54w
python3 tools/sauti_ffmpeg_qmf_instrument.py ~/Sauti-tools/ffmpeg-sbr54w
cd ~/Sauti-tools/ffmpeg-sbr54w
git diff -- libavcodec/aacsbr_template.c
./configure --prefix="$HOME/Sauti-tools/ffmpeg-sbr54w-local" --disable-doc --disable-ffplay --disable-ffprobe --disable-shared --enable-static
make -j4 ffmpeg
```

**Real reference measurement** (execute only after build succeeds, on a mono 50-frame HE-AAC fixture):
```sh
cd ~/Sauti
SAUTI_SBR54W_CORE=/tmp/sbr54w-shared-core.f32le \
SAUTI_SBR54W_QMF=/tmp/sbr54w-ffmpeg.qmf.f32le \
~/Sauti-tools/ffmpeg-sbr54w/ffmpeg -hide_banner -v warning -threads 1 \
 -i ~/Sauti-fixtures/he_test.aac -frames:a 50 -f null -
python3 tools/sauti_sbr54w_verify_reference.py /tmp/sbr54w-shared-core.f32le /tmp/sbr54w-ffmpeg.qmf.f32le
python3 tools/sauti_ffmpeg_qmf_manifest.py --core-rate 24000 /tmp/sbr54w-shared-core.f32le /tmp/sbr54w-ffmpeg.qmf.f32le /tmp/sbr54w-ffmpeg.json
python3 tools/sauti_he_aac_qmf_reference_audit.py \
 /tmp/sbr54w-sauti.qmf.f32le /tmp/sbr54w-sauti.json \
 /tmp/sbr54w-ffmpeg.qmf.f32le /tmp/sbr54w-ffmpeg.json
```

**CAUTION:** The experimental FFmpeg patch is deliberately intrusive and may need revision to compile against the precise checked-out FFmpeg commit. The synthetic instrumentation-anchor test does NOT establish successful compilation, run-time frame coverage or actual numerical agreement. Verify the resulting diff and source version; errors are diagnostic evidence. Do not infer that FFmpeg decoded exactly 50 SBR frames merely from `-frames:a 50`—the binary size validator must pass. No Sauti production decoder, original QMF path, or installed FFmpeg should be changed.


### SBR-54w III Fedora result: FFmpeg QMF calls exceed supplied 50-frame input (2026-11-05)

User successfully built and launched instrumented FFmpeg git-2026-11-05-f8a03cd. With exactly 50 shared synthetic 1024-sample frames, running `-frames:a 50` triggered `SBR-54w: core PCM exhausted or truncated` and `abort` before verification. Important: `-frames:a 50` governs **encoded output frames**, not the number of internal SBR QMF analysis invocations; decoder may perform additional lookahead/drain or differently grouped calls. This does not yet identify the exact cause or prove any Sauti QMF error.

Corrected patcher `tools/sauti_ffmpeg_qmf_instrument.py` now supports `--repair-exhaustion` for the **already instrumented disposable FFmpeg checkout**, with exact fail-closed matching to the old injected text. A complete EOF after N=50 shared input frames is no longer fatal: log one warning, stop overriding FFmpeg QMF input and stop exporting further QMF records, while letting FFmpeg finish on its native input. **Partial input frames and nonfinite data remain fatal**. The output must still pass full-frame size/finite/preroll checks and remain diagnostic-only.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-ffmpeg-qmf-instrument
python3 tools/sauti_ffmpeg_qmf_instrument.py --repair-exhaustion --dry-run ~/Sauti-tools/ffmpeg-sbr54w
python3 tools/sauti_ffmpeg_qmf_instrument.py --repair-exhaustion ~/Sauti-tools/ffmpeg-sbr54w
cd ~/Sauti-tools/ffmpeg-sbr54w
make -j4 ffmpeg
cd ~/Sauti
rm -f /tmp/sbr54w-ffmpeg.qmf.f32le
SAUTI_SBR54W_CORE=/tmp/sbr54w-shared-core.f32le SAUTI_SBR54W_QMF=/tmp/sbr54w-ffmpeg.qmf.f32le \
 ~/Sauti-tools/ffmpeg-sbr54w/ffmpeg -hide_banner -v warning -threads 1 \
 -i ~/Sauti-fixtures/he_test.aac -frames:a 50 -f null -
python3 tools/sauti_sbr54w_verify_reference.py /tmp/sbr54w-shared-core.f32le /tmp/sbr54w-ffmpeg.qmf.f32le
```

The repair uses no production Sauti code; `--dry-run` and subsequent build/validation still require Fedora confirmation. A valid 50-frame output establishes data completeness, **not** conformance or identity of decoder internal calls.


### SBR-54w III — first independent QMF numeric comparison (2026-11-05)

Fedora verified 50 complete shared-input instrumented FFmpeg W[i-6] frames: reference binary passed size/finite/preroll checks after EOF-safe patch. On Sauti-vs-FFmpeg 1594 valid slots per LOW band:

| LOW | Sauti/FFmpeg energy | complex coherence magnitude | RMS difference / FFmpeg |
|---|---:|---:|---:|
| 2 | 9.36014605e-10 | 0.998701117 | 0.999969451 |
| 3 | 9.1630863e-10 | 0.997067138 | 0.999969821 |
| 4 | 6.74557229e-8 | 0.0833019686 | 0.999978537 |
| 5 | 2.38823965e-7 | 0.048921026 | 0.999976341 |

FFmpeg/Sauti **RMS amplitude ratios** inferred from energy: roughly 32686x (LOW 2) and 33035x (LOW 3), suggestive of a 32768 PCM amplitude-unit convention mismatch. This is **not proof** of which implementation is incorrectly scaled. LOW 4–5 have much poorer complex coherence; normalization alone cannot fix phase/filter response. No production changes justified.

Added `tools/sauti_sbr54w_gain_audit.py` for diagnostic-only best-fit real and complex gains plus residuals **normalized to Sauti** (the existing strict audit normalizes to FFmpeg; huge scale differences mask differences in that metric). Requires byte-identical shared input manifests; does not rewrite QMF or production code. Run:
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
python3 tools/sauti_sbr54w_gain_audit.py \
 /tmp/sbr54w-sauti.qmf.f32le /tmp/sbr54w-sauti.json \
 /tmp/sbr54w-ffmpeg.qmf.f32le /tmp/sbr54w-ffmpeg.json
```

Next: validate amplitude convention, review low-band effective energy for bands 4–5, phase and filter edge effects. Do not equate `abs_coherence` with fraction of bit-exact agreement.


### SBR-54w III — best complex-fit residuals, phase and lag next (2026-11-05)

Fedora `sauti_sbr54w_gain_audit.py` measured, per LOW band, gain-corrected complex residual normalized to Sauti:

| LOW | FFmpeg/Sauti RMS | Residual after complex gain | Coherence |
|---|---:|---:|---:|
| 2 | 32685.7675 | 0.0509517304 | 0.998701117 |
| 3 | 33035.3651 | 0.0765318377 | 0.997067138 |
| 4 | 3850.26481 | 0.996524351 | 0.0833019686 |
| 5 | 2046.2611 | 0.99880265 | 0.048921026 |

LOW 2–3 suggest near-common waveform with large scaling mismatch. LOW 4–5 remain uncorrelated after complex gain. Before changing the decoder check QMF integer-slot alignment: `tools/sauti_sbr54w_lag_audit.py` scans -16..16 reference-relative slot shifts, excluding the first 6 preroll slots in **both** signals and reporting best coherence and best complex residual. A high-coherence nonzero lag indicates potential time indexing mismatch; no improvement favors filter/window/modulation differences or weak energy, not necessarily a specific bug.

```sh
cd ~/Sauti && git pull --ff-only origin feature/aac-family-asm
python3 tools/sauti_sbr54w_lag_audit.py \
 /tmp/sbr54w-sauti.qmf.f32le /tmp/sbr54w-sauti.json \
 /tmp/sbr54w-ffmpeg.qmf.f32le /tmp/sbr54w-ffmpeg.json
```

No production AAC changes; no QMF correctness claim from synthetic-only tests.


### SBR-54x — independent four-tone QMF transfer experiment (2026-11-05)

Observed on Fedora (50 common-input frames) after optimal per-band complex scaling: LOW2 residual 0.05095 / coherence 0.99870, LOW3 residual 0.07653 / coherence 0.99707, LOW4 residual 0.99652 / coherence 0.08330, LOW5 residual 0.99880 / coherence 0.04892. Lag sweep -16..16 slots reached maximum correlation at **zero lag in all four bands**, so simple integer slot displacement is not the explanation.

Added `tools/sauti_sbr54x_tone_sweep.py`: four deterministic sine inputs centered nominally on 32-channel QMF bands LOW 2,3,4,5: 937.5 / 1312.5 / 1687.5 / 2062.5 Hz at 24 kHz. For each fresh input it independently exports Sauti and FFmpeg W[i-6] complex QMF samples, checks the **exact shared PCM SHA256** via strict manifests, checks size/finite/preroll, and computes per-band complex coherence, gain-corrected residual and **normalized energy shares within LOW 2..5 only**. It saves per-case artifacts and `report.tsv` under `/tmp/sbr54x-tones`. Do not interpret the energy share as percent of the entire 32-band spectrum. This is synthetic PCM directly entering the analysis filter, not encoded/decoded AAC-LC and not production acceptance.

On Fedora, after the instrumented FFmpeg binary built successfully and fixture `~/Sauti-fixtures/he_test.aac` exists:
```sh
cd ~/Sauti && git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-tones PKG_CONFIG=/bin/false
```

No system FFmpeg or production decoder changes; all operations are separate tool invocations. The full four-tone suite was committed but **not yet executed on Fedora**; results require user-run validation. A non-zero exit is a failed/incomplete sweep, not an incorrect production decoder verdict.


### SBR-54x Fedora measurement and SBR-54y steady-state audit (2026-11-05)

The four-tone SBR-54x run on Fedora completed for all tone/observed-band pairs. Dominant excited band (LOW 2, 3, 4, 5 respectively) consistently showed complex coherence ~0.998525, best-fit residual ~0.05428 and FFmpeg/Sauti RMS ~33225.7. Dominant energy shares, **only among LOW 2..5**, Sauti ~0.99777..0.99817, FFmpeg ~0.99963..0.99984. Off-target bins had low energies and correlations; this does not by itself prove a defect in QMF.

Added `tools/sauti_sbr54y_steady_audit.py` using the **existing** `/tmp/sbr54x-tones/low{2..5}` exported PCM-validated, SHA-matched manifests and complex QMF. It reports per-band, per-window complex coherence, best complex gain residual relative to Sauti, FFmpeg/Sauti amplitude RMS ratio and energy fractions within LOW 2..5. Time windows in global slots: early slots [6,64), transition [64,256), steady [256,1600) for 50 frames. The first six missing W[i-6] slots are excluded. Reports 4 tones × 3 windows × 4 observed bands to `/tmp/sbr54x-tones/steady_report.tsv`, with a compact per-target summary to stdout.

```sh
cd ~/Sauti && git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-steady PKG_CONFIG=/bin/false
```

The synthetic unit test validates metrics and window boundaries but **real Fedora results are pending**. This audit does not rebuild FFmpeg, rerun the four tones, change production AAC/QMF code or infer full-band (32-bin) leakage.


## CHECKPOINT — 2026-11-05 — STOP FOR TODAY: SBR-54y complete, SBR-54z next

**Status:** Work paused intentionally at the user's request. All recent measurements below were actually run on Fedora, not merely synthetic predictions. No further AAC/QMF production changes are authorized by these results. Continue next session at **SBR-54z**, inspecting the first 64 global QMF slots individually.

**Independent reference established:** a separately compiled instrumented FFmpeg git-2026-11-05-f8a03cd (in `~/Sauti-tools/ffmpeg-sbr54w`) successfully exported exactly 50 frames of complex W[i-6] LOW 2–5 from byte-identical synthetic PCM as Sauti, with a verified byte count, finite values, and zero initial six-slot preroll. FFmpeg's extra internal QMF calls past 50 shared frames are now handled without abort. FFmpeg remains a local diagnostic reference, not a Sauti dependency. Strict source-hash manifests and exported QMF binaries underpin comparisons; a manifest flag alone does not authenticate independence.

**SBR-54w:** initial mixed-frequency input: complex coherences LOW2 0.998701117, LOW3 0.997067138, LOW4 0.0833019686, LOW5 0.048921026. Complex-gain-adjusted relative residuals: 0.0509517304, 0.0765318377, 0.996524351, 0.99880265 respectively. Lag sweep [-16,+16] slots: best lag **0 for all bands**. Do not interpret low coherence in weakly excited bands as proof of defective filtering.

**SBR-54x:** four independent mono 24-kHz synthetic sinusoids, nominal LOW centers: 937.5, 1312.5, 1687.5, 2062.5 Hz; 50 frames each. The excited/dominant band in every run had complex coherence ~0.998525–0.998526, complex-gain residual ~0.05428 and FFmpeg/Sauti RMS ratio ~33225.7. Main-band energy fractions normalized **only over LOW 2..5**: Sauti ~0.99777–0.99817; FFmpeg ~0.99963–0.99984. Low cross-band energy can explain unexpectedly low overall correlation when those bins are not directly excited; these values are not full 32-band spectral measurements. Full saved report: `/tmp/sbr54x-tones/report.tsv`.

**SBR-54y: Fedora PASS — test actually completed.** `make test-aac-sbr-qmf-steady PKG_CONFIG=/bin/false` passed Python syntax, synthetic self-test, and real analysis of the four previously exported tone datasets. Windows in global QMF slots: early [6,64), transition [64,256), steady [256,1600). Results for **the stimulated band** across all four tones:

| Window | Complex coherence | Complex-gain residual relative to Sauti | FFmpeg/Sauti RMS |
|---|---:|---:|---:|
| Early | ~0.95871–0.95874 | ~0.28428–0.28438 | ~31744–31746 |
| Transition | 0.99987797 | ~0.0156218–0.0156219 | ~33275.90 |
| Steady | 0.99987797 | ~0.0156218–0.0156219 | ~33275.90 |

Transition and steady values being virtually identical indicate the discrepancy is strongly **startup-dependent**; this does **not** yet prove a defect in Sauti's history initialization or identify which code path is responsible. Remaining steady-state ~1.56% gain-fitted residual is nonzero. Full report: `/tmp/sbr54x-tones/steady_report.tsv`. Note: correlation is not bit-exact agreement, and amplitude ratio ~33k is not yet assigned definitively to a PCM scale convention.

**NEXT: SBR-54z (NOT YET STARTED).** Diagnose QMF transient **slot by slot** during global slots [0,64), excluding first 6 undefined/zero W[i-6] slots when comparing. Measure windowed and per-slot complex mismatch and coherence (single complex sample requires a different statistic than multi-slot coherence), energy, and gain/phase evolution. Look for the exact stabilization boundary; then investigate filter history, window and modulation conventions. Preserve strict shared-source provenance and full-frame geometry. Never edit production QMF/AAC solely on this evidence; perform independent diagnostic tests first.

**Resume prerequisites on user's Fedora:** `~/Sauti` branch `feature/aac-family-asm`; independent FFmpeg binary `~/Sauti-tools/ffmpeg-sbr54w/ffmpeg`; retained four tone folders under `/tmp/sbr54x-tones/low{2,3,4,5}`. `/tmp` is ephemeral: if removed, regenerate via `make test-aac-sbr-qmf-tones PKG_CONFIG=/bin/false` (using the already built instrumented FFmpeg) before evaluating SBR-54z. No background tasks; work intentionally stops here.


### SBR-54z — initial startup-slot audit (2026-11-06)

One small diagnostic step, no changes to production AAC or QMF: `tools/sauti_sbr54z_startup_slots.py` uses existing SBR-54x data and strict matching producer/PCM-SHA manifests. For each excited band 2..5 it fits one complex reference→Sauti gain on steady slots [256, end), and applies that **same** gain to each startup slot [6,64). It records per-slot Sauti magnitude, calibrated FFmpeg magnitude, magnitude ratio, phase difference in degrees, complex residual relative to calibrated reference, and low-energy markers. It DOES NOT report meaningless single-sample coherence. It prints grouped early-time summaries and exports `/tmp/sbr54x-tones/startup_slots_report.tsv` (4×58 records).

Fedora command:
```sh
cd ~/Sauti && git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-startup-slots PKG_CONFIG=/bin/false
```

If /tmp was cleared, regenerate previous tones with `make test-aac-sbr-qmf-tones PKG_CONFIG=/bin/false` first. A synthetic self-test exercises stable calibration, forced transient, and zero-reference handling. **Real Fedora validation of SBR-54z pending.** The aim is to localize the startup mismatch without adjusting or rebuilding production filters.


### SBR-54z — stereo fixture discovery and reference-method correction (2026-11-06)

User confirmed via `ffprobe` that `~/Sauti-fixtures/he_test.aac` reports **48000 Hz stereo** (SBR output rate; the shared synthetic PCM used for each 1024-sample QMF analysis frame is 24000 Hz). In the pre-fix FFmpeg instrumentation the first 50 *QMF invocations*, not 50 frames of one channel, consumed shared PCM. Instrumented reference complex W across slots 6..31 and 38..63 were **bitwise identical**, all LOW 2..5; this is consistent with left and right channels each starting with fresh filter history. The instrumented FFmpeg QMF decoder has separate per-channel `analysis_filterbank_samples` and `W`. The preliminary 28.4% "startup" deviation and anomalous slots 38..42 therefore **cannot be attributed to Sauti's QMF history**; they reflect mixed-channel methodology. All prior comparison results must be remeasured using one consistent channel before any QMF correctness conclusion.

Added `tools/sauti_ffmpeg_qmf_stereo_select.py`: one-time fail-closed patch for an **already instrumented** disposable checkout `~/Sauti-tools/ffmpeg-sbr54w`. Adds opt-in environment `SAUTI_SBR54W_CHANNELS=2`, selecting **even-indexed** QMF calls (left channel, assuming verified sequential L/R order) to read the shared core PCM and export W[i-6]. Odd-indexed calls still execute normal FFmpeg analysis using their own input and histories, but are not recorded; static selection assumes sequential `-threads 1` and one fixture stream. No modifications to production Sauti or system FFmpeg. Synthetic anchors self-test does not verify actual decode call order.

Safe next Fedora steps:
```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-stereo-select PKG_CONFIG=/bin/false
python3 tools/sauti_ffmpeg_qmf_stereo_select.py --dry-run ~/Sauti-tools/ffmpeg-sbr54w
python3 tools/sauti_ffmpeg_qmf_stereo_select.py ~/Sauti-tools/ffmpeg-sbr54w
cd ~/Sauti-tools/ffmpeg-sbr54w && make -j4 ffmpeg
```

Then use `SAUTI_SBR54W_CHANNELS=2` **in addition** to `SAUTI_SBR54W_CORE` and `SAUTI_SBR54W_QMF` when running the modified FFmpeg; re-run the complete four-tone sweep with the additional env variable and regenerate all results. DO NOT compare new mono-channel FFmpeg exports with old mixed-channel manifests. Instrumentation metadata provenance and correct channel order still require observational validation. If a different fixture is used, confirm its channels; the selection is specialized to two-channel sequential calls.


### SBR-54z — selected stereo channel export length correction (2026-11-06)

Fedora after applying the stereo-aware QMF patch: with `SAUTI_SBR54W_CHANNELS=2 make test-aac-sbr-qmf-tones`, LOW2 exported 26624 bytes = **26 complete single-channel QMF frames** instead of the 51200 bytes = 50 frames required; script correctly stopped without false PASS. This is an output-duration/fixture-coverage issue, **not** evidence of a Sauti decoder failure. Earlier `-frames:a 50` limited FFmpeg's audio output, not per-channel QMF invocations, so two-channel analysis yielded only ~26 selected left calls.

Updated `tools/sauti_sbr54x_tone_sweep.py` defaults FFmpeg's `-frames:a` cap to **twice the requested per-channel QMF frame count** (50 selected → cap 100) with override `--decode-frames`. It verifies the exact exported byte count before any manifest or comparison. If the source fixture itself runs out, a larger cap does not extend it: use longer genuine stereo HE-AAC fixture or explicitly lower `--frames`; never invent or pad QMF records. No rebuild of already instrumented FFmpeg or any production Sauti change is required.

Retry:
```sh
cd ~/Sauti && git pull --ff-only origin feature/aac-family-asm
SAUTI_SBR54W_CHANNELS=2 make test-aac-sbr-qmf-tones test-aac-sbr-qmf-startup-slots PKG_CONFIG=/bin/false
```
If output is still short, report how many real selected QMF frames the fixture supplied; next step is checking AAC fixture length rather than relaxing strict validation. All previously mixed-channel comparisons must remain labeled invalid for channel-specific transient claims.


### SBR-54z — 20-frame selected-channel repeat unresolved; trace call state (2026-11-06)

Fedora `SAUTI_SBR54W_CHANNELS=2 python3 tools/sauti_sbr54x_tone_sweep.py --frames 20 --decode-frames 100`: successfully exported 20 complete QMF frames, passed size/preroll/finite checks, but **correctly failed** repeat-frame guard: FFmpeg selected-channel frames 0 and 1 contain exactly the same complex bins, despite left-channel selection. Only LOW2 files regenerated; LOW3..5 directories held older 50-frame results. The prior stereo explanation is therefore not established as the complete cause. Do not change Sauti's production QMF or interpret this as a conformance verdict.

New `tools/sauti_ffmpeg_qmf_call_trace.py` adds read-only logging for the first 96 actual QMF invocations to the already instrumented throwaway FFmpeg checkout. Log records call index, whether it was selected for shared PCM, address of history buffer `x`, `W` pointer, `buf_idx`, and the first/last historical float **before** normal analysis. It makes no changes to sampling, filter history, or export; a synthetic anchor self-test protects patch application. Correlate selected calls with history-pointer identity and alternating L/R order.

Run on Fedora:
```sh
cd ~/Sauti && git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-call-trace
python3 tools/sauti_ffmpeg_qmf_call_trace.py --dry-run ~/Sauti-tools/ffmpeg-sbr54w
python3 tools/sauti_ffmpeg_qmf_call_trace.py ~/Sauti-tools/ffmpeg-sbr54w
make -C ~/Sauti-tools/ffmpeg-sbr54w -j4 ffmpeg
cd ~/Sauti
SAUTI_SBR54W_CHANNELS=2 python3 tools/sauti_sbr54x_tone_sweep.py --frames 20 --decode-frames 100 > /tmp/sbr54z-trace-run.log 2>&1
grep '^SBR54Z_CALL=' /tmp/sbr54z-trace-run.log | head -48
```

Expected sweep still exits nonzero until the root cause is fixed; the trace log is the intended deliverable. The first 96 calls are logged regardless of exported frame count. This diagnostic patch should only be used on the disposable FFmpeg binary.


### SBR-54z — observed call/state result; change selection hypothesis (2026-11-06)

Fedora QMF trace: invocation **0** had a different history `x` pointer (`0x7f8c6087ee60`) than calls **1..47** (`0x7f8c5402ba20`). Calls 1..47 shared one history pointer, alternated `buf_idx` 0/1, and had nonzero history after initial startup. Therefore **alternating call parity cannot be used as channel identity**; even/odd selection was invalid. The exact audio-decoder reason for call 0's separate context remains unproven. Synthetic measurement duplicate startup can originate from mixing the one-off context with the continuous context.

Added `tools/sauti_ffmpeg_qmf_context_select.py`, a **separate, one-time upgrade** to the existing trace-patched, disposable FFmpeg source. With `SAUTI_SBR54W_CHANNELS=2` enabled, skip first QMF call, then select every subsequent invocation provided the `x` history pointer remains identical; **abort on any pointer change**. This is explicitly specialized to the observed fixture and does not prove all calls are from the same logical channel. A subsequent 20-frame run must validate startup continuation and exact frame counts. Do not change Sauti production decoder. The preexisting env name is retained for patch compatibility, but now means opt-in selection by observed context, not stereo parity.

```sh
cd ~/Sauti && git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-context-select
python3 tools/sauti_ffmpeg_qmf_context_select.py --dry-run ~/Sauti-tools/ffmpeg-sbr54w
python3 tools/sauti_ffmpeg_qmf_context_select.py ~/Sauti-tools/ffmpeg-sbr54w
make -C ~/Sauti-tools/ffmpeg-sbr54w -j4 ffmpeg
SAUTI_SBR54W_CHANNELS=2 python3 tools/sauti_sbr54x_tone_sweep.py --frames 20 --decode-frames 100
```

**Do not** run the dependent startup-slot audit if the strict four-tone sweep rejects the selected reference. If the new selection aborts on a pointer mismatch, retain diagnostic traces for investigation; do not weaken the guard.


### SBR-54z — Fedora validation of single-history-context reference (2026-11-06)

Confirmed on Fedora, in a throwaway instrumented FFmpeg checkout: `tools/sauti_ffmpeg_qmf_context_select.py` synthetic self-test, dry run and patch passed, and `make -C ~/Sauti-tools/ffmpeg-sbr54w -j4 ffmpeg` completed (compiler gave unrelated read_sbr_grid -Wstringop-overflow warnings). Then `SAUTI_SBR54W_CHANNELS=2 python3 tools/sauti_sbr54x_tone_sweep.py --frames 20 --decode-frames 100` completed **all four tones with no failure**. First QMF invocation from distinct pointer was skipped; selected calls 1..20 used same history address with alternating W buf_idx; later calls continued on the same address. 20 real QMF frames exported per tone; shared PCM and strict manifests, size and finite checks passed. Dominant-band metrics, each compared on its own tone:

| Excited/observed LOW | Complex coherence | Best complex-scaled residual | FFmpeg/Sauti RMS | Sauti energy share of LOW2..5 | FFmpeg share of LOW2..5 |
|---|---:|---:|---:|---:|---:|
| 2 | 0.999878 | 0.015621 | 33275 | 0.998098 | 0.999817 |
| 3 | 0.999878 | 0.015622 | 33275 | 0.997602 | 0.999585 |
| 4 | 0.999878 | 0.015622 | 33275 | 0.997584 | 0.999569 |
| 5 | 0.999878 | 0.015622 | 33275 | 0.998020 | 0.999739 |

The repeated-startup-frame guard passed after switching from erroneous call-parity selection to one context with continuous history. This establishes consistency for the **tested 20-frame, 4-tone, LOW2..5 diagnostic scenario only**. The `x` pointer continuity does not, by itself, prove logical channel identity, exact bitwise QMF correctness, full 32-band conformance or production AAC/HE-AAC decoding correctness. The ~1.56% residual and amplitude scaling convention remain to explain. Previous 50-frame mixed-context results are invalid as a continuous-reference startup audit.

**Next:** execute `make test-aac-sbr-qmf-startup-slots PKG_CONFIG=/bin/false` on the newly regenerated 20-frame tone datasets; verify no anomalous slots 38..42, then consider a fixed-gain/reference scaling analysis before any production change.


### SBR-54z — final Fedora startup audit PASS (2026-11-06)

User ran `make test-aac-sbr-qmf-startup-slots PKG_CONFIG=/bin/false` after the 20-frame-per-tone **single-history-context** SBR-54x exports. Python syntax and synthetic self-test both PASS; all 4 × 58 observed startup slots were usable. The formerly huge error spikes at slots **38–42 disappeared in every excited band**. Fixed steady-state complex gain reference→Sauti was near 3.0048104e-05 with negligible imaginary component. For LOW 2..5, median per-slot relative complex error was ~0.015624 across startup windows [6,16), [16,32), [32,48), [48,64). Max in [6,16) LOW2 .01569423, LOW3 .01576285, LOW4 .01585006, LOW5 .0159518; from slot 16 onward typical/max reported values were ~0.015624 without the former >1 transient. Report `/tmp/sbr54x-tones/startup_slots_report.tsv`.

This closes the original false startup discontinuity finding under **this specific diagnostic fixture and measurement geometry**, not full 32-band or AAC production conformance. Remaining systematic ~1.56% gain-calibrated complex error and large raw FFmpeg/Sauti amplitude scale ratio (~33275) warrant a separate scaling/filter-convention audit. Production AAC and QMF source untouched. No reason to rebuild production filtering from the old invalid mixed-context results.


## HANDOFF / KONIEC SESJI — 2026-11-06 — SBR-54z ZAMKNIĘTY

**Stan na koniec:** zakończono SBR-54z — diagnostykę rozruchu QMF na syntetycznych sygnałach LOW 2–5. Dziś nie rozpoczynamy kolejnego etapu. Gałąź: `feature/aac-family-asm`.

**Co zostało potwierdzone na Fedorze:**
- Niezależna, lokalnie instrumentowana wersja FFmpeg po korekcie metodologii pomiarowej eksportuje **20 kolejnych ramek QMF z jednego obserwowanego kontekstu historii**; test czterech tonów SBR-54x zakończony PASS.
- Test `make test-aac-sbr-qmf-startup-slots PKG_CONFIG=/bin/false` zakończony PASS; **zniknęły anomalie w slotach 38–42** wszystkich czterech pasm. To były artefakty wcześniejszego doboru wywołań referencyjnego FFmpeg, nie potwierdzona wada Sauti.
- Korelacja zespolona pasma pobudzonego ~`0.999878` w każdym z czterech eksperymentów. Pozostały systematyczny błąd po korekcji zespolonego wzmocnienia ~`0.01562` (1,56%). Stosunek RMS FFmpeg/Sauti ~`33275`; nie wyjaśniono jeszcze jego konwencji skali.
- **Kod produkcyjny AAC/QMF Sauti NIE był zmieniany.** Wyniki dotyczą diagnostycznego analizatora, konkretnych częstotliwości i pasm LOW 2–5; nie są deklaracją pełnej zgodności HE-AAC lub wszystkich 32 pasm.

**Początek kolejnej sesji — proponowany SBR-55:** wykonać odrębny audyt skali i konwencji współczynników filtra (zwłaszcza różnicy ~1,56% po dopasowaniu zysku) na zweryfikowanych plikach `/tmp/sbr54x-tones/low{2,3,4,5}`. Najpierw diagnostyka, żadnych pochopnych zmian implementacji. Jeśli katalog `/tmp` zniknie, ponownie wyeksportować tony przez `SAUTI_SBR54W_CHANNELS=2 python3 tools/sauti_sbr54x_tone_sweep.py --frames 20 --decode-frames 100` na wcześniej przygotowanym, instrumentowanym FFmpeg. `SAUTI_SBR54W_CHANNELS=2` jest w tej wersji instrumentacji przełącznikiem **selekcji jednego obserwowanego kontekstu historii**, nie dowodem tożsamości kanału stereo. Nie porównywać ze starymi 50-ramkowymi pomiarami mieszającymi konteksty.

### SBR-55 — statyczny audyt konwencji okna QMF (2026-11-06)

Pierwszy krok SBR-55 jest wyłącznie diagnostyczny. Dodano
`tools/sauti_sbr55_qmf_convention_audit.py` oraz cel
`make test-aac-sbr-qmf-convention`. Test własny sprawdza parsery, dokładne
mapowanie co-drugiego współczynnika i fail-closed kotwice źródła FFmpeg.

W pełnym uruchomieniu narzędzie przyjmuje jawne pliki
`aacsbrdata.h` i `aacsbr_template.c` z **jednego, jednorazowego checkoutu
FFmpeg**. Wymaga 640-tapowej tablicy Sauti, 320-tapowej tablicy FFmpeg oraz
kotwic kolejności historii: przesunięcia 320 próbek, dopisania wejścia pod
`x+288`, odwróconego mnożenia okna, sumowania `64x5` i obu shuffle. Sprawdza
relację `ffmpeg_ds[i] == sauti_prototype[2*i]` dla `i=0..319`.

To może wykluczyć wyłącznie błąd skopiowanej tablicy prototypu, znaku lub
prostej skali jej współczynników. Nie może dowieść zgodności transformacji,
tożsamości kanału, geometrii fixture'u, skali PCM ani zgodności HE-AAC. Nie
zmieniono produkcyjnego dekodera ani filtrów; różnica ~1,56% pozostaje
otwartą hipotezą do sprawdzenia na ponownie wygenerowanych danych SBR-54x.

Przykładowe uruchomienie referencyjne:
```sh
python3 tools/sauti_sbr55_qmf_convention_audit.py \
  --ffmpeg-data ~/Sauti-tools/ffmpeg-sbr54w/libavcodec/aacsbrdata.h \
  --ffmpeg-template ~/Sauti-tools/ffmpeg-sbr54w/libavcodec/aacsbr_template.c
```

#### SBR-55 — nieprawidłowy pierwszy PASS audytu statycznego (2026-11-06)

Pierwszy raport `max_abs=0`, `rms=0` był **nieprawidłowy**: parser akceptował
`-Q31(...)` jako dodatnie `Q31(...)`, więc pominął znak unarny występujący w
referencyjnej tablicy FFmpeg. Nie wolno używać tego wcześniejszego wyniku jako
dowodu zgodności prototypu. Poprawiony parser zachowuje znak zewnętrzny,
posiada test własny dla `-Q31(...)` i fail-closed porównuje wszystkie 320
współczynników.

#### SBR-55 — świeże eksporty i brak opóźnienia całych slotów (2026-11-06)

Odtworzono wszystkie cztery tony SBR-54x z `--frames 20` i
`--decode-frames 100`, przy `SAUTI_SBR54W_CHANNELS=2`. Każdy eksport przeszedł
sprawdzenie pełnego rozmiaru, skończoności, prerollu oraz ciągłości jednego
kontekstu historii. Dla pobudzonego pasma LOW 2..5 wynik powtarza
`coherence≈0.99987797`, `residual≈0.0156218` i
`FFmpeg/Sauti RMS≈33275.9` po okresie rozruchu.

`sauti_sbr54w_lag_audit.py --max-lag 8` wskazał dla każdego pobudzonego
pasma jednoznacznie `best_lag=+0`. Nie jest to więc przesunięcie o całkowitą
liczbę slotów QMF. Pasma niepobudzone mają zbyt małą energię, by ich lag
interpretować jako wynik zgodności. Dodano kolejny, tylko-diagnostyczny
profil `tools/sauti_sbr55_modulation_audit.py` i cel
`test-aac-sbr-qmf-modulation`: po tym samym stałym dopasowaniu zespolonego
zysku mierzy zakres modułu/fazy ilorazu oraz dominującą okresową składową
reszty. Nie zmienia QMF ani PCM i nie stanowi podstawy do korekcji dekodera.

#### SBR-55 — reszta ma parzystość co slot (2026-11-06)

Pełne widmo 576 stabilnych slotów wykazało we wszystkich czterech pobudzonych
pasmach tę samą dominującą harmoniczną `288` (Nyquist czasu slotów). Jej
amplituda jest równa RMS reszty (`~0.0156237..0.0156238`), czyli praktycznie
cała pozostałość po dopasowaniu stałego wzmocnienia zmienia znak **co jeden
slot QMF**. Zakres ilorazu Sauti/(gain×FFmpeg) to odpowiednio LOW2
`[0.98625, 1.01381]`, LOW3 `[0.98797, 1.01213]`, LOW4 `[0.99016, 1.00998]`,
LOW5 `[0.99273, 1.00746]`; zakresy fazy nie przekraczają około `±0.80°`.

Narzędzie domyślnie bada teraz całe widmo do Nyquista (`--max-harmonic 0`)
i jawnie oznacza `alternating_slot_residual=yes`. To zawęża hipotezę do
parzystości/koniugacji ścieżki modulacji lub formatu zapisu W, nie dowodzi
jednak, po której stronie leży błąd ani nie uzasadnia przełączania znaków w
kodzie produkcyjnym. Następny bezpieczny krok: wyeksportować przed/post-shuffle
oraz wejście transformacji z instrumentowanego FFmpeg i porównać je z
odpowiednikiem Sauti na jednym slocie parzystym i nieparzystym.

#### SBR-55 — przygotowanie śladu granic modulacji (2026-11-06)

Dodano dwa narzędzia diagnostyczne, jeszcze bez zmiany dekodera:
`tools/sauti_ffmpeg_qmf_stage_trace.py` tworzy fail-closed patch wyłącznie
dla wcześniej instrumentowanego, jednorazowego checkoutu FFmpeg, a
`tools/sauti_qmf_stage_export.c` eksportuje z tego samego shared PCM wektor
Sauti po sumowaniu polifazowym oraz wektor w współrzędnych FFmpeg sprzed
`post_shuffle`. Patch FFmpeg zapisuje po 64 `float` po `sum64x5` i po MDCT,
tylko dla aktywnych ramek shared PCM; wymaga obu ścieżek wyjściowych albo
kończy pracę błędem.

Kolejny przebieg musi najpierw przejść syntetyczny test kotwic patchera,
następnie kompilację **wyłącznie** tego checkoutu FFmpeg i pełną kontrolę
rozmiaru 20×32×64 wartości dla obu etapów. Dopiero porównanie wektorów
parzystego i nieparzystego slotu może wykazać granicę, na której powstaje
reszta Nyquista. Żadne dane śladowe nie są wejściem ani zmianą dekodera.

`tools/sauti_sbr55_stage_audit.py` odrzuca niepełne lub niefinitywne dane,
sprawdza liczbę wektorów i raportuje najlepszą z dwóch jawnych orientacji
wektora przed transformacją (`identity` lub `reverse64`) oraz niezależny
residual po MDCT. Dopuszcza tylko stały rzeczywisty gain diagnostyczny; nie
ukrywa błędu przez korekcję co slot lub przez zmianę produkcyjnych współczynników.

#### SBR-55 — pierwszy ślad etapów: źródło przed modulacją (2026-11-06)

Patcher SBR-55 przeszedł test syntetyczny, zbudowano jednorazowy FFmpeg i
uruchomiono go dla wspólnego tonu LOW2 z 20 ramkami. Wszystkie cztery ślady
(`Sauti pre/post`, `FFmpeg pre/post`) miały wymagane `20×32×64×4 = 163840`
bajtów. Audyt wybrał `identity` dla kolejności wektora przed transformacją,
ale otrzymał już tam `gain=0.98488856`, `residual=0.122932428`. Po MDCT,
przed `post_shuffle`, residual pozostaje praktycznie identyczny
`0.122932423` (przy spodziewanej różnicy globalnej skali `gain≈3.00564e-05`).

Zatem obserwowana później reszta Nyquista nie ma źródła w `pre_shuffle`,
MDCT ani `post_shuffle`: jest obecna w danych po `sum64x5`, tj. w historii
wejściowej, odwróceniu okna albo indeksowaniu polifazy. Dodano rozbicie
parzyste/nieparzyste do audytu etapów; przed zmianą implementacji trzeba
sprawdzić tę parzystość oraz porównać pięć składników każdego `sum64x5`.

Wynik wstępny zawierał również pierwsze sloty po nieudostępnionym wywołaniu
rozgrzewającym FFmpeg. Audyt etapów od wersji SBR-55 domyślnie odrzuca sloty
`[0,64)`, identycznie jak zweryfikowany audyt SBR-54y; wynik dla całego
zakresu jest zachowany wyłącznie jako diagnostyka rozruchu, nie jako miara
stabilnej konwencji filtra.

Audyt orientacji etapu polifazowego został rozszerzony z dwóch przypadków do
wszystkich 64 przesunięć cyklicznych w obu kierunkach. Dzięki temu wynik nie
zakłada z góry ani kolejności wektora, ani jego fazy indeksowej; raportuje
najmniejszy residual jako `forward+N` lub `reverse+N` przed oceną wartości
składników filtra.

Na stabilnym fragmencie LOW2 audyt elementowy zawęził różnicę jeszcze dalej:
elementy `1..63` po `sum64x5` zgadzają się z FFmpeg w granicy śladu `float`,
natomiast różni się tylko element `0` (maksymalnie `0.0809907466` w tym
eksperymencie). Następny ślad ma więc eksportować wyłącznie pięć iloczynów
tworzących gałąź `z[0]`, zamiast rozbudowywać całe QMF lub modyfikować filtr.

#### SBR-55 — ślad pięciu składników gałęzi `z[0]` (2026-11-06)

Dodano fail-closed patcher `sauti_ffmpeg_qmf_branch0_trace.py`, który w
jednorazowym FFmpeg zapisuje przed `sum64x5` wyłącznie `z[0]`, `z[64]`,
`z[128]`, `z[192]` i `z[256]`. Eksporter Sauti zapisuje odpowiadające im pięć
iloczynów historii i okna, a `sauti_sbr55_branch0_audit.py` porównuje każdy
składnik od stabilnego slotu 64. Nie jest to poprawka dekodera; wynik ma
jedynie wskazać konkretny offset historii wymagający audytu.

#### SBR-55 — korekta dwóch znaków prototypu QMF (2026-11-06)

Pomiar pięciu składników ustalił, że zgodne są gałęzie historii `0`, `64` i
`128`, a rozbieżne `192` i `256`. Powtórzenie na nieokresowym, deterministycznym
sygnale dało ten sam wynik; wykluczono więc artefakt pobudzenia tonalnego.
Analiza tablicy FFmpeg wykazała następnie dwa szczególne wpisy zapisane jako
`-Q31(...)`: `ds[192]` i `ds[256]`, czyli odpowiednio `prototype[384]` i
`prototype[512]`. W Sauti oba były dodatnie.

Skorygowano te dwa współczynniki w wspólnej 640-tapowej tablicy do
`-0.3611589903f` oraz `-0.0132718220f`. To jest korekta danych prototypu,
nie kompensacja skali ani fazy. Poprawiony audyt statyczny oraz ponowny ślad
etapów muszą przejść przed uznaniem jej za zweryfikowaną; nadal nie stanowi to
deklaracji pełnej zgodności HE-AAC.

#### SBR-55 — walidacja korekty prototypu (2026-11-06)

Po korekcie poprawiony audyt statyczny podał `max_abs=0`, `rms=0` dla pełnych
320 par `FFmpeg ds[i]` i `Sauti prototype[2*i]`; obejmuje on teraz również
zewnętrzny znak `-Q31(...)`. Na 20 ramkach wspólnego, deterministycznego i
nieokresowego PCM pięć składników `z[0]` ma `max_abs_delta=0` dla każdego z
offsetów historii `0, 64, 128, 192, 256`. Ślad etapu polifazowego po odrzuceniu
rozgrzewki `[0,64)` osiągnął:

```text
pre_gain(FF->Sauti)=0.999999999  pre_residual=5.55748444e-08
post_gain(FF->Sauti)=3.05175786e-05  post_residual=1.12306577e-07
```

Stały czynnik po MDCT to `1/32768`, tj. jawna skala Q31 po stronie FFmpeg;
nie jest korygowany w kodzie Sauti. Kolejny niezależny przebieg SBR-54x dla
czterech tonów LOW 2–5 potwierdził `coherence=1.000000`, stosunek RMS
`FFmpeg/Sauti=32768` i residual pasma pobudzonego zaokrąglony do `0.000000`
(największy ślad w sąsiednich badanych pasmach `0.000009`).

Przeszły także `test-aac-sbr-qmf-synth-window`, `test-aac-sbr-qmf-synth-mod`,
`test-aac-sbr-qmf-analysis`, `test-aac-sbr-qmf-frame` oraz testy własne
narzędzi SBR-55 przy `-Wall -Wextra -Werror`. To waliduje 32-pasmową analizę
QMF wobec tego konkretnego instrumentowanego FFmpeg i wspólnego wejścia;
nie dowodzi jeszcze kompletności dekodera HE-AAC, parsowania SBR ani jakości
końcowego PCM.

Na końcu przeszedł również pełny pakiet `make test-aac-native
PKG_CONFIG=/bin/false`, obejmujący wszystkie rodzime testy AAC/SBR, ADTS-QMF
i adapter AAC-LC. Nie użyto systemowego FFmpeg ani nie zmodyfikowano jego
instalacji; zewnętrzny checkout służył wyłącznie jako jednorazowa referencja
diagnostyczna.

### SBR-56 — pełna statyczna zgodność prototypu syntezy QMF (2026-11-06)

Rozszerzono `tools/sauti_sbr55_qmf_convention_audit.py` o fail-closed odczyt
obu tabel FFmpeg: 320-tapowego `sbr_qmf_window_ds` dla analizy oraz pełnego
640-tapowego `sbr_qmf_window_us` dla syntezy. Parser zachowuje także zewnętrzny
znak `-Q31(...)`; sam test własny obejmuje tę składnię.

Pomiar na tym samym jednorazowym checkoutcie referencyjnym zakończył się:

```text
analysis  max_abs=0  rms=0  (FFmpeg ds[i] == Sauti prototype[2*i])
synthesis max_abs=0  rms=0  (FFmpeg us[i] == Sauti prototype[i])
```

W ten sposób tabela używana przez `sauti_aac_sbr_qmf_synth_window_64` jest
potwierdzona liczbowo, łącznie z dwiema wyjątkowymi inwersjami znaków 384/512.
Nie jest to jeszcze dynamiczna walidacja syntezy: modulacja, historia 10
bloków, skalowanie i opóźnienie końcowego PCM wymagają osobnego porównania
wektorów QMF→PCM z referencją.

### SBR-57 — przygotowanie dynamicznego śladu syntezy QMF (2026-11-06)

Dodano niezależny od dekodera eksporter `sauti_qmf_synth_export` i dwa
narzędzia diagnostyczne. Patcher `sauti_ffmpeg_qmf_synth_trace.py` może być
zastosowany wyłącznie do jawnie podanego, jednorazowego checkoutu FFmpeg; przy
włączonych **obu** zmiennych `SAUTI_SBR57_SYNTH_X` i
`SAUTI_SBR57_SYNTH_PCM` zapisuje dla pełnego trybu float 32 sloty wejściowych
współczynników `(re, im)` i odpowiadające im 2048 próbek PCM. Brak jednej ze
ścieżek, profil niepełny albo tryb downsampled kończy program błędem — bez
cichego częściowego pomiaru. Log zawiera także numer wywołania i adres stanu
`v0`, aby dane kanałów/kontekstów nie zostały przypadkowo połączone.

`sauti_sbr57_trace_select.py` wybiera wyłącznie całe rekordy o określonym
starcie i kroku, po uprzedniej kontroli geometrii strumienia. Jest to wymagane
dla stereo: każdy kanał ma własną historię syntezy i musi zostać przekazany do
eksportera jako osobny, ciągły strumień — nie wolno przeplatać rekordów dwóch
adresów `v0`.

Eksporter dzieli wartości referencyjne przez jawną skalę wejściową (dla
FFmpeg float oczekiwane `32768`) i zachowuje własny stan 10 bloków Sauti.
`sauti_sbr57_synth_audit.py` oblicza tylko pojedynczy gain, residual i
maksymalną różnicę — nie wprowadza kompensacji do dekodera. Na tym etapie
przeszły wyłącznie testy własne oraz kompilacja narzędzi; pomiar z
instrumentowanego checkoutu i wynik zgodności dynamicznej są jeszcze otwarte.

### SBR-58 — rozdzielenie modulacji od okna syntezy (2026-11-06)

Pierwszy pomiar SBR-57 na rzeczywistym `he_test.aac` zachował geometrię
102 rekordów. Dwa początkowe adresy stanu były jednorazowe; od rekordu 2
następowały naprzemiennie dwa stabilne adresy stereo. Po wybraniu wyłącznie
parzystego kanału (`start=2`, `stride=2`) otrzymano 50 pełnych ramek, ale
porównanie całego toru dało `gain=0.689597073`, `residual=0.716026104`.
To jest wynik diagnostyczny, który **wyklucza** uznanie dynamicznej syntezy
za zgodną.

Dlatego dodano SBR-58: fail-closed patcher zapisuje bezpośrednio 32 bloki
`V[128]` po deinterleave referencyjnego modulatora i przed oknem
polifazowym, a osobny eksporter Sauti zapisuje 32 bloki z
`sauti_aac_sbr_qmf_synth_modulate_64`. Pomiar ma wskazać, czy przyczyną jest
konwencja `(re, im)`/faza modulacji, czy późniejsze okno i historia. Kod
produkcyjny nie został na tym etapie zmieniony.

#### SBR-58 — znaleziona i zweryfikowana korekta modulacji

Ślad `V` wykazał, że błąd jest w SBR-26, przed oknem: dotychczasowe
`phase = π*(k+0.5)*(2*n-63)/128` przesuwało modulację o 32 sloty fazowe.
Referencja używa dokładnie:

`phase = π*(k+0.5)*(2*n+1)/128`

`V[n] = -(1/64) * Σ(Re(X[k])*cos(phase) - Im(X[k])*sin(phase))`.

Zmieniono tę postać w implementacji, interfejsie i niezależnej referencji
testu; zaktualizowano również wektory golden oraz symetrię wokół `n=63.5`.
To jest zmiana normatywnej konwencji modulacji, nie dopasowanie gainu ani
korekcja zależna od ramki.

Na 50 ciągłych ramach **każdego** kanału rzeczywistego `he_test.aac`
(kanały: rekordy `2,4,...,100` i `3,5,...,101`; każdy ma osobny stan)
pomiar po korekcie dał w obu przypadkach:

```text
V:   gain(FFmpeg->Sauti)=0.99999998  residual=8.29265846e-08
PCM: gain(FFmpeg->Sauti)=0.99999998  residual=8.31454214e-08
```

Maksymalne różnice po pojedynczym gainie wyniosły odpowiednio
`2.49718017e-08` i `3.50104732e-08`. Wraz z SBR-56 (identyczna tabela
640 współczynników) zamyka to dynamiczną walidację **64-pasmowej syntezy QMF
dla tej referencji float i tego wejścia**. Nie oznacza jeszcze zakończenia
całego dekodera HE-AAC: parser SBR, HF generation/adjustment, połączenie z
rdzeniem AAC oraz testy wielostrumieniowe pozostają osobnymi etapami.

#### SBR-58 — sprawdzenie ścieżki używanej przez dekoder

Korekta nie jest ograniczona do eksportera: normalna droga
`sauti_aac_he_adts_qmf_decode_ex` wywołuje
`sauti_aac_sbr_qmf_frame_process`, a ten używa
`sauti_aac_sbr_qmf_synth_64`. Pełne `make test-aac-native
PKG_CONFIG=/bin/false` przeszło po zmianie, w tym SBR-26, SBR-28, SBR-31 i
ADTS-QMF.

`./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50` nadal uczciwie
zatrzymuje właściwy dekoder po dwóch ramach na nieobsługiwanym wariancie
`EXT_SBR_DATA`; nie wstawia ciszy. Jego niezależna diagnostyka przeprowadza
jednak 50 ramek ciągłego rdzenia/temporalnego QMF i tworzy skończony preview
64-QMF. To potwierdza użycie poprawionej syntezy w istniejącej ścieżce
diagnostycznej, ale nie jest deklaracją dekodowania tego pliku do zgodnego
końcowego PCM HE-AAC.

### SBR-59 — rozdzielenie poprawnego LOW od niezweryfikowanego HF (2026-11-06)

Po SBR-58 wykonano pomiar end-to-end na 50 ramach `he_test.aac`: temporalny
parser i diagnostyczny 64-QMF przechodzą pełny ciąg, a FFmpeg dostarcza
niezależne mono/48 kHz `f32le`. Surowy preview SBR-54a nadal nie jest zbieżny
z referencją (`corr=0.099916`, `lag=-4896`, błąd po najlepszej skali
`99.645%`).

Wariant SBR-54i `units` (dzielenie gotowego aktywnego HF przez `32768`) dał
pozornie bardzo dobry wynik całego przebiegu: `lag=384`, `corr=1.000000`,
`gain=1.4142129` oraz błąd `0.043%`. Nie został jednak uznany za poprawkę.
Eksport SBR-54c rozdzielił bowiem ten sam przebieg na trzy syntezy QMF:

```text
LOW:   corr=1.000000, lag=384, gain=1.4142131, relative error=0.034%
HF:    corr=-0.000040 względem pełnej referencji
noise: corr= 0.000011 względem pełnej referencji
```

Oznacza to, że dopasowanie `units` było zdominowane przez już zgodny tor LOW,
nie że HF stał się zgodny. To ważne rozróżnienie: globalna korelacja pełnego
PCM nie może być używana jako walidacja wysokich pasm, gdy ich energia jest
mała względem sygnału podstawowego.

Rozszerzono `tools/sauti_he_aac_compare_pcm.py` o tryb fail-closed:

```text
python3 tools/sauti_he_aac_compare_pcm.py --sbr59-audit LOW.f32le UNITS.f32le REF.f32le
```

Najpierw ustala on opóźnienie i pojedynczą skalę **wyłącznie** na LOW, a
potem porównuje `scale * (units - low)` z resztą referencji po odjęciu LOW.
Ma własny syntetyczny test, który uniemożliwia pomylenie korelacji całego
sygnału z korelacją kandydackiego HF. Narzędzie, SBR-54i i wszystkie wyniki
z tego akapitu pozostają diagnostyką; nie zmieniono ścieżki produkcyjnej,
dekwantyzacji energii ani skali publicznego PCM.

Na tym samym eksporcie właściwy audyt rozdzielony podał:

```text
reszta referencji po LOW: RMS=4.0628541e-05
kandydat HF units:        RMS=4.1687096e-05
korelacja tylko HF:       0.216312
błąd tylko HF RMS:        5.1534094e-05
```

Skale RMS są przypadkowo podobne, ale korelacja wyklucza traktowanie ich jako
tego samego sygnału. SBR-54i pozostaje odrzuconym A/B; nie wolno włączać go
w dekoderze ani kompensować tym wynikiem końcowego PCM.

Następny etap produkcyjny musi ustanowić jedną konwencję jednostek dla
źródła QMF, energii obwiedni, gainu, szumu i sinusoidy **przed** ich złożeniem
i syntezą. Dopiero osobny ślad HF wobec referencji może uzasadnić przeniesienie
tej konwencji poza diagnostykę.

### SBR-60 — jawna granica energii QMF w driverze temporalnym (2026-11-06)

SBR-59 wskazał konkretną niespójność: `sauti_aac_sbr_energy_dequant` zachowuje
wartości energii obwiedni w konwencji referencyjnego QMF, podczas gdy
`sauti_aac_sbr_temporal_qmf_prepare` analizuje znormalizowane próbki float.
Porównanie energii bez konwersji mieszało więc `E_ref` z `E_ref / 32768²`.

Dodano transakcyjny adapter
`sauti_aac_sbr_temporal_payload_normalize_qmf_energy`:

```text
E_unit = E_reference / 2^30
Q_noise = Q_noise          (stosunek bez wymiaru)
```

Nie zmienia on parsera, jego stanu, dekwantyzacji ani publicznego dekodera.
Wykorzystuje go wyłącznie `sauti_he_aac_probe`, bezpośrednio po udanym
parsowaniu side-data i przed zbudowaniem historii temporalnej. Test jednostkowy
sprawdza dokładne `1 -> 1`, `2^29 -> 0.5`, pozostawienie `Q` bez zmian oraz
atomowość przy `NaN` i niepoprawnym `Q`; jest częścią `test-aac-native`.

Na 50 ramach tego samego wejścia poprawka zmieniła diagnostyczny budżet z
`HF+noise/target=0.909652518` na `0.994497015` i RMS surowego preview z
`0.977584752` na `0.0852400952`; zniknęło więc zawyżenie energii o `2^30`.
Wynik pełnego przebiegu nadal ma tylko diagnostyczną zgodność LOW
(`corr=1.000000`, `lag=384`, skala około `sqrt(2)`). Audyt reszty po LOW dla
nowego preview podaje korelację HF `0.252289` i błąd RMS `5.1936747e-05`.
To jest postęp w określeniu jednostek, **nie** potwierdzenie normatywnego HF,
noise, sine, smoothing ani PCM HE-AAC.

Po dodaniu adaptera przeszedł pełny `make test-aac-native
PKG_CONFIG=/bin/false`, w tym SBR-26/27/28/31, parser temporalny SBR-44 i
nowy test SBR-60. Test ten nie korzysta z zewnętrznego kodeka.

### SBR-61 — per-frame HF+noise residual audit with a LOW anchor (2026-11-06)

Added `tools/sauti_sbr61_hf_frame_audit.py` (standalone standard-library
Python tool, diagnostic only) and `make test-aac-sbr-hf-frame-audit`;
its synthetic self-test is also a prerequisite of `test-aac-native`.

**Question.** SBR-60 improved energy-domain consistency but the HF-only
correlation with the reference remains low (~0.2523). A single whole-file
metric cannot reveal whether the mismatch is persistent, localized to
particular frames or caused by almost silent reference residuals.

**Method.** Read the SBR-54c LOW-only PCM, SBR-60 full diagnostic
preview, and independent FFmpeg PCM as mono float32-le. Determine lag,
gain and DC *only from LOW versus reference* using the existing
`sauti_he_aac_compare_pcm.py` comparison; refuse if LOW correlation
is below 0.98. Keep this anchor fixed for each complete 2048-sample
frame (skip first two frames by default). Calculate:
- reference residual = reference[frame + LOW_lag] - (LOW * LOW_gain + LOW_DC)
- candidate HF+noise = (full diagnostic preview - LOW) * LOW_gain
- per-frame reference/candidate RMS, HF-residual correlation and relative
  RMS error, plus whole-run metrics over valid frames.

Very weak reference residual frames are explicitly marked
`weak-reference`; a zero-variance candidate has undefined correlation,
not a false PASS. No HF-specific gain, phase, offset or lag is tuned.
The tool lists the eight worst-correlated measurable frames by default,
or all frames with `--all-frames`. NaN/Inf, incomplete frames and poor
LOW alignment are rejected.

**Run on Fedora with the user's real fixture:**

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-frame-audit PKG_CONFIG=/bin/false
make probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 \
  /tmp/sbr61.preview.f32le /tmp/sbr61
ffmpeg -hide_banner -loglevel error -y \
  -i ~/Sauti-fixtures/he_test.aac -vn -ac 1 -ar 48000 \
  -f f32le /tmp/sbr61.reference.f32le
python3 tools/sauti_sbr61_hf_frame_audit.py \
  --low /tmp/sbr61.low.f32le \
  --candidate /tmp/sbr61.preview.f32le \
  --reference /tmp/sbr61.reference.f32le --all-frames
```

**Evidence so far.** The tool passed a synthetic self-test with
perfect HF, a wrong-HF-phase control despite a matching LOW carrier,
one reference-silent frame, invalid geometry and NaN rejection;
`python3 -m py_compile` passed. The real `he_test.aac` run and
the complete native test gate were **not run in this change**.
Do not interpret LOW-subtracted full FFmpeg PCM as an isolated normative
HF stem: it also contains noise and imperfectly modeled LOW.
No production decode, dequantization, gain, noise or public PCM was modified.

 
### SBR-62 — HF/noise stem attribution, based on the real SBR-61 run (2026-11-06)

Fedora `he_test.aac` / 50-frame SBR-61 result supplied after `97a19e6`:
- Self-test: PASS; all 50 native-core / diagnostic temporal-SBR frames exported, while the strict public HE-AAC decoder **still rejects frame 2** (`unsupported_fil`); this is NOT a completed HE-AAC decode.
- LOW anchor: lag 384 samples; correlation 0.9999999; scale 1.41421308.
- Reference residual RMS 3.19625123e-05; preview HF+noise RMS 3.15371274e-05.
- 47 measurable complete frames (2..48), none too weak; combined HF+noise correlation 0.3926397, relative RMS error 1.094866.
- Frame 2: correlation 0.11107, error 1.33028; frames 3..46: mean per-frame correlation approximately 0.7093, in a stable 3-frame pattern (normally 0.705..0.718), relative error about 0.754; frames 47..48: 0.69115 and 0.63071. The repeating 3-frame pattern may come from the periodic test input, **not** necessarily from a 3-frame decoder bug.
- Interpretation: LOW is aligned; the signal mismatch persists in the steady portion, although reference and candidate residual RMS are similar. The frame-2 transient disproportionately affects the whole-file correlation, so **do not replace per-frame metrics with one global figure**.

Added `tools/sauti_sbr62_hf_stem_audit.py` to isolate diagnostic SBR-54c `hf` and `noise` PCM stems relative to the **same** FFmpeg-minus-LOW residual. It uses the fixed, independent LOW alignment/gain/bias (no HF-specific tuning), reports `HF`, `noise` and `HF+noise` correlation and relative errors, and rejects mismatched stem files using PCM reconstruction closure: `preview ≈ low + hf + noise`. Its synthetic self-test covers valid reconstruction, a deliberately changed noise stem, corrupted closure, malformed geometry and NaN; `make test-aac-sbr-hf-stem-audit` runs it, also added to `test-aac-native`.

**Run next on Fedora (no need to decode/re-export; all four components are already in /tmp from SBR-61):**

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-stem-audit PKG_CONFIG=/bin/false
python3 tools/sauti_sbr62_hf_stem_audit.py \
  --low /tmp/sbr61.low.f32le \
  --hf /tmp/sbr61.hf.f32le \
  --noise /tmp/sbr61.noise.f32le \
  --preview /tmp/sbr61.preview.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --first-frame 3 --last-frame 46 --all-frames | tee /tmp/sbr62.log
```

This excludes the onset and tail to diagnose the 44 stable frames separately.
The SBR-62 real-fixture measurement was subsequently received (see SBR-63 below). No production gain, QMF,
parser, HF generation or PCM format was changed. The reference residual
is *not* a clean FFmpeg HF stem: it contains LOW-model residual and other effects.

### SBR-63 — frequency-resolved HF residual comparison (2026-11-06)

**SBR-62 results received from Fedora, `he_test.aac` (44 stable frames 3..46):**
- `make test-aac-sbr-hf-stem-audit PKG_CONFIG=/bin/false`: PASS.
- LOW anchor `lag=384`, `corr=0.99999994`, `gain=1.41421308`, bias `3.17e-10`.
- Component closure RMS `3.3482038e-09` against tolerance `8.5240095e-06`: the three stems reconstruct the exported preview.
- FFmpeg-minus-LOW reference residual RMS `2.2349621e-05`; 44 measurable frames, none marked weak.
- HF alone: correlation `0.7094658`, RMS `2.1832996e-05`, relative error `0.753771`.
- Noise alone: correlation `-0.0008203`, RMS `3.7760634e-08`, relative error `1.000003`.
- HF+noise: correlation `0.7094623`, RMS `2.1833062e-05`, relative error `0.753777`.
- Thus current output noise is about 578 times weaker in RMS than HF, and does not materially change the current reference-residual mismatch. **This does not demonstrate that noise generation itself is correct**: it could be absent or underrepresented. The FFmpeg-minus-LOW residual is not an isolated reference HF stem.

**Next discriminating test:** A uniform 0.709 HF correlation could still conceal much better agreement in some frequency regions and worse agreement in others. Added `tools/sauti_sbr63_hf_spectral_audit.py` and `make test-aac-sbr-hf-spectral-audit` (also part of `test-aac-native`). It computes independent 2048-point Hann-windowed FFTs over the same stable frames of the HF stem and the anchored FFmpeg-minus-LOW reference residual, with eight 3-kHz bands covering 0..24 kHz. All bins use the same LOW-derived lag/gain/bias; **there is no HF-specific alignment or phase/gain fitting**. Results are signed spectral coherence, magnitude coherence, mean phase, amplitude ratio, relative spectral error, and each band's share of reference-residual energy. Weak-energy bands are flagged inconclusive, rather than being assigned a PASS.

The new synthetic self-test passes on two sinusoidal bands with exact agreement, then intentionally reverses the upper band's sign; it detects correct agreement below 9 kHz and negative coherence in 15..18 kHz, and rejects invalid frame ranges. This is **not** a HE-AAC standards conformance test, and does not isolate normative FFmpeg-generated HF.

Run on Fedora without re-exporting the AAC fixture:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-spectral-audit PKG_CONFIG=/bin/false
python3 tools/sauti_sbr63_hf_spectral_audit.py \
  --low /tmp/sbr61.low.f32le \
  --hf /tmp/sbr61.hf.f32le \
  --noise /tmp/sbr61.noise.f32le \
  --preview /tmp/sbr61.preview.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --first-frame 3 --last-frame 46 | tee /tmp/sbr63.log
```

**Pending:** real SBR-63 spectral results from the user's fixture; full `make test-aac-native` gate has not been rerun after the new audit was added. No production HE-AAC decoder, AAC-LC, QMF or PCM path was changed.

### SBR-64 — detailed 375 Hz HF edge audit after real SBR-63 (2026-11-06)

**Real SBR-63 measurement received on Fedora**: `make test-aac-sbr-hf-spectral-audit PKG_CONFIG=/bin/false` PASS; 44 steady frames (3..46), LOW anchor `lag=384`, `corr=0.99999994`, `gain=1.41421308`; PCM component closure `3.3482038e-09`.

| Band (Hz) | Share of FFmpeg-minus-LOW residual | HF/reference amplitude | Signed complex-spectrum agreement | Relative spectral error |
|---|---:|---:|---:|---:|
| 0–3000 | below threshold | 0.139500 | -0.011539 | inconclusive |
| 3000–6000 | below threshold | 0.198972 | 0.434824 | inconclusive |
| 6000–9000 | 0.173652 | 1.024643 | 0.186436 | 1.291445 |
| 9000–12000 | 0.322498 | 1.000991 | 0.998551 | 0.053874 |
| 12000–15000 | 0.406050 | 0.853688 | 0.995341 | 0.171353 |
| 15000–18000 | 0.097798 | 1.300623 | 0.003015 | 1.638224 |
| 18000–24000 | below threshold | — | — | inconclusive |

**Interpretation, not yet a root-cause claim**: 72.85% of reference-residual energy lies in 9–15 kHz where spectral agreement is high, although the 12–15 kHz amplitude is lower (0.854×). The 6–9 kHz and 15–18 kHz edge ranges account for most of the remaining spectral mismatch. Because complex coherence is near zero at the top edge, its reported average phase (4.79°) is not a meaningful basis for phase correction. The synthesized HF is also a different signal from an isolated normative FFmpeg HF: FFmpeg-minus-LOW includes noise and LOW reconstruction error.

**New test (diagnostic only)**: `tools/sauti_sbr64_hf_edge_audit.py`, `make test-aac-sbr-hf-edge-audit` (also included in `test-aac-native`). Uses the exact same SBR-61 exported components and fixed LOW-derived lag/gain/bias as SBR-63. Splits the Hann-windowed FFT into 64 buckets, each 375 Hz wide (equivalent frequency width to a 64-channel QMF band at 48 kHz, **not** a direct measurement of complex QMF subband outputs). Reports reference and candidate spectral-energy fractions, signed coherence, amplitude ratio, relative spectral error and the band's absolute contribution to error power. Strong unexplained candidate-only bands receive `EXTRA_CANDIDATE`, not `PASS`. Default output includes every bucket at 6–9 and 15–18 kHz plus the ten largest error contributors; `--all-bands` displays all 64.

Synthetic test built into the new script: exact three-tone reference at QMF-width bucket centers, intentional inversion of the 16.5–17 kHz component, extra unreferenced HF tone near 19.5 kHz, invalid frame selection. **No real SBR-64 measurement or full AAC native gate has been run yet**.

Run on Fedora (existing /tmp/sbr61 files suffice):

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-edge-audit PKG_CONFIG=/bin/false
python3 tools/sauti_sbr64_hf_edge_audit.py \
  --low /tmp/sbr61.low.f32le \
  --hf /tmp/sbr61.hf.f32le \
  --noise /tmp/sbr61.noise.f32le \
  --preview /tmp/sbr61.preview.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --first-frame 3 --last-frame 46 | tee /tmp/sbr64.log
```

No changes to AAC-LC, SBR parser, gain, HF transpose, QMF synthesis, or final PCM.

### SBR-65 — isolate wrong HF tone frequencies after the real SBR-64 edge report (2026-11-06)

**Fedora result for SBR-64 (44 stable frames 3..46)**: `make test-aac-sbr-hf-edge-audit PKG_CONFIG=/bin/false` PASS. Fixed LOW alignment `lag=384`, `corr=0.99999994`, `gain=1.41421308`. The three Sauti component stems still reconstruct the full preview (closure RMS `3.34820381e-09`).

With FFT buckets 375 Hz wide (not complex QMF traces), the crucial observations are:
- 6750–7125 Hz: reference energy share 0.14573, candidate share 0.15550, amplitude ratio 1.03298, signed coherence **0.03917**, relative error 1.40930, absolute error share 0.289435. Similar RMS but almost no waveform agreement: a phase or frequency mismatch is plausible, not proven.
- 8625–9000 Hz: share 0.02785, candidate 0.02675, coherence 0.99681: the entire 6–9 kHz range is **not** uniformly wrong.
- 12.375–15 kHz: high coherence (`0.99195`–`0.99913`) with amplitude ratios roughly 0.82–0.85. Good waveform agreement but some under-level.
- 15750–16125 Hz: reference share 0.09779, candidate 0.00164, amplitude ratio 0.12939, coherence -0.00248. An expected reference feature is essentially absent from that output bucket.
- Adjacent candidate-only spectral energy: 15375–15750 Hz (candidate share 0.08805), 16125–16500 Hz (0.04095), 16500–16875 Hz (0.03090); plus 15000–15375 Hz (0.00351). This could be a shifted/dispersed tone or an error in HF generation, but remains a hypothesis. The very small reference energy in these buckets makes the large `HF/ref` amplitude ratios unsuitable for direct gain corrections.
- The large error buckets 6750–7125 (`0.289435`), 15375–15750 (`0.087625`), 15750–16125 (`0.099488`), 16125–16500 (`0.040749`), 16500–16875 (`0.030755`) together account for approximately **0.548 of the full reference residual's power** in SBR-64's reported normalization. This concentrates the investigation on a handful of frequency components.

**Next test**: added `tools/sauti_sbr65_hf_peak_audit.py` and Make target `test-aac-sbr-hf-peak-audit` (also part of `test-aac-native`). Like SBR-63/64 it reads the existing LOW, HF, noise, full-preview and independent FFmpeg f32le files; uses *only* the LOW-derived lag, gain and bias; checks additive PCM closure and independently aggregates Hann-windowed FFT-bin power across the stable 44 frames. Frequency resolution is 48000/2048 = **23.4375 Hz**. For the 6–9 kHz and 15–18 kHz regions it reports the four strongest distinct peaks in reference and Sauti HF, dominant peak displacement, candidate power at the reference peak, and exact reference-bin signed coherence. No scale, phase or lag optimization is permitted on HF.

The script includes a self-test using two exact bin-centered tones, then shifts both candidate peaks by a known number of bins; verifies the measured displacement, the exact-match control and rejection of malformed frame selection and NaN. **Real SBR-65 output and full native gate have not yet been run**; do not claim a production decoder fix from SBR-64's PCM-spectrum evidence alone.

Run on Fedora using the existing files:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-peak-audit PKG_CONFIG=/bin/false
python3 tools/sauti_sbr65_hf_peak_audit.py \
  --low /tmp/sbr61.low.f32le \
  --hf /tmp/sbr61.hf.f32le \
  --noise /tmp/sbr61.noise.f32le \
  --preview /tmp/sbr61.preview.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --first-frame 3 --last-frame 46 | tee /tmp/sbr65.log
```

The next substantive decoder change, if warranted, should be tested against an independently captured complex QMF HF reference before modifying patch mapping, transposition or harmonic reconstruction. **No production AAC-LC, SBR, QMF, HF or PCM code was changed in SBR-65.**


### SBR-66 — test the already-existing harmonic preview against exact HF tones (2026-11-06)

**Real SBR-65 result from Fedora:** `make test-aac-sbr-hf-peak-audit PKG_CONFIG=/bin/false` PASS. Forty-four steady frames (3..46), LOW `lag=384`, `corr=0.99999994`, `gain=1.4142131`, closure RMS `3.34820381e-09`. Relative to the *FFmpeg-minus-Sauti-LOW* residual:
- 6–9 kHz reference peak **6937.50 Hz** (55.83% of band power), Sauti HF peak **7007.81 Hz** (49.20% of its band power), shift **+70.31 Hz**. Candidate power in the exact reference peak bin only **0.01628% of candidate 6–9 kHz band power**; signed coherence in that bin 0.021344.
- 15–18 kHz reference peak **15937.50 Hz** (66.62% of band power), Sauti HF strongest peaks **15492.19 Hz** (29.06%), **16500.00 Hz** (15.30%), **16242.19 Hz** (10.39%). Dominant shift **-445.31 Hz**, candidate power at reference bin only **0.00294% of candidate 15–18 kHz power**, signed coherence -0.054003.
- The two strong reference peaks are exactly the nominal centers of 64-channel QMF bands **k=18** (`18.5*375=6937.5 Hz`) and **k=42** (`42.5*375=15937.5 Hz`) at 48 kHz. This **raises**, but does not prove, the possibility of missing or misplaced SBR harmonic sinusoids (`add_harmonic`); QMF patch mapping, phase, leakage and the residual reference definition also remain possible explanations.
- The reference is a mixture: FFmpeg PCM minus the synthesized Sauti LOW. It is not a clean standalone FFmpeg HF stem. FFT peak shifts alone cannot justify editing the normative QMF patch map or decoder phase.

**Next experiment without any re-encoding/re-decoding**: Sauti probe already exported separate 50-frame PCM previews from the SAME `he_test.aac` run: raw SBR-54a preview, SBR-54h harmonic/sine preview, SBR-54i amplitude-domain A/B, SBR-54m G-only A/B, and SBR-54q imaginary-conjugation A/B. Added `tools/sauti_sbr66_tonal_ab_audit.py` with `make test-aac-sbr-tonal-ab-audit` (also a `test-aac-native` dependency). It uses the same LOW-derived lag, gain and DC for all variants, and compares *each candidate minus LOW* to the exact same FFmpeg-minus-LOW reference residual over frames 3..46.

The new test specifically measures at exact FFT-bin centers 6937.5 Hz and 15937.5 Hz and checks the following bands: 6750–7125, 8625–9000 (working-band control), 12000–15000 (working-band control), and 15375–16875 Hz. For each A/B candidate it prints amplitudes, signed spectral agreement, relative errors, and change in error vs untouched baseline. No candidate-specific gain/lag/phase/frequency search is allowed. Synthetic self-test covers two exact QMF-center tones, known displacements, a corrected A/B path, malformed sizes and NaN. **SBR-66 fixture output and full AAC gate still need Fedora execution.**

Run:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-tonal-ab-audit PKG_CONFIG=/bin/false
python3 tools/sauti_sbr66_tonal_ab_audit.py \
  --low /tmp/sbr61.low.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --baseline /tmp/sbr61.preview.f32le \
  --sine /tmp/sbr61.preview.f32le.sine.f32le \
  --units /tmp/sbr61.preview.f32le.units.f32le \
  --gonly /tmp/sbr61.preview.f32le.refhf.f32le \
  --conj /tmp/sbr61.preview.f32le.refhfconj.f32le \
  --first-frame 3 --last-frame 46 | tee /tmp/sbr66.log
```

**Interpretation rule**: A/B result can identify which pre-existing experimental processing path helps or hurts the 6937.5/15937.5 Hz residual. It cannot make that path normative, nor establish why any FFmpeg/Sauti peaks differ. The subsequent justified engineering step is to compare complex HF QMF from a known single FFmpeg context against Sauti at bands k=18/k=42 (including `add_harmonic` and per-envelope owner). No production AAC-LC, SBR, synthesis QMF or PCM code was changed.


### SBR-66 hotfix — spectral accumulator bounds (2026-11-06)

Real Fedora invocation stopped before any A/B result, both in self-test and in real-fixture evaluation, with `IndexError` at the reference power array (the initial implementation iterated `enumerate(ref_f)`). This is an implementation bug in the NEW SBR-66 Python diagnostic, **not a failure of the decoder, AAC-LC, SBR, or QMF**.

Root cause: `sauti_sbr63_hf_spectral_audit.fft()` returns `FRAME=2048` complex bins, whereas SBR-66 allocates only `FRAME//2+1=1025` nonnegative-frequency accumulators (the correct geometry for real-valued PCM). Fixed the reference and variant accumulation loops to iterate only `range(FRAME//2+1)`. Commit `6cd1bcd5342065e3bddcd8b12debed96ebea5f12`.

The earlier SBR-66 output contains **no real results** and must not be interpreted as failure of any HF A/B hypothesis. An independent 2048/1025 index-bounds smoke check passed; **the complete SBR-66 Python self-test and the real recording have not yet been rerun after this patch**.

Next: `git pull --ff-only origin feature/aac-family-asm && make test-aac-sbr-tonal-ab-audit PKG_CONFIG=/bin/false`, followed by the same SBR-66 command from the preceding section. No production audio code was touched.


### SBR-67 — harmonic-preview sign control, derived from SBR-66 real data (2026-11-06)

**SBR-66 Fedora result**: fixed tool now passes `make test-aac-sbr-tonal-ab-audit PKG_CONFIG=/bin/false` and the 44-frame real fixture at LOW lag=384, LOW correlation=0.99999994, gain=1.41421308. The previous IndexError was confined to the SBR-66 Python FFT diagnostic; no AAC production code failed.

**Critical observation at exact nominal QMF-band centers:**
- k=18 / 6937.5 Hz: baseline HF amplitude ratio `0.017490`, signed coherence `+0.021698`, reference-bin relative error `0.999773`; experimental SBR-54h `sine` preview produces amplitude ratio **0.630838**, signed coherence **-0.999597**, relative error `1.630682`.
- k=42 / 15937.5 Hz: baseline HF amplitude ratio `0.008640`, signed coherence `-0.048676`, relative error `1.000458`; SBR-54h `sine` amplitude ratio **0.770049**, signed coherence **-0.999689**, relative error `1.769914`.
- In 8625–9000 Hz and 12000–15000 Hz, both baseline and `sine` previews retain high agreement with the FFmpeg-minus-LOW reference (about 0.9968 and 0.9953) and essentially unchanged errors. The harmonic preview worsens 6750–7125 Hz and 15375–16875 Hz. The `units`, `gonly` and `conj` experimental paths have near-zero amplitude at these exact tone centers, so their apparently smaller *numerical errors* near 1.0 are NOT a successful reconstruction.

**Interpretation:** Both reference peaks were missing in baseline, while `sine` puts a strong component *at the correct frequency* but approximately 180 degrees out of phase. This is a strong phase/sign hypothesis, **not proof of a normative oscillator sign bug**: the SBR-54h preview is explicitly nonnormative and changes both tonal addition and ROM-noise gating. The baseline subtraction uses Sauti LOW; its residual against FFmpeg also contains LOW differences and reference noise. FFmpeg's `sbr_hf_assemble` uses a per-slot `indexsine` rotating modulo four with subband parity: https://ffmpeg.org/doxygen/trunk/aacsbr_8c_source.html ; a sign change must be checked against independently captured complex QMF and slot alignment before a production patch.

**SBR-67 controlled A/B:** Added `tools/sauti_sbr67_tonal_delta_ab.py` with target `test-aac-sbr-tonal-delta-audit`, wired into `test-aac-native`. The fixed, no-fit hypothesis is `delta = sine_preview - baseline_preview`; `inverse_delta = baseline_preview - delta = 2*baseline - sine_preview`. Compare baseline, actual sine, and inverted-delta against the SAME FFmpeg-minus-LOW reference using the already verified SBR-66 per-tone and per-band analysis. This does not invert ONLY the sinusoid: it also reverses the effect of SBR-54h noise gating, so it must NOT be applied directly to the C implementation regardless of the result. Synthetic self-test checks a deliberately anti-phase two-tone preview at k18/k42, sign-inverted reconstruction, malformed PCM, NaN and missing delta. The real Fedora test and complete native AAC gate **have not yet been executed**.

Run with existing SBR-61 files, no re-export required:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-tonal-delta-audit PKG_CONFIG=/bin/false
python3 tools/sauti_sbr67_tonal_delta_ab.py \
  --low /tmp/sbr61.low.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --baseline /tmp/sbr61.preview.f32le \
  --sine /tmp/sbr61.preview.f32le.sine.f32le \
  --first-frame 3 --last-frame 46 | tee /tmp/sbr67.log
```

**After the test:** If inversion substantially improves both k18 and k42 without harming control bands, investigate SBR oscillator phase-index / delay (especially source-to-synthesis timing and FIXVAR boundaries) by exporting reference complex QMF at the same emitted slots. Do not do a blanket oscillator or final PCM sign flip, or tune amplitude at isolated FFT bins. If inversion does not help, split the effect of tonal addition from noise removal with a QMF-only A/B branch.

No production AAC-LC, SBR parser, HFG, QMF analysis/synthesis, phase state, or output PCM was modified.


### SBR-68 — test only the two spectral lines of the tonal-preview delta (2026-11-06)

**Real SBR-67 Fedora results**: `make test-aac-sbr-tonal-delta-audit PKG_CONFIG=/bin/false`: PASS. Forty-four stable frames, LOW anchor lag=384, corr=0.99999994, gain=1.41421308. `sine - baseline` experimental PCM difference RMS=7.95278956e-06.

| Target QMF-band center | baseline: correlation / relative error | original SBR-54h sine: correlation / error | inverse *entire* delta: correlation / error |
|---|---|---|---|
| k=18, 6937.5 Hz | +0.021698 / 0.999773 | −0.999597 / 1.630682 | **+0.999635 / 0.369052** |
| k=42, 15937.5 Hz | −0.048676 / 1.000458 | −0.999689 / 1.769914 | **+0.999239 / 0.232973** |

At the wider 6750–7125 Hz and 15375–16875 Hz bands, errors remain high even after inverse-delta: **1.059688** and **1.303162**, respectively. This shows that the two lines explain substantial but not all differences. **Do not interpret the result as a normative sign fix**; `inverse_delta = baseline - (sine-baseline)` reverses both experimentally synthesized tones and noise-gating differences.

**Next step:** new `tools/sauti_sbr68_target_lines_ab.py` with Make target `test-aac-sbr-tonal-lines-audit` (also a prerequisite of `test-aac-native`). Project the 2048-sample *sine-minus-baseline* PCM difference per frame onto just two previously identified, exact Fourier lines: FFT bin **296** (=6937.5 Hz) and bin **680** (=15937.5 Hz) at 48 kHz. The projection uses ONLY Sauti PCM; the reference FFmpeg PCM is never used to choose coefficients or fit a phase, gain, frequency or lag. Parseval/orthogonality closure is checked to reject numerical inconsistencies.

Compare unchanged baseline, unchanged sine, inverted whole delta, inverted TWO lines but preserving the remaining experimental difference (`sine-2*line`), inverted two lines only (`baseline-line`), and remaining delta only (`sine-line`). These variants disentangle how much of SBR-67's benefit comes from the two strong tones versus other Fourier components. They are **NOT** physically isolated normatively generated harmonic sinusoids: projection includes any noise/leakage at the exact frequencies and discards sidebands, so this is an A/B PCM diagnostic only.

Self-test: intentionally wrong-phase tones at the two bins plus an unrelated independent tone, source-blind projection, orthogonal energy closure, nonfinite PCM, zero delta and malformed frame geometry. The synthetic test and Python syntax compilation passed outside the actual Fedora fixture; **real SBR-68 results, the GitHub/Fedora self-test and complete native AAC gate are pending**. No SBR production code was edited.

Run on Fedora (reuses existing `/tmp/sbr61*` files):

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-tonal-lines-audit PKG_CONFIG=/bin/false
python3 tools/sauti_sbr68_target_lines_ab.py \
  --low /tmp/sbr61.low.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --baseline /tmp/sbr61.preview.f32le \
  --sine /tmp/sbr61.preview.f32le.sine.f32le \
  --first-frame 3 --last-frame 46 | tee /tmp/sbr68.log
```

After SBR-68, moving closer to a genuine decoding fix requires a *complex QMF-level* phase-index and sinusoid-versus-noise split against a verified FFmpeg reference. Do not flip the production oscillator's sign based on PCM projections alone.


### SBR-69 — isolate the 180° harmonic oscillator hypothesis in complex QMF (2026-11-06)

**SBR-68 real Fedora result** (`he_test.aac`, 44 stable frames 3..46): `make test-aac-sbr-tonal-lines-audit PKG_CONFIG=/bin/false` PASS. Fixed LOW alignment `lag=384`, `corr=0.99999994`, `gain=1.41421308`. Separating exactly the two preselected 2048-point PCM FFT lines (k=296 / 6937.5 Hz and k=680 / 15937.5 Hz) from the *Sauti sine-minus-baseline* experimental PCM difference explains **52.2980% of that difference's energy**; remaining energy 47.7020%; orthogonality closure `3.03e-14`.

Comparison against the SAME independent FFmpeg-minus-LOW residual:
- Tone 6937.5 Hz: original baseline correlation +0.021698, relative error 0.999773; original experimental sine correlation −0.999597, error 1.630682; **invert only these two lines** correlation **+0.999635**, error **0.369067** (`inverse_whole_delta` error 0.369052).
- Tone 15937.5 Hz: baseline correlation −0.048676, error 1.000458; sine correlation −0.999689, error 1.769914; **invert two lines** correlation **+0.999239**, error **0.233027** (`inverse_whole_delta` error 0.232973).
- Working-band control remains stable: 8625–9000 Hz correlation ~0.996795, error ~0.081745; 12–15 kHz correlation ~0.995335, error ~0.171377.
- The wider erroneous bands still have significant residual errors: 6750–7125 Hz ~1.0597 and 15375–16875 Hz ~1.3032 after two-line inversion. Therefore the SBR tone phase hypothesis is strongly supported at these two lines **but is not a complete HF repair**.

**SBR-69 next test — complex QMF, not PCM manipulation**: added the strictly experimental function `sauti_aac_sbr_temporal_harmonic_preview_phase2()` alongside the existing `sauti_aac_sbr_temporal_harmonic_preview()`. The original SBR-54h ABI and behavior remain accessible through a wrapper calling the same internal implementation with oscillator phase offset `0`. The new variant uses `(phase+2)&3` ONLY when adding each active harmonic's complex tone; it retains the identical `raw - noise_vector*noise_amplitude` term, per-slot owner, `onset_allowed`, transient gating and four-phase oscillator clock. A +2 phase offset produces a sign-reversed QMF harmonic **without reversing the noise gate**, unlike SBR-67/68's PCM delta inversion.

**Synthetic C gate**: `tests/test_aac_sbr_harmonic_phase2_ab.c`, Make target `test-aac-sbr-harmonic-phase2-ab` (also prerequisite of `test-aac-native`). Verifies both even and odd `k` parity, identical oscillator state and report across variants, exactly opposite tone vectors after subtracting the SAME noise gate, cross-frame continuation with no new onset, harmonic flag bypass, corrupted NaN and frame-index rollback. This makes the experiment narrower and more discriminating than SBR-68.

**Pending**: Fedora execution of this new C unit test and the complete AAC native gate. This stage does **not** yet create a real AAC output comparison for `phase2`: it validates the isolated QMF-domain hypothesis against synthetic structures. Next, after this gate passes, wire an *independent* QMF+PCM preview state in `sauti_he_aac_probe` and compare phase0/phase2 from the SAME fixture and FFmpeg, without touching production AAC-LC/SBR decoder.

Run on Fedora:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-harmonic-phase2-ab test-aac-sbr-temporal-harmonic-preview PKG_CONFIG=/bin/false
```

**Important:** This is not a normative HE-AAC oscillator sign fix and must not be wired into production merely because it improves correlation for two selected tones. Independent complex-QMF validation, harmonic flag/phase-index audit, general real fixtures and full native regression are needed first.


### SBR-70 — independent phase0 vs phase+2 PCM/QMF paths on the real ADTS fixture (2026-11-06)

**SBR-69 Fedora results received:** `make test-aac-sbr-harmonic-phase2-ab test-aac-sbr-temporal-harmonic-preview PKG_CONFIG=/bin/false` **PASS**. Both C modules compile with `-O2 -std=c11 -Wall -Wextra -Werror`, and all reported checks pass:
- +2 oscillator phase reverses *only* the added complex QMF harmonic, with identical raw ROM noise subtraction and frame ownership;
- FIXVAR-style continuation, history/persistence, harmonic bypass and failure rollback pass;
- the **unchanged** original SBR-54h delayed temporal harmonic/noise-gate tests also pass.

These are **synthetic component tests**, not evidence of a real-file audio fix.

**SBR-70 code:** Expanded only the diagnostic `tools/sauti_he_aac_probe.c`. It now owns independent `phase2_qmf_state` and `phase2_pcm_state`, separate from `sine_qmf_state` and `sine_pcm_state`. For each emitted HF frame it calls the *new experimental* `sauti_aac_sbr_temporal_harmonic_preview_phase2()` on the exact same raw QMF, noise vectors, SBR owner/onset masks as phase0, compares emitted frame indexes and phase/harmonic history, then renders `phase2_high` using an independent continuous 64-QMF synthesis. The original PCM output and existing `.sine.f32le` continue unchanged. It also runs the **last deferred HF frame through both phase2 QMF and the final PCM flush**. Export is spooled, gated on full `limit` frames, full QMF and PCM counts, a closed synth state and matching frame indexes; partial output is not published. The additional candidate is named `<PCM_PREVIEW>.sinephase2.f32le`.

A new Python comparison, `tools/sauti_sbr70_qmf_phase_ab.py` with Make target `test-aac-sbr-qmf-phase-ab` (included in `test-aac-native`), evaluates **three separately synthesized** waveforms: normal baseline, phase0 (the existing `.sine.f32le`), and phase+2 (the new `.sinephase2.f32le`). It uses the **same LOW-anchored lag, gain and bias** for all and measures exact QMF-center tones at 6937.5 Hz (k18) and 15937.5 Hz (k42) plus 6750–7125, 8625–9000, 12–15 kHz and 15375–16875 Hz. There is **no PCM editing, no frequency/phase fitting, and no FFmpeg-driven phase selection in this stage**. Its synthetic self-test contains opposing and corrected two-tone QMF-center controls, rejects incomplete data, zero A/B differences and NaN.

**Pending:** neither the new SBR-70 probe build on Fedora, the SBR-70 synthetic Python self-test, the real 50-frame export, nor the full native AAC test gate has yet been run. The production AAC-LC / public HE-AAC decoder, standard SBR-19 oscillator and QMF synthesis are untouched. The experimental SBR-54h implementation gained a *separate* phase+2 function but retains original phase0 behavior.

Run on Fedora:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-harmonic-phase2-ab test-aac-sbr-temporal-harmonic-preview \
     test-aac-sbr-qmf-phase-ab probe-he-aac PKG_CONFIG=/bin/false
./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 \
  /tmp/sbr70.preview.f32le /tmp/sbr70 | grep -E 'SBR-70|SBR-54h|odrzucon'
python3 tools/sauti_sbr70_qmf_phase_ab.py \
  --low /tmp/sbr70.low.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --baseline /tmp/sbr70.preview.f32le \
  --phase0 /tmp/sbr70.preview.f32le.sine.f32le \
  --phase2 /tmp/sbr70.preview.f32le.sinephase2.f32le \
  --first-frame 3 --last-frame 46 | tee /tmp/sbr70.log
```

**Interpretation:** A strong change of sign/correlation at both HF tone centers, with phase0 unchanged relative to SBR-66 and stable middle-band controls, would confirm that a **QMF oscillator phase-only** modification explains the improvement seen with PCM inversions in SBR-67/68. It would still **not** prove the normative phase convention, because the entire SBR-54h path is a nonnormative preview. To justify a production fix, compare captured complex HF QMF phase and indexsine against a verified reference decoder across multiple real bitstreams, including transients, different add_harmonic flags and alternate headers.


### SBR-71 — check phase+2 across individual frames, not just whole-file FFT (2026-11-06)

**Actual Fedora results of SBR-70 were received.** `make test-aac-sbr-harmonic-phase2-ab test-aac-sbr-qmf-phase-ab test-aac-he-probe-smoke probe-he-aac PKG_CONFIG=/bin/false`: the C phase A/B test, new Python synthetic self-test, probe compilation under `-O2 -std=c11 -Wall -Wextra -Werror`, and the two-frame smoke probe **PASS**. The public HE-AAC decoder's test fixture decodes two QMF frames, but the real `he_test.aac` production decoder still rejects at frame 2. This is a known separate limitation: the diagnostic core-sequence exporter emitted complete **50-frame** previews for both phase0 and phase+2.

**Real SBR-70 result** (44 steady frames 3..46, same independently exported FFmpeg PCM reference and LOW anchor lag=384, corr=0.99999994, gain=1.41421308):

| QMF center frequency | phase0 amplitude/coherence/relative error | phase+2 amplitude/coherence/relative error |
|---|---|---|
| k18 = 6937.5 Hz | 0.630838 / −0.999597 / 1.630682 | **0.631562 / +0.999635 / 0.369062** |
| k42 = 15937.5 Hz | 0.770049 / −0.999689 / 1.769914 | **0.769476 / +0.999239 / 0.233051** |

Independent QMF/PCM phase0-baseline difference RMS `7.9527896e-06`; phase+2-baseline `7.952825e-06`; difference between both `1.5905605e-05`. The control regions remain unchanged: 8625–9000 Hz ~0.996795 spectral agreement and ~0.08175 relative error; 12000–15000 Hz ~0.995335 and ~0.17138. Wider edge errors remain significant despite phase+2: 6750–7125 Hz ~1.059725, 15375–16875 Hz ~1.303164.

**Interpretation**: This rules out a *mere PCM-editing artifact* as the reason why reversing the experimental harmonic oscillator's tone phase improves the selected two frequencies; both results were produced by separate continuous QMF synthesis histories from the same bitstream. It still does **not** prove that phase+2 is correct per the HE-AAC specification or on other bitstreams: SBR-54h is a nonnormative diagnostic assembly, and FFmpeg PCM minus Sauti LOW is not an independent normative HF stem. The remaining edge-band errors have not been fixed.

**SBR-71 next test**, implemented as `tools/sauti_sbr71_phase_frame_audit.py`, Make target `test-aac-sbr-qmf-phase-frames` (also included in `test-aac-native`). It reuses the **already exported** `/tmp/sbr70` outputs and `/tmp/sbr61.reference.f32le` without decoding again. It measures the complex 2048-point Hann-windowed PCM Fourier coefficients at preselected bins 296 and 680, independently **in each frame**; compares fixed LOW-anchored reference residual against phase0 and phase+2; reports per-frame signed cosine angle, relative error, amplitude ratio, number of improvements/regressions across steady frames 3..46, medians, and transition frames 2/47/48. No candidate-specific lag, phase or gain fitting. Weak reference bins below 1e-4 of the peak within the selected interval are flagged and excluded rather than classified as successes.

Standalone Python self-test compiled and **PASS** in the assistant's local runtime: anti-phase tones with a known gain, one intentionally silent reference frame at k42, 2048-frame geometry rejection, short reference, no A/B delta, and NaN rejection. The new SBR-71 test and the full native AAC gate on Fedora are **pending**. There are no production decoder, FFmpeg integration, or QMF synthesis edits in SBR-71.

Run:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-qmf-phase-frames PKG_CONFIG=/bin/false
python3 tools/sauti_sbr71_phase_frame_audit.py \
  --low /tmp/sbr70.low.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --baseline /tmp/sbr70.preview.f32le \
  --phase0 /tmp/sbr70.preview.f32le.sine.f32le \
  --phase2 /tmp/sbr70.preview.f32le.sinephase2.f32le \
  --first-frame 2 --last-frame 48 | tee /tmp/sbr71.log
```

If most valid steady frames and transition frames show the same polarity benefit, the next high-value engineering task is a **normative complex-QMF comparison** of k18/k42 phase-index and harmonic flags against an independent reference, not another blind gain or phase correction. If only some frames benefit, correlate the exceptions with SBR envelope ownership and transient/onset state.


### SBR-72 — distinguish an oscillator-phase error from full HF timing (2026-11-06)

**SBR-71 real Fedora run was successful**. `make test-aac-sbr-qmf-phase-frames PKG_CONFIG=/bin/false`: PASS, including synthetic guards. Recorded against unchanged FFmpeg PCM after fitting ONLY the same Sauti LOW baseline (`lag=384`, `corr=0.99999994`, `gain=1.41421308`).

| Exact QMF center | Steady frames improved by phase+2 | Median phase0 relative error | Median phase+2 relative error | Median signed phase2 angle |
|---|---:|---:|---:|---:|
| k18 = 6937.5 Hz | **44/44**, none worsened | 1.627986 | **0.366541** | 0.999855 |
| k42 = 15937.5 Hz | **44/44**, none worsened | 1.777365 | **0.218520** | 0.999979 |

No reference-weak frames in 3..46. Early frame 2 improves for both k18 (`1.12344 → 0.80449`) and k42 (`1.43691 → 0.26100`). Frames 47 and 48 likewise improve for both. The per-frame result supports a stable **phase-only effect** in our experimental QMF tone generation, but the harmonic amplitudes remain about 0.63× and 0.78× of the reference tone amplitude; upper and lower edge-band errors are still not solved.

**New timing hypothesis**: the LOW-vs-FFmpeg alignment `384 samples` equals exactly **6 full-rate 64-QMF output slots**. The experimental harmonic oscillator has a four-slot phase cycle; `6 mod 4 = 2`, exactly the phase advance that fixed our two tone polarities. For nominal centers `(k+0.5)*375 Hz` at 48 kHz, a six-slot (=384-sample) pure time shift gives an odd half-cycle for **every** nominal QMF-center tone: `6937.5*384/48000 = 55.5` cycles and `15937.5*384/48000 = 127.5` cycles. Thus a relative HF/LOW timing origin, 6-slot filterbank latency or phase-index clock convention could explain the sign difference. This is **only a hypothesis**, not proof: the 384-sample LOW correction is already applied in our spectral comparisons and may reflect independent synthesis latency rather than an HF clock error.

**SBR-72 implementation**: added `tools/sauti_sbr72_hf_timing_control.py`, Make target `test-aac-sbr-hf-timing-control` and inclusion in `test-aac-native`. Using only pre-existing `/tmp/sbr70` experimental baseline, phase0 and phase+2 PCM, it compares against the same independent FFmpeg-minus-Sauti-LOW residual with frozen LOW lag/gain/bias. The alternative hypothesis is expressed by **predeclared** HF-only PCM offsets `−384, −128, +128, +384` samples, while the LOW part of the waveform stays unchanged. There is **no reference-derived lag or amplitude search** for any HF candidate. It measures both strong exact tone centers and the unaffected control bands 8625–9000 Hz and 12000–15000 Hz. A whole-HF shift that creates correct tone polarity but destroys well-matched non-tonal controls is evidence against a *global HF time shift* as a safe fix; it does not decide all per-slot indexing alternatives. No modified PCM is written to disk.

Synthetic SBR-72 self-test models 384-sample LOW delay and two nominal-center tones, plus independent control tones at 8789 Hz and 12800 Hz. It checks the 6-slot/four-phase arithmetic, phase0 versus phase2 signs, destruction of previously correct control tones by ±128 full-HF timing shifts, non-slot-aligned lag rejection and invalid/NaN geometry. The script has been statically inspected in repo; **the test has not yet run on Fedora**, and no full native AAC gate has been rerun after adding it. This experiment is nonnormative.

Run on Fedora (no AAC re-decode or new FFmpeg reference required):

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-hf-timing-control PKG_CONFIG=/bin/false
python3 tools/sauti_sbr72_hf_timing_control.py \
  --low /tmp/sbr70.low.f32le \
  --reference /tmp/sbr61.reference.f32le \
  --baseline /tmp/sbr70.preview.f32le \
  --phase0 /tmp/sbr70.preview.f32le.sine.f32le \
  --phase2 /tmp/sbr70.preview.f32le.sinephase2.f32le \
  --first-frame 3 --last-frame 46 | tee /tmp/sbr72.log
```

**After SBR-72:** an independent complex-QMF trace of reference HF assembly must be compared with the same delayed Sauti QMF slots, including `f_indexsine` origin, 4-phase clock, `kx`/parity, envelope `add_harmonic` flags, transients and per-channel filter histories. FFmpeg's float implementation increments `indexsine` modulo four once per HF assembly slot and stores it per channel: https://ffmpeg.org/doxygen/trunk/aacsbr_8c_source.html . Whether Sauti's phase0 or phase+2 convention should ultimately be normative cannot be settled by PCM correlation alone. No production AAC-LC, SBR decoder or QMF synth code changed in SBR-72.


### SBR-73 — trace the oscillator clock in Sauti and an independent FFmpeg checkout (2026-11-06)

**Actual SBR-72 Fedora test: PASS** (`make test-aac-sbr-hf-timing-control PKG_CONFIG=/bin/false` plus same-file real PCM audit). Across 44 stable frames LOW anchor stayed `lag=384`, `corr=0.99999994`, `gain=1.41421308`. Relative spectral errors at nominal HF tone centers and in two known-good control regions:

| Candidate | 6937.5 Hz | 15937.5 Hz | Control 8625–9000 Hz | Control 12000–15000 Hz |
|---|---:|---:|---:|---:|
| baseline | 0.999773 | 1.000458 | 0.081745 | 0.171377 |
| phase0 | 1.630682 | 1.769914 | 0.081751 | 0.171376 |
| **phase+2** | **0.369062** | **0.233051** | **0.081749** | **0.171379** |
| shift entire HF −384 samples | 0.368993 | 0.230254 | **0.313725** | **0.361863** |
| shift entire HF −128 samples | 0.369744 | 0.230586 | **1.181516** | **1.228396** |
| shift entire HF +128 samples | 0.369559 | 0.231360 | **1.176671** | **1.238955** |
| shift entire HF +384 samples | 0.369133 | 0.231619 | **0.220915** | **0.286490** |

The 384-sample fixed LOW/reference offset equals 6 full-rate QMF slots, and `6 % 4 = 2`; it rotates any ideal nominal QMF-center sinusoid by 180°. This numerical identity is **not proof of a misplaced delay**. Shifting entire HF in time can mimic the tone correction but degrades otherwise working nonharmonic frequency regions. Therefore **do not** shift the full HF stream and **do not yet** make phase+2 normative.

**Inspect actual implementation before a production fix**: In the FFmpeg float source `libavcodec/aacsbr.c`, the channel state variable `ch_data->f_indexsine` is read at the start of `sbr_hf_assemble`, used when adding harmonic `s_m[e][m]` and incremented after each HF synthesis slot (`indexsine = (indexsine + 1) & 3`). Sauti's separate experimental SBR-54h preview maintains its own four-phase counter, not necessarily FFmpeg's time origin or envelope start. Source: https://ffmpeg.org/doxygen/trunk/aacsbr_8c_source.html .

**Added SBR-73 instruments, not a decoder fix:**
- `tools/sauti_sbr73_ffmpeg_phase_trace.py`: opt-in, fail-closed source instrumentation for **a disposable FFmpeg float source checkout only** at `libavcodec/aacsbr.c`. Requires unique `sbr_hf_assemble`, exact `f_indexsine`, per-slot `g_filt_tab`, and modulo-four increment anchors. Writes FFmpeg trace only when the instrumented binary runs with `SAUTI_SBR73_FFMPEG_PHASE=/tmp/sbr73.ffmpeg.csv`: columns `call,env,slot,indexsine,kx,mmax,sine18,sine42`. `call` is a local FFmpeg HF-assembly invocation index, **not** automatically an AAC frame number or a Sauti channel index. Never instrument installed/system FFmpeg.
- `tools/sauti_he_aac_probe.c`: with opt-in `SAUTI_SBR73_TRACE=1`, writes `SBR73_SAUTI` CSV lines to **stderr** for 32 slots of each real emitted SBR frame (including final flush), carrying emitted frame, phase0, phase+2, kx, k2, original source/envelope, flags at k18/k42 and original onset. It is a read-only trace and does not change PCM, QMF or default probe behavior.
- `tools/sauti_sbr73_phase_trace_audit.py`: reads both traces; verifies full 32-slot frames and phase progression from Sauti, independent per-call FFmpeg slot/phase continuity, usable finite tone amplitudes, and no duplicate/malformed slots. By default it prints *separate source diagnostics*, not a supposed matched clock. It can compare clocks only with **explicit user-provided** `--ffmpeg-call-offset N --ffmpeg-slot-offset M`. These numbers must come from independently verified matching channel and QMF timeline; the tool **does not automatically infer them** to force a positive correlation.
- Make target `test-aac-sbr-ffmpeg-phase-trace` compiles and runs both new Python synthetic checks; wired into `test-aac-native`.

**Validation status**: SBR-73 changes are committed; Fedora C probe compilation, both Python self-tests, a real opt-in Sauti trace and independent instrumented FFmpeg HF trace have not yet run. There are **no new actual FFmpeg measurements** and no normative conclusion. The older FFmpeg QMF instrumentation already in this repo can be reused, but `sauti_sbr73_ffmpeg_phase_trace.py` targets specifically the *floating-point* `aacsbr.c` source, not the synthesis filterbank or installed FFmpeg binary.

Initial Fedora tests (do not change your FFmpeg checkout yet):

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-family-asm
make test-aac-sbr-ffmpeg-phase-trace probe-he-aac PKG_CONFIG=/bin/false
SAUTI_SBR73_TRACE=1 ./build/sauti_he_aac_probe \
  ~/Sauti-fixtures/he_test.aac 50 \
  /tmp/sbr73.preview.f32le /tmp/sbr73 \
  2> /tmp/sbr73.sauti.log | grep -E 'SBR-70|SBR-54h|odrzucon'
grep -c '^SBR73_SAUTI,' /tmp/sbr73.sauti.log
```

Expected trace count if all 50 emitted HF frames succeeded: 50*32=1600, **but never claim it before measuring**. For a disposable, known FFmpeg checkout use `python3 tools/sauti_sbr73_ffmpeg_phase_trace.py /path/to/disposable-ffmpeg --dry-run` before applying without `--dry-run`; then build that isolated FFmpeg and run it with `SAUTI_SBR73_FFMPEG_PHASE` pointed to a separate CSV. After both traces exist, first run the auditor without explicit alignment to validate trace geometry. An aligned comparison requires external verification of FFmpeg channel/call and slot time origin.

Future: compare complex reference HF QMF k18/k42 **after** the same envelope and synthesis slot mapping is established. Until that point keep phase+2 experimental, the AAC-LC production decoder and standard QMF synthesis unchanged.


### SBR-73 — end-of-day checkpoint (2026-11-06)

**STOP FOR TODAY.** This is the handoff for the next session. Working branch: `feature/aac-family-asm` of `Klucznik26/Sauti`. Previous implementation HEAD before the checkpoint: `0835bd1be54eb7e7f83b6293afd81a84e4f2b955`.

**Verified final Fedora run (real fixture, 2026-11-06):**
- `git pull --ff-only origin feature/aac-family-asm`: fast-forward `e983c19..0835bd1` PASS.
- `make test-aac-sbr-ffmpeg-phase-trace probe-he-aac PKG_CONFIG=/bin/false`: PASS; Python syntax compilation plus two synthetic tests: `PASS SBR-73 FFmpeg indexsine tracer: exact source anchors, fail-closed` and `PASS SBR-73: strict per-slot oscillator CSV, explicit origins and fail-closed inputs`. The `sauti_he_aac_probe` compiled with `-O2 -std=c11 -Wall -Wextra -Werror`.
- `SAUTI_SBR73_TRACE=1 ./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 /tmp/sbr73.preview.f32le /tmp/sbr73 2> /tmp/sbr73.sauti.log`: diagnostic QMF/PCM preview reported **50 frames**, including the existing harmonic `phase0` path and independent `phase+2` path.
- SBR-54h: QMF frames=50, PCM frames=50, marked slot-bands=3020, active tones=3000, gated noise=3000, PCM RMS=0.0852400951, peak=0.157160804, difference RMS vs SBR-54a=7.95278956e-06.
- Exported `phase0`: `/tmp/sbr73.preview.f32le.sine.f32le`, **50 frames / 102400 samples**. Exported `phase+2`: `/tmp/sbr73.preview.f32le.sinephase2.f32le`, **50 frames / 102400 samples**.
- `grep -c '^SBR73_SAUTI,' /tmp/sbr73.sauti.log` returned **1600 = 50 × 32 slots**; this confirms line count, not independent validation of individual fields.
- **Important limitation:** strict/public HE-AAC decode still outputs `Pierwsza odrzucona ramka: 2`. Successful 50-frame experimental QMF preview is NOT a completed production HE-AAC decoder.

**Prior experiment conclusion:** SBR-69 to SBR-71 confirmed that the isolated experimental QMF phase+2 harmonic branch (same noise gating, independent QMF synth) improves k18=6937.5 Hz and k42=15937.5 Hz for **44/44 steady frames** apiece. SBR-72 showed shifting the entire HF PCM by ±128/±384 samples harms previously well-aligned nonharmonic controls. LOW-to-FFmpeg PCM alignment `lag=384` equals 6 full-rate QMF slots, whose phase clock modulo 4 equals 2. A clock-origin offset is a hypothesis, **not proof** of normative SBR phase or a reason to modify production.

**NEXT SESSION — resume at SBR-73, do not start SBR-74 or change production code yet:**
1. Inspect the saved `/tmp/sbr73.sauti.log` and preserve it/PCM if the machine may be rebooted; `/tmp` may be cleared.
2. Find a **disposable FFmpeg source checkout** and first run `python3 tools/sauti_sbr73_ffmpeg_phase_trace.py /PATH/TO/DISPOSABLE-FFMPEG --dry-run`. Today's FFmpeg source test was synthetic only; no actual FFmpeg trace, build or independent complex QMF result exists yet.
3. If and only if the source anchors match, instrument/build only that disposable FFmpeg checkout and capture its channel-local `f_indexsine` HF assembly log with `SAUTI_SBR73_FFMPEG_PHASE=/tmp/sbr73.ffmpeg.csv` on the known fixture. Do not modify installed/system FFmpeg.
4. Validate the separate Sauti and FFmpeg traces with `tools/sauti_sbr73_phase_trace_audit.py`. **Do not assume FFmpeg call index is Sauti frame index.** Establish matching channel, call/frame, 64-QMF slot origin, envelope and `add_harmonic` mapping independently; apply explicit offsets only once grounded.
5. Then compare the **actual complex HF QMF** at k18/k42 and the oscillator clock `f_indexsine` across transients and frame ownership. Keep phase+2 experimental until a normative reference confirms a fix. Preserve AAC-LC, normal QMF and rollback gates.

**Documentation-only checkpoint.** No decoder, generator, tests or FFmpeg source changed by this end-of-day commit.
## AAC-61 — cel ukończenia rodziny AAC i baseline (2026-11-07)

Cel rozwoju jest teraz jawny: doprowadzić do kompletnego, uczciwie
zweryfikowanego dekodowania wszystkich profilów AAC, które Sauti już
deklaruje i dla których zawiera kod: AAC-LC mono/stereo oraz HE-AAC/SBR
mono/stereo. Wydajnościowe jądra bitreader/Huffman/dekwantyzacja/spektralne
operacje/IMDCT pozostają w NASM; kod C zachowuje rolę bezpiecznego parsera,
zarządzania stanem i walidacji granic. Nie jest poprawne ogłaszanie obsługi
niezaimplementowanych w repozytorium profili AAC Main, SSR, LTP, LD, ELD ani
Parametric Stereo (PS) — wymagają one własnej gramatyki, stanu i wektorów
zgodności.

**Warunek ukończenia obsługi deklarowanych profili:**

1. publiczny adapter nie odrzuca poprawnego ADTS z `EXT_SBR_DATA` lub
   `EXT_SBR_DATA_CRC` wyłącznie dlatego, że jest to FIL; przekazuje payload do
   atomowego parsera SBR i zachowuje rollback przy błędzie;
2. mono i stereo mają ciągły stan LC/SBR/QMF, jawne zasady zmian nagłówka,
   rematrixingu CPE oraz brak cichego fallbacku do ciszy lub samego LOW;
3. zgodność jest mierzona na rzeczywistych plikach ADTS wobec niezależnej
   referencji PCM osobno dla LOW, HF/noise i pełnego sygnału; test syntetyczny
   lub pojedyncza globalna korelacja nie wystarcza;
4. wszystkie jednostkowe testy, testy odmowy/mutacji oraz testy end-to-end
   przechodzą przy `-Wall -Wextra -Werror`, a dokumentacja rozróżnia wynik
   eksperymentalny od produkcyjnego.

**Baseline gałęzi `feature/aac-family-asm` (`d8490c2`):** wykonane lokalnie
`make test-aac-sbr-adts-mono test-aac-sbr-adts-qmf test-aac-he-probe-smoke
PKG_CONFIG=/bin/false` przeszło. SBR-30 potwierdza ADTS AAC-LC SCE wraz z
niewyrównanym `FIL/EXT_SBR_DATA`, reuse nagłówka i rollback; SBR-31 potwierdza
dwie pełne ramki do 2048 próbek QMF z atomowym rollbackiem. Nie jest to
jeszcze końcowy PCM HE-AAC: publiczna ścieżka AAC-LC nadal fail-closed odrzuca
FIL, a aktualny probe ma charakter diagnostyczny. Pierwsza implementacja
AAC-61 ma usunąć tę rozbieżność bez obniżania wymagań fail-closed.

### AAC-62 — publiczny adapter HE-AAC/SBR, jeszcze eksperymentalny (2026-11-07)

Pierwszy etap AAC-61 został domknięty na granicy API, bez ukrywania
nieukończonej zgodności HE-AAC. Dodano osobny, jawnie opt-inowy
`sauti_decoder_aac_he_native_ops` o nazwie
`aac-he-sbr-native-adts-experimental`. Nie zastępuje on istniejącego
`aac-lc-native-adts`: ten nadal jest adapterem produkcyjnego AAC-LC stereo.

Nowy adapter korzysta z tej samej atomowej ścieżki
`sauti_aac_he_adts_qmf_decode_ex`, a więc zachowuje razem stan AAC overlap,
SBR, analizy/syntezy QMF, faz ROM i cache PCM. Udany access unit zwraca 2048
mono próbek przy dwukrotnej częstotliwości rdzenia (dla testu: ADTS 24 kHz ->
PCM 48 kHz). Seek zapisuje snapshot co 64 access unity i odtwarza stan od
najbliższego snapshotu, tak jak adapter AAC-LC.

Celowo ograniczony kontrakt v1 to wyłącznie ADTS AAC-LC, jeden raw data block,
SCE mono oraz jawne `FIL/EXT_SBR_DATA` typu 13, bez ADTS CRC. Sam nagłówek
mono AAC-LC może przejść tani `probe`, lecz `open` dekoduje pierwszą ramkę i
odmawia otwarcia, jeśli nie ma poprawnego SBR. Dzięki temu zwykły AAC-LC mono
nie otrzymuje fałszywej etykiety HE-AAC ani podwojonego sample rate. Późniejsza
niewłaściwa ramka przerywa `read`; adapter nie wstawia interpolowanego LOW,
ciszy ani zewnętrznego PCM.

Nadal **nie** są obsłużone `EXT_SBR_DATA_CRC` (14), PS, CPE/stereo,
multichannel ani poprawna zgodność HF/noise/sine/smoothing z referencją.
Dlatego adapter pozostaje eksperymentalny i nie spełnia jeszcze warunku
ukończenia AAC-61. Następny etap musi najpierw dodać zweryfikowaną obsługę
CRC typu 14 oraz niezależne wektory rzeczywistego HE-AAC, a dopiero potem
rozszerzać parser na stereo/CPE.

Test SBR-61 buduje dwa rzeczywiste syntaktycznie access unity SCE+FIL,
odczytuje 4096 próbek przez publiczne `SautiDecoderOps`, wykonuje seek do
próbki 1231 i porównuje 777 ponownie odczytanych próbek bit-po-bicie z
odczytem liniowym. Sprawdza także EOF oraz odmowę zwykłej ramki AAC-LC bez
SBR. Po zmianie lokalnie przeszły przy `-Wall -Wextra -Werror`:

```text
make test-aac-native-adapter test-aac-sbr-adts-qmf PKG_CONFIG=/bin/false
```

Pierwszy test zachowuje regresję AAC-LC (ADTS/read/seek/EOF/fail-closed), a
drugi obejmuje SBR-31, rollback SBR-46 oraz nowy publiczny SBR-61. Reguły
linkowania obu testów zawierają teraz kompletny graf HE/SBR/QMF, aby sam
adapter nie miał niezweryfikowanych symboli poza testem integracyjnym.

### AAC-63 — `EXT_SBR_DATA_CRC` (14) z normatywną walidacją CRC-10 (2026-11-07)

Domknięto pierwszy brak wymieniony w AAC-61: SCE mono z
`EXT_SBR_DATA_CRC` (typ 14) nie jest już odrzucany tylko z powodu obecności
CRC. Dodano mały, niezależny moduł `sauti_aac_sbr_crc.c` oraz jego publiczny
kontrakt w `sauti_aac_sbr_crc.h`. Implementuje on CRC-10 MSB-first,
wartość początkową zero i wielomian:

```text
G10(x) = x^10 + x^9 + x^5 + x^4 + x + 1
```

Granice są ważniejsze niż sama arytmetyka: CRC obejmuje dokładnie bity
`sbr_extension_data()` po 10-bitowym `bs_sbr_crc_bits` i przed
`bs_fill_bits`. Nie obejmuje ani 4-bitowego `extension_type`, ani
wyrównania FIL. Parser najpierw dekoduje pełną składnię do lokalnych kopii,
sprawdza CRC na jej rzeczywistej granicy, a dopiero potem publikuje historię
SBR i side-data. Niezgodność CRC pozostawia historię, PCM i wynik parsera
byte-for-byte bez zmian.

Most ADTS mono akceptuje teraz zarówno typ 13, jak i typ 14; typ 14 może
przejść wyłącznie z prawidłowym CRC-10. Test SBR-30 zawiera nie wyrównaną do
bajtu ramkę ADTS SCE+FIL typu 14, a SBR-61 podaje pierwszą ramkę type-14 do
publicznego adaptera HE-AAC i drugą type-13 z reuse nagłówka, następnie
sprawdza bitowo zgodny seek. SBR-62 sprawdza niezależny wektor `0xA5 ->
0x140`, prawidłowy type-14 oraz atomową odmowę po zmianie samego pola CRC.

Po zmianie przeszły z `-Wall -Wextra -Werror`:

```text
make test-aac-sbr-payload test-aac-sbr-adts-mono \
     test-aac-sbr-adts-qmf test-aac-native-adapter PKG_CONFIG=/bin/false
```

To nie jest obsługa 16-bitowego CRC ADTS (`protection_absent=0`) — ten
wariant dalej jest świadomie odrzucany, podobnie jak PS, SBR stereo/CPE i
multichannel. CRC-10 type-14 nie rozwiązuje też otwartego problemu zgodności
HF/noise/sine z niezależnym referencyjnym PCM.

### AAC-64 — sprzężone stereo SBR/CPE: parser i energie kanałów (2026-11-07)

Dodano pierwszy rzeczywisty element ścieżki HE-AAC stereo: atomowy parser
`sauti_aac_sbr_payload_cpe_parse`. Obejmuje klasyczny SBR
`sbr_channel_pair_element` z `bs_coupling=1`, zarówno dla type-13, jak i
CRC-zweryfikowanego type-14. Parser czyta wspólną siatkę, oba zestawy
`sbr_dtdf`, wspólne `sbr_invf`, a następnie właściwie przeplataną składnię
envelope/noise obu kanałów. Lewy kanał dekoduje poziom, prawy kanał używa
codebooków balance; istniejące `sauti_aac_sbr_coupled_dequant` wykonuje
normatywne rozdzielenie energii i noise floor na L/R.

Wynikiem są dwa niezależne `SautiAacSbrPayloadFrame` oraz dwa ważne
`SautiAacSbrHfJointConfig`, gotowe dla odrębnych historii QMF. Stan nagłówka
i obu kanałów Huffmana aktualizuje się jedynie po przejściu kompletnej
składni, CRC i paddingu. Test SBR-63 tworzy pełny coupled CPE, sprawdza
type-13 oraz type-14, poprawność obu konfiguracji HF i byte-for-byte rollback
po zmianie jednego bitu CRC.

W chwili utworzenia AAC-64 API celowo odrzucało `bs_coupling=0`, a parser
side-data nie publikował PCM. Ograniczenia te usunięto następnie w AAC-65 i
AAC-67; ten akapit zachowuje stan historyczny etapu.

Zweryfikowano lokalnie:

```text
make test-aac-sbr-payload PKG_CONFIG=/bin/false
```

### AAC-65 — ADTS CPE → rdzeń AAC-LC stereo → podwójny QMF SBR (2026-11-07)

Domknięto pierwszy pionowy, testowalny przebieg stereo HE-AAC dla sprzężonego
CPE. Funkcja `sauti_aac_lc_cpe_synthesize` wydziela z dotychczasowego dekodera
ADTS AAC-LC część przetwarzającą już sparsowane `SautiAacCpeData`: scalefactors,
PNS, MS/intensity stereo, TNS i syntezę obu kanałów. Dzięki temu nowy,
restrykcyjny most `sauti_aac_he_adts_cpe_decode_ex` może sparsować pojedynczy
ADTS AAC-LC CPE, przyjąć dokładnie jeden SBR CPE FIL type-13 albo
CRC-sprawdzony type-14, a następnie atomowo zwrócić 1024 stereofoniczne ramki
rdzenia (2048 próbek przeplatanych).

`sauti_aac_he_adts_cpe_qmf_decode_ex` uruchamia następnie osobną historię QMF
i HF dla lewego oraz prawego kanału i zwraca 2048 stereofonicznych ramek przy
podwojonej częstotliwości (4096 próbek przeplatanych) wraz z dwoma raportami
limitera. Liczniki historii rdzenia, SBR i obu QMF są kontrolowane jako jedna
transakcja; niespójny stan zostaje odrzucony przed dekodowaniem.

Test SBR-64 buduje prawidłowy CPE z coupled SBR type-14, sprawdza rdzeń,
obie historie QMF oraz skończoność wszystkich 4096 wyników. Następnie zmienia
wyłącznie bit CRC-10 i potwierdza rollback stanu, PCM i side-data. Test stał
się także częścią zbiorczego `test-aac-native`.

Po AAC-67 ten sam SBR-64 uruchamia ponadto pełny przebieg uncoupled
`bs_coupling=0` z type-14 — od ADTS, przez rdzeń i dwa QMF, po adapter
publiczny — oraz weryfikuje skończoność wyjściowego PCM.

Zakres jest nadal jawnie ograniczony: brak CRC ADTS, PS, wielu elementów
audio, innych konfiguracji kanałów i walidacji PCM na
niezależnym materiale HE-AAC. To eksperymentalna ścieżka CPE, nie deklaracja
pełnej zgodności stereo HE-AAC.

Zweryfikowano lokalnie:

```text
make test-aac-sbr-adts-cpe PKG_CONFIG=/bin/false
```

### AAC-66 — eksperymentalny adapter publiczny stereo HE-AAC CPE (2026-11-07)

Dodano `sauti_decoder_aac_he_cpe_native_ops`, osobny adapter publicznego API
o nazwie `aac-he-sbr-cpe-native-adts-experimental`. Wystawia wyłącznie
restrykcyjny wariant z AAC-65: ADTS AAC-LC CPE bez CRC, jeden SBR
type-13/type-14 i dwa kanały QMF. Interfejs zwraca 2048 stereofonicznych ramek
na access unit, `sample_rate = 2 * core_sample_rate`, natomiast własny cache
ma 4096 próbek przeplatanych.

Adapter najpierw waliduje wszystkie nagłówki ADTS, następnie dekoduje pierwszą
pełną ramkę zanim poda wywołującemu podwojoną częstotliwość. Dzięki temu zwykły
AAC-LC stereo o pasującym nagłówku nie może zostać błędnie opisany jako HE-AAC.
Seek odtwarza historię z checkpointów co 64 access unity, więc zachowuje stan
IMDCT, SBR i obydwu kanałów QMF bez buforowania całego PCM.

SBR-64 rozszerzono o pełny odczyt adaptera, seek do środka strumienia i
bitową równość odtworzonego fragmentu oraz odrzucenie CPE bez SBR. Adapter
pozostaje eksperymentalny i nie rozszerza zakresu AAC-65.

Zależności wspólnego adaptera w Makefile zostały celowo zebrane w
`AAC_NATIVE_ADAPTER_DEPS`: każdy binarny klient adaptera linkuje także most
CPE/QMF i jego komplet symboli, nawet jeśli dany test wykonuje tylko wariant
mono albo AAC-LC. Eliminuje to błąd linkera po dodaniu nowej gałęzi ops i nie
zmienia wyboru dekodera przez rejestrację.

### AAC-67 — `bs_coupling=0` dla SBR CPE (2026-11-07)

Parser CPE obsługuje teraz także uncoupled SBR (`bs_coupling=0`). W tym
wariancie kanały mają osobne gridy, `sbr_dtdf` i `sbr_invf`, zaś składnia
przenosi najpierw envelope lewego, potem envelope prawego kanału, a dopiero
następnie oba noise floors. Dodano transakcyjną funkcję
`sauti_aac_sbr_quant_decode_cpe_uncoupled`, która zachowuje niezależne
historie Huffmana, odczytuje tę kolejność i zatwierdza oba kanały wyłącznie
po pełnym sukcesie.

SBR-63 serializuje teraz rzeczywisty uncoupled CPE o różnych wartościach
envelope i noise dla L/R, sprawdza obie konfiguracje HF oraz rollback po
ucięciu danych. SBR-64 prowadzi ten wariant przez ADTS, rdzeń CPE, dwa QMF i
adapter publiczny. Brakuje jeszcze niezależnej próbki referencyjnej HE-AAC do
porównania PCM.

CRC-16 CPE dołącza dwa normatywne regiony: pierwsze 192 bity elementu po
`id_syn_ele` oraz dodatkowe 128 bitów drugiego `individual_channel_stream`.
Pozycja drugiego ICS jest zapisywana podczas istniejącego parsowania CPE,
więc nie powstaje osobny heurystyczny skaner. Adapter stereo przyjmuje
jednoblokowy ADTS CRC wyłącznie przez ten walidowany most.

SBR-64 zawiera fixture transportowego CRC CPE: niezależnie wylicza oba
regiony z granic zwróconych przez parser, dekoduje poprawną ramkę i zmienia
jeden chroniony bit. Ta mutacja jest odrzucana przed commit stanu CPE/SBR/QMF.

Ten sam jednoelementowy walker SCE jest użyty także przez zwykły dekoder
AAC-LC mono, więc CRC nie jest cechą zastrzeżoną dla SBR. Stereo AAC-LC CPE
jest podłączone do analogicznego walkera drugiego ICS; nadal świadomie
odrzucane pozostają multi-RDB oraz nieobsługiwane elementy kanałowe.

Do adaptera `aac-lc-native-adts` trafia teraz również jedno-RDB ADTS z CRC;
walidacja odbywa się przy dekodowaniu ramki, przed cache PCM oraz checkpointem
seek. Nagłówkowe dopasowanie nie wystarcza do opublikowania audio.

Zależność CRC-16 jest własnością wspólnego `AAC_LC_FRAME_SRC`, dzięki czemu
ścieżki AAC-LC i HE-AAC linkują ten sam prymityw dokładnie raz; pełna regresja
wykryła i usunęła wcześniejsze podwójne linkowanie w teście mono SBR.

Narzędzie `sauti_he_aac_probe` raportuje teraz CRC jako weryfikowane dla
obsługiwanego podzbioru jedno-RDB SCE/CPE, zamiast nieaktualnego komunikatu o
braku obsługi.

### AAC-68 — następny zakres: transportowe CRC-16 ADTS (2026-11-07)

Pozostałą świadomą luką transportową jest `protection_absent=0`. ADTS w tym
trybie niesie 16-bitowe `crc_check`; CRC używa generatora
`x^16 + x^15 + x^2 + 1` (`0x8005`), rejestru początkowego `0xffff`, kolejności
MSB-first i bez końcowej inwersji. Nie wolno jednak walidować go prostym CRC
po całej ramce: norma definiuje chroniony zakres względem elementów
`raw_data_block`, a przy wielu blokach również pozycje i osobne pola CRC.

W aktualnie wspieranym podzbiorze (`raw_blocks=1`) następny etap musi zatem
najpierw dodać dokładny walker chronionych bitów dla SCE/CPE i testy golden
wektorów, a dopiero potem odblokować ADTS CRC w adapterach mono i stereo.
Do tego czasu odrzucenie takich ramek jest zachowaniem bezpiecznym, a nie
brakiem walidacji.

Pierwszym krokiem implementacyjnym jest niezależny
`sauti_aac_adts_crc16_bits`: przyjmuje jawny zakres bitów, działa MSB-first i
nie przypisuje semantyki całej ramce. Test obejmuje wektor kontrolny
`"123456789" → 0xaee7`, niewyrównany zakres bitowy oraz atomowość przy
błędnych granicach. Test `test-aac-adts-crc` należy do `test-aac-native`.

Walker użyje też `sauti_aac_adts_crc16_update_bits`, który atomowo dokleja
kolejny zakres do podanego rejestru. Test potwierdza, że dwa rozłączne zakresy
bitowe dają identyczny wynik jak jeden zakres ciągły.

Zaimplementowano pierwszy konsument walkera dla pojedynczego SCE w mono
HE-AAC: po sparsowaniu SCE porównuje on `crc_check` z CRC 56-bitowego
nagłówka oraz pierwszych 192 bitów elementu po `id_syn_ele`, dopełnionych
zerami. FIL i `ID_END` nie wchodzą do zakresu. Następnym krokiem jest fixture
z poprawnym `crc_check` i mutacją chronionego bitu, zanim zniesione zostanie
ogólne oznaczenie eksperymentalne tej ścieżki.

Fixture SBR-65 realizuje już ten krok: buduje pełny ADTS AAC-LC SCE + SBR z
`protection_absent=0`, wylicza CRC po właściwych regionach i potwierdza
dekodowanie. Zmiana jednego chronionego bitu musi odrzucić ramkę bez zmiany
stanu, PCM ani parametrów SBR. Zakres ukończony na tym etapie to wyłącznie
jedno-RDB mono SCE; CPE wymaga dodatkowego 128-bitowego regionu drugiego ICS.

Adapter `aac-he-sbr-native-adts-experimental` nie odrzuca już samego
`protection_absent=0`: skanuje taki sam jednoblokowy transport jak wariant
bez CRC, a pierwsza i każda kolejna rama przechodzi walidację SCE przed
zapisem do cache PCM. Nie rozszerza to jeszcze zakresu o CPE ani multi-RDB.

### SBR-73 — measured f_indexsine on he_test.aac (2026-11-13)

Disposable checkout only: `/home/marek/Sauti-tools/ffmpeg-sbr54w` (FFmpeg 8.0.git). `tools/sauti_sbr73_ffmpeg_phase_trace.py --dry-run` matched the float `sbr_hf_assemble` anchors, then the same script patched **only** `libavcodec/aacsbr.c`. Installed `/usr/bin/ffmpeg` was not modified. The pre-existing SBR-54/55/57/58 traces in `aacsbr_template.c` stay env-gated and were left unset. Incremental `make ffmpeg` rebuilt `libavcodec/aacsbr.o` and `ffmpeg`.

Traces, kept outside `/tmp`:

- FFmpeg: `SAUTI_SBR73_FFMPEG_PHASE=/home/marek/Sauti-tools/sbr73-traces/ffmpeg.csv` while decoding `~/Sauti-fixtures/he_test.aac` (ffprobe: HE-AACv2, stereo, 48 kHz). CSV has 1632 slot rows, 51 `sbr_hf_assemble` calls, one channel (`kx=18`, `m_max=27`).
- Sauti: `SAUTI_SBR73_TRACE=1 ./build/sauti_he_aac_probe ~/Sauti-fixtures/he_test.aac 50 …` wrote 1600 `SBR73_SAUTI` rows (50 emitted frames × 32 slots) to `/home/marek/Sauti-tools/sbr73-traces/sauti.log`. Probe exit status 1 is the existing “pozostałe ramki nie były zastępowane ciszą” limit, not a trace failure. `sauti_sbr73_phase_trace_audit.py` accepted both logs with no offsets.

**Call index is not the Sauti frame index.** Four calls are not 32 slots: call 3 has 34 slots and call 4 continues at slot 2 (34+30=64); call 49 has 38 slots and call 50 continues at slot 6 (38+26=64). `f_indexsine` is one continuous channel clock across those spills. Reassembling each call into 32-slot output frames by carrying the overflow slots forward yields 51 frames and no leftover. That reassembly uses the traced slot index and the 32-slot frame length. It does not search for a phase match.

The envelope string of Sauti emitted frame N is identical to reconstructed FFmpeg frame N+1 on all 50 frames, including the four non-flat borders (Sauti 2, 3, 48, 49). FFmpeg frame 0 is the extra leading assemble and has no Sauti counterpart. At that alignment, 1600/1600 slots have the same envelope and `f_indexsine == Sauti phase0`. `f_indexsine == phase+2` on 0/1600 slots. Every aligned frame starts at phase 0 because the running clock stays a multiple of 4 at each 32-slot boundary, including after the spills.

The only amplitude disagreement on that alignment is Sauti frame 2, slots 0–9: Sauti harmonic flags at k18 and k42 are set, while FFmpeg `s_m` is 0 for both bands. Those ten slots are envelope 0 and `onset=0`. Envelope id and `f_indexsine` still match there. On the other 1590 slots the k18 sine is present exactly when Sauti flag 18 is set. k18 and k42 amplitudes are not equal to each other (1500 FFmpeg slots); presence of the two tones tracked together on this file.

**No production change.** phase+2 stays experimental. This clock measurement rejects “Sauti’s oscillator origin differs from FFmpeg `f_indexsine`” for this fixture. It does not compare complex HF QMF samples or PCM, so it does not explain the earlier k18/k42 spectral residual. AAC-LC and the public QMF path were not edited.

### SBR-73 — complex HF QMF at k18/k42 (2026-11-14)

Same fixture, same 32-slot reassembly, same map Sauti frame N = FFmpeg output frame N+1. The new traces are read-only and off unless the env var is set. FFmpeg logs `Y1` after gain, noise and sine (`SAUTI_SBR73_FFMPEG_QMF`, disposable `aacsbr.c` only). The probe logs the experimental `sine_high` and `phase2_high` buffers (`SAUTI_SBR73_SAUTI_QMF`). Files: `/home/marek/Sauti-tools/sbr73-traces/ffmpeg-qmf.csv` (1632 slots) and `sauti-qmf.csv` (1600 slots). This is pre-synthesis mono HF, not post-PS stereo and not PCM.

On the aligned grid the best slot lag in either band, over steady Sauti frames 4–47, is 0. A 2-slot or 6-slot shift does not raise correlation.

| Band | Path | Slots | Correlation | Best-gain angle | Residual / FFmpeg RMS |
| --- | --- | --- | --- | --- | --- |
| k18 | phase0 | 1408 steady | 0.536 | −0.12° | 0.844 |
| k18 | phase+2 | 1408 steady | 0.531 | −179.87° | 0.847 |
| k42 | phase0 | 1408 steady | 0.808 | +0.04° | 0.589 |
| k42 | phase+2 | 1408 steady | 0.806 | +179.96° | 0.592 |

phase+2 is a 180° inversion of the steady tone, not a better clock. k42 phase0 has the same complex direction as FFmpeg on 1408/1408 steady slots; 65% of FFmpeg energy is explained by one complex gain. k18 phase0 explains only 29%. Its direction agrees on 322/352 slots of phase bucket 0 and 235/352 on buckets 1, 2 and 3, so the k18 residual is not one inverted quarter-cycle.

Sauti frame 0 matches FFmpeg frame 1 exactly up to a positive real scale. Both bands give `|g| = 51933.781`, which is `32768 * 10^(2/10)`. That is Sauti’s documented 1/32768 HF unit times FFmpeg’s +2 dB limiter cap. The shape residual on that frame is ~0. Sauti frame 1 is the first real divergence: Sauti k18 RMS rises about 26× while FFmpeg stays near the frame-0 level, and correlation falls to 0.17. Frames 2–3 are the transient where phase+2 has a higher shape correlation (k18 0.925 vs 0.650) but only after a ~160° rotation, on the same frame whose first ten slots have a Sauti harmonic flag and a zero FFmpeg `s_m`.

**No production change.** The oscillator sign to keep is phase0. The open HF error is amplitude and waveform shape, largest at k18 after the startup frame, not a phase-origin or a slot delay. AAC-LC and the public QMF path were not edited.

### SBR-73 — gain HF and sinusoid are each right, at two different scales (2026-11-14)

Read-only split on the same alignment. FFmpeg logs `Y1` after `hf_g_filt` and before noise/sine (`SAUTI_SBR73_FFMPEG_GAIN`). The probe logs gain-adjusted HF, unsmoothed noise amplitude and the harmonic flag (`SAUTI_SBR73_SAUTI_PARTS`). The tone is `sine_high − gain HF`, which is what the experimental preview adds after removing its own noise. Files: `ffmpeg-gain.csv`, `sauti-parts.csv`, plus the earlier `ffmpeg-qmf.csv` and `sauti-qmf.csv`.

On steady Sauti frames 4–47 (1408 slots, harmonic flag set, FFmpeg still on the noise-plus-sine path):

| Component | Band | Correlation | Scale | Residual / reference |
| --- | --- | --- | --- | --- |
| Gain-adjusted HF only | k18 | 1.0000 | 45.2548 = `32*sqrt(2)` | 0.001 |
| Gain-adjusted HF only | k42 | 0.9941 | 105.8 | 0.108 |
| Added tone | k18 | 1.0000 | 51933.78 = `32768*10^(2/10)` | ~0 |
| Added tone | k42 | 0.9996 | 42462 | 0.030 |

Every k18 phase bucket has the same tone scale. Frames 0–1 have no gain HF on either side; their whole subband is noise, and that noise also matches at exactly `32768*10^(2/10)` (64 slots, correlation 1). The earlier “frame 0 matches” result was this noise, not transposed HF.

The two k18 scales differ by `32768*10^(2/10) / (32*sqrt(2))` ≈ 1147.6. FFmpeg adds the tone and the gain HF in one domain, so the tone dominates (RMS 12.68 against gain RMS 0.0175). Sauti adds them as stored, so the tone (RMS 2.44e-4) is the same order as the gain HF (RMS 3.87e-4). One global scale cannot make the sum match. A constant cap is also not enough for k42: its tone scale is 42462, not the +2 dB ceiling.

Startup edge, separate from that scale split: Sauti frame 1 slots 26–31 already contain gain HF at k18 and k42, and the aligned FFmpeg slots are still zero. A lag search over frames 1–3 does not turn that ramp into a single 6-slot shift.

**No production change.** The experimental tone must be scaled into the gain-adjusted QMF domain, including the actual per-band limiter boost, before it is added. Hardcoding `1147.6` would fit only this file’s pegged k18. AAC-LC and the public QMF path were not edited.

### SBR-73 — stored limiter boost is not FFmpeg’s k42 boost (2026-11-14)

Checked the stored boost before multiplying the experimental tone. Same fixture, same alignment. On steady Sauti frames 4–47, FFmpeg `Y_full − Y_gain` is exactly `s_m` on one axis: the orthogonal residual is 0 at both k18 and k42. `hf_apply_noise` adds the noise table only when `s_m` is 0, so that difference is not a noise+sine mixture. The earlier k42 residual of 0.030 was one scale fitted across three real boosts.

`s_m / |Sauti tone| / 32768` is constant inside a frame and is the boost FFmpeg applied:

| Band | Steady frames 4–47 | Boost |
| --- | --- | --- |
| k18 | all 44 | `1.584893192` (the +2 dB cap), tone scale `51933.78` |
| k42 | repeating every frame | `1.347441`, `1.256347`, `1.282795` |

The probe now logs the calculators that already ran, off unless the env var is set. `SAUTI_SBR73_SAUTI_BOOST` writes one row per completed envelope (`sauti-boost.csv`, 57 envelopes). `SAUTI_SBR73_SAUTI_BOOST_BINS` writes the per-bin inputs (`sauti-boost-bins.csv`, 1539 bins). source=1 applies to emitted frame `ingest`; source=0 completes the previous frame’s deferred tail.

SBR-53d `limiter_boost` on those steady frames matches FFmpeg at k18, because both sit on the cap. At k42 it follows the same 3-frame phase but is `1.026`–`1.106`, a factor `1.197`–`1.225` below FFmpeg. The factor is not constant, so a correction on top of SBR-53d would not collapse the tone. SBR-54k `group_boost` at k42 is the cap on these frames, and its post-boost `sine_amplitude` stays in the normalized-energy domain.

The gap is in the inputs, not in the oscillator. SBR-60 divides envelope energy by `2^30` and does not divide the noise floor. SBR-53d then takes `sqrt(E/|X|^2)` and its `after` sum has no `s_m^2`. On this file the k18 limiter group has pre-gain `|X|^2 ≈ 31` on the sine bin, and the cap hides that. The k42 group does not. Replaying FFmpeg `sbr_gain_calc` on the logged bins with `E*2^30`, the noise floor left as stored, and `|X|^2*32768^2` reproduces the k18 cap and comes within 2% on k42 (rms 1.2%, worst steady frame −1.96%). That remaining error keeps the same 3-frame sign, so `32768` is not a constant to build into the tone. No search over a single `|X|` scale removed it.

**No tone scale was applied.** `harmonic_preview`, the `2^-30` normalizer, AAC-LC, and the public QMF path were not edited. phase0 stays. The tone can be multiplied by a limiter boost only after that boost equals FFmpeg’s per band, including k42.

### SBR-73 — k42 boost gap is the quiet low-QMF copies (2026-11-14)

Pre-gain complex samples, same fixture, steady Sauti frames 4–47. Sauti logs `baseline_hf` at ingest (`SAUTI_SBR73_SAUTI_X`, `sauti-x.csv`, 50 frames × 32 slots). FFmpeg logs `X_high` at the start of `sbr_env_estimate` (`SAUTI_SBR73_FFMPEG_X`, `ffmpeg-x.csv`, 51 calls × 40 slots). The unique alignment is Sauti frame N, slot t against FFmpeg call N+1, slot t+2. A lag of ±1 drops k18 from correlation 1.0000 to 0.984. Call N+2 matches the shape only after a −120° rotation and leaves a relative residual of 0.866, so it is the next frame of the same subband oscillator, not a better clock.

At that alignment the loud bins are the same waveform, real scale, angle 0:

| Band | Correlation | Scale | Relative residual | Per-frame RMS scale |
| --- | --- | --- | --- | --- |
| k18 | 1.000000 | 32768.154 | 0.00068 | 32764 .. 32776 |
| k19 | 0.999997 | 32767.991 | 0.0024 | 32766 .. 32773 |
| k42 | 0.9959 | 32640.9 | 0.091 | 32771 .. 32820 |
| k35 | 0.9729 | 31927 | 0.235 | 28858 .. 35472 |
| k36 | 0.9694 | 31802 | 0.255 | 28519 .. 35745 |
| k41 | 0.9732 | 31911 | 0.231 | 31571 .. 34258 |

k18 and k19 stay on 32768. k35, k36 and k41 are still the same signal (per-frame correlation 0.97–0.99, lag 0), but their magnitude ratio wanders by about ±10% and about a fifth of the energy in the bin is not a single real scale. Conjugation does not improve the fit.

A second disposable trace (`SAUTI_SBR73_FFMPEG_HFGEN`, `ffmpeg-hfgen.csv`, 1377 rows) shows why those bins can disagree without touching the limiter. `bw_array` is 0 on every band of every call, and `X_high` over the generated window is a bit-exact copy of the patched `X_low` (`copy_rel` 0 on all 1377 rows). The LPC coefficients are nonzero (k18 `|alpha0| ≈ 2.17`) and are multiplied by `bw`, so they do not enter `X_high`. Chirp is not this gap.

The patch map is stable. First patch: k18..k32 copy low bands 2..16. Second patch: k33..k44 copy low bands 5..16. The k42 limiter group is that second patch. On call 11 the generator-window power of k33..k44 sums to 435, the same `sumC` the limiter logged. k35 (low 7), k36 (low 8) and k41 (low 13) are about 74% of it. k42 itself (low 14) is about 1.7%. Low band 2 is ~84 dB above low band 7.

So the ~2% k42 boost error is the 32-band analysis disagreeing on those quiet source bands, then being copied verbatim into the second patch and summed by the limiter. The boost formula, the sine clock, and the HF generator are not the remaining bug. Sauti’s experimental analysis accumulates the even prototype taps and the direct cosine/sine modulator in double, then stores float. FFmpeg’s float analysis is the windowed DCT path; this machine’s build uses the SSE QMF shuffles. That difference is the next surface to measure. It is not a measured fix yet.

**No production change.** The tone was not scaled. `32768` was not written into the tone. The `2^-30` normalizer, `harmonic_preview`, AAC-LC, and the public QMF path were not edited. phase0 stays.

### SBR-73 — the quiet-bin gap is a 6-slot energy window (2026-11-14)

The sentence above is superseded. The 32-band analysis matches. The slot `t+2` alignment that made k18 look exact is a 6-slot shift, and band 2 hides it.

Core PCM first, so the scale is not in the samples. `ffmpeg-core.f32le` (51 calls) against `sauti-core.f32le` (50 frames), steady frames 4–47, call = frame+1, lag 0: relative residual `3.2e-7`, max abs `7.5e-8`, scale `1.000`. Offsets 0 and +2 are residual ~1.73. Multiplying the core by 32768 is the wrong model. The 32768 on the QMF bins is FFmpeg’s analysis MDCT scale (`mdct_ana` is an inverse float MDCT of length 64, scale `-2*32768`).

`sbr_lf_gen` uses `t_HFGen = 8`. `X_low[i] = current W[i-8]` for `i` in `[8, 40)`, and `X_low[i] = previous W[i+24]` for `i` in `[0, 8)`. With `bw = 0`, `hf_gen` copies that into `X_high` only on absolute slots `[2, 34)` (`ENVELOPE_ADJUSTMENT_OFFSET` plus a full 32-slot envelope). A full-frame `e_curr` therefore averages previous `W[26:32]` and current `W[0:26]`. Slots `X[34:40]` are outside that copy. A flat “slot + 8” over all 32 Sauti slots pairs those stale samples with current `W[26:32]` and cannot match.

Pointwise, steady frames 4–47, expecting `X = Sauti * 32768`:

| Mapping | k18 | k35 (low 7) | k42 (low 14) |
| --- | --- | --- | --- |
| `X[8:34]` = current `W[0:26]` | `3.6e-7` | `3.9e-4` | `1.6e-3` |
| `X[2:8]` = previous `W[26:32]` | `3.6e-7` | `3.9e-4` | `1.8e-3` |
| `e_curr` window, both pieces | `3.6e-7` | `3.9e-4` | `1.6e-3` |
| old flat slot + 2 | `6.8e-4` | 0.237 | 0.091 |
| old flat slot + 8 | 0.532 | 0.458 | 0.551 |

The check sample holds: frame 10, low band 7, Sauti slot 0 × 32768 against `X` call 11 slot 8, relative `2.2e-4`; previous slot 26 against `X` slot 2, relative `1.7e-4`. Replaying Sauti’s analyzer from `sauti-core.f32le` matches the probe’s k18 trace to `9e-9`. Quiet-band absolute error stays under `4e-8`; the larger relative figure is the small amplitude.

Band 2 advances `+120.000°` per slot (`-0.5 + 0.866j`, 1364 steps, magnitude 1). Six slots is `+0.002°`, two full turns, so a 6-slot shift still fits a positive real scale. That is why k18 at slot `t+2` had residual 0.00068 while k35 at the same index had 0.237. The quiet bands do not have that constant step.

Power of the delayed window against traced `X[2:34] / 32768^2` has median ratio 1.00000–1.0005 on k18, k35, k36, k41 and k42. The same trace against Sauti’s own 32 slots stays at 1.000 on k18 (range 0.9997–1.0005) and wanders on the quiet sources (k35 median 1.004, range 0.776–1.172).

Replaying float `sbr_gain_calc` on the k42 group (k33..k44, 12 bands, 44 steady calls) closes the boost:

| `e_curr` source | Worst relative error vs logged boost |
| --- | --- |
| logged FFmpeg `e_curr` | `3.9e-9` |
| delayed window, previous 6 + current 26, times `32768^2` | `8.4e-5` |
| Sauti’s current 32 slots, times `32768^2` | 0.0194 |

Call 11 (Sauti frame 10) is the loud cycle: logged boost `1.347441`, delayed `1.347328`, current-32 `1.321295`. `sumC` is 434.92, 434.97 and 399.73. The move is low bands 7 and 8 (k35 `120.1 → 102.5`, k36 `130.3 → 109.5`). The previously reported ~2% is this window, not a limiter-formula error and not a bad cosine.

**No production change.** The tone was not scaled. `32768` was not written into the tone, the window, or the modulator. The `2^-30` normalizer, `harmonic_preview`, AAC-LC, and the public QMF path were not edited. phase0 stays. `sauti_aac_sbr_temporal_power_measure` still averages the envelope’s own slots; its tests define that unshifted mean, and a split envelope is not the same 6-slot borrow. The next experimental change is that energy window only: for grid borders `t_env`, average `X[2*t_env+2 : 2*t_env_next+2]`, which is analysis slot `i-6`, borrowing the previous frame when the index goes negative. Do it beside the tested mean, then replay k42 before any tone scale.

### SBR-73 — live HF-generator window matches FFmpeg e_curr (2026-11-14)

`sauti_aac_sbr_temporal_power_hfgen_window` averages analysis slots `[start-6, end-6)` of one envelope. A negative slot comes from the previous frame. The tested unshifted mean is unchanged. SBR-53d and SBR-54l still average the envelope's own slots. The probe writes this power only when `SAUTI_SBR73_SAUTI_HFGEN_POWER` is set.

On `he_test.aac` the live QMF, times `2^30`, matches FFmpeg `e_curr` for every steady bin in frames 4–47 (1188 bins). Worst relative error is `8.4e-7` at k18 and `9.1e-4` at the quietest bin. Replaying `sbr_gain_calc` with that live power recovers all 56 logged k42 groups that Sauti can form. The worst boost error is `1.12e-4` (call 4, boost `1.342937` vs `1.342786`). The three-cycle values stay `1.346013`, `1.256347`, `1.282795`, and `1.347441`. FFmpeg calls 0 and 1 have no Sauti window: call 0 is before the first frame, and frame 0 has no previous QMF yet.

The stored SBR-53d and SBR-54l boosts still ignore this window. The next section is an opt-in limiter that reads it. `harmonic_preview`, AAC-LC, and the public QMF path still do not.

### SBR-73 — opt-in limiter on the 6-slot window (2026-11-14)

`sauti_aac_sbr_gain_partition_flags` and `sauti_aac_sbr_gain_limit_partition_flags` keep the two sinusoid maps apart. The index flag adds `S_M` on the band midpoint only. The mapped flag selects the tonal HF-gain equation for every bin of that envelope band. The old one-flag functions pass the same flag twice, so SBR-54l and its tests stay on the unshifted normalized path.

The probe calls the split limiter only when `SAUTI_SBR73_SAUTI_LIMIT` is set. Each current-frame envelope uses `e_curr = ldexp(window, 30)`, `E = ldexp(normalized envelope energy, 30)`, the stored unscaled `Q`, the center and mapped sine flags, and `delta = attack[env] ? 0 : 1`. The `2^30` factor stays in the probe. No applied gain and no tone is changed.

On `he_test.aac` the trace has 1512 rows, Sauti frames 1–49. FFmpeg calls 0 and 1 still have no window. The limiter group matches `lim` on every row. Apart from the two envelopes below, the worst boost relative error is `1.12e-4` (frame 3, k42 group, `1.342786` vs `1.342937`). Steady frames 4–47, all 1188 bins, worst boost relative error is `7.88e-5`. The k42 cycle is `1.347335` vs `1.347441`, `1.256376` vs `1.256347`, `1.282750` vs `1.282795`. Frame 47 is `1.258883` vs `1.258873`. k18 stays on the cap `1.58489319` (relative `2.5e-8`).

FFmpeg's boost CSV logs `s_m` and `g` before the boost multiply. This limiter logs them after. Dividing by the logged boost matches the pre-boost sine to `2e-9` on steady k18 and k42, and the pre-boost gain to `4.4e-4` at the worst steady bin.

Two envelopes are not this window:
- Frame 2, envelope 0. Sauti sets the k18 and k42 sine, and FFmpeg `s_indexmapped` is 0. The k42 boost relative error is `5.6%`.
- Frame 49, envelope 0. Sauti `delta` is 1 and FFmpeg `delta` is 0 on all 27 bins, so the k42 boost relative error is `4.0%`. `attack_env` is unchanged. An onset that lands on envelope 0 still cannot be told apart from no onset.

The tone was compared offline and was not written into `harmonic_preview`. The per-frame boost from this limiter, times `1024/sqrt(2)`, is the gain-adjusted QMF factor. In FFmpeg's raw domain that is `tone * 32768 * boost`. Steady frames 4–47, call = frame+1, same slot:
- k18 tone correlation is 1 and the relative residual is `7.1e-8`. The old single scale `51933.78` is this cap case.
- k42 tone correlation is 1 and the relative residual is `5.2e-5`. One global scale `42462` still leaves relative residual `0.030`.
- The k18 sum of the unchanged SBR-53d gain HF and that scaled tone matches FFmpeg `Y` to relative `9.4e-7`.
- The k42 sum relative residual is `0.0011`. The sine matches. What remains is the old gain HF: its best scale is still `105.8` rather than `32*sqrt(2)`, and that shape residual is `0.108`. SBR-53d is unchanged.

`harmonic_preview`, AAC-LC, the public QMF path, and `hf_sine_mix` do not use this boost. The gain-partition, gain-limit, and temporal-reference-gain tests pass. The probe still exits 1 on this fixture after a complete trace. That exit is the existing public-parser limit.

### SBR-73 — experimental tone uses the limiter boost (2026-11-14)

`sauti_aac_sbr_temporal_harmonic_scale` multiplies a tone the unscaled preview has already added. The factor is that slot's limiter boost times `1024/sqrt(2)`. `harmonic_preview` still adds `sqrt(E/(1+Q))`, and its existing test still expects `sqrt(8)`. phase+2 is not scaled.

The probe keeps the windowed limiter boost on every frame. The sine is emitted one loop later, and a `source=0` slot reads the previous frame's boost. A missing window leaves that bin unscaled. The experimental sine PCM uses the scaled QMF. `SAUTI_SBR73_SAUTI_SCALED` writes k18 and k42. SBR-53d, the units path, AAC-LC, the public QMF path, and `hf_sine_mix` keep their previous values.

On `he_test.aac` the scaled QMF matches that formula to relative `3e-8` (1600 slots, frames 0–49). Steady frames 4–47, call = frame+1, same slot, after multiplying by `32*sqrt(2)`:
- k18 tone relative residual is `8.3e-8`. The sum with the unchanged gain HF is `9.4e-7`.
- k42 tone relative residual is `5.2e-5`. The sum is `0.00106`. What remains is the SBR-53d gain HF.

The harmonic-preview test passes, including the new scale case. The probe still exits 1 after the complete dump.

### SBR-73 — experimental gain HF reads analysis slot t-6 (2026-11-14)

`sauti_aac_sbr_temporal_hf_shift_gain` multiplies one emitted HF frame by a gain the caller already converted. Output slot `t` reads analysis slot `t-6` of that frame. When `t<6` it reads the previous frame at slot `t+26`. The probe passes the limiter's post-boost `hf_gain` times `1024/sqrt(2)`. A missing previous frame, or a band with no saved window, keeps the unshifted SBR-53d sample. The function does not scale by `32768` itself.

SBR-53d, `gain_one`, the unscaled oscillator, phase+2, the units path, AAC-LC, the public QMF path, and `hf_sine_mix` are unchanged. `SAUTI_SBR73_SAUTI_SCALED` is still the old gain plus the scaled tone. `SAUTI_SBR73_SAUTI_GAINHF` writes the shifted product. `SAUTI_SBR73_SAUTI_SUM` adds the already scaled tone on bins where a tone was added. The experimental sine PCM uses that sum.

On `he_test.aac`, steady frames 4–47, call = frame+1, same slot, after multiplying by `32*sqrt(2)` (1408 slots):
- Gain-only k18 relative residual is `1.31e-7`, best scale `1.000000018`. Slots `t<6` are `1.52e-7` and slots `t>=6` are `1.25e-7`.
- Gain-only k42 relative residual is `0.00161`, best scale `1.000057`. Slots `t<6` are `0.00177` and slots `t>=6` are `0.00157`.
- The unchanged SBR-53d gain HF on k42 is still best scale `2.34`, shape residual `0.108`. All 1408 steady k42 bins of the new product differ from that path.
- The sum against full `Y` is k18 relative `8.7e-8` and k42 relative `5.2e-5`. The sinusoid is much larger than the gain HF, so this sum sits on the tone residual.
- Frame 0 stays on SBR-53d for all 32 slots. That frame has no saved limiter window.

The apply test passes, including the new shift case. The probe still exits 1 after the complete 50-frame dump. The remaining k42 gain-only residual is `0.00161`, the same number as the offline product of this shift.

### SBR-73 — the k42 gain residual is the quiet-band analysis floor (2026-11-14)

The shifted product was split on the same steady 1408 slots. FFmpeg `Y` after `g_filt` equals `X_high[t+2]` times the unsmoothed post-boost gain to relative `3.5e-8` at both k18 and k42. Every one of the 50 headers has `bs_smoothing_mode=1`, so `h_SL` is 0 and the smoother does not run. Replacing only the gain leaves the k42 residual at `0.00163`. Replacing only `X` drops it to `1.67e-4`, which is the limiter-gain mismatch. The `0.00161` is the pre-gain sample.

That sample is low band 14 copied into k42. Sauti frame N, analysis slot `t`, times `32768`, against FFmpeg call N+1, `X` slot `t+8` (and the next call's slots 2..7 for analysis slots 26..31):

| Band | Relative | Absolute RMS | Power relative, worst frame |
| --- | --- | --- | --- |
| low 2 (k18) | `3.58e-7` | `0.066` | `8.4e-7` |
| low 7 (k35) | `3.90e-4` | `0.0033` | `1.1e-4` |
| low 14 (k42) | `1.62e-3` | `0.0044` | `5.3e-4` |

`X` at k42 has RMS `2.70`. `Y` after gain has RMS `0.019`. Full `Y` has RMS `10.37`, so this gain-sample error is relative `3.0e-6` of the assembled bin. The sum's `5.2e-5` is the tone. Per frame, the k42 tone scale misses FFmpeg by the same amount as the boost, worst `7.88e-5`, with no extra shape.

The window sum `u[64]` already matches the FFmpeg dump to relative `3.24e-7` (absolute RMS `2.0e-8`). A float accumulation of the same five taps is not closer. Running Sauti's direct modulator on FFmpeg's own `u` still leaves relative `8.2e-4` on low band 14 (absolute RMS `0.0022`) and `7.2e-8` on low band 2. The rest of the band-14 error is that `u` difference after the `32768` modulation (absolute RMS `0.0036`). Both pieces are the float MDCT rounding of a small projection, not a wrong slot, a missing smooth, or a bad gain formula.

**No production change.** The analysis, the modulator, SBR-53d, and the experimental shift stay as they are. `32768` was not written into the QMF. AAC-LC and the public QMF path were not edited.
