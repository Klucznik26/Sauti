# Poprawki do bibliotek zewnętrznych (do zgłoszenia upstream)

Każdy plik `.patch` to różnica względem zaimportowanego stanu (commit `e7fd9a6`), ścieżki skrócone do nazwy pliku (`patch -p1` w katalogu z oryginałem).
Wszystkie te biblioteki są w domenie publicznej / MIT-0 / CC0, więc poprawki można zgłaszać bez ograniczeń licencyjnych. **Nic nie zostało jeszcze zgłoszone**:
zgłoszenie (pull request / issue w obcym repozytorium) wymaga decyzji właściciela projektu.

| plik | biblioteka (upstream) | co naprawia | test regresji |
|---|---|---|---|
| `stb_vorbis.patch` | nothings/stb, `stb_vorbis.c` v1.22 | przepełnienie sterty przez obcięcie rozmiaru do `int`; niezainicjowane wskaźniki zwalniane przy błędzie ustawień; nieprawidłowe `free()` wskaźnika do pamięci tymczasowej; wycieki bloków tymczasowych; dereferencja NULL `comment_list`; limity długości vendor/komentarzy; UB przesunięcia wartości ujemnej | `tests/test_third_party_hardening.c`, `make test-third-party-hardening` |
| `dr_flac.patch` | mackron/dr_libs, `dr_flac.h` 0.13.4 | UB przepełnienia `int` w predykcji i sumach reszt (arytmetyka bez znaku); wyścig danych w wykrywaniu możliwości procesora (słowo atomowe zamiast globali) | audyt sond (UBSan), `scripts/test_maktabasauti_coldstart.sh` (TSan) |
| `tml.patch` | schellingb/TinySoundFont, `tml.h` | `memcpy(NULL, ..., 0)`; UB przesunięcia 255<<24 w nagłówku MTrk | audyt sond (UBSan) |
| `minimp3.patch` | lieff/minimp3, `minimp3_ex.h` | niezainicjowane `free_format_bytes` w `mp3dec_detect_cb` (odczyt przed początkiem bufora); UB przesunięć bajtów >= 128 o 24 bity (rozmiar APEv2, liczba ramek Xing) | `tests/test_decoder_mp3.c`, `tests/test_third_party_hardening.c` |

Przed zgłoszeniem: sprawdzić, czy błąd nie został już naprawiony w aktualnym `master` danej biblioteki (nasze kopie to migawki), i dołączyć plik wejściowy
odtwarzający błąd (generowanie: `tests/test_third_party_hardening.c` buduje je z `tests/data/probe_real.*`).

Komentarze `SAUTI` w łatkach są robocze (część po polsku): przed zgłoszeniem zastąpić je neutralnym komentarzem po angielsku.
