# Pliki Atari z prawdziwego sprzętu (program 525) jako wzorce

Stan: 2026-11-25. Zamyka lukę „żaden prawdziwy plik AVR / SND / DVS nie został sprawdzony”: wszystkie trzy formaty są dekodowane **identycznie co do próbki** z wzorcem na 15 plikach wyprodukowanych przez oryginalny program 525 („Five to Five”, WinRec 1.41) uruchomiony w emulatorze Hatari (Falcon, EmuTOS).

## Jak powstały
1. Syntetyczne źródła (`tests/data/atari_hw/src_s44s16.wav` stereo, `src_m44m16.wav` mono; 4410 klatek 44,1 kHz 16 bit: chirp, szum, prostokąt, wartości skrajne 32767 / -32768 w pierwszych klatkach). Muzyka użytkownika w repozytorium NIE występuje.
2. Hatari sterowany z zewnątrz (klawiatura jako mysz, zrzuty ekranu), program 525: import WAV, wybór formatu (DVS / AVR / SND) i redukcji danych (Keep, 16 stereo, deltapack, 8 stereo, mono, µ-law), eksport.
3. Wzorce: AVR i SND dekoduje niezależnie FFmpeg; DVS dekoduje sam program 525 (import pliku DVS, eksport WAV; 525 zapisuje WAV z rozmiarem chunku `fmt` równym 0, więc FFmpeg go nie czyta, dane od bajtu 44).
4. `tests/data/make_atari_hw.py` kopiuje pliki i buduje `.ref` (int16 LE przeplecione). Test: `make test-maktabasauti-atari-hw` (`tests/test_maktabasauti_atari_hw.c`, ASan / UBSan), każdy plik dwa razy: wejście przewijalne ze znanym rozmiarem i nieprzewijalne bez rozmiaru z odczytami po 7 bajtów.

## Wyniki
| Format | Warianty (liczba plików) | Wynik |
|---|---|---|
| AVR | 16 bit stereo, 8 bit stereo, 16 bit mono, 8 bit mono (ze źródła stereo), 16 bit mono (ze źródła mono): 5 | identyczne z FFmpeg i (16 bit) ze źródłem |
| SND (Sun/NeXT `.snd`) | 16 stereo, 16 stereo µ-law, 8 stereo, 16 mono, 16 mono µ-law, 8 mono: 6 | identyczne z FFmpeg |
| DVS | 16 stereo (PCM), 16 stereo deltapack (Pack-2), 8 stereo, 8 mono: 4 | identyczne z dekoderem programu 525 (bit w bit, także deltapack) |

Pozostałe obserwacje (niewymagające zmian dekodera):
* **525 nie tworzy DVS 16 bit mono**, nawet ze źródła mono (pola wyszarzone); mono 16 bit w DVS (i Pack-2 / Pack-4 mono) nadal sprawdzają tylko wzorce z WinRec Pro (`tests/data/dvsm_*`) i generator losowy.
* DVS: częstotliwość 44100 Hz zapisana jako kod 7 = 49170 Hz (dane nie są przepróbkowane), zgodnie z tablicą dekodera.
* SND µ-law: koder 525 przepełnia się przy pełnej skali (32767 → 0 w pierwszych dwóch próbkach); dekoder zgadza się z FFmpeg, więc to wada kodera 525, nie dekodera. Wzorzec `.ref` pochodzi z FFmpeg.
* SND 8 bit: ze znakiem (kodowanie 2), jak w Sun AU.

## Błąd znaleziony tą metodą (naprawiony)
Program 525 po redukcji do mono (i także dla źródła mono) zapisuje w nagłówku AVR (offset 26) liczbę próbek **większą niż jest w pliku** (liczba ze źródła stereo albo podwojona). Dekoder oddawał wszystkie próbki, ale kończył `E_TRUNCATED`, a FFmpeg czyta plik czysto. Poprawka (`maktabasauti_dvs.c`, `avr_open`): przy znanym rozmiarze wejścia długość jest przycinana do faktycznych danych (EOF, `total_frames` = prawda); bez znanego rozmiaru zostaje `E_TRUNCATED` po wszystkich danych. Skutek uboczny: przy znanym rozmiarze obcięty plik AVR kończy się czysto zamiast błędem (jak w odtwarzaczach); test `tests/test_maktabasauti_dvs_stream.c` dostosowano i dodano `test_avr_overstated`. Mutanty (wyłączone przycinanie, odwrócony znak 8 bit) zabite przez oba testy.

## Czego to nie obejmuje
Pętle i metadane AVR (nie są stosowane), warianty 12/14 bit, Pack-4 (Voice) i ADPCM (525 ich nie zapisuje; pokrywają je wzorce z WinRec Pro), SND jako zasób Maca.
