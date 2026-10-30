# Sauti — uzgodnione kierunki rozwoju

**Data rozmowy:** 2026-10-30  
**Status dokumentu:** plan i propozycje architektoniczne, **nie lista funkcji już zaimplementowanych**.  
**Priorytet:** najpierw ukończyć Sauti; dopiero potem tworzyć Sauti Converter.

## 1. Cel projektu i zakres formatów audio

Sauti ma dążyć do jak najszerszej praktycznej obsługi współczesnych i historycznych formatów audio. Obsługa oznacza prawidłowe odtwarzanie, a nie samo rozpoznanie rozszerzenia. Dosłowne wsparcie każdego istniejącego, zamkniętego lub przyszłego formatu nie może być zagwarantowane.

- Utrzymywać rejestr formatów z rozróżnieniem: działający, częściowy, planowany, niedostępny.
- Testować dekodowanie, długość, przewijanie (gdy format pozwala), metadane, okładki, kanały i wysokie częstotliwości próbkowania.
- Uzupełnić brakujące współczesne formaty, odmiany kodeków i kontenery; przykładowo MP3, AAC, FLAC, ALAC, Opus, Vorbis, WAV, WavPack.
- Kontynuować formaty retro: Atari 8-bit / POKEY / SAP, Commodore 64 / SID, Amiga / Paula i formaty modułowe (np. MOD).
- W rozmowie zgłoszono ukończenie planowanej obsługi formatów Atari ST/Falcon. **Nie jest to niezależnie potwierdzony wynik audytu kodu ani testów repozytorium.**

## 2. Swobodnie wymieniane skórki

System skórek ma pozwalać na **dowolny układ interfejsu**, nie tylko zmianę kolorów i obrazów, wzorując się na wygodzie Winampa.

- Skórka opisuje wymiary i pozycje elementów, grafikę, kształt okna, powiązanie elementów z akcjami, wizualizację i opcjonalnie wiele okien.
- „Wgraj skórkę”: instalacja paczki przygotowanej przez innego użytkownika, bez rekompilacji Sauti.
- Podczas zmiany skórki odtwarzanie nie powinno być przerywane.
- Propozycja formatu: paczka zasobów + deklaratywny opis układu (np. JSON); **nie wykonywać arbitralnego kodu ze skórki**.
- W przyszłości: graficzny edytor skórek z przeciąganiem elementów.
- Wdrożenie etapami: dwie skórki o odmiennych układach → przełączanie w czasie działania → okna nieregularne → edytor.

## 3. Dekodery jako wtyczki

Użytkownik ma móc **doinstalować brakujący format bez zmiany kodu ani rekompilacji głównej aplikacji**.

- Oddzielić silnik odtwarzania, wybór dekodera i konkretne moduły dekodujące.
- Zdefiniować stabilne, wersjonowane API/ABI wtyczki: identyfikacja możliwości, wykrycie pliku na podstawie treści, otwarcie, odczyt/PCM, metadata, seek (jeżeli dostępny), zamknięcie i obsługa błędów.
- Linux: możliwa implementacja modułów jako bibliotek dynamicznych `.so`; inne systemy wymagają własnych adapterów.
- Menedżer wtyczek: instaluj, aktualizuj, włącz/wyłącz, pokaż wspierane formaty; nowa wtyczka może rozszerzyć listę skojarzeń MIME.
- Ustalić politykę zaufania, weryfikacji pakietów i zgodności wersji. Biblioteki uruchamiane w procesie Sauti nie są izolowane; dla obcych dekoderów rozważyć osobne procesy.
- Ponowne wykrycie wtyczki bez restartu to **cel**, nie gwarancja przed zaprojektowaniem zarządzania aktywnymi dekoderami.

## 4. Autorski kontener muzyczny Sauti

Opracować własny **format kontenerowy**, który jest czytelny również poza AyoOS przez aplikacje posiadające odpowiedni demultiplekser/odtwarzacz. Sam Linux nie rozpozna automatycznie autorskiego kontenera bez obsługującego go programu.

### Wiele reprezentacji tego samego utworu

- Jeden plik może zawierać MP3, WAV, FLAC lub inne obsługiwane strumienie tego samego utworu.
- Dopuszczalne również różne miksy: stereo, 5.1, instrumentalny, koncertowy — z jednoznacznymi identyfikatorami i metadanymi.
- Wspólne metadane, okładka, tekst utworu, informacje o strumieniach, ich czasie i właściwościach.
- Koszt rozmiaru: suma danych poszczególnych reprezentacji oraz narzut kontenera.
- Przełączanie w trakcie odtwarzania wymaga wyrównania osi czasu i świadomej obsługi różnic w dekodowaniu.

### Polityka wyboru strumienia

Preferencje użytkownika, rozdzielające kodek i układ kanałów:
1. automatycznie według jakości, obsługi kodeka i możliwości sprzętu;
2. preferowany format / układ kanałów;
3. własna lista priorytetów (fallback);
4. zawsze pytaj o wariant.
   
Dodatkowo zapamiętywanie wyboru per utwór/kontener, reguły downmixu i fallbacku. Przykład „MP3 5.1” był tylko ilustracją mechanizmu preferencji; standardowe MP3 nie jest powszechnym formatem 5.1.

### Rodziny AYFS2

- Kontener może zawierać opcjonalną sekcję metadanych związanych z **rodzinami AYFS2**; nieobsługujące jej aplikacje ją pomijają, a audio nadal jest odtwarzalne w zgodnym dekoderze.
- AYFS2 pozostaje autorytatywnym źródłem rzeczywistych relacji rodzinnych i wersji plików.
- Dane w kontenerze mogą być jedynie **wskazówkami do importu/odtworzenia relacji**, nigdy bezwarunkowym uprawnieniem do zmiany rodzin.
- Import powinien walidować identyfikatory, pochodzenie danych, konflikty, uprawnienia i sytuacje kopiowania na exFAT lub inne systemy plików.
- Wiele reprezentacji audio w jednym kontenerze **nie jest** limitem trzech wersji pliku AYFS2. Rozdzielić te pojęcia.

### Szkic zasad formatu (do późniejszego projektu specyfikacji)

- Jawna sygnatura, numer wersji, długości bloków, indeks strumieni, bezpieczne pomijanie nieznanych sekcji i limity rozmiarów.
- Oddzielić demultiplekser kontenera od dekoderów poszczególnych kodeków.
- Dokumentacja formatu i referencyjny demultiplekser; brak wymogu DRM.
- Nie ustalono jeszcze nazwy, rozszerzenia, binarnego układu nagłówka ani konkretnego kodeka jako obowiązkowego.

## 5. Integracja z Linuksem

### Skojarzenia plików

- Instalator może rejestrować plik `.desktop` i typy MIME, także dla rzadkich formatów historycznych.
- W ustawieniach Sauti: „Skojarzenia plików”, wybór konkretnych formatów i opcja ustawienia Sauti jako odtwarzacza domyślnego.
- Nie przejmować istniejących skojarzeń bez zgody użytkownika.
- Rejestrowanie formatu w systemie i ustawienie aplikacji domyślnej to dwa odrębne działania; rozważyć `xdg-mime`, `mimeapps.list` i bazę shared-mime-info.
- Nowa wtyczka może zgłosić format do systemowej listy typów, ale nie powinna samowolnie zmieniać domyślnego programu.

### Ikony typów plików

- Proponowany własny zestaw ikon Sauti dla Atari, Amigi, Commodore, autorskiego kontenera oraz opcjonalnie poszczególnych formatów.
- Ikony typów MIME są niezależne od wyboru aplikacji domyślnej; instalację ikon zaoferować osobno.
- Nie nadpisywać bez pytania globalnych ikon popularnych typów.
- Wygląd może zależeć od motywu, środowiska graficznego i wyświetlania miniatur.

## 6. Sauti Converter — projekt **po** ukończeniu Sauti

- Osobna aplikacja, która korzysta ze wspólnych modułów Sauti.
- Zakres formatów Convertera: tylko formaty obsługiwane przez Sauti, ale **odtwarzanie nie oznacza automatycznie możliwości zapisu**; potrzebne są osobne kodery i eksporterzy.
- Docelowo konwersja zwykła (np. FLAC ↔ WAV), wsadowa, metadane, ustawienia jakości, a także trudniejsze przekształcenia między współczesnymi i historycznymi formatami.
- Pierwszy rozważany format docelowy: Atari Falcon **DVS/DVSM**, zwłaszcza nieskompresowane próbki; przed implementacją zweryfikować specyfikację, warianty plików, nagłówki i dostępne próbki testowe.
- Straty przy zmianie próbkowania/bit-depth; MP3 → bezstratny format nie odzyskuje wcześniej utraconych informacji.
- MP3/FLAC → YM/SID/MIDI jest **transkrypcją lub rekonstrukcją/aranżacją**, a nie zawsze możliwą wierną konwersją danych.
- Użytkownik chce **rzeczywiste pliki w formatach retro**, a nie wyłącznie efekty nałożone na WAV/MP3. Wynik może brzmieć gorzej z uwagi na ograniczenia pierwotnej platformy.

## 7. Kolejność realizacji i kryteria

**Teraz:** stabilizacja istniejącego Sauti, audyt dekoderów oraz stopniowe dodawanie brakujących formatów współczesnych i retro; bez zatrzymywania działających funkcji.  
**Równolegle projektowo:** zaprojektować kontrakt wtyczek, aby nowe dekodery nie wymagały później przebudowy odtwarzacza.  
**Następnie:** system skórek i integracja z systemem; ich kolejność można ustalić po audycie architektury.  
**Później:** specyfikacja i prototyp kontenera z wyborem wielu strumieni i opcjonalnymi metadanymi AYFS2.  
**Na końcu:** Sauti Converter.

**Zasada pracy:** małe, testowane zmiany; nie oznaczać funkcji jako wdrożonej, dopóki nie przejdzie odpowiednich testów.

---

Ten dokument utrwala pomysły z rozmowy z 30 października 2026 r. i służy planowaniu. Nie stanowi potwierdzenia realizacji funkcji ani gwarancji zgodności formatów bez audytu źródeł i testów.
