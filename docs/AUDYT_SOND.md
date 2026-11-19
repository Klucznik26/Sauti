# Audyt bezpieczeństwa sond (`probe`) i rozpoznawania formatów (2026-11-20)

Zakres: wszystkie funkcje `probe` rejestru domyślnego MaktabaSauti (19 kodeków), sondy starego API poza tym rejestrem, mechanizm rozpoznawania w rdzeniu
(`mks_decoder.c`, `mks_reader.c`, `mks_registry.c`, `mks_io.c`) oraz kod bibliotek zewnętrznych wykonywany przy rozpoznawaniu i otwieraniu (minimp3, stb_vorbis, dr_flac, tml).
Punkt wyjścia: błąd minimp3 z commita `b2bdd74` (niezainicjowane `free_format_bytes`), ujawniony, gdy nowy rdzeń zaczął sondować WSZYSTKIE kodeki na każdym pliku.

## 1. Metoda

1. **Przegląd kodu** każdej sondy i każdej funkcji, którą wołają (parser nagłówka ADTS w asemblerze, `sauti_aac_{mp4,latm,adif}_probe`, `mp3dec_detect_cb`).
2. **Analiza statyczna:** `gcc -fanalyzer -Wuninitialized -Wmaybe-uninitialized -Wnull-dereference -Warray-bounds=2 -Wstringop-overflow=4 -Wshift-overflow=2` na wszystkich
   plikach z sondami i opakowaniami: bez ostrzeżeń (analiza statyczna nie wykryła żadnego z błędów poniżej: wszystkie znalazło dopiero wykonanie na uszkodzonych danych).
3. **Harness dynamiczny** `tests/test_maktabasauti_probe_audit.c` (`make test-maktabasauti-probe-audit`), 3 budowy: ASan+UBSan (`-fno-sanitize-recover`),
   zwykła `-O2 -ftrivial-auto-var-init=pattern` (zmienne automatyczne wypełniane wzorcem: wykrywa niezainicjowane odczyty także bez MSan) i `-O0` bez sanitizerów.
   Każde wywołanie sondy dostaje bufor o **dokładnym rozmiarze** w osobnym mapowaniu: strony `PROT_NONE` tuż za końcem albo tuż przed początkiem, bufor tylko do odczytu
   (zapis do wejścia = SIGSEGV), a stos jest przed wywołaniem zaśmiecany różnymi wzorcami. Handler awarii podaje kodek, plik, tryb i długość.
   - Korpus: syntetyczne pliki wszystkich formatów (generowane z kodu), obce dane (AVI, PNG, gzip, tekst, Opus, WavPack, zera, 0xFF, 1–3 bajty), warianty MP3 (ID3v2, ID3v1,
     APEv2, free-format, ID3 bez ramek, APEv2 z rozmiarem ≥ 2^31), fixtury AAC (`tests/data`) i prawdziwe pliki FLAC/Ogg/MP3/WAV/AIFF/VOC/Opus/ALAC (`tests/data/probe_real.*`).
   - Przypadki: każda sonda × każdy plik (macierz rozpoznań: właściciel musi rozpoznać swój plik, obcy kodek nie może go przejąć poza udokumentowanymi nakładaniami ADTS/MP3/FFmpeg);
     każde obcięcie 0..96 bajtów i długości graniczne (±1..±227, 4095–4097, połowa); każdy bajt nagłówka (pierwsze 128 B) ustawiony na 0x00/0x01/0x7F/0x80/0xFE/0xFF;
     każde 4 bajty nagłówka (BE i LE) ustawione na 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF i inne wartości graniczne; dane losowe 0–5000 B; „etykieta formatu + losowy ogon”; losowe mutacje i obcięcia.
   - Faza `open`: te same dane otwierane wszystkimi kodekami bezpośrednio (host z buforem ochronnym jako „całe wejście”), z odczytem, `seek`, `reset`, `close` i kontrolą licznika alokacji
     oraz czasu (jedna operacja > 20 s = zawieszenie). To konieczne, bo rdzeń woła `open` każdego kodeka, którego sonda przepuściła.
