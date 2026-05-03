# Problem: Freeze ekranu przy zmianie menu

## Opis problemu

Projekt: ESP32-S3 z wyświetlaczem 800×480, LVGL 9.x (direct mode), EEZ framework, ESP-IDF v6.0.1.

**Objaw:** Ekran freezeje (zatrzymuje się) podczas próby zmiany menu (nawiązanie innego screena). System nie crashuje, ale UI nie reaguje. Po chwili pojawia się błąd watchdog timeout:

```
E (13861) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (13861) task_wdt:  - IDLE1 (CPU 1)
E (13861) task_wdt:  - taskLVGL (CPU 1)
Backtrace: ... (wskazuje na funkcje LVGL)
```

---

## Konfiguracja systemu

### Hardware
- **MCU**: ESP32-S3 (240 MHz, 320 KB internal RAM, 8 MB PSRAM)
- **Display**: RGB LCD 800×480, 16-bit color
- **Touch**: GT911 (I2C)

### Software
- **Framework**: ESP-IDF v6.0.1
- **UI**: LVGL 9.x (direct mode)ja 
- **Architektura**: EEZ framework + LVGL UI (generowane z SquareLine/Studio)
- **Direct mode**: `direct_mode = true`, `num_fbs = 2`, `fb_in_psram = true`
- **LVGL memory**: 128 KB heap
- **Task LVGL**: priority 4, stack 8 KB, CPU1, `task_max_sleep_ms = 500`, `timer_period_ms = 5`
- **Main task**: pętla co 100 ms, lock LVGL, `ui_tick()`

---

## Co działa poprawnie

- Bootloader, PSRAM (8 MB) — detected.
- LCD inicjalizacja, backlight, touch — bez błędów.
- Boot成功后 UI się pojawia.
- Brak screen tearing (dzięki `avoid_tearing = true`).
- Compilacja bez błędów (po zwiększeniu partycji factory do 4 MB/8 MB).

---

## Logi systemu (fragment)

```
I (1766) main_task: Started on CPU0
...
I (2123) main: Application started
E (13861) task_wdt: Task watchdog got triggered. - IDLE1 (CPU 1), taskLVGL (CPU 1)
```

**Wydarzenie:** Watchdog timeout występuje po ~13.8 sekundach od startu, a potem ponownie po ~5 sekundach.

### Interpretacja
- Task `taskLVGL` (utworzony przez `esp_lvgl_port`) nie resetuje watchdog w czasie.
- Task `IDLE1` też nie resetuje (jest idle task, zwykle resetuje, ale jeśli inne taski blokują, może nie mieć szans).
- Oznacza to, że **`taskLVGL` blokuje (nie dostaje CPU) przez dłuższy czas (>5s)**.

---

## Możliwe przyczyny (hipotezy)

### 1. **Blokada mutexa LVGL w main task**
W `main/main.cpp`:
```c
while (1) {
    vTaskDelay(pdMS_TO_TICKS(100));
    if (lvgl_port_lock(10)) {
        ui_tick();              // <-- może blokować długo?
        lvgl_port_unlock();
    }
}
```
Jeśli `ui_tick()` wykonuje ciężką operację (I2C, dekompresja, update wielu widgetów) pod lockiem LVGL, to:
- Main task trzyma mutex LVGL przez długi czas.
- Task LVGL (prio 4) chce mutex → blokuje.
- Task LVGL nie resetuje watchdog → timeout.

### 2. **Brak pamięci RAM (internal)**
Logi heapa (nie mamy jeszcze):
- Internal RAM: ~242 KB dostępne.
- LVGL heap: 128 KB → zostaje ~114 KB dla EEZ, stacków, systemu.
- Jeśli EEZ/UI alokują dużo (obrazy w RAM, fonty, style), może zabraknąć RAM.
- Alokacje mogą blokować (czekanie na mutex heapa) → taskLVGL blokuje.

### 3. **Stack overflow tasku LVGL**
Stack 8 KB może być za mało, jeśli LVGL ma wiele timerów, animacji, complex UI.
Overflow → crash/bloack.

### 4. **EEZ framework blokuje w `processEvents()`**
Jeśli EEZ ma task z długimi operacjami (np. komunikacja, calculations) i ma wyższy priorytet niż LVGL, może preemptować, ale to nie powinno blokować LVGL. Chyba że blokuje mutex LVGL.

### 5. **Flush callback w direct mode blokuje**
W direct mode, `esp_lvgl_port` rejestruje flush callback. Jeśli ten callback wykonuje długą operację (np. czeka na I2C), to taskLVGL blokuje.

### 6. **Watchdog timeout z powodu zbyt długiego `task_max_sleep_ms`**
Jeśli taskLVGL nie ma nic do roboty, może spać do 500 ms. Ale timer ma 5 ms period, więc nie powinno to być problemem. Natomiast jeśli blokuje, nie uśpi się, więc to nie o to chodzi.

---

## Co już sprawdzono

- PSRAM: 8 MB detected, OK.
- Partition: zwiększono factory do 4 MB → binary (4.42 MB) się mieści.
- Direct mode: działa, brak tearing.
- Boot: bez błędów.

---

## Co NIE sprawdzono (brakujące dane)

