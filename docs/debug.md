# Debugowanie i Optymalizacja Wydajności (ESP32-S3 + LVGL 9)

Poniżej znajduje się podsumowanie zmian wprowadzonych w celu wyeliminowania zacięć (freeze) oraz błędów Watchdoga (`task_wdt`) przy renderowaniu zaawansowanego UI.

### Wprowadzone zmiany:

- **Odblokowanie pamięci PSRAM (`CONFIG_LV_USE_CLIB_MALLOC=y`)**
    - **Dlaczego:** Domyślnie LVGL używał własnego alokatora z limitem 128 KB. Powodowało to błędy przy próbie stworzenia dużych buforów dla przeskalowanych obrazów.
    - **Efekt:** LVGL może teraz dynamicznie pobierać pamięć z pełnych 8 MB PSRAM, co pozwala na płynne działanie dużych warstw graficznych.

- **Optymalizacja Kompilatora (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`)**
    - **Dlaczego:** Projekt był wcześniej w trybie `DEBUG` (wolny kod). Skalowanie obrazów to czysta matematyka procesora.
    - **Efekt:** Przyspieszono obliczenia matematyczne (mnożenie pikseli) od 3 do 10 razy dzięki przełączeniu na tryb wydajnościowy (`-O2` / `-O3`).

- **Statyczny bufor warstwy (`CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=2304000`)**
    - **Dlaczego:** Pozwala LVGL na narysowanie całego ekranu (800x480) w jednej operacji zamiast dzielenia go na setki małych fragmentów.
    - **Efekt:** Drastyczne zmniejszenie narzutu procesora przy operacjach `scale` i `opa`.

- **Renderowanie jednordzeniowe (`CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1`)**
    - **Dlaczego:** Przy dwóch rdzeniach dochodziło do walki o szynę pamięci PSRAM, co skutkowało "białym ekranem".
    - **Efekt:** Stabilny obraz i brak konfliktów z kontrolerem DMA wyświetlacza.

---

## 🚨 KRYTYCZNA OPTYMALIZACJA: lv_textarea_set_text() - Watchdog Timeout

### Problem:
Po dodaniu ekranu logów z `lv_textarea` widgetem, urządzenie wpadało w **task_wdt** na `taskLVGL`. FPS spadał z 30 do 0 i urządzenie się resetowało.

### Symptomy:
```
E (65001) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (65001) task_wdt:  - IDLE1 (CPU 1)
E (65001) task_wdt: Tasks currently running:
E (65001) task_wdt: CPU 1: taskLVGL

Backtrace:
--- 0x42064393: lv_textarea_set_text at lv_textarea.c:397
--- 0x42063e25: lv_textarea_add_char at lv_textarea.c:265
--- 0x4205caa4: lv_label_get_letter_pos at lv_label.c:357
--- 0x4204f4ca: lv_text_get_next_line at lv_text.c:352
```

### Przyczyna (ANALIZA ŹRÓDŁA LVGL 9.4):

**`lv_textarea_set_text()` ma DWA ścieżki wykonania:**

#### 🔴 SLOW PATH (O(N²)) - Gdy `max_length > 0`:
```c
// lv_textarea.c, line 388
if(lv_textarea_get_max_length(obj)) {
    // Wywołuje lv_textarea_add_char() dla KAŻDEGO znaku osobno!
    for (każdy znak w tekście) {
        lv_textarea_add_char(obj, znak);
        // → lv_label_ins_text()
        // → lv_textarea_set_cursor_pos()
        // → lv_obj_update_layout()  ← PRZELICZA CAŁY LAYOUT DRZEWA
        // → lv_text_get_next_word() ← ITERUJE PO WSZYSTKICH ZNAKACH
    }
}
```

**Złożoność: O(N²)**
- Dla 500 znaków: 500 × 500 = **250,000 operacji**
- Blokuje wątek LVGL na SEKUNDY → watchdog timeout!

#### 🟢 FAST PATH (O(N)) - Gdy `max_length == 0`:
```c
// lv_textarea.c, line 400
else {
    // Wywołuje lv_label_set_text() bezpośrednio!
    lv_label_set_text(ta->label, txt);
    lv_textarea_set_cursor_pos(obj, LV_TEXTAREA_CURSOR_LAST);
}
```

**Złożoność: O(N)**
- Dla 500 znaków: **500 operacji**
- Natychmiastowe wykonanie!

### Rozwiązanie:

**ZAWSZE ustawiać `lv_textarea_set_max_length(obj, 0)` przed `lv_textarea_set_text()`!**

```c
// ZŁE - wymusza SLOW PATH O(N²)
lv_textarea_set_max_length(obj, 4096);
lv_textarea_set_text(obj, log_buffer);

// DOBRE - wymusza FAST PATH O(N)
lv_textarea_set_max_length(obj, 0);
lv_textarea_set_text(obj, log_buffer);
```

**Implementacja w `main/utils/log/log.cpp`:**
```c
void log_update_textarea(void) {
    if (objects.log_terminal == NULL) return;
    // ... sprawdzanie czy są nowe logi ...
    lv_textarea_set_max_length(objects.log_terminal, 0);  // ← WYMUSZA FAST PATH
    lv_textarea_set_text(objects.log_terminal, log_buffer);
}
```

### Dlaczego EEZ Studio ustawia `max_length > 0`?
EEZ Studio domyślnie ustawia `max_length = 4096` dla textarea - to bezpieczne ustawienie dla formularzy z limitem znaków, ale **KATASTROFA dla logów/terminali**.

### Ważne:
- Ta poprawka jest w `main/utils/log/log.cpp` - **NIE jest nadpisywana przez EEZ Studio**
- Nawet jeśli EEZ Studio wyeksportuje `max_length = 4096` w `screens.c`, nasz kod w `log_update_textarea()` **nadpisze go na 0** przed każdym użyciem
- To rozwiązanie jest **ODPORNE na regenerację UI przez EEZ Studio**

---

### 🔍 Jak do tego doszedłem (proces diagnostyczny):

1. **Obserwacja:** Problem występuje TYLKO na ekranie logi, inne ekrany działają OK
2. **Porównanie:** tick_screen_logi() i tick_screen_zegar() są IDENTYCZNE (3x evalTextProperty)
3. **Różnica:** Zegar używa `lv_label`, logi używa `lv_textarea`
4. **Fałszywe tropy:** Throttling czasowy (500ms), licznik zmian - nic nie pomagało
5. **Kluczowy moment:** Analiza **backtrace z watchdog** - pokazała dokładną ścieżkę wywołań
6. **Lektura źródła LVGL:** `lv_textarea.c` linia 388 - warunek `if(max_length)` decyduje o ścieżce
7. **Eureka:** `max_length = 4096` z EEZ Studio wymusza SLOW PATH O(N²)!

### Nauka na przyszłość:
- **NIGDY nie ustawiać `max_length > 0` dla textarea używanego jako terminal/logi**
- **Backtrace z ESP-IDF to złoto** - pokazuje dokładną ścieżkę wywołań aż do źródła problemu
- Jeśli LVGL działa wolno, sprawdź źródło LVGL - często jest ukryty warunek decydujący o wydajności
- Throttling nie pomoże jeśli JEDNA operacja blokuje CPU na sekundy

---

### ⚠️ UWAGA DLA PROGRAMISTY (Ręczne poprawki po EEZ Studio)

Plik `main/ui/ui/screens.c` jest generowany automatycznie. Po każdym eksporcie z EEZ Studio należy sprawdzić/dodać poniższe poprawki, które nie są wspierane bezpośrednio przez generator:

1. **Reset Scrolla przy wejściu na ekran:**
   W handlerach zdarzeń (np. `event_handler_cb_menu_menu`) należy dodać obsługę `LV_EVENT_SCREEN_LOAD_START`, aby zresetować pozycję przewijania:
   ```c
   if (event == LV_EVENT_SCREEN_LOAD_START) {
       lv_obj_scroll_to_y(lv_event_get_target(e), 0, LV_ANIM_OFF);
   }
   ```
   Bez tego Menu może być przesunięte w górę/dół, jeśli użytkownik wcześniej używał gestów przewijania do nawigacji.