4. **Wrogie wejście** `tests/test_maktabasauti_hostile_io.c` (`make test-maktabasauti-hostile-io`): callbacki `io` łamiące kontrakt, błąd `io` w każdym punkcie, odczyty po 1 bajcie,
   deklarowane rozmiary (−5 … `INT64_MAX`), `seek` kłamiący o pozycji, skrajne wywołania usług hosta przez moduł (`seek` z `INT64_MIN/MAX` i każdym `whence`, `read`/`peek` z `SIZE_MAX`),
   sondy modułów zwracające `INT32_MIN`/`INT32_MAX`/ujemne (obcięcie do 1..100, kolejność kandydatów, kod błędu najwyższej rangi).
5. **Regresje bibliotek zewnętrznych** `tests/test_third_party_hardening.c` (`make test-third-party-hardening`).
6. **Fuzzing:** ten sam harness w pętli losowych mutacji na buildzie ASan + UBSan w trybie ciągłym (błąd UBSan nie przerywa, więc jeden przebieg zbiera wszystkie miejsca).
   Łącznie ok. 40 przebiegów z różnymi ziarnami; ostatnia seria po wszystkich poprawkach: 12 przebiegów × (100 000 iteracji sond + 60 000 mutowanych otwarć wszystkich kodeków),
   **0 znalezisk** (wcześniejsze serie znalazły F3–F9 i dwa miejsca UB w dr_flac, naprawione). Brak libFuzzera (brak clanga): mutator wbudowany w harness.
7. **Wielowątkowość:** `tests/test_maktabasauti_threads.c` rozszerzony o równoległe rozpoznawanie ~440 wariantów (korpus + 8 mutacji każdego pliku) rejestrem domyślnym i własnym; wynik
   (kod, parametry strumienia, skrót PCM) musi być równy wynikowi jednowątkowemu, licznik alokacji wątku wraca do zera. W CI pod ThreadSanitizerem (**wymagany**).

## 2. Przegląd sond

| kodek (rejestr) | sonda | warunki | ocena |
|---|---|---|---|
| wav-pcm | `n>=12`, `RIFF`, `WAVE` @8 | długość sprawdzona przed odczytem | bezpieczna |
| creative-voc-pcm | `n>=26`, 20 B etykiety, `le16` @20/22/24, `le16(p+20)<=n` | j.w. | bezpieczna |
| aiff-pcm | `n>=12`, `FORM`, `AIFF/AIFC` @8 | j.w. | bezpieczna |
| aac-lc-native-adts, -mono-adts, -multi-adts, aac-he-native-adts, aac-he-cpe-native-adts | `size>=7` + `sauti_aac_adts_parse_asm` (NASM: sprawdza `size>=7` i 9 przy CRC, długość ramki ≤ rozmiar) | czyta bajty 0..8 po sprawdzeniu; kanały/AOT/liczba bloków wg nagłówka | bezpieczne; nakładanie (stereo ADTS rozpoznają LC i HE-CPE) z założenia, wygrywa kolejność rejestru |
| aac-native-mp4 | `size>=12`, `ftyp` @4 | j.w. | bezpieczna |
| aac-native-latm | `size>=6`, bajty 0–2, `data[3+len]`, `data[4+len]` | `3+len>size` odrzuca; drugi odczyt po `size>=3+len+2` | bezpieczna |
| aac-native-adif | `size>=8`, `ADIF` | j.w. | bezpieczna |
| ogg-vorbis-stb, midi-basic-synth, flac-drflac, atari-avr-pcm, atari-falcon-dvsm-pcm, atari-falcon-digital-tracker, atari-ym2149-experimental | `memcmp` etykiety po sprawdzeniu `n` (YM: 4 lub 12 B) | j.w. | bezpieczne |
| mp3-minimp3 | `length>12`, odrzuca RIFF/WAVE/OggS, `mp3dec_detect_buf` | **błąd F1 (`b2bdd74`), błąd F2 (UB przesunięć APEv2)** | naprawiona, patrz niżej |
| poza rejestrem: opus-opusfile, aac/alac/webm (FFmpeg), wavpack, sndh | `n>=36` + okno ≤ 4096 B; `n>=12/7/4`; `n>=4`; `n>=16` + okno ≤ 512 B | j.w. | bezpieczne (Opus i FFmpeg w harnessie; WavPack i SNDH przejrzane ręcznie: pierwsze wymaga libwavpack, drugie AtariAudio) |

Wszystkie sondy rejestru zwracają 0 lub 50 (opakowanie) — wynik poza 0..100 rdzeń obcina, test to sprawdza.

## 3. Wykryte problemy i poprawki

Numeracja: F = znaleziony i naprawiony błąd, R = ryzyko opisane (niezmieniane).

| nr | miejsce | opis | skutek | poprawka | test regresji |
|---|---|---|---|---|---|
| F1 | minimp3 `mp3dec_detect_cb` | niezainicjowane `free_format_bytes`; duża ujemna wartość ze stosu → `mp3d_match_frame` czytał przed początkiem bufora | SEGV przy sondowaniu plików nie-MP3 (CI TSan, `f88db37`) | inicjalizacja `= 0` (`b2bdd74`) | `tests/test_decoder_mp3.c` (zasmiecony stos), audyt: stary kod pada w buildzie plain i pattern |
| F2 | minimp3 `mp3dec_skip_id3v1` (APEv2), `mp3dec_check_vbrtag` (Xing) | bajt ≥ 128 przesuwany o 24 bity jako `int` | UB (UBSan); w praktyce błędny rozmiar tagu | przesunięcia bez znaku | `test_third_party_hardening` (Xing 0xFF000000, APE 0xFF000000), korpus `syn_ape_huge.mp3` |
| F3 | stb_vorbis `setup_malloc` | rozmiar `sizeof(x)*N` obcinany do `int` (np. liczba komentarzy 0x20000001 → alokacja 8 B, potem pętla zapisuje daleko poza nią) | **przepełnienie sterty** na spreparowanym Ogg | rozmiar `size_t`, odmowa > `INT_MAX-8` | hardening (8 wartości liczby komentarzy, 6 długości vendora, 3 długości komentarza) |
| F4 | stb_vorbis `vorbis_deinit` | wołana przy każdym błędzie ustawień; zwalniała składowe niewypełnionych struktur (pamięć z `malloc`) | **SIGSEGV/nieprawidłowe `free()`** na uszkodzonym nagłówku ustawień | `setup_malloc` zeruje pamięć | hardening: obcięcie na każdej długości + 3 wartości w każdym z 4096 pierwszych bajtów (12288 wariantów), ASan |
| F5 | stb_vorbis | bloki `setup_temp_malloc` wyciekały przy każdym błędzie `start_decoder`; wskaźnik do bloku tymczasowego mógł trafić do `setup_free` (`c->codewords`) | wyciek pamięci na każdym uszkodzonym pliku (DoS przy powtarzanym otwieraniu); naiwna naprawa dałaby podwójne zwolnienie | lista bloków w dekoderze, `setup_free` pomija bloki żywe, zwolnienie reszty na końcu `vorbis_deinit` | j.w. (LeakSanitizer) |
| F6 | stb_vorbis | długości vendor/komentarzy i liczba komentarzy bez limitu; `comment_list == NULL` przy `comment_list_length > 0` po błędzie alokacji | alokacje rzędu GiB, miliardy iteracji, dereferencja NULL | limity 16 MiB / 65536, zerowanie licznika, strażnik NULL | hardening |
| F7 | stb_vorbis `get32_packet` | przesunięcie wartości ujemnej (`EOP = -1`) | UB (UBSan) | przesunięcia bez znaku | audyt (UBSan) |
| F8 | dr_flac `drflac__calculate_prediction_32` | przepełnienie `int` w akumulacji predykcji | UB (UBSan, gcc; atrybut `no_sanitize` tylko dla clanga) | arytmetyka `uint32_t` (wyniki bit w bit takie same) | audyt (UBSan), regresja FLAC w `test-maktabasauti-regression`/`make test` |
| F9 | tml.h | `memcpy(NULL, …, 0)`; MTrk `track_header[4] << 24` jako `int` | UB (UBSan) | strażnik, przesunięcia bez znaku | audyt (UBSan) |
| F10 | rdzeń `mks_reader.c` `fill`/`read`/`mks_reader_map` | `io.read` zwracający więcej niż zażądano zwiększał `len`/`used` ponad pojemność bufora | **przepełnienie sterty** (odczyt/zapis poza buforem), dalej nieskończona pętla przy `INT64_MAX` | kontrola `got <= zażądano`, w przeciwnym razie `E_IO` | `test_maktabasauti_hostile_io` (na starym kodzie ASan: heap-buffer-overflow) |
| F11 | rdzeń `mks_reader.c` | ujemny kod z `io.read/seek` rzutowany na `maktabasauti_result_t` (`INT64_MIN` → 0 = `OK`); `mks_reader_map` zwracał `OK` z niezainicjowanymi `data/size` | błędne „powodzenie” otwarcia, użycie niezainicjowanych wskaźników | `io_err()`: tylko znane `E_*`, reszta `E_IO` | hostile-io |
| F12 | rdzeń `mks_reader_seek` | `cur + offset`, `sz + offset` bez kontroli przepełnienia ze znakiem | UB przy skrajnym `offset` z modułu | kontrola przed dodaniem, `E_OUT_OF_RANGE` | hostile-io (`seek` `INT64_MIN/MAX`, każdy `whence`), UBSan |
| F14 | stb_vorbis `crc32_init` | globalna tablica CRC wypełniana przy każdym otwarciu (oryginalny komentarz: „always init it, to avoid multithread race conditions”) | **wyścig danych** przy równoległym otwieraniu plików Ogg; znalazł go ThreadSanitizer w CI dopiero po dodaniu równoległego rozpoznawania (wcześniej żaden test wielowątkowy nie otwierał Vorbis) | tablica stała w czasie kompilacji (`static const`), `crc32_init` puste | `scripts/test_maktabasauti_threads.sh` pod TSan (CI, wymagany) |
| F13 | rdzeń `mks_reader_map` | pojemność początkowa = deklarowany rozmiar (do 1 GiB) bez pokrycia w danych | niekontrolowana alokacja zależna od `io.size` | początkowo ≤ 64 MiB, dalej podwajanie wg faktycznie odczytanych danych | hostile-io (żadne pojedyncze żądanie > 64 MiB + 4 KiB dla kilkukilobajtowego wejścia) |

