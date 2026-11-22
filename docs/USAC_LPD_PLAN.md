# Plan implementacji USAC LPD (ACELP / TCX / FAC) w MaktabaSauti

Stan: 2026-11-21. Cel: **własna** implementacja bez obowiązkowej zależności od libxaac; libxaac (Apache 2.0, `~/Sauti-tools/libxaac`, commit 2fbadd5, poza repozytorium) służy jako niezależny wzorzec testowy i źródło strumieni. Zasada użytkownika: **nic nie jest kopiowane automatycznie**; każdy element (kod, tablica, stała) przeznaczony do biblioteki musi mieć udokumentowane pochodzenie i zgodność licencyjną (wpis w rejestrze `docs/BIBLIOTEKA_PLAN.md`: źródło, plik / linie, commit, licencja, suma kontrolna wyciągniętych danych, zgoda).

## 0. Stan wykonania (2026-11-21, SBR-110)

| Etap | Stan | Uwagi |
|---|---|---|
| L0 składnia | ZROBIONY | `sauti_usac_lpd.c`; surowe indeksy; `fac_data()` także w strumieniu FD |
| L1 LPC | ZROBIONY | AVQ RE8, LSF, LSP, LPC, interpolacja; LSF zgodne z libxaac co do 0,000000 Hz |
| L2 ACELP | ZROBIONY | bit w bit względem float libxaac; start ramki zaczynającej się od ACELP po FD (SBR-111) |
| L3 TCX | ZROBIONY | 98 – 130 dB względem float libxaac, 72 – 78 dB względem 16-bitowego `xaacdec` |
| L4 FAC / przejścia | ZROBIONY (SBR-111) | FAC wewnątrz ramki, FD ↔ LPD (osiem okien krótkich, start, `ONLY_LONG`, ACELP / TCX po FD), `lpd_bpf_fix`; niesprawdzone (koder nie produkuje): `LONG_STOP` wprost po LPD |
| L5 CPE / eSBR | CZĘŚCIOWO | para kanałów obu LPD zweryfikowana (`usac_l7`, 70 dB), eSBR 2:1 podłączone (bez strumienia LPD + eSBR: koder libxaac nie koduje LPD przy 32 / 48 kHz); jeden kanał FD + jeden LPD: `-1`; rdzeń 768 z LPD: `-1` |
| L6 silnik | ZROBIONY dla strumieni LPD | stan LPD w stanie kanału (kopiowany na punktach kontrolnych), seek, pre-roll, fuzzing |

Tablice: pochodzenie i zgoda: `docs/BIBLIOTEKA_PLAN.md` (rejestr), generator `tools/gen_usac_lpd_tables.py`.

## 1. Czym jest LPD i co już mamy

W USAC każda ramka kanału ma `core_mode`: 0 = FD (MDCT + kodowanie arytmetyczne; mamy), 1 = LPD (liniowa predykcja: 4 podramki po 256 próbek rdzenia, `lpd_mode` wybiera na każdą ćwiartkę ACELP albo TCX 256 / 512 / 1024). Wykryte dziś: nasz dekoder rozpoznaje `core_mode = 1` i zwraca „nieobsługiwane” (−1) bez błędnego dekodowania (także gdy LPD siedzi w pre-rollu). Brakuje całej ścieżki LPD.

## 2. Wzorce i strumienie (zmierzone, patrz `docs/AAC_FAMILY_ASM.md` SBR-108)

* `xaacenc -aot:42 -usac:2|0 -esbr:0` przy **16 i 24 kHz** (nie 32 / 48) koduje prawdziwe strumienie LPD (td = same LPD, switched = FD / LPD); CBR, np. 768 B / ramkę przy 16 kHz. Pierwsza jednostka niesie pre-roll (2 jednostki). `xaacdec` je dekoduje (16 bit, zawijanie przy przesterowaniu).
* FDK (2.0.3): w USAC tylko konfiguracje 1–2 i bez 768; ma LPD (wzorzec dodatkowy dla kanałów mono / stereo, wymaga korekty +5 dB).
* FFmpeg: brak LPD.
* Z jednej pary (koder + dekoder) da się budować **zestaw zgodności per funkcja**: sygnały dobierane tak, by koder wybierał konkretny tryb (np. szum → TCX, mowa-podobny sygnał impulsowy → ACELP, przejścia → FAC), a wynik porównywany z `xaacdec` na PCM. Pośrednie sygnały (LSF, pobudzenie) można podejrzeć wyłącznie na zinstrumentowanej KOPII libxaac poza repozytorium (Apache 2.0, instrumentacja nie wchodzi do Sauti), tak jak zinstrumentowaną kopię FDK używano dla eSBR.

## 3. Etapy (każdy: własny kod, test zgodności z libxaac, mutanty, docs, bramka)

