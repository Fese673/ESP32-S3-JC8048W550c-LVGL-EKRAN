# Plan naprawy RAM: 3 najważniejsze, potwierdzone miejsca do poprawy

Poniżej nie powtarzam całego audytu. Ten plan wybiera trzy najważniejsze miejsca, które po przejrzeniu kodu faktycznie występują i mają największy wpływ na stabilność RAM / heap w tym projekcie.

## Weryfikacja decyzji

Po sprawdzeniu kodu nie traktuję jako top-3 "klasycznych leaków" rzeczy typu pojedyncze singletony, mutexy czy timery tworzone raz na starcie. To są stałe koszty życia aplikacji, a nie główny mechanizm degradacji pamięci.

Największy realny problem w tym repo to nie leak w sensie "rosnąco niezwalniana pamięć", tylko **fragmentacja i churn** na ścieżkach, które działają ciągle albo często.

## Potwierdzone top 3

### 1. EEZ tick: `evalTextProperty()` -> `Value::makeStringRef()` -> stały churn małych alokacji

Status: **potwierdzone**

Dlaczego to jest nr 1:

- działa w głównym ticku UI
- dotyczy wielu etykiet na wielu ekranach
- generuje powtarzalne alloc/free dla małych stringów
- to jest klasyczny wzorzec fragmentacji heap w embedded

Co w kodzie to potwierdza:

- `main/ui/ui/ui.c` wywołuje `tick_screen(g_currentScreen)` w głównej pętli UI
- `main/ui/ui/screens.c` ma wiele wywołań `evalTextProperty(...)`
- `main/eez-framework/src/eez/core/value.cpp` w `Value::makeStringRef()` tworzy osobny obiekt `StringRef` i osobny bufor tekstu

Mechanizm problemu:

Każda aktualizacja tekstu może tworzyć nowy `StringRef` i nowy bufor stringa, po czym poprzedni obiekt jest zwalniany. To nie musi leakować, ale przy dużej liczbie krótkich stringów rozbija internal heap na drobne kawałki.

Plan naprawy:

1. Zidentyfikować pola tekstowe, które naprawdę zmieniają się w ticku, i odciąć od nich bezwarunkowe przeliczanie.
2. Dodać cache/dirtiness na poziomie property, tak żeby tekst nie był budowany, jeśli wartość się nie zmieniła.
3. Dla najczęstszych małych stringów rozważyć pool allocator lub reużywane bufory o stałym rozmiarze.
4. Nie wprowadzać dodatkowych alokacji w LVGL tasku.

Embedded ocena:

- to jest realny problem
- to jest najbardziej podejrzane miejsce na długoterminową fragmentację
- to ma największy priorytet naprawy

---

### 2. Log screen: 8 KB bufor + okresowe przepychanie tekstu do `lv_textarea`

Status: **potwierdzone**

Dlaczego to jest nr 2:

- bufor logów jest duży i stale żyje w RAM
- log screen robi okresowe odświeżanie tekstu
- przy pełnym refreshu `lv_textarea_set_text()` kopiuje duży string do LVGL
- to jest mniej częste niż tick EEZ, ale bardziej kosztowne per operacja

Co w kodzie to potwierdza:

- `main/utils/log/log.cpp` definiuje `static char log_buffer[8192]`
- `log_update_textarea()` używa `lv_textarea_set_text(objects.log_terminal, log_buffer)` przy full refresh
- przy dopisywaniu nowych danych używa `lv_textarea_add_text(...)`, ale nadal pracuje na dużym, rosnącym buforze

Mechanizm problemu:

To nie jest leak, ale jest to duży, długo żyjący bufor + okresowe kopiowanie dużego tekstu do LVGL. Na embedded to może rozbić heap i zwiększyć presję na internal RAM, zwłaszcza jeśli ekran logów jest aktywny przez dłuższy czas.

Plan naprawy:

1. Zmniejszyć częstotliwość pełnych refreshy i trzymać się dopisywania inkrementalnego, ile się da.
2. Rozważyć label zamiast textarea, jeśli edycja nie jest potrzebna.
3. Jeśli textarea musi zostać, ograniczyć full refresh tylko do prawdziwego wrap/clear.
4. Jeżeli log ma rosnąć długo, przenieść bufor do pamięci mniej krytycznej lub wprowadzić twardszy ring-buffer policy.

Embedded ocena:

- potwierdzone
- nie jest to leak, ale jest to realny koszt pamięci i fragmentacji
- trzeba to trzymać pod kontrolą, bo ekran logów lubi obnażać problemy heap

---

### 3. Flappy Bird: `lv_label_set_text_fmt()` na wynikach i ekranach statusu

Status: **potwierdzone**

Dlaczego to jest nr 3:

- dotyczy interaktywnej ścieżki gry
- używa `lv_label_set_text_fmt(...)`, które nie przechodzi przez `safe_label.h`
- generuje dodatkowe alokacje LVGL przy zmianach wyniku i napisów statusowych
- to nie jest największy koszt w repo, ale jest prosty do poprawy i warto go zamknąć

Co w kodzie to potwierdza:

- `main/app/game/flappy_bird.cpp` używa `lv_label_set_text_fmt(s_score_label, "%d", s_score)`
- ten sam plik używa `lv_label_set_text_fmt(...)` także dla napisów game over / rekord / start record
- `safe_label.h` interceptuje `lv_label_set_text`, ale nie interceptuje `lv_label_set_text_fmt`

Mechanizm problemu:

Każda zmiana wyniku lub tekstu statusowego może powodować wewnętrzne alloc/free po stronie LVGL. Skala nie jest tak duża jak przy EEZ ticku, ale wzorzec jest ten sam: dynamiczny tekst w miejscu, gdzie można użyć stałych buforów.

Plan naprawy:

1. Zamienić `lv_label_set_text_fmt()` na przygotowanie tekstu w lokalnym buforze i wywołanie `lv_label_set_text()`.
2. Jeśli tekst jest powtarzalny, przechowywać gotowe stringi w stałych buforach i zmieniać tylko wtedy, gdy wartość rzeczywiście się zmienia.
3. Unikać formatowania w ścieżce render/score tick, jeśli można to zrobić przy zmianie stanu gry.
4. Zostawić `safe_label.h` jako mechanizm ochronny dla zwykłych labeli, ale nie opierać na nim formatowanych stringów.

Embedded ocena:

- potwierdzone
- mniejszy wpływ niż EEZ tick, ale prosty fix i dobry cleanup jakościowy
- warto zamknąć przed dalszymi testami długotrwałymi

---

## Co odrzucam jako top-3

Nie wrzucam tutaj jako głównych problemów:

- pojedynczych mutexów tworzonych raz na start
- singletonowych timerów tworzonych raz na start i żyjących do końca aplikacji
- rzeczy, które są po prostu stałym kosztem architektury, ale nie pokazują kumulacji pamięci

To są elementy do notatki technicznej, ale nie do priorytetowej naprawy RAM.

## Kolejność wdrażania

1. Najpierw EEZ tick / StringRef churn.
2. Potem log screen i wielkie przepychanie tekstu.
3. Na końcu Flappy Bird `lv_label_set_text_fmt()`.

## Kryterium sukcesu

Zmiany uznam za dobre, jeśli po długim stres-teście:

- `heap_caps_get_largest_free_block()` nie spada systematycznie
- internal heap nie traci największego ciągłego bloku po powtarzalnych cyklach
- UI nie robi się coraz wolniejsze po kilku godzinach
- nie pojawiają się objawy WDT / braku pamięci przy tym samym scenariuszu wejść i wyjść

## Moja krótka konkluzja

Najpierw naprawiałbym `evalTextProperty()` / `makeStringRef()`, bo to jest najgłębszy i najbardziej systemowy generator fragmentacji. Potem log screen, bo robi duży transfer tekstu. Flappy Bird zamknąłbym jako szybki i pewny cleanup jakościowy.