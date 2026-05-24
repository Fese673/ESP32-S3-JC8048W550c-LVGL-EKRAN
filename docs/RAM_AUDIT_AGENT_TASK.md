# Zadanie dla agenta AI: pełny audyt RAM, leaków i fragmentacji

Masz przejść cały kod tego repozytorium i zrobić możliwie najdokładniejszy audyt pamięci.

To nie ma być ogólny komentarz ani streszczenie. Masz znaleźć jak najwięcej konkretnych miejsc podejrzanych o:

- wycieki RAM
- fragmentację heap / SRAM / PSRAM
- nadmierne lub powtarzalne alokacje
- niezamknięte zasoby
- zbyt ciężkie wzorce dla LVGL / FreeRTOS / ESP-IDF

Traktuj to jak bezwzględny code review pod pamięć. Jeśli coś wygląda podejrzanie, opisz to. Jeśli masz wątpliwość, oznacz jako podejrzenie z argumentem, a nie zamiataj pod dywan.

## Zakres

Przeszukaj całe repozytorium, łącznie z:

- `main/`
- `docs/` tylko jeśli zawiera istotne informacje o architekturze lub pamięci
- `ekran_zegar_kopia/` jeśli pokazuje wzorce generowanego UI, które wpływają na RAM
- wszystkie komponenty LVGL / EEZ / communication / services / app / hal / utils
- wygenerowane pliki, jeśli mogą ujawniać wzorce alokacji, callbacków, timerów lub resetów UI

Nie ograniczaj się do oczywistych miejsc. Szukaj również w kodzie, który:

- tworzy obiekty LVGL
- rejestruje callbacki
- tworzy timery / taski / kolejki / mutexy / semafory
- alokuje bufory dynamicznie lub używa `static` buforów o dużym rozmiarze
- wykonuje `nvs_open`, `malloc`, `new`, `strdup`, `lv_obj_create`, `lv_timer_create`, `xTaskCreate`, `xQueueCreate`
- manipuluje stringami, formatowaniem, fontami, obrazami, payloadami i buforami ramki
- robi częste aktualizacje UI, `setGlobalVariable`, `lv_label_set_text`, `lv_obj_set_style_*`

## Co masz znaleźć

Masz przygotować dwa oddzielne zestawienia:

### 1. Leak RAM

W tej sekcji umieść tylko miejsca, gdzie istnieje realne ryzyko, że pamięć lub zasób nie jest zwalniany, albo jest zwalniany niepewnie.

Szukaj między innymi:

- `malloc` bez `free`
- `new` bez `delete`
- `lv_obj_create` bez pewnego cleanupu
- `lv_timer_create` bez `lv_timer_delete`
- `xTaskCreate` / `xQueueCreate` / `xSemaphoreCreate*` bez jasnego lifecycle
- callbacki dodawane wielokrotnie do tego samego obiektu
- bufory przypinane do `user_data` lub statycznych struktur bez cleanupu
- uchwyty NVS / plików / UART / WiFi / BLE bez zamknięcia lub resetu
- przypadki, gdzie zasób żyje dłużej niż ekran, stan lub tryb, który go stworzył

### 2. Fragmentacja pamięci

W tej sekcji umieść miejsca, gdzie kod może nie leakować bezpośrednio, ale z czasem rozbija heap, tworzy małe śmieciowe alokacje albo generuje niestabilny układ pamięci.

Szukaj między innymi:

- częstych alokacji małych buforów
- tworzenia i niszczenia obiektów LVGL w pętli lub przy częstych przełączeniach
- `lv_label_set_text` / `lv_label_set_text_fmt` na wielu dynamicznych etykietach
- tworzenia nowych stringów w tickach / timerach / callbackach
- alokacji zależnych od długości tekstu
- wzorców typu alloc/free per update
- sytuacji, w których bufor 64B / 128B / 256B jest tworzony dla wielu obiektów i może rosnąć ilościowo
- rozproszenia między PSRAM, SRAM i internal heap

## Metodyka

Masz działać jak dociekliwy audytor, nie jak generator ogólników.

Wykonaj co najmniej:

1. Przeszukanie repo pod wzorce alokacji, tworzenia obiektów i callbacków.
2. Przegląd lifecycle każdego znalezionego zasobu: kto tworzy, kto przechowuje, kto zwalnia, kiedy i na jakich warunkach.
3. Odróżnienie leaków od fragmentacji.
4. Odróżnienie problemów rzeczywistych od świadomych kompromisów projektowych.
5. Zidentyfikowanie miejsc, które są "kosztowne", ale nie są leakami.

## Priorytety oceny

Oceniaj każde znalezisko według tej skali:

- `critical` - bardzo prawdopodobny leak lub silna fragmentacja, która może się kumulować
- `high` - realne ryzyko, prawdopodobne problemy przy dłuższym użyciu
- `medium` - podejrzane, wymaga sprawdzenia lub stres-testu
- `low` - możliwy koszt, ale raczej akceptowalny kompromis

## Wymagania dla każdego znaleziska

Każdy wpis ma zawierać:

- unikalny identyfikator, np. `RAM_LEAK_001` albo `FRAG_004`
- dokładną lokalizację pliku i najlepiej zakres linii
- krótki opis mechanizmu
- dlaczego to może leakować albo fragmentować
- co jest alokowane / tworzono / rejestrowane
- co powinno to zwolnić lub zresetować
- czy problem jest stały, okresowy czy zależny od cyklu ekranu / payloadu / timera
- ocenę ryzyka
- prostą propozycję naprawy albo przynajmniej kierunek naprawy

Jeśli masz mocny dowód, napisz to wprost. Jeśli to tylko podejrzenie, też napisz wprost i uzasadnij.

## Czego oczekuję w analizie

Masz szukać szczególnie agresywnie w takich obszarach:

- ekranowe lifecycle LVGL
- `resetUi`, `updateUi`, ticki, timery i callbacki
- komunikacja UART / BLE / WiFi / NVS
- generowane UI EEZ i ich obsługa tekstu
- animacje i gry, jeśli tworzą własne timery, obiekty i style
- wszelkie bufory statyczne, które mogą być legalne, ale niepotrzebnie duże
- wielokrotne `setGlobalVariable`, szczególnie w częstych ścieżkach
- formatowanie tekstu, Unicode, ikony, long scroll, stylowanie i label lifecycle

## Czego nie robić

- Nie mieszaj leaków z fragmentacją w jednym worklogu.
- Nie pisz ogólników typu "warto sprawdzić pamięć".
- Nie ograniczaj się do miejsc, które już wyglądają podejrzanie.
- Nie pomijaj generowanych plików tylko dlatego, że są wygenerowane.
- Nie zakładaj, że coś jest bezpieczne, jeśli nie prześledziłeś lifecycle.

## Format wyjścia

Na końcu zwróć wynik w takiej strukturze:

### A. Najważniejsze znaleziska
Lista od najbardziej groźnych do najmniej groźnych.

### B. Leak RAM
Tabela lub lista:

- ID
- plik i linia
- mechanizm
- wpływ
- ryzyko
- rekomendacja

### C. Fragmentacja
Osobna tabela lub lista z tym samym układem.

### D. Fałszywe alarmy / kompromisy
Wypisz rzeczy, które wyglądają groźnie, ale po analizie są prawdopodobnie akceptowalne.

### E. Priorytet testów
Podaj, co najpierw stres-testować na urządzeniu, żeby potwierdzić lub obalić podejrzenia.

## Dodatkowe wymaganie

Jeśli znajdziesz miejsce, które nie jest leakiem, ale może powodować niestabilność, spadki FPS, wzrost CPU albo nadmierny churn heap, też je odnotuj, tylko wyraźnie oddziel od leaków i fragmentacji.

## Oczekiwany styl

- techniczny
- bez lania wody
- bez uprzejmych ogólników
- dużo konkretu
- dużo dowodów z kodu
- rozdzielone tematy

Masz przejść kod dokładnie i bez taryfy ulgowej.

UWAGA NAJWAŻNIEJSZE CAŁY TWÓJ RAPORT UTWORZ JAKO PLIK MD 