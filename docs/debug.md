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
