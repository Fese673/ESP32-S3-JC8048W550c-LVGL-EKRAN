# Archiwum Błędów - ZEGAR-ESP32

---
### [ID: ERR_012] | FLAPPY_BIRD_REENTER_FREEZE | IMPACT: HIGH
**Files:** `[main.cpp, flappy_bird.cpp]`

**PROBLEM:** Po wyjściu z gry Flappy Bird do menu głównego i ponownym jej uruchomieniu, gra nie reagowała na tapnięcia i nie dało się grać.

**CAUSE:**
1. Brak wywołania `flappy_bird_cleanup()` podczas opuszczania ekranu `SCREEN_ID_GRY_TEST` w `main.cpp` powodował, że stary `s_game_container` oraz jego widgety nie były usuwane z pamięci.
2. Przy ponownym wejściu do gry tworzony był nowy kontener, ale stary overlay Game Over (`s_gameover_overlay`) z poprzedniej rozgrywki wciąż istniał pod spodem i posiadał aktywną flagę `LV_OBJ_FLAG_CLICKABLE`. Swallowował on wszystkie kliknięcia, przez co nowa instancja gry nie otrzymywała zdarzeń dotykowych.
3. Callback dotykowy był zarejestrowany na globalnym ekranie `parent_screen`, co powodowało nawarstwianie się callbacków i wycieki przy każdym przejściu ekranowym.

**LOGIC_CHANGE:**
- W `main.cpp` w `ui_tick_timer_cb()` dodano obsługę przejść dla ekranu gry: wywołanie `flappy_bird_cleanup()` przy opuszczaniu `SCREEN_ID_GRY_TEST` oraz `flappy_bird_init()` przy wejściu.
- W `flappy_bird.cpp` w `flappy_bird_cleanup()` dodano jawne usuwanie kontenera gry `lv_obj_delete(s_game_container)` i zresetowanie flagi `s_game_initialized = false`.
- Przeniesiono callback dotykowy z globalnego `parent_screen` na `s_game_container` z flagą `LV_OBJ_FLAG_CLICKABLE`, aby automatycznie sprzątał się przy usuwaniu kontenera.

**VERIFICATION:** Gra poprawnie reaguje na dotyk po wielokrotnym wchodzeniu i wychodzeniu przez menu główne. Wszystkie obiekty są poprawnie usuwane i alokowane na nowo.
---
### [ID: ERR_011] | CLOCK_WORLD_ZONES_CRASH | IMPACT: CRITICAL
**Files:** `[clock_logic.cpp]`

**PROBLEM:** Guru Meditation (LoadProhibited, NULL dereference) przy wejściu na ekran Zegar. Reboot po ~1s.

**CAUSE:** `clock_logic.cpp` używał `assignStringProperty(fs, 72/78/84, 3, ...)` z hardkodowanymi indeksami komponentów EEZ Flow. Po re-eksporcie UI z EEZ Studio (dodanie BPM/SpO2 flow variables) indeksy na ekranie zegara uległy zmianie. `component->properties[3]->evalInstructions` → NULL → crash w `evalExpression()`.

**LOGIC_CHANGE:**
- Zastąpiono `assignStringProperty(fs, index, ...)` z getFlowState na `lv_label_set_text(objects.hour_text_N, ...)` używając nazwanych obiektów z `screens.h`
- `getFlowState(0, 7)` i cały blok `if (fs)` usunięto — nie są potrzebne przy bezpośrednim LVGL API
- Nazwane obiekty (`hour_text_23/24/25`) są stabilne między eksportami EEZ, w przeciwieństwie do indeksów komponentów flow

**VERIFICATION:** Build OK (85/85). Brak zależności od EEZ Flow component indices dla światek stref czasowych.
---
### [ID: ERR_007] | DROPDOWN_CALLBACK_LEAK | IMPACT: HIGH
**Files:** `[main.cpp]`

**PROBLEM:** `init_custom_dropdown_hook()` w `main.cpp:37-43` dodawał callback `LV_EVENT_VALUE_CHANGED` do `settings_melody_dropdown` przy KAŻDYM wejściu na ekran ustawień. Dropdown nie jest niszczony między ekranami, więc callbacki kumulowały się bez limitu.

**CAUSE:** Brak flagi `static bool` chroniącej przed wielokrotną rejestracją, analogicznie do ERR_005 (set_wifi.c). Po 10 wejściach zmiana melodii wysyłała 10 identycznych ramek UART.

**LOGIC_CHANGE:**
- Dodano `static bool s_dropdown_hook_inited` - callback rejestrowany tylko raz
- `lv_dropdown_set_selected()` nadal wywoływany przy każdym wejściu (EEZ Flow może resetować UI)
- Wzorzec guarda identyczny jak w ERR_005

**VERIFICATION:** Kompilacja OK. Dropdown ma dokładnie jeden callback niezależnie od liczby wejść.
---
**Files:** `[log.cpp]`

**PROBLEM:** `lv_textarea_set_text(objects.log_terminal, log_buffer)` deep-copyował cały 8KB bufor logów co 500ms. Powodowało to niepotrzebne alloc/free cykle (heap churn) i potencjalną fragmentację SRAM.

**CAUSE:** Pełne przepisywanie zawartości textarea zamiast dopisywania tylko nowych linii.

**LOGIC_CHANGE:**
- Dodano śledzenie pozycji `s_last_buffer_pos` - dopisywane są tylko nowe dane od ostatniej aktualizacji
- Przy overflow bufora lub `log_clear()` wykonywany jest pełny reset przez `lv_textarea_set_text`
- Flaga `s_force_full_refresh` wymusza pełne przeładowanie po wejściu na ekran logów

**VERIFICATION:** Kompilacja OK. Logi są dopisywane, nie przepisywane - eliminacja 8KB kopii co 500ms.
---
### [ID: ERR_005] | SETWIFI_CALLBACK_LEAK | IMPACT: HIGH
**Files:** `[set_wifi.c]`

**PROBLEM:** Każde wejście na ekran ustawień WiFi wywoływało `lv_obj_add_event_cb(wifi_connect_btn, ...)`, dodając DUPLIKAT callbacka. LVGL nie sprawdza duplikatów - po N wejściach przycisk miał N handlerów, a w SRAM wyciekało N × `sizeof(lv_event_dsc_t)`.

**CAUSE:** `wifi_screen_init()` wywoływany za każdym razem przy zmianie ekranu, bez guarda na rejestrację callbacków. `wifi_connect_btn` jest tworzony raz przez EEZ Flow i nie jest niszczony między wejściami.

**LOGIC_CHANGE:**
- Dodano `static bool s_wifi_callbacks_registered` - rejestracja callbacka na przycisku "Połącz" tylko przy pierwszym wywołaniu
- Przyciski listy (mock_networks) są nadal rejestrowane za każdym razem, bo lista jest czyszczona i odtwarzana (`lv_obj_clean`)

**VERIFICATION:** Kompilacja OK. Przycisk "Połącz" ma dokładnie jeden callback niezależnie od liczby wejść na ekran.
---
**Files:** `[UartTransport.cpp]`

**PROBLEM:** `stopUart()` ustawiał `s_started = false` i natychmiast czyścił `s_taskHandle`, nie czekając na faktyczne zakończenie zadania `communicationTask`. Szybkie wywołanie `beginUart()` po `stopUart()` mogło stworzyć zombie-task lub podwójne zadanie.

**CAUSE:** Brak synchronizacji między sygnalizacją stop a potwierdzeniem zakończenia wątku.

**LOGIC_CHANGE:**
- `communicationTask` zeruje `s_taskHandle` przed `vTaskDelete(nullptr)`
- `stopUart()` czeka w pętli z `vTaskDelay(1ms)` na wyzerowanie handle'a (timeout 100ms)
- Duże bufory w `sendRawFrame` zmieniono z alokacji stosowej na `static`, eliminując ~412B presji na stos

**VERIFICATION:** Kompilacja OK. Logika gwarantuje, że task zakończył działanie przed powrotem z `stopUart()`.
---
### [ID: ERR_003] | LOG_MUTEX | IMPACT: HIGH
**Files:** `[log.cpp]`

**PROBLEM:** Wszystkie operacje na `log_buffer` (zapis w `log_add_to_buffer`, czyszczenie w `log_clear`, odczyt w `log_update_textarea`) były wykonywane bez żadnej synchronizacji. Logowanie z wielu wątków (WiFi, UART, LVGL) mogło spowodować korupcję bufora i crash.

**CAUSE:** Brak mutexu chroniącego współdzielone zmienne `log_buffer`, `log_buffer_pos`, `log_change_counter`.

**LOGIC_CHANGE:**
- Dodano `SemaphoreHandle_t s_logMutex` z inicjalizacją lazy
- Wszystkie operacje na buforze logów (dodawanie, czyszczenie, odczyt do textarea) owinięto `xSemaphoreTake`/`xSemaphoreGive`

**VERIFICATION:** Kompilacja OK. Logowanie z dowolnego taska jest teraz bezpieczne wątkowo.
---
### [ID: ERR_002] | COMM_RACE | IMPACT: HIGH
**Files:** `[CommunicationState.cpp, CommunicationProtocol.cpp, CommunicationState.h]`

**PROBLEM:** Race condition między Core 0 (UART Task) a Core 1 (LVGL Task) przy odczycie/zapisie flag dirty. Klasyczny wzorzec `isDirty()` → `getData()` → `clearDirty()` miał okno, w którym nowe dane mogły być ustawione po `getData()` ale przed `clearDirty()`, powodując utratę aktualizacji UI.

**CAUSE:** Oddzielne wywołania `isDirty()` (bez blokady), `getLocalSnapshot()` (z blokadą) i `clearDirty()` (bez blokady) - brak atomowości między kopią danych a czyszczeniem flagi.

**LOGIC_CHANGE:**
- Dodano funkcje `consumeSnapshot/Settings/Resources/OutdoorWeather` - atomowo kopiują dane i czyszczą flagę pod jednym mutexem
- `updateUi()` w CommunicationProtocol.cpp używa nowych funkcji zamiast starego wzorca
- `initCommunicationState()` przeniesione poza anonimową przestrzeń nazw, wywoływane z `CommunicationProtocol::begin()` - eliminuje TOCTOU race przy lazy init mutexu
- `isResetUiPending`/`setResetUiPending`/`clearResetUiPending` chronione mutexem (`lockSnapshot`)

**VERIFICATION:** Kompilacja OK. Wzorzec consume-and-clear pod jednym mutexem eliminuje okno race condition.
---
### [ID: ERR_001] | UI_MEMORY | IMPACT: CRITICAL
**Files:** `[safe_label.h]`

**PROBLEM:** Krytyczny **wyciek pamięci SRAM** w mechanizmie "bezpiecznych etykiet". Każda aktualizacja tekstu na nowym ekranie alokowała 64 bajty, które nigdy nie były zwracane do systemu po zamknięciu ekranu.

**CAUSE:** Bufor alokowany przez `malloc` był przechowywany w `user_data` obiektu LVGL. LVGL przy usuwaniu obiektu nie wiedział o konieczności zwolnienia zewnętrznie zaalokowanej pamięci.

**LOGIC_CHANGE:**
- Dodano funkcję callback `safe_label_delete_cb` zwalniającą pamięć przez `free()`.
- Wprowadzono automatyczną rejestrację zdarzenia `LV_EVENT_DELETE` podczas pierwszej alokacji bufora dla labela.
- Dodano **defensive checks** (NULL pointers) dla parametrów wejściowych funkcji.

**VERIFICATION:** Kompilacja OK. Logika zgodna z modelem zdarzeń LVGL 9.4.
---
### [ID: ERR_008] | WIFI_FLASH_WRITE_TEARING | IMPACT: CRITICAL
**Files:** `[wifi_service.c, set_wifi.c]`

**PROBLEM:** Persistent display tearing po kliknięciu "Połącz" — UI przesunięte, dublowane, rozdarte. Efekt utrzymuje się na wszystkich ekranach nawet po wyjściu z WiFi.

