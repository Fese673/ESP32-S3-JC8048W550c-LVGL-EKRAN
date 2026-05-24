# Pełny Audyt Pamięci — RAM, Leaki i Fragmentacja

**Data:** 2026-05-24  
**Projekt:** ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ  
**Metodologia:** Ręczny przegląd kodu z narzędziami grep/glob, analiza lifecycle każdego zasobu.  
**Zakres:** `main/`, `main/ui/ui/`, `main/eez-framework/src/eez/`, `docs/`, `ekran_zegar_kopia/src/ui/`

---

## A. NAJWAŻNIEJSZE ZNALEZISKA (ranked od najgroźniejszych)

### 🔴 Poziom krytyczny

| # | ID | Tytuł | Typ |
|---|-----|-------|-----|
| 1 | `RAM_LEAK_001` | Timer UI i Stopwatch UI — brak cleanupu LVGL timerów i obiektów | LEAK (timer), FRAG (obj) |
| 2 | `FRAG_001` | `tick_screen` + `evalTextProperty` + `lv_label_set_text` — ~100 alloc/free cykli na 100ms | FRAG |
| 3 | `FRAG_002` | `Value::makeStringRef()` + `toString()` — per-tick alokacje StringRef | FRAG |
| 4 | `RAM_LEAK_002` | Flappy Bird — brak cleanupu przy zewnętrznym przełączeniu ekranu | LEAK |
| 5 | `FRAG_003` | EEZ allocator — brak size classes, mieszanie alokacji 4B–400B na SRAM | FRAG |
| 6 | `RAM_LEAK_003` | WiFi `s_work_queue` — nigdy nie zwalniany | LEAK (low impact) |

### 🟡 Poziom wysoki

| # | ID | Tytuł | Typ |
|---|-----|-------|-----|
| 7 | `RAM_LEAK_004` | `s_mutex` (CommState) i `s_logMutex` — nigdy nie zwalniane | LEAK (low impact) |
| 8 | `FRAG_004` | BPM chart — `lv_chart_add_series` przy re-init (potencjalny) | FRAG |
| 9 | `FRAG_005` | `sendRawFrame()` — 3 bufory na stosie (412 B) w jednej funkcji | STACK |
| 10 | `FRAG_006` | `comm_log_flush_now()` — 2 bufory 256 B na stosie z LVGL tickera | STACK |
| 11 | `FRAG_007` | `lv_label_set_text_fmt` w flappy_bird (line 538) — heap churn per pipe pass | FRAG |
| 12 | `FRAG_008` | `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` — próg za wysoki na SRAM | KONFIG |

### 🟢 Poziom średni/niski

| # | ID | Tytuł | Typ |
|---|-----|-------|-----|
| 13 | `FRAG_009` | 8 KB log buffer na SRAM, cykliczne alokacje co 500ms | FRAG |
| 14 | `RAM_LEAK_005` | `uart_driver_install` bez `uart_driver_delete` | LEAK (latent) |
| 15 | `RAM_LEAK_006` | `esp_event_handler_instance_register` bez unregister | LEAK (latent) |
| 16 | `FRAG_010` | `safe_label.h` — 64 B per label, ~50 labeli = 3.2 KB SRAM stale | FRAG (compromise) |
| 17 | `FRAG_011` | `clock_task` — handle NULL, brak możliwości stop | LEAK (low) |

---

## B. LEAK RAM — szczegółowa lista

### RAM_LEAK_001 — Timer UI / Stopwatch UI: brak cleanupu (CRITICAL)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/app/timer_ui.cpp:9,140,189`, `main/app/stopwatch_ui.cpp:9,72,112` |
| **Linie** | timer_ui: 9 (s_timer), 140 (init), 189 (lv_timer_create); stopwatch_ui: 9, 72, 112 |
| **Mechanizm** | `timer_ui_init()` i `stopwatch_ui_init()` są wołane raz w `app_main()`. Tworzą łącznie 12 labeli (9 timer + 3 stopwatch) i 2 LVGL timery (100ms, 20ms). **Brak funkcji cleanup**. Timery nigdy nie są pauzowane ani usuwane — działają wiecznie. |
| **Co leakuje** | LVGL timery (stały koszt ~64 B każdy w LVGL heap), LVGL obiekty labeli (stały koszt), CPU (timery fire'ują wiecznie z early-return guard) |
| **Co powinno zwolnić** | `lv_timer_delete(s_timer)` + `lv_obj_delete(parent_of_labels)` przy opuszczeniu ekranu `SCREEN_ID_ZEGAR` |
| **Cykl** | Stały (raz utworzone, nigdy nie zwalniane) |
| **Ryzyko** | **high-medium** — same timery to nie leak pamięci (stała alokacja), ale CPU waste na 50Hz timer = 50 wywołań/s nawet na innych ekranach. Timer callbacki mają early-return guard, ale to wciąż context switch + call. |
| **Propozycja naprawy** | Dodaj `timer_ui_deinit()` i `stopwatch_ui_deinit()` wołane z `main.cpp` w bloku `if (last_screen_id == SCREEN_ID_ZEGAR)` (screen switch). W deinit: `lv_timer_del(s_timer)`, ukryj/usun labelki. Albo przynajmniej `lv_timer_pause()` przy wychodzeniu z ekranu. |

---

### RAM_LEAK_002 — Flappy Bird: brak cleanupu przy zewnętrznym przełączeniu (HIGH)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/app/game/flappy_bird.cpp:893-935` |
| **Linie** | 897 (lv_timer_delete), 902 (lv_obj_delete) |
| **Mechanizm** | `flappy_bird_cleanup()` istnieje i jest wołany TYLKO z `main.cpp:100` gdy `last_screen_id == SCREEN_ID_GRY_TEST`. Jeśli EEZ Flow przełączy ekran bez przejścia przez Menu (np. zewnętrzna komenda BLE, timeout, przycisk sprzętowy), cleanup **nie zostanie wykonany**. |
| **Co leakuje** | LVGL timer (fire'uje co 30ms wiecznie z guardem), ~25 obiektów LVGL (game_container i potomkowie) pozostają widoczne/ukryte w drzewie widgetów. |
| **Co powinno zwolnić** | `flappy_bird_cleanup()` — ale nie jest wołany przy wszystkich exit paths |
| **Cykl** | Zależny — tylko przy alternatywnych exit paths |
| **Ryzyko** | **medium** — timer waste CPU, obiekty wiszą w pamięci (ale to tylko ~25 LVGL obiektów). Główny problem: timer na 30ms na Core 1 (LVGL) = 33 wywołania/s. |
| **Propozycja naprawy** | Dodaj `flappy_bird_deinit()` i wołaj z screen-switch hooka niezależnie od tego który screen. Użyj `static bool` guarda żeby wołać cleanup tylko jeśli gra była zainicjowana. |

---

### RAM_LEAK_003 — WiFi `s_work_queue` nigdy nie zwalniany (MEDIUM-LOW)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/services/wifi/wifi_service.c:238` |
| **Mechanizm** | `s_work_queue = xQueueCreate(1, sizeof(wifi_work_msg_t))` — utworzony, nigdy `vQueueDelete` |
| **Co leakuje** | FreeRTOS queue (1 element × ~100 B + struktura queue) |
| **Co powinno zwolnić** | `vQueueDelete(s_work_queue)` w shutdown path |
| **Ryzyko** | **low** — wifi_service żyje do resetu; queue nie kumuluje się. |
| **Propozycja naprawy** | Dodaj `vQueueDelete(s_work_queue)` + `s_work_queue = NULL` w shutdown handler. |

---

### RAM_LEAK_004 — Mutexy nigdy nie zwalniane (MEDIUM-LOW)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/communication/CommunicationState.cpp:77`, `main/utils/log/log.cpp:37,137,158` |
| **Mechanizm** | `s_mutex = xSemaphoreCreateMutex()` (CommState); `s_logMutex = xSemaphoreCreateMutex()` (log) — tworzone, nigdy `vSemaphoreDelete` |
| **Co leakuje** | FreeRTOS mutex handle |
| **Ryzyko** | **low** — oba żyją do resetu. Dodatkowo logMutex tworzony lazy w 3 różnych miejscach — może być stworzony wielokrotnie (ale guard `== NULL` chroni przed duplikacją). |
| **Propozycja naprawy** | Rozważ dodanie `mutex_deinit()` w shutdown path lub przynajmniej `assert(s_logMutex != NULL)` żeby sprawdzić, że tworzenie jest deterministyczne. |

---

### RAM_LEAK_005 — `uart_driver_install` bez `uart_driver_delete` (LOW)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/communication/UartTransport.cpp:145` (install), brak delete |
| **Mechanizm** | `uart_driver_install(UART_NUM_1, 1024, 2048, 0, NULL, 0)` — alokuje 1024+2048 B wewnętrznych ringbufferów. `stopUart()` nie woła `uart_driver_delete()`. |
| **Co leakuje** | 3 KB ringbufferów w SRAM (lub DMA) — przy każdym re-init UART |
| **Ryzyko** | **low** — `beginUart()` wołane raz z `app_main()`. Jeśli `stopUart()` + `beginUart()` są wołane ponownie, `uart_driver_install` zwróci `ESP_ERR_INVALID_STATE`. |
| **Propozycja naprawy** | Dodaj `uart_driver_delete(kUartPort)` w `stopUart()` przed wyjściem. |

---

### RAM_LEAK_006 — Event handler bez unregister (LOW)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/services/wifi/wifi_service.c:235-236` |
| **Mechanizm** | `esp_event_handler_instance_register(WIFI_EVENT, ...)` i `(IP_EVENT, ...)` — handle NULL (discarded). Nigdy `_unregister`. |
| **Ryzyko** | **low** — jeśli `wifi_service_init` wołane raz (a tak jest), podwójna rejestracja nie występuje. |
| **Propozycja naprawy** | Zachowaj handle i wywołaj unregister w shutdown path. |

---

### RAM_LEAK_007 — `clock_task` handle NULL (LOW)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/app/clock_logic.cpp:75` |
| **Mechanizm** | `xTaskCreatePinnedToCore(clock_task, "clock_task", 4096, NULL, 3, NULL, 0)` — handle to `NULL`. Nie można zatrzymać taska z zewnątrz. |
| **Ryzyko** | **low** — task żyje wiecznie. Handle do `NULL` uniemożliwia graceful shutdown. |
| **Propozycja naprawy** | Przechowuj handle i dodaj `stopClockTask()` do zatrzymania. |

---

## C. FRAGMENTACJA — szczegółowa lista

### FRAG_001 — `tick_screen()` + `evalTextProperty()` — masowe alloc/free per tick (CRITICAL)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/ui/ui/screens.c:1221-5586`, `main/ui/ui/eez-flow.cpp:6805-6812`, `main/app/main.cpp:130` |
| **Mechanizm** | `main.cpp:130` woła `ui_tick()` co 100ms. `ui_tick()` → `tick_screen(g_currentScreen)` → dla każdej etykiety na ekranie: `evalTextProperty(flowState, ...)` → `evalProperty()` → `toString()` → **`makeStringRef()`** (alloc StringRef + alloc string data). Wynik jest porównywany z obecnym tekstem i tylko jeśli różny wołany `lv_label_set_text()` (który safe_label.h przerzuca na `lv_label_set_text_static`). |
| **Skala** | Screen `main` (SCREEN_ID_MAIN) ma ~20-30 etykiet. `tick_screen_main()` (linie 1221-1550) robi ~30x evalTextProperty co 100ms. **Łącznie na tick: ~30 alloc/free StringRef + string data.** |
| **Dlaczego fragmentuje** | `makeStringRef()` alokuje 2 bloki: `StringRef` (~16 B) + string data (zmienny rozmiar 4-50 B). Wszystkie alokacje są małe (< 16384 B) → **leca na SRAM**. `Value::toString()` robi alloc dla STRING_REF, który jest zaraz uwalniany po `toText()` (gdy tymczasowy `Value` wychodzi z scope'a). Każdy tick = ~30 par alloc/free na SRAM o zmiennych rozmiarach. Po godzinie: 30×600×2 = 36 000 alloc/free cykli. |
| **Ryzyko** | **CRITICAL** — główna przyczyna fragmentacji SRAM opisana w `docs/FRAGMENTACJA_PAMIE.md`. Może prowadzić do `malloc` NULL na SRAM po długim uptime, watchdog resetów i artefaktów. |
| **Cykl** | Stały (co 100ms) |
| **Propozycja naprawy** | Trudna — wymaga zmian w EEZ framework. Opcje: (1) Zwiększyć `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` (przenieść małe alokacje do PSRAM), (2) Pool allocator dla `StringRef` (stały rozmiar 16 B), (3) Buforowanie per-label z `safe_label.h` — ale to działa dopiero po `lv_label_set_text`. Problem leży w `evalTextProperty` która za każdym razem tworzy nowy StringRef. |

---

### FRAG_002 — `Value::makeStringRef()` + `toString()` — per-tick alokacje StringRef (CRITICAL)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/eez-framework/src/eez/core/value.cpp:1378-1406`, `main/eez-framework/src/eez/core/value.h:627` |
| **Mechanizm** | `Value::toString(uint32_t id)` always calls `makeStringRef()`. `evalTextProperty` → `evalProperty()` → `value.toString()` → alloc new StringRef (2 allocations: `lv_malloc(sizeof(StringRef))` + `lv_malloc(len+1)`). Then `toText()` copies into static `textValue[1024]`. The temporary Value goes out of scope → `freeRef()` → `eez::free(str)` + `eez::free(StringRef)`. |
| **Skala** | Per tick: 30x call do `evalTextProperty`. Każdy = 2 allocs. 60 alloc/free na tick = 600 alloc/free na sekundę. |
| **Dlaczego fragmentuje** | String data ma zmienne rozmiary (od "0" = 2 B do "Oczekiwanie..." = 30 B). Krótkie stringi po długich tworzą nieciągłości. StringRef (stały 16 B) zapycha luki między string data — typowy wzorzec fragmentacji. |
| **Ryzyko** | **CRITICAL** — identyfikowane w `docs/FRAGMENTACJA_PAMIE.md` jako drugi największy problem po `lv_label_set_text`. |
| **Cykl** | Stały (co 100ms) |
| **Propozycja naprawy** | (1) Pool allocator dla StringRef (stały rozmiar 16 B → idealny do pool), (2) Caching `Value` z toString — sprawdzać czy string jest już taki sam i nie tworzyć nowego. |

---

### FRAG_003 — EEZ allocator: brak size classes (CRITICAL)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/eez-framework/src/eez/core/alloc.cpp:38-53` |
| **Mechanizm** | `eez::alloc(size)` → `lv_malloc(size)` wprost. Brak segregacji rozmiarów, brak własnych pooli, brak małych allocatorów (jak TLSF ma size classes, ale EEZ dodaje kolejną warstwę przez LVGL allocator). `lv_malloc` (LVGL 9.x) deleguje do `stdlib` (clib malloc) gdy `CONFIG_LV_USE_CLIB_MALLOC=y`. |
| **Skala** | Każda alokacja StringRef (16 B), string data (4-50 B), FlowState (variable) — wszystkie lecą na tę samą stertę. |
| **Dlaczego fragmentuje** | Mieszanie alokacji 16 B z 400 B bez pooli → szybkie rozbicie sterty. `AllocBlock` istnieje w alloc.cpp (linie 98-268) dla platform STM32 z własnym heapem, ale dla `EEZ_FOR_LVGL` nie jest używany. |
| **Ryzyko** | **CRITICAL** — multiplikuje efekt FRAG_001 i FRAG_002. |
| **Propozycja naprawy** | (1) Podmień LVGL allocator na własny z size classes, (2) Użyj `heap_caps_malloc` z preferencją PSRAM dla małych alokacji, (3) Pool allocator dla `Value`, `StringRef`, `FlowState`. |

---

### FRAG_004 — BPM chart: potencjalny duplicate series (MEDIUM)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/services/bpm/bpm_chart_handler.c:57-63` |
| **Mechanizm** | `bpm_timer_cb` tworzy chart lazily: jeśli `s_chart == NULL`, woła `find_chart()` + `configure_chart()` → `lv_chart_add_series()`. Jeśli `s_chart` zostanie zresetowane (np. przez przypadek) gdy obiekt chart wciąż istnieje, `lv_chart_add_series()` doda drugą serię. |
| **Ryzyko** | **medium** — wymaga błędnego kodu żeby się wydarzyło; ale jest to słaby punkt |
| **Propozycja naprawy** | Dodaj `if (s_series) return` guard przed `lv_chart_add_series()`. |

---

### FRAG_005 — sendRawFrame(): 3 bufory na stosie (412 B) (HIGH)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/communication/UartTransport.cpp:207-220` |
| **Mechanizm** | `sendRawFrame()` alokuje na stos: `frame[134]` + `cobsBuffer[138]` + `txBuffer[140]` ≈ 412 B. Wszystkie żyją jednocześnie. |
| **Ryzyko** | **high** — ~10% stacku taska LVGL (4096 B) lub UART TX (3072 B) w jednej funkcji. Przy zagnieżdżonych wywołaniach może przepełnić stos. |
| **Cykl** | Na każde wysłanie ramki (typowe 1-10/s) |
| **Propozycja naprawy** | Współdzielony bufor: `frame[134]` + COBS in-place encoding (albo jeden bufor 256 B z offsetami). Redukcja stacku ~270 B. |

---

### FRAG_006 — comm_log_flush_now(): 2 bufory 256 B na stosie z LVGL tickera (HIGH)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/utils/comm_log/comm_log.cpp:74-75` |
| **Mechanizm** | `comm_log_flush_now()` → `char tx_buf[256]` + `char rx_buf[256]` na stosie. Wołane z `comm_log_flush_if_needed()` → z `ui_tick_timer_cb()` (LVGL task stack). |
| **Ryzyko** | **high** — 512 B stack na LVGL tasku (~6-8 KB). Przy współistnieniu z innymi funkcjami w jednym ticku może dać stack overflow. |
| **Cykl** | Co ~10-30s |
| **Propozycja naprawy** | Zamień na `static char buf[256]` (koszt 256 B BSS). |

---

### FRAG_007 — `lv_label_set_text_fmt` w flappy_bird (MEDIUM)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/app/game/flappy_bird.cpp:538` |
| **Mechanizm** | `lv_label_set_text_fmt(s_score_label, "%d", s_score)` — wołane za każdym razem gdy ptak przeleci przez rurę. `safe_label.h` nie interceptuje `lv_label_set_text_fmt`. LVGL wewnętrznie robi `lv_free(stary) + lv_strdup(nowy)`. |
| **Ryzyko** | **medium** — score zmienia się ~10-20 razy na grę. Przy 200 grach = 2000-4000 alloc/free cykli. Małe, ale zmienne rozmiary (1-3 cyfry). |
| **Cykl** | Tylko podczas gry, ale kumuluje się |
| **Propozycja naprawy** | Zamień na `snprintf(buf, sizeof(buf), "%d", s_score)` + `lv_label_set_text(s_score_label, buf)` (safe_label.h intercepts). |

---

### FRAG_008 — `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` — próg za wysoki (HIGH)

| Pole | Wartość |
|------|---------|
| **Plik** | `sdkconfig.defaults` (lub `sdkconfig`) |
| **Mechanizm** | Alokacje ≤16384 B lecą do SRAM. Większość alokacji w systemie to małe bloki (16-1000 B) → wszystkie w SRAM. SRAM ma ~200 KB free po starcie. |
| **Ryzyko** | **high** — gdyby próg był niższy (np. 4096), małe alokacje trafiałyby do 8 MB PSRAM, a SRAM byłby zarezerwowany dla DMA/krytycznych. Obecnie ~100 KB SRAM jest "zajęte" przez małe alokacje które mogłyby być w PSRAM. |
| **Cykl** | Stały |
| **Propozycja naprawy** | Obniż `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`. Zysk: 50-100 KB SRAM. |

---

### FRAG_009 — 8 KB log buffer na SRAM (MEDIUM)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/utils/log/log.cpp:25` |
| **Mechanizm** | `static char log_buffer[8192]` na SRAM (BSS). Co ~500ms: `lv_textarea_set_text(objects.log_terminal, log_buffer)` → LVGL wewnętrznie alokuje kopię stringa (8 KB) na heap, potem ją zwalnia przy następnym update (albo incremental). |
| **Ryzyko** | **medium** — 8 KB blok na SRAM, jeśli alokowany i zwalniany cyklicznie, rozrywa ciągłość sterty. `lv_textarea_set_max_length(obj, 0)` pomaga (watchdog fix), ale nie eliminuje alloc 8 KB. |
| **Cykl** | Co ~500ms gdy ekran logów jest aktywny |
| **Propozycja naprawy** | (1) Użyj `heap_caps_malloc(8192, MALLOC_CAP_SPIRAM)` dla log_buffer → przenieś do PSRAM. (2) Użyj `lv_label_set_text_static(log_buffer)` zamiast `lv_textarea_set_text()` (ale tracisz edycję). |

---

### FRAG_010 — safe_label.h: 64 B per label na SRAM (LOW — świadomy kompromis)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/app/safe_label.h:38-51` |
| **Mechanizm** | Każdy label dostaje `malloc(64)` na SRAM (bo 64 < 16384 → ALWAYSINTERNAL). ~50 labeli = 3.2 KB SRAM stale. |
| **Ryzyko** | **low** — świadomy kompromis eliminujący ~270 alloc/free na sekundę. Koszt: 3.2 KB SRAM. |
| **Propozycja naprawy** | Można zmniejszyć bufor do 32 B (większość labeli to 1-2 cyfry + jednostka). Albo użyć `heap_caps_malloc(64, MALLOC_CAP_SPIRAM | MALLOC_CAP_INTERNAL)`. |

---

### FRAG_011 — clock_task handle NULL, brak stop (LOW)

| Pole | Wartość |
|------|---------|
| **Plik** | `main/app/clock_logic.cpp:75` |
| **Mechanizm** | Task handle `NULL` → nie można zatrzymać taska. Task robi `lvgl_port_lock()` co sekundę. |
| **Ryzyko** | **low** — task żyje wiecznie. |
| **Propozycja naprawy** | Przechowuj handle, dodaj `clock_logic_stop()`. |

---

## D. FAŁSZYWE ALARMY / ŚWIADOME KOMPROMISY

| ID | Lokalizacja | Dlaczego to nie jest problem |
|----|-------------|------------------------------|
| `SAFE_01` | `safe_label.h:39` — malloc(64) per label | **Świadomy kompromis.** 3.2 KB SRAM stale za eliminację ~270 alloc/free na sekundę. Zastosowanie `lv_label_set_text_static()` z własnym buforem — zero alloc na tick. `LV_EVENT_DELETE` z free() zapewnia cleanup. |
| `SAFE_02` | EEZ Flow screens — wszystkie 13 tworzone raz, nigdy nie usuwane | **Zamierzona architektura.** Screens są create-once, reuse-forever. ~100 KB na widgety w PSRAM, ale eliminuje alloc/free przy przełączaniu. `g_deleteScreenFunc` never registered — świadomie. |
| `SAFE_03` | `wifi_service.c:235-236` — event handlers bez unregister | **Singleton service.** `wifi_service_init()` wołane raz. Brak unregister nie powoduje problemów w runtime. |
| `SAFE_04` | `CommunicationState.cpp:77` s_mutex bez delete | **Singleton.** Żyje do resetu. Koszt alokacji stały (< 100 B). |
| `SAFE_05` | `UartTransport.cpp:163,168` — task handles | **Samodeletujące.** RX i TX taski same wołają `vTaskDelete(nullptr)` przy stopie UART. `stopUart()` czeka na zakończenie. |
| `SAFE_06` | `bpm_chart_handler.c:90` — timer never deleted | **Singleton screen.** Chart i timer tworzone raz. Timer ma guard screenID → early return. Chart tworzony przez EEZ Flow, nie ręcznie. |
| `SAFE_07` | `utf8.h:487,714` — malloc w header-only bibliotece | **Tylko przy wywołaniu.** Alokacje w bibliotece UTF8 są zwalniane. Małe ryzyko. |
| `SAFE_08` | EEZ allocator w `eez-flow.cpp:40-50` — deleguje do `lv_malloc`/`lv_free` | **Poprawny.** Wszystkie alokacje EEZ idą przez ten sam mechanizm co LVGL — nie ma double allocator problemu. |
| `SAFE_09` | `toText()` static buffer `textValue[1024]` | **Safe sequential access.** Wszystkie wywołania w obrębie jednego tick_screen są sekwencyjne, nie zagnieżdżone. 1024 B wystarcza dla najdłuższego stringa. |
| `SAFE_10` | `FLASH` → `RAM` storage dla WiFi config | **Świadomy kompromis przeciw tearingowi.** `WIFI_STORAGE_RAM` + deferred NVS save jako zabezpieczenie przed tearingiem (PSRAM/RGB contention). |

---

## E. DODATKOWA WARSTWA — POTENCJALNE PROBLEMY NIESTABILNOŚCI, FPS, CPU

### CPU churn

| ID | Lokalizacja | Opis | Wpływ |
|----|-------------|------|-------|
| `CPU_01` | `main.cpp:131-132` | `stopwatch_ui_sync_start_stop_label()` + `timer_ui_sync_start_stop_label()` — wołane co 100ms nawet gdy nie na zegarze. Każde to null check + strcmp. | Niski |
| `CPU_02` | `bpm_chart_handler.c:47-53` | Timer 100ms wołany wiecznie, guard na screenID. ~10 wywołań/s na Core 1. | Niski |
| `CPU_03` | `flappy_bird.cpp:379-381` | Guard screenID w timer callback — 33 wywołania/s na Core 1 gdy gra nieaktywna. | Średni (niepotrzebne WFI disturb) |
| `CPU_04` | `CobsFraming.cpp:5-17` | CRC16 bitwise zamiast lookup table — 1072 iteracji na ramkę. | Niski (ramek mało) |

### Stack pressure

| ID | Lokalizacja | Opis | Wpływ |
|----|-------------|------|-------|
| `STACK_01` | `log.cpp:42,99,111,123` | `char message[256]` na stosie w 4 funkcjach. Wołane z log_info/warn/error. | Niski (każda to osobna ramka) |
| `STACK_02` | `CommUiMapper.cpp:15-22` | `formatWithComma()` — wołana z `applySnapshot*`. Stack używany przez snprintf. | Niski |
| `STACK_03` | `main.cpp:128-136` | `ui_tick_timer_cb` — woła 6+ funkcji, każda ma ~256 B stack. Łącznie ~1.5 KB stack na jednym ticku. | Średni — LVGL task stack 8192 B, ~20% w tick |

---

## F. PRIORYTET TESTOW NA URZĄDZENIU

Kolejność testowania od najważniejszego:

### Test 1: Fragmentacja SRAM przy długim uptime
**Co testować:** Uruchom system na 24-72h z aktywną główną stroną (`SCREEN_ID_MAIN`). Monitoruj:
- `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` co 10s
- `heap_caps_get_total_size(MALLOC_CAP_INTERNAL)` vs `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`
- Liczba `malloc` fail (`ESP_LOGE` z braku pamięci)
- WDT resety

**Oczekiwany wynik:** Bez safe_label.h — znaczący spadek largest_free_block w ciągu godzin.
Z safe_label.h — stabilny.

### Test 2: Flappy Bird exit paths
**Co testować:** Wszystkie sposoby wyjścia z gry:
- Menu button (cleanup wołany) ✓
- BLE command do przełączenia ekranu (cleanup NIE wołany)
- Timeout automatyczny (jeśli istnieje)
- Back/hardware button

**Monitoruj:** Czy timer gry (30ms) nadal fire'uje po wyjściu spoza Menu. Czy obiekty LVGL znikają z memory usage.

### Test 3: Timer / Stopwatch CPU waste
**Co testować:** Zmierz `esp_timer_get_time()` w `log_ui_tick_end` (już istnieje; linia 191-193 w log.cpp). Sprawdź czy nie ma slow tick ostrzeżeń na żadnym ekranie. Monitoruj FPS na ekranie ZEGAR vs MAIN.

### Test 4: UART + WiFi + sensor data overnight
**Co testować:** Podłącz sensor board (MAX30102 + BME280 + inne). Uruchom na 8-12h z pełną komunikacją:
- Czy handshake nie resetuje się z powodu braku pamięci?
- Czy UART TX queue drops (log warning) nie rosną?
- Czy `heap_caps_get_largest_free_block` utrzymuje się?

### Test 5: WiFi connect/disconnect cycle
**Co testować:** 100x WiFi connect → disconnect → rescan. Sprawdź:
- Czy `malloc` scan results nie leakuje (testowane w kodzie — OK)
- Czy worker task i queue nie kumulują się
- Czy NVS close/open para nie leakuje (testowane — OK)

### Test 6: Wszystkie screeny, szybkie przełączanie
**Co testować:** Przełączaj między wszystkimi 13 screenami co 1-2s przez 30 minut.
**Monitoruj:** memory usage, largest_free_block, FPS, WDT, stack usage (uxTaskGetStackHighWaterMark).

---

## G. PODSUMOWANIE LICZBOWE

| Kategoria | Liczba | Krytyczne | Wysokie | Średnie | Niskie |
|-----------|--------|-----------|---------|---------|--------|
| RAM Leak | 7 | 1 | 1 | 1 | 4 |
| Fragmentacja | 11 | 3 | 3 | 3 | 2 |
| CPU / Stack | 7 | 0 | 0 | 1 | 6 |
| **Razem** | **25** | **4** | **4** | **5** | **12** |

### Najważniejsze 3 rzeczy do zrobienia:

1. **Obniżyć `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`** — przenosi małe alokacje (w tym StringRef, Value, string data) z SRAM do PSRAM. **Zysk: 50-100 KB SRAM.** Potencjalnie eliminuje fragmentację SRAM jako problem produkcyjny.

2. **Dodać `timer_ui_deinit()` / `stopwatch_ui_deinit()`** — pauzować timery przy opuszczeniu ekranu ZEGAR. Koszt: mały, zysk: eliminacja niepotrzebnych callbacków i potencjalny pierwszy krok do pełnego lifecycle managementu.

3. **Refaktoryzować `sendRawFrame()`** — jeden bufor zamiast trzech na stosie. **Zysk stacku: ~270 B.** Zmniejsza ryzyko stack overflow na LVGL tasku.

---

## H. LEGENDA IDENTYFIKATORÓW

| Przedrostek | Opis |
|-------------|------|
| `RAM_LEAK_*` | Zasób który nie jest zwalniany |
| `FRAG_*` | Wzorzec alokacji który fragmentuje stertę |
| `CPU_*` | Marnowanie CPU (ale nie RAM) |
| `STACK_*` | Presja na stos (ryzyko stack overflow) |
| `SAFE_*` | Fałszywy alarm — świadomy kompromis |
