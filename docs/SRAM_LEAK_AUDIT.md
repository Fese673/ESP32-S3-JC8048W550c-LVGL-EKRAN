# Raport Audytu Pamięci SRAM - ZEGAR-ESP32

## 1. Wykryte wycieki pamięci (SRAM Leaks)

### [ID: LEAK_001] Wielokrotna rejestracja callbacku Dropdown (main.cpp)
**Lokalizacja:** `main/app/main.cpp:37-43` oraz wywołanie w `ui_tick_timer_cb:86`.

**Opis błędu:** 
Podczas każdego wejścia na ekran `SCREEN_ID_USTAWIENIA`, wywoływana jest funkcja `init_custom_dropdown_hook()`. Wewnątrz niej znajduje się linia:
```cpp
lv_obj_add_event_cb(objects.settings_melody_dropdown, my_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
```
Ponieważ obiekt `settings_melody_dropdown` jest globalny i nie jest niszczony przy zmianie ekranów, każda wizyta w ustawieniach "dokleja" kolejną strukturę zdarzenia (`lv_event_dsc_t`) do obiektu.

**Skutki:**
1.  **Wyciek SRAM:** Każda wizyta na ekranie zabiera kilkanaście/kilkadziesiąt bajtów na strukturę opisu zdarzenia.
2.  **Multi-trigger:** Po 10 wejściach w ustawienia, jednokrotna zmiana melodii spowoduje wywołanie `my_dropdown_event_cb` 10 razy, co wyśle 10 identycznych ramek UART do drugiego modułu.

**Rekomendacja naprawy:**
Wprowadzić flagę `static bool s_dropdown_hook_inited` analogicznie do poprawki w WiFi.

---

## 2. Analiza Stosu (Stack Pressure)

### Zadania RTOS
Obecna konfiguracja:
*   `comm_uart1`: 4096 bajtów.
*   `clock_task`: 4096 bajtów.
*   `taskLVGL`: 8192 bajtów.

**Obserwacja:** Po przeniesieniu buforów ramki w `UartTransport.cpp` z sekcji stosu na `.bss` (statyczne), zadanie komunikacyjne zużywa minimalną ilość stosu. 4096 bajtów jest obecnie wartością bardzo bezpieczną, wręcz nadmiarową.

**Rekomendacja:** 
W fazie finalnej można użyć `uxTaskGetStackHighWaterMark()` i zredukować stos zadań o ok. 1-2 KB każde, odzyskując SRAM na bufory logów lub LVGL.

---

## 3. Weryfikacja LVGL Objects

### Lista WiFi (`set_wifi.c`)
**Stan:** **BEZPIECZNY**.
Użycie `lv_obj_clean(objects.wifi_list)` przed dodaniem nowych przycisków gwarantuje, że LVGL poprawnie zwalnia pamięć powiązaną z poprzednimi elementami listy, w tym ich callbacki.

### Safe Label (`safe_label.h`)
**Stan:** **BEZPIECZNY** (po poprawce z `LV_EVENT_DELETE`).
Dzięki automatycznemu zwalnianiu buforów 64-bajtowych, fragmentacja SRAM przy przełączaniu ekranów została wyeliminowana.

---

## 4. Rekomendacje "Production-Ready"

1.  **Logi systemowe:** Bufor logów `8192` bajtów jest alokowany statycznie. Jest to OK, ale upewnij się, że nie brakuje pamięci na duże operacje (np. TLS przy WiFi, jeśli zostanie dodane).
2.  **Heap Integrity Check:** Warto dodać w `main.cpp` (pętla `while(1)`) okresowe wypisywanie `esp_get_free_heap_size()`. Jeśli po 100 przełączeniach ekranów wartość ta będzie stała (poza wahaniami LVGL), firmware można uznać za szczelny.
3.  **Wyrównanie buforów:** Dla operacji UART DMA, bufory w `.bss` powinny mieć atrybut `WORD_ALIGNED`, aby uniknąć problemów przy wyższych prędkościach transmisji.