**CAUSE:** `esp_wifi_set_config()` z `WIFI_STORAGE_FLASH` zapisuje do NVS. Na ESP32-S3 flash i PSRAM współdzielą SPI1. Podczas flash write PSRAM jest niedostępny → EDMA (RGB display) czyta śmieci z framebuffera → tearing utrwalony.

**LOGIC_CHANGE:**
- `WIFI_STORAGE_RAM` dla `set_config()` podczas connect (zero flash write)
- Deferred NVS save przez `nvs_set_blob("wifi", "sta")` po GOT_IP (worker task)
- `g_wifi_nvs_save_complete` → `lv_obj_invalidate(lv_scr_act())` — full redraw kasuje śmieci w 1 klatce

**VERIFICATION:** Tearing znika po 16ms. Config persistuje przez bezpośrednie NVS API z kluczem "sta".
---
### [ID: ERR_010] | STOPER_FLICKER | IMPACT: MEDIUM
**Files:** `[stopwatch_ui.cpp]`

**PROBLEM:** Stopwatch labels flicker ~10Hz na ekranie Zegara i na każdym innym ekranie. Wygląda jak "drugi stoper" nakładający się na właściwy.

**CAUSE:** EEZ Flow `tick_screen_zegar()` w `screens.c` (100ms) pisze `"0"` do `objects.minutes_stoper_label` (flow variable `minutes_stoper = 0` → string `"0"`). Nasz `stopwatch_timer_cb` (20ms) pisze `"00"`. `safe_label.h` wykrywa `"0" != "00"` → `lv_label_set_text_static()` → `lv_obj_invalidate()` → konflikt co 100ms. Na innych ekranach (przed dodaniem screen guarda) timer pisał do ukrytych labeli → invalidate zbędny → migotanie.

**LOGIC_CHANGE:**
- Ukryto EEZ Flow etykiety: `lv_obj_add_flag(objects.xxx, LV_OBJ_FLAG_HIDDEN)`
- Stworzono własne etykiety (`s_min_label`, `s_sec_label`, `s_cs_label`) w tym samym parent, z tą samą pozycją/fontem/kolorem
- Timer pisze TYLKO do własnych etykiet — tick_screen_zegar() pisze do ukrytych EEZ Flow labeli (niewidoczne → brak efektu)

**VERIFICATION:** Zero flicker na wszystkich ekranach. Stoper działa płynnie 50fps. Build OK. Szczegóły w `ERROR_ARCHIVE/ERR_010_STOPER_FLICKER.md`.
---
### [ID: ERR_009] | WIFI_CONNECT_RACE | IMPACT: HIGH
**Files:** `[wifi_service.c, set_wifi.c, main.cpp]`

**PROBLEM:** Trzy bugi: (1) double `esp_wifi_connect()` race — worker + event handler wołały dwa razy, (2) NVS-loaded config powodował 3s disconnect po resecie, (3) SNTP init przed GOT_IP.

**CAUSE:** Worker wołał `esp_wifi_disconnect()` → DISCONNECTED → event handler retryował `esp_wifi_connect()` (❶). Worker też wołał `esp_wifi_connect()` (❷). Auto-connect przez `esp_wifi_connect()` z NVS-loaded config miał inną ścieżkę niż manualny przez `set_config(RAM)` → DHCP fail. SNTP start przed WiFi → DNS fail.

**LOGIC_CHANGE:**
- Worker: usunięto `esp_wifi_disconnect()` — tylko `set_config(RAM)` + `connect()`
- Event handler DISCONNECTED: dla manual connect = ignorowany (timer 15s decyduje)
- Auto-connect przez worker: `queue_saved_connect()` → czyta NVS → worker jak manual
- `STA_START`: zamiast `esp_wifi_connect()` → `queue_saved_connect()`
- Worker + queue tworzone PRZED `esp_wifi_start()`
- NVS save: `nvs_set_blob("wifi", "sta")` — zgodny z ESP-IDF WiFi driver
- SNTP: start w GOT_IP handlerze, nie w `app_main`

**VERIFICATION:** Auto-connect po resecie działa (worker task). SNTP startuje po GOT_IP. Manual connect bez double race.
---