Zmiany wyłącznie w kodzie bibliotek zewnętrznych są opisane osobno w `third_party/<biblioteka>/README-SAUTI.md`; łatki do zgłoszenia upstream: `docs/upstream-patches/`
(nic nie zostało zgłoszone: wymaga decyzji właściciela projektu). PCM dla poprawnych plików nie zmienił się: `make test-maktabasauti-regression` (bit w bit wobec starego API), `make test`, `make test-aac-native`.

## 4. Pozostałe ryzyka

- **R1: wczytanie całości dla sond.** Kodeki z `NEEDS_FULL_BUFFER` dostają przy sondowaniu cały plik, więc każde otwarcie rejestrem domyślnym wczytuje całe wejście do pamięci
  (limit 1 GiB, wzrost tylko o faktycznie odczytane dane), także dla danych, których żaden kodek nie rozpozna. Sama sonda jest O(1), ale nie da się tego zmienić bez zmiany
  semantyki starych sond (część wymaga rozmiaru: ADTS „bez ramek częściowych”, LATM, VOC). Usunie to etap 2 (moduły strumieniowe z sondą po prefiksie 4 KiB).
- **R2: `seek` MIDI jest liniowy** w liczbie ramek (znane ograniczenie syntezatora bazowego); uszkodzony plik może deklarować godziny. Harness ogranicza odległość `seek`. Do naprawy snapshotami zdarzeń.
- **R3: kody zewnętrzne.** stb_vorbis jest porzucony przez autora (v1.22 z 2021), a fuzzing wykazał w nim kilka błędów; poprawki lokalne usuwają znalezione, **nie dowodzą braku innych**
  (dekoder: setup, residue, floor, IMDCT). Do rozważenia: własny dekoder Vorbis zamiast stb_vorbis w wydaniu. dr_flac i tml: znalezione wyłącznie UB, nie błędy pamięci; nie znaczy to, że fuzzing był wyczerpujący.