| Etap | Zawartość | Weryfikacja |
|---|---|---|
| L0 | Składnia `lpd_channel_stream()`: `acelp_core_mode`, `lpd_mode`, `bpf_control_info`, `core_mode_last` / `fac_data`, `lpc_data` (kwantyzacja LSF: 1. stopień 8-bitowy, AVQ 2. stopnia, tryby `mod[]`), parametry ACELP (okres pitch absolutny / względny, indeksy książki algebraicznej 12–64 bity, wzmocnienia), TCX (`tcx_global_gain`, noise factor, widmo arytmetyczne z kontekstem TCX), pre-roll i pęknięcia | czysty parser: te same granice bitowe co libxaac na strumieniach 16 / 24 kHz (suma bitów ramki = rozmiar), mutanty |
| L1 | LPC: dekwantyzacja LSF (książka + AVQ), LSF → LSP → LPC, interpolacja podramek (4 zestawy / ramkę, 16. rząd), ważenie | porównanie z zinstrumentowaną kopią libxaac (LSF / LPC) i z PCM |
| L2 | ACELP: książka adaptacyjna (opóźnienie pitch z ułamkiem 1/4 lub 1/2, filtr interpolacyjny), książka algebraiczna (impulsy w ścieżkach, dekodowanie indeksów), wzmocnienia, filtr syntezy LPC, postfiltry (basowy / pitch), de-emfaza | ramki tylko-ACELP (`-usac:2` przy sygnale impulsowym), PCM vs `xaacdec` |
| L3 | TCX: dekodowanie widma (kodowanie arytmetyczne z kontekstem TCX), kształtowanie szumu w dziedzinie widma z LPC (FDNS), wypełnianie szumem, wzmocnienie, odwrotne MDCT z oknami niskiego nakładania, LTP / bass postfilter | ramki TCX (szum, muzyka) |
| L4 | FAC (Forward Aliasing Cancellation) i przejścia FD ↔ LPD, `core_mode_last`, kształty okien na granicach, ramki mieszane `-usac:0` | strumienie „switched” |
| L5 | LPD w parze kanałów (CPE), z M/S i predykcją zespoloną; LPD z eSBR (rdzeń 16 / 24 kHz dla 2:1 daje wyjście 32 / 48 kHz) | stereo, eSBR |
| L6 | Integracja z silnikiem (checkpointy niosą stan LPD: pamięć syntezy, filtry de-emfazy, historia LSF / LPC, pamięć adaptacyjnej książki, FAC), seek, pre-roll (już gotowy), fuzzing | testy silnika |

Szacunek: L0–L2 to „dekoder mowy” (ACELP) — najwięcej logiki, najmniej tablic; L3 dokłada MDCT i kontekst arytmetyczny (reużywamy naszego kodera arytmetycznego FD tam, gdzie wspólny); L4 jest najdelikatniejszy (zgodność na granicach). Wielkość odpowiednika w libxaac (do orientacji, NIE do kopiowania): ~2 300 linii logiki (`ixheaacd_acelp_decode.c` 685, `_acelp_bitparse.c` 594, `_lpc.c` 828, `_lpc_dec.c` 279, `_acelp_mdct.c` 243, `_acelp_tools.c` 203) plus tablice.

## 4. Tablice i pochodzenie (do decyzji, tablica po tablicy)

Potrzebne dane (nie da się ich wyprowadzić ze wzoru; wartości normatywne w ISO/IEC 23003-3): książka LSF 1. stopnia (16 × 256 liczb: w libxaac `ixheaacd_dico_lsf_abs_8b_flt` w `ixheaacd_rom.c`), średnie LSF (`mean_lsf_conc`), wartości początkowe LSF (`lsf_init`), filtr interpolacyjny ACELP (`ixheaacd_interpol_filt`), ROM kwantyzacji AVQ (`ixheaacd_avq_rom.c`), tablice kontekstu arytmetycznego TCX (częściowo wspólne z FD, które już mamy z FFmpeg LGPL), tablice okien (sinus / niskie nakładanie: wyprowadzalne ze wzoru), współczynniki FAC / postfiltrów (część wyprowadzalna). Warianty pozyskania, dla każdej tablicy osobno: (A) **wyprowadzenie z normy** (wymaga tekstu normy lub publikacji AMR-WB+ / USAC) — preferowane, brak zależności licencyjnej; (B) **tablica z libxaac** (Apache 2.0): dozwolona po Twojej zgodzie, z wpisem w rejestrze (plik, linie, commit 2fbadd5, suma SHA-256 danych), z notą licencji Apache 2.0 i plikiem NOTICE, w osobnym module wymiennym (`sauti-tables`), z testem, że dane są identyczne z danymi z normy tam, gdzie da się to sprawdzić niezależnie (np. przez wyprowadzone wzory); (C) pomiar z czarnej skrzynki (np. odpowiedź impulsowa dekodera) — odrzucony jako źródło tablic (kruche, wątpliwe prawnie). Patenty: AMR-WB+ / USAC LPD jest objęty patentami MPEG; to nadal pytanie do prawnika, niezależne od licencji kodu.

## 5. Ryzyka i pułapki znane z dotychczasowej pracy

* Odciski i testy zgodności tylko na własnym generatorze maskują błędy (pre-roll i faza szumu były niewidoczne, dopóki nie pojawił się prawdziwy koder): LPD testować OD RAZU na strumieniach libxaac.
* Wzorzec 16-bitowy zawija przy przesterowaniu; FDK stosuje normalizację głośności (+5 dB): w skryptach dopasowywać wzmocnienie i wykluczać próbki ≥ 0,999.
* Zgodność bit po bicie z libxaac (stałoprzecinkowy) nie jest celem: celem są 70+ dB, a wzorcem numerycznym definicja z normy.
* LPD wymaga szybkiego stanu na checkpoint: pamięć syntezy 1024 próbek + książka adaptacyjna + LPC: policzyć wpływ na rozmiar punktów kontrolnych (jak przy USAC 8 kanałów: stan na stercie).
