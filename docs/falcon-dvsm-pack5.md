# Falcon DVSM, Pack 5 (ADPCM): jak został odczytany (2026-11-23)

## Wejście
Opis formatu (`DVS.TXT` z WinReca, Atari Wiki `DVSM`) wymienia tylko `pack = 5: ADPCM (only WinRec Pro)`. Algorytm jest w programie: pakiet **WinCut Pro 1.3** (z **WinRec Pro 2.11H**) z archiwum Atari TOSser (link na stronie "WinRec Pro 2", Google Drive), plik `WIN_REC.PRG` (GEMDOS, 90 844 B kodu 68030 + 79 034 B danych, bez symboli), pobrany POZA repozytorium (`~/Sauti-tools/winrec-pro2/`; to oprogramowanie MAXON / A. Binner z 1995, nie jest tu dystrybuowane i nie jest uruchamiane jako program). Katalog `DEMO` zawiera tylko pliki Pack 0 i 2.

## Co zostało ustalone (dezasemblacja Capstone M68K + uruchomienie procedur w emulacji Unicorn M68K)
* **Tablica kroków** na `0x2639e` (32-bitowe słowa big-endian) = standardowa tablica IMA ADPCM, 89 pozycji (7 ... 32767); tablica poprawek indeksu {-1,-1,-1,-1,2,4,6,8} (każda ósemka dwa razy, dla znaku) tuż przed nią.
* **Dekoder** `0x866e` (`d0` = liczba ramek, `a0` = dane, `a1` = wyjście, `d1` = kanał 0 / 1, na stosie wskaźnik stanu: słowo predyktora + bajt indeksu): kanał 0 bierze starszy półbajt bajtu, kanał 1 młodszy; różnica = `step>>3` + (bit 4: `step`) + (bit 2: `step>>1`) + (bit 1: `step>>2`), bit 8 = znak; predyktor nasycany do -32768..32767; indeks += poprawka, obcięty do 0..88; krok brany z NOWEGO indeksu dla następnej próbki. Wyjście: słowo 16-bit co 4 bajty (lewy pod `a1`, prawy pod `a1 + 2`).
* **Koder** `0x8586` robi to samo w drugą stronę (jeden bajt = kod lewego w starszym półbajcie, prawego w młodszym).
* **Kontener:** program zeruje stany obu kanałów (`0x297de`, `0x297e2`; 4 bajty każdy) przy starcie nagrywania / odtwarzania i nie wczytuje ich z pliku; blok wczytany z dysku to po prostu `n` bajtów = `n` ramek stereo (przełącznik na bajcie `pack` w `0x7000`: 0 kopiuje, 2 / 4 rozpakowują bloki, 5 rozpakowuje ciągłym strumieniem). Pole `blocklen` nie dotyczy Pack 5 (zgodnie z `DVS.TXT`).
* Druga ścieżka (`0x88d0`, wysyłanie bajtów przez port hosta DSP `0xFFFFA206`) odpowiada opcji "DSP benutzen"; nie badana, dekoder CPU jest wzorcem pliku.

## Weryfikacja
1. Procedura dekodująca WinReca uruchomiona w emulacji na 60 losowych przypadkach (losowe, same-wysokie i zerowe dane, losowy stan początkowy, oba kanały, serie 1 - 1000 ramek) daje to samo co implementacja wzorcowa (zero różnic, razem ze stanem końcowym).
2. Sygnał syntetyczny (9000 ramek: punkty kontrolne co 4096 ramek są używane przy przewijaniu) zakodowany procedurą koderską WinReca i zdekodowaną jego dekoderem: SNR 38 - 40 dB w partiach spokojnych (zakres normalny dla IMA przy 4 bitach), predyktor nasyca się na przebiegu prostokątnym pełnej skali.
3. Nasz dekoder daje na tym pliku PCM IDENTYCZNE bit w bit z dekoderem WinReca (`tests/test_dvsm_adpcm.c`, także po przewijaniu i przy odczytach dowolnej długości). Fixtura i PCM oczekiwane są generowane skryptem `tests/data/make_dvsm_adpcm_fixture.py` (przechowywane są tylko WYNIKI procedur, nie ich kod).
Mutanty: 7 z 7 zabitych (kolejność półbajtów, granica nasycenia, `step>>2`, granica indeksu 88, przesunięcie przewijania, zapis indeksu w punkcie kontrolnym, tablica poprawek).

## Granice
* Tylko stereo 16-bit: procedura dekodująca nie ma wariantu mono ani 8-bitowego, więc takie pliki odrzucamy (nie zgadujemy).
* Brak prawdziwego pliku Pack 5 spoza WinReca. Zgodność z tym, co ZAPISUJE WinRec, wynika z użycia jego własnych procedur kodera i dekodera; nagłówek `headlen` / ciasteczka obsługujemy jak dla Pack 2.
* Pack 4 (Voice): w programie nie znaleziono tablicy 15 wartości jako słów 16-bit; 16. wartość nadal domyślna (16384).