- **R4: pokrycie fuzzingu.** Mutator jest wbudowany (bez pokrycia kodu jak libFuzzer/AFL), korpus bez prawdziwych plików Opus/ALAC/WavPack/SNDH w fazie `open`
  (brak dekoderów w buildzie lub brak bibliotek); AAC SBR/PS na fixturach mutowanych losowo. Zalecenie: ciągły fuzzing libFuzzerem w CI nocnym (wymaga clanga).
- **R5: niezainicjowane odczyty** wykrywa tylko wzorzec stosu / `-ftrivial-auto-var-init=pattern` (ASan ich nie widzi, MSan niedostępny bez clanga); pamięć sterty
  z `malloc` jest zatruwana tylko w buildzie testów rdzenia.
- **R6: moduły zewnętrzne** (przyszłe) mogą naruszyć zasady sondy; rdzeń nie izoluje sondy (brak sandboxa), tylko obcina wynik. Sonda dostaje `const` bufor, ale dla całego wejścia jest to kopia
  rdzenia, więc złośliwa sonda mogłaby ją zmodyfikować; do rozważenia kopia obronna przy włączaniu modułów dynamicznych (etap 3).
- **R8: inicjalizacja możliwości procesora w dr_flac** (`drflac__init_cpu_caps`: trzy globalne flagi zapisywane leniwie, idempotentnie) jest oznaczona przez autora `DRFLAC_NO_THREAD_SANITIZE`, więc
  ThreadSanitizer jej nie raportuje; formalnie to wyścig danych o jednakowej wartości zapisu. Pozostałe statyczne dane zapisywalne w dekoderach (tablice AAC `standard_trie`, plany IMDCT) mają
  inicjalizację atomową i są objęte testem TSan dla AAC. Skan: `nm` obiektów rdzenia i dekoderów (symbole `b/B/d/D`), 2026-11-20.
- **R7: TSan** nie działa lokalnie (brak libtsan); wynik wielowątkowy lokalnie to ASan/UBSan + porównanie bit w bit; wymagany przebieg TSan jest w CI.

## 5. Jak uruchomić

```
make test-maktabasauti-probe-audit       # 3 budowy, ~1 min
make test-maktabasauti-hostile-io
make test-third-party-hardening
scripts/test_maktabasauti_threads.sh     # równolegle: dekodowanie + rozpoznawanie
# dłuższy fuzzing (ziarno, liczba iteracji sond, liczba otwarć):
MKS_AUDIT_SEED=7 MKS_AUDIT_ITERS=300000 MKS_AUDIT_OPEN_ITERS=100000 ASAN_OPTIONS=allocator_may_return_null=1 build/test_maktabasauti_probe_audit_asan tests/data/*.aac tests/data/*.m4a ... tests/data/probe_real.*
```
`allocator_may_return_null=1` odpowiada środowisku produkcyjnemu (nadmierne żądanie alokacji kończy się `NULL`, nie przerwaniem); bez niego ASan zgłasza każde takie żądanie jako błąd.