- **Logi heapa** (free/malloc/failure) — nie mamy.
- **Czas wykonania `ui_tick()`** — nie wiemy, ile trwa.
- **Priorytety wszystkich tasków** — tylko LVGL (4), main (domyślny 1).
- **Użycie WiFi/Bluetooth/network** — jeśli włączone, zużyje RAM i może blokować.
- **Zawartość `ui_tick()`** — czy tam są ciężkie operacje?

---

## Plan debugowania (krok po kroku)

### Krok 1: Monitoring pamięci i czasu wykonania
Dodaj do `main/main.cpp`:
- Logowanie stanu RAM (internal/PSRAM) co 1–5s.
- Pomiar czasu `ui_tick()`.
- Logowanie, ile czasu mutex LVGL jest trzymany.

### Krok 2: Zmniejsz `task_max_sleep_ms` LVGL task
 Zmień z 500 ms na 50 ms (w `sunton_esp32_8048s050c.c`), by taskLVGL częściej budził się i miał szansę resetować watchdog nawet przy lekkich blokadach.

### Krok 3: Zwiększ timeout watchdog (tymczasowo)
W `menuconfig`: Component config → ESP32-specific → Task Watchdog timeout → 10 sekund.
Potwierdzi, czy freeze trwa >5s.

### Krok 4: Sprawdź `ui_tick()` pod kątem długich operacji
Jeśli `ui_tick` wykonuje I2C, dekompresję obrazu, update wielu widgetów — przenieś te operacje do osobnego taska.

### Krok 5: Zwiększ stack LVGL task (jeśli overflow)
Z 8 KB do 12 KB.

### Krok 6: Sprawdź, czy EEZ framework nie blokuje
Zweryfikuj, czy `eez::fsm::processEvents()` (jeśli jest używane) nie blokuje na semaforach/timeout.

### Krok 7: Test z disable WiFi/network
Jeśli projekt nie potrzebuje sieci, wyłącz WiFi w `menuconfig` → oszczędność RAM i brak blokad stacka.

### Krok 8: Zmniejsz LVGL heap (jeśli brak RAM)
Z 128 KB do 64 KB, jeśli UI nie jest złożone.

### Krok 9: Izolacja problemu — test minimalny LVGL
Usuń tymczasowo EEZ, zrób minimalistyczny ekran z przyciskiem zmieniającym screen. Jeśli działa → problem w EEZ.

---

## Który scenariusz jest NAJWIĘCEJ PRAWDOPODOBNY?

Biorąc pod uwagę architekturę:
- Main task co 100 ms lockuje LVGL i wywołuje `ui_tick()`.
- Jeśli `ui_tick()` ma stałe, długie operacje (np. odczyt z czujnika, update wielu labeli), to lock trwa długo.
- LVGL task (prio 4) chce mutex → blokuje → watchdog.

**Najprawdopodobniej `ui_tick()` wykonuje zbyt wiele operacji pod lockiem LVGL.**

---

## Jak potwierdzić bez kodu?

Niestety, potrzebujemy logów z:
- `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`
- Pomiar czasu `ui_tick()`.

---

## Co teraz?

**Zaproponuję utworzyć zmodyfikowany `main/main.cpp` z logami** (w folderze `logs/` nie, bo to zmiana kodu). Ale zgodnie z Twoją prośbą "narazie nie edytuj kodu", możemy:

1. **Przygotować plan** (ten plik).
2. **Zapytać Ciebie:** czy masz możliwość edycji `main/main.cpp`? Czy chcesz, żebym przygotował patch?
3. **Sprawdzić `sdkconfig.defaults`** — czy `CONFIG_SPIRAM_USE_MALLOC=y`? Czy `CONFIG_LV_MEM_SIZE_KILOBYTES=128`?
4. **Przejrzeć `ui.c`/`screens.c`** — czy `ui_tick()` robi ciężkie rzeczy?

---

## Pytania do użytkownika

1. Czy możesz edytować kod źródłowy w `main/main.cpp` i `sunton_esp32_8048s050c.c`?
2. Czy projekt używa WiFi/Bluetooth? (szukaj `CONFIG_ESP_WIFI_` w `sdkconfig`)
3. Czy freeze występuje natychmiast po starcie, czy po jakiejś akcji użytkownika (np. kliknięciu menu)?
4. Czy masz dostęp do plików UI generowanych (np. `main/ui/ui.c`, `main/ui/screens.c`)? Czy możesz wyszukać `ui_tick`?
5. Czy zmiana `task_max_sleep_ms` z 500 na 50 pomogła? (to mała zmiana, można spróbować)

---

## Rekomendacja najszybsza

**Zmodyfikuj `main/main.cpp` dodając logi** — to da nam dane. Jeśli nie chcesz modyfikować kodu, można spróbować **zmienić priorytet tasków przez monitorowanie** (np. `idf.py menuconfig` → Component config → FreeRTOS → task priorities), ale to nie daje nam informacji.

---

## Podsumowanie

Problem: **Task LVGL blokuje → watchdog timeout → freeze**.

Root cause: Prawdopodobnie **długie wykonanie `ui_tick()` pod lockiem LVGL** lub **brak pamięci RAM**.

Kolejne kroki: zbierz dane (heap, timing), następnie optymalizuj `ui_tick` lub architekturę.
