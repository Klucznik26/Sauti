# Wykaz pochodzenia danych i kodu (2026-11-19)

Cel: wiedzieć, co w Sauti może wymagać zmiany lub izolacji, zanim biblioteka będzie rozpowszechniana jako freeware z zamkniętym kodem (`BIBLIOTEKA_PLAN.md`). **To nie jest ocena prawna.** Kolumna „pochodzenie wg kodu" cytuje komentarze w plikach; ich prawdziwości tu nie zweryfikowałem (nie ma metadanych, kto i skąd przepisał dane), więc traktuję je jako stan deklarowany. Rozdzielam trzy rzeczy: **dane normatywne** (tablice zdefiniowane w ISO/IEC 14496-3), **algorytmy** (kod własny wg normy vs kod odtworzony z FFmpeg) i **wzorce testowe** (FFmpeg, FAAD2, libfdk-aac używane tylko w testach i narzędziach).

## 1. Tablice danych w wydawanym kodzie

| plik | zawartość (liczb) | pochodzenie wg kodu | wyprowadzalne ze wzoru? | ryzyko | działanie |
|---|---|---|---|---|---|
| `sauti_aac_sbr_qmf_synth_window.c` | okno prototypu QMF, 640 współczynników (1289) | „Prototype coefficients **sourced from** FFmpeg n6.1 `aacsbrdata.h` (LGPL-2.1+)" | **nie** (filtr optymalizowany numerycznie, tabela w normie) | **wysokie**: zadeklarowane źródło to plik FFmpeg | wymienić na wartości z ISO/IEC 14496-3 (tabela 4.A.89); do tego czasu izolować w `sauti-tables` |
| `sauti_aac_sbr_noise_rom.c` | ROM szumu SBR, 512 par zespolonych (1542) | „standardized reference data, **cross-checked against** FFmpeg `sbrdsp_template.c`" | **nie** (ustalona tablica w normie) | średnie/wysokie | j.w. (tabela 4.A.88) |
| `sauti_aac_tables.c` | księgi Huffmana AAC: skalowanie + widmowe 1–11, 1362 słowa kodowe (1380) | „cross-checked against FFmpeg `aactab.c` (LGPL)"; plik zawiera własną uwagę o przeglądzie licencyjnym | nie (dane normatywne, ale to konkretny układ i kolejność) | średnie | wymienić/zweryfikować wobec ISO (Annex 4.A); do tego czasu izolować |
| `sauti_aac_sbr_huffman.c` | księgi Huffmana SBR (98) | „**independently reconstructed** from normative codewords and cross-checked against FFmpeg SBR VLC and FDK-AAC trees" | częściowo (kody kanoniczne z długości) | niskie/średnie | zweryfikować, że odtworzenie jest niezależne od FFmpeg; udokumentować |
| `sauti_aac_syntax.c` (część PS) | księgi Huffmana PS: IID, ICC, IPD, OPD (~190) | „Numeric codewords were **cross-checked against** FFmpeg n6.1 `aacpsdata.c` (Alex Converse, LGPL-2.1+)"; własna uwaga o przeglądzie | nie | średnie | j.w. |
| `sauti_aac_bands.c` | przesunięcia pasm skalowania wg częstotliwości (223) | „Numeric tables **validated against** FFmpeg `aactab.c`" | częściowo (małe tablice faktograficzne z normy) | niskie | zweryfikować wobec normy; małe, faktograficzne |
| `sauti_aac_ps_qmf_low.c` | prototypy filtra hybrydowego PS (43) | „Numerical reference: ISO/IEC 14496-3 PS filter prototypes; compare against FFmpeg `aacps_tablegen.h`" | nie (tabele filtrów w normie) | niskie/średnie | zweryfikować wobec normy |
| `sauti_aac_ps_decorrelate.c` | stałe all-pass i opóźnień (23) | „follow ISO/IEC 14496-3 PS; **audited against** FFmpeg tablegen" | **tak** (wzory trygonometryczne) | niskie | kod własny; udokumentować wzory |
| `sauti_aac_twinvq_tables.c` | tablice TwinVQ (AOT 7): księga LSP + predyktor MA, księgi widma (długie / krótkie), księga obwiedni Bark, księga pitch (5672 liczby) | **oprogramowanie referencyjne ISO/IEC 14496-5**, `src_tf/ntt_get_cdbk.c` (nota: licencja wolna dla produktów zgodnych z MPEG-4 Audio, nota musi być w kopiach i pochodnych; ostrzeżenie o patentach), wygenerowane `tools/gen_twinvq_tables.py`; nota w nagłówku pliku i w `licenses/ISO-MPEG4-AUDIO-REFSOFT.txt` | **nie** (dane z normy, nie wzór) | **średnie** (warunek zgodności z MPEG-4 Audio; patenty) | zgoda użytkownika 2026-11-28; do oceny prawnej przed wydaniem |
| `sauti_aac_imdct.c`, okna sinus/KBD, twiddle'e | liczone w czasie działania | kod własny (SBR-93) | tak | brak | — |
| `sauti_decoder_dvsm.c`, `ym`, `avr`, … | retro | niezwiązane z FFmpeg | — | poza zakresem tego wykazu | — |

## 2. Algorytmy (kod)

| moduł | stan | działanie |
|---|---|---|
| `sauti_aac_sbr_channel.c`, `sbr_qmf_*`, `ps_stereo`, `ps_mix`, `ps_phase`, `ps_map`, parsery (`sbr_payload`, `syntax`) | napisane od zera wg struktury normy (SBR-82…92), weryfikowane względem FFmpeg i FAAD2 jako wzorców | kod własny; zachować historię ustaleń w `AAC_FAMILY_ASM.md` |
| `sauti_aac_sbr_gain_limit_partition.c` (używany przez `sbr_channel`) | komentarz: „**Numerics follow FFmpeg `aacsbr.c:sbr_gain_calc()`**" | **przeglądnąć i w razie potrzeby przepisać wg normy**; sprawdzić, czy kod jest własny, czy odtworzony z FFmpeg |
| moduły „audit/preview/temporal_*/ps5_*/he_adts_ps/probe" | pozostałości toru badawczego (SBR-75…81), część nadal linkowana przez ładunek SBR | **wyłączyć z wydania** (to narzędzia badawcze, nie dekoder); linker usuwa tylko 181 sekcji, więc wymaga to refaktoryzacji zależności `sbr_payload` |
| LC: `lc_frame`, `cpe`, `lc_sections`, `scalefactors`, `spectral`, `tns`, `pns`, `dequant`, `window`, `lc_synth` | kod własny; część cech zgodna z FFmpeg co do zaokrągleń float (TNS w float32), PNS własny skorelowany wg normy | przejrzeć komentarze „match FFmpeg" pod kątem pochodzenia |
| `sauti_decoder_ffmpeg.c` | adapter do libavcodec (LGPL), opcjonalny zapasowy dekoder odtwarzacza | **nie wchodzi do wydawanej biblioteki** |

## 3. Wzorce testowe (poza wydaniem)

FFmpeg (LGPL; własny klon instrumentowany w `~/Sauti-tools`), FAAD2 (GPL, ładowany przez `dlopen` w `tools/sauti_faad_oracle.c`), libfdk-aac (licencja Fraunhofera; dekoder i enkoder `fdkaac` tylko do generowania strumieni testowych). **Żaden z nich nie może być częścią wydawanego pakietu ani linkowany do biblioteki**; używane są wyłącznie w narzędziach i skryptach testowych, które nie wchodzą do wydania. Wyniki porównań (liczby dB) nie przenoszą na Sauti żadnej licencji.

## 4. Kolejność działań (uzgodniona)

1. Teraz: ten wykaz + usunięcie współdzielonego stanu (zrobione dla AAC).
2. Ocenić **każdą tablicę osobno** (nie wszystkie liczby podlegają ochronie): dla tabel normatywnych potrzebne jest **źródło niezależne od FFmpeg**, czyli norma ISO/IEC 14496-3 (jej zakup lub dostęp po stronie właściciela projektu). Do tego czasu tablice z sekcji 1 oznaczone jako „izolować" trafiają do osobnego modułu `sauti-tables`.
3. Sprawdzić licencję każdego źródła, sposób łączenia i możliwość zastąpienia komponentu LGPL przez użytkownika (to samo wydzielenie nie wystarcza).
4. Przepisać `sbr_gain_limit_partition` i inne moduły z komentarzem „follow FFmpeg" wg normy, jeśli przegląd wykaże odtworzenie z FFmpeg.
5. Wyłączyć z wydania adapter FFmpeg i kod badawczy.
6. Przed wydaniem: przegląd prawnika (w tym patenty AAC).
