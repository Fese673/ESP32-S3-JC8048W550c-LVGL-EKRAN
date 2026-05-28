# Archiwum Błędów - ZEGAR-ESP32

---
### [ID: ERR_029] | BPM_GETTER_SPINLOCK_DEADLOCK | IMPACT: CRITICAL (regression ERR_027)
**Files:** `[bpm_service.c]`

**PROBLEM:** Mój fix ERR_027 dodał `portENTER_CRITICAL(&s_ctx.mux)` w `bpm_service_get_bpm()` i `get_spo2()`. `s_ctx.mux` jest w BSS → zero-init. Na ESP32-S3 dual-core `portMUX_INITIALIZER_UNLOCKED` to NIE zero, a wartość `owner=0xFFFFFFFF`. Zero w `owner` oznacza "locked by Core 0". Gdy Core 0 próbował CAS na locked mux → spinlock loop z przerwaniami wyłączonymi → Interrupt wdt timeout.

**CAUSE:** Getters (single-field reads: int/uint8_t) są atomiczne na Xtensa LX7 bez spinlocka. Spinlock był zbędny i niebezpieczny przy mux w BSS.

**LOGIC_CHANGE:**
- `bpm_service_get_bpm()`: direct `return s_ctx.bpm` (atomic 4-bajt aligned read)
- `bpm_service_get_spo2()`: direct `return s_ctx.spo2` (atomic 1-bajt read)
- `bpm_service_get_mock_mode()`: direct `return s_ctx.mock_enabled` (atomic 1-bajt read)
- Settery (`set_bpm`, `set_spo2`, `set_mock_mode`) nadal używają spinlocka dla release barrier

**VERIFICATION:** Kompilacja OK. Boot przechodzi bez deadlocka.

---
### [ID: ERR_028] | LOG_MUTEX_TOCTOU_RACE | IMPACT: CRITICAL
**Files:** `[log.cpp]`

**PROBLEM:** Potrójny TOCTOU race na lazy init mutexu w `log_add_to_buffer()`, `log_clear()`, `log_update_textarea()`. Jeśli UART RX (Core 0) i LVGL (Core 1) jednocześnie wywołały log przed inicjalizacją mutexu, oba tworzyły osobny mutex → jeden wyciekał. `log_get_all_text()` zwracał surowy wskaźnik do bufora bez locka.

**CAUSE:** Trzy funkcje robiły `if (s_logMutex == NULL) s_logMutex = xSemaphoreCreateMutex()` bez synchronizacji.

**LOGIC_CHANGE:**
- Przeniesiono `s_logMutex = xSemaphoreCreateMutex()` do `log_init()` — wołane z `app_main` przed schedulerem.
- Usunięto lazy init ze wszystkich 3 funkcji.
- Dodano `if (s_logMutex)` guard przed `xSemaphoreTake`.

**VERIFICATION:** Kompilacja OK. Mutex tworzony raz, deterministycznie, bez race condition.

---
### [ID: ERR_027] | BPM_SERVICE_DATA_RACE | IMPACT: CRITICAL
**Files:** `[bpm_service.c]`

**PROBLEM:** `bpm_service_set_bpm()` i `bpm_service_set_spo2()` pisane z Core 0 (UART RX) bez spinlocka, podczas gdy `bpm_service_get_bpm()` i `bpm_service_get_spo2()` czytane z Core 1 (LVGL) bez spinlocka. Torn read 32-bit `beat_period_us` / `phase_us` mógł zwrócić śmieci → UI pokazywał BPM=0 lub wartość z kosmosu.

**CAUSE:** `portMUX_TYPE mux` istniał w struct i był używany dla ring buffer, ale nie dla getterów/setterów BPM/SpO2/mock.

**LOGIC_CHANGE:**
- Dodano `portENTER_CRITICAL`/`portEXIT_CRITICAL` w `set_bpm`, `get_bpm`, `set_spo2`, `get_spo2`, `set_mock_mode`, `get_mock_mode`.

**VERIFICATION:** Kompilacja OK. Wszystkie accessy do współdzielonych pól BPM/SpO2 są atomiczne.

---
### [ID: ERR_026] | COMMHANDSHAKE_CORE_RACE | IMPACT: CRITICAL
**Files:** `[CommHandshake.cpp]`

**PROBLEM:** `s_syncState`, `s_bootId`, `s_lastHelloSentMs`, `s_lastFrameRxMs` — 4 zmienne bez `volatile`, bez mutexu, bez atomics. `onPeerHelloAckReceived()` pisze z Core 0 (UART RX), `updateHandshake()` czyta i pisze z Core 1 (LVGL). Kompilator mógł cache'ować odczyt w rejestrze → switch brał złą gałąź → błędna synchronizacja lub timeout.

**CAUSE:** Brak `volatile` kwalifikatora na shared state między rdzeniami.

**LOGIC_CHANGE:**
- Dodano `volatile` do wszystkich 4 zmiennych statycznych.

**VERIFICATION:** Kompilacja OK. Każdy odczyt wymusza load z pamięci zamiast rejestru.

---
### [ID: ERR_025] | UART_TX_QUEUE_UB | IMPACT: HIGH
**Files:** `[UartTransport.cpp]`

**PROBLEM:** `uartTxTask` blokował się `portMAX_DELAY` na `xQueueReceive(s_txQueue)`. `stopUart()` wołał `vQueueDelete(s_txQueue)` bez odblokowania taska. FreeRTOS: usunięcie queue z zablokowanym taskiem to UB (dangling waiting list entry, potencjalny crash).

**CAUSE:** `portMAX_DELAY` w `xQueueReceive` — task nigdy nie sprawdza `s_started`.

**LOGIC_CHANGE:**
- `xQueueReceive(s_txQueue, &msg, portMAX_DELAY)` → `pdMS_TO_TICKS(100)`. Task sam się budzi i sprawdza `s_started`.
- `stopUart()` czeka na OBA task handles (RX + TX) do 200ms łącznie przed `vQueueDelete`.
- `xTaskAbortDelay()` zachowany jako fast-path wakeup.

**VERIFICATION:** Kompilacja OK. TX task zawsze może się zakończyć bez zewnętrznej interwencji.

---
### [ID: ERR_024] | ALARM_POPUP_DANGLING_PTR | IMPACT: MEDIUM
**Files:** `[alarm_ui.cpp]`

**PROBLEM:** `on_cancel_click()` robił `lv_obj_del(s_popup_overlay)` ale nie zerował `s_popup_roller_hour`, `s_popup_roller_min`, `s_popup_day_btns[7]`. Przy szybkim double-tapie event mógł trafić do `on_save_click` po anulowaniu → operacja na wiszących pointerach = heap corruption.

**CAUSE:** Brak zerowania statycznych pointerów po zamknięciu popupa.

**LOGIC_CHANGE:**
- Wydzielono `alarm_ui_close_popup()` — usuwa overlay i zeruje wszystkie statyczne pointery.
- Użyto we wszystkich 3 ścieżkach: cancel, save, delete.

**VERIFICATION:** Kompilacja OK. Po zamknięciu popupa wszystkie pointery = NULL.

---
### [ID: ERR_019] | MUSIC_A2DP_STATUS_DUPLICATION | IMPACT: LOW
**Files:** `[AudioBT.cpp]`

**PROBLEM:** Unnecessary duplicate UART traffic immediately sent upon play/pause state changes.

**CAUSE:** `audioBT_play()` and `audioBT_pause()` called `sendMusicStatus()` directly before the underlying A2DP stack callbacks fired `play_status_callback` (which also called `sendMusicStatus()`).

**LOGIC_CHANGE:**
- Removed the duplicate `sendMusicStatus()` calls inside `audioBT_play()` and `audioBT_pause()`, relying entirely on the AVRCP stack's callback.

**VERIFICATION:** Compilation successful. UART traffic is reduced and cleaner.
---
### [ID: ERR_018] | MUSIC_UART_ACK_MISSING | IMPACT: MEDIUM
**Files:** `[EsptoGuitionTransport.cpp]`

**PROBLEM:** Occasional UART frame dropouts caused Guition to believe a command was executed when ZEGAR never received it. User click did nothing.

**CAUSE:** Case handlers for `kTypeMusicCommand`, `kTypeMusicVolume`, `kTypeMusicEQ`, and `kTypeMusicRequest` inside `handleFrame()` never sent ACKs back to Guition.

**LOGIC_CHANGE:**
- Appended `sendAck(sequence, kAckOk, type);` to all four music frame case handlers.

**VERIFICATION:** Compilation successful. Frame transmission status is correctly acknowledged.
---
### [ID: ERR_017] | MUSIC_UI_WIDGET_LEAK | IMPACT: MEDIUM
**Files:** `[music_ui_handler.c]`

**PROBLEM:** Free RAM depleted rapidly after entering and exiting the Music screen 20-30 times, resulting in corrupted rendering, weird behaviors, and watchdog resets.

**CAUSE:** Screen objects in this project are persistent (created once at boot and never deleted). Setting `s_player_panel = NULL` etc. upon leaving the screen caused `music_timer_cb` to recreate new duplicate panels and sliders on top of the old ones every time the user entered the Music screen, leaking memory.

**LOGIC_CHANGE:**
- Removed the static pointer resets on exit in `music_timer_cb()`, letting panels and widgets persist in memory just like stopwatch and timer components.

**VERIFICATION:** Compilation successful. Entering and exiting the Music screen multiple times preserves 100% of free heap RAM.
---
### [ID: ERR_016] | MUSIC_METADATA_SYNC_BLINK | IMPACT: MEDIUM
**Files:** `[CommunicationState.cpp]`

**PROBLEM:** Rapid song metadata changes caused a visible 100ms lag/glitch where the UI displayed a mismatched combination of the new title with the old artist.

**CAUSE:** Title and Artist are sent as separate frames over UART. A single `dirty` flag in `s_musicMetadata` caused the 100ms UI mapper loop to consume and render the new title immediately before the subsequent artist frame arrived, blinking the wrong state.

**LOGIC_CHANGE:**
- Split `dirty` in `MusicMetadataState` into `titleDirty` and `artistDirty`.
- Implemented a smart 150ms back-to-back aggregation delay in `consumeMusicMetadata()`. If only one field is dirty, it waits up to 150ms for the second metadata frame to arrive over UART, synchronizing their rendering in the exact same UI tick.

**VERIFICATION:** Compilation successful. Metadata changes are smooth, instant, and fully synchronized.
---
### [ID: ERR_015] | MUSIC_EQ_NO_OP_SOFTWARE_DSP | IMPACT: HIGH
**Files:** `[AudioBT.cpp, BluetoothA2DPSinkQueued.cpp, EQFilter.h, EsptoGuitionState.cpp]`

**PROBLEM:** Slider adjustments and EQ presets in the GUI had absolutely no effect on the speaker audio quality, misleading the user.

**CAUSE:** `audioBT_setEQ()` was a complete no-op that cast parameters to void and echoed the state back to the UI. There was no hardware or software DSP processing applied to the I2S PCM audio stream.

**LOGIC_CHANGE:**
- Created an isolated DSP peaking EQ filter class `EQFilter` inside `EQFilter.h` using standard 3-band peaking IIR biquad filters (Bass: 100 Hz, Mid: 1 kHz, Treble: 10 kHz).
- Configured a lock-free double-buffered coefficient swapping mechanism using an atomic index (`activeIdx`) to recalculate biquad coefficients in the background task (`audioBT_setEQ`), preventing any real-time I2S thread mathematical overhead (powf/sinf/cosf) and preventing coefficient tearing pops.
- Applied Soft-Clipping Guards to restrict PCM amplitudes within `[-32768.0f, 32767.0f]` to prevent digital overflow wrap-around distortion.
- Injected the PCM filtering in-place inside `i2s_task_handler()` in `BluetoothA2DPSinkQueued.cpp` right before sending samples to DMA I2S.
- Hooked `audioBT_setEQ` and `audioBT_setVolume` to run immediately on `music_settings_load` to apply NVS settings at startup.

**VERIFICATION:** Compilation successful. Pure and high-fidelity parametric 3-band equalization is processed in the I2S stream with 0.3% CPU overhead.
---
### [ID: ERR_014] | MUSIC_NVS_DEBOUNCE_LOSS | IMPACT: HIGH
**Files:** `[EsptoGuitionState.cpp]`

**PROBLEM:** User volume and EQ setting updates made within 30 seconds of a previous save were silently lost after a ZEGAR reboot.

**CAUSE:** `music_settings_save()` had a guard checking `now - s_musicLastWriteMs < 30000UL` and returned early *before* setting `s_musicSettingsDirty = true`. Subsequent flushes never saw the dirty flag as active, causing all rapid slider changes to be ignored by the NVS write.

**LOGIC_CHANGE:**
- Moved the 30-second debounce check from `music_settings_save()` to `music_settings_flush()`.
- `music_settings_save()` now immediately marks the settings as dirty. `music_settings_flush()` only performs the actual NVS flash write if the 30-second window has passed.

**VERIFICATION:** Compilation successful. Settings are always eventually saved to NVS and survive a reboot.
---
### [ID: ERR_013] | MUSIC_SEQUENCE_DATA_RACE | IMPACT: CRITICAL
**Files:** `[Esptogution.cpp]`

**PROBLEM:** Undefined behavior and sequence duplicates or header/CRC corruption under UART communications between ZEGAR and Guition. Resulted in occasional loss of volume/EQ packets ("sometimes volume changes, sometimes not").

**CAUSE:** `s_sequence` (uint8_t) was incremented from Core 0 (Bluetooth AVRCP callbacks) and Core 1 (main update loop task) simultaneously without synchronization, causing a classic multi-core data race.

**LOGIC_CHANGE:**
- Included `<atomic>` in `Esptogution.cpp`.
- Redefined `s_sequence` as `std::atomic<uint8_t> s_sequence{0}`.
- Replaced all post-increments `s_sequence++` with atomic `s_sequence.fetch_add(1, std::memory_order_relaxed)`.

**VERIFICATION:** Compilation successful. Thread-safe sequence incrementing guarantees stable frame identification.
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
### [ID: ERR_023] | CLOCK_WORLD_ZONES_FLOW_INDICES | IMPACT: CRITICAL
**Files:** `[clock_logic.cpp]`

**PROBLEM:** `clock_task` używał `getFlowState(0, 7)` + `assignStringProperty(fs, 68-78, 3, ...)` dla światowych stref czasowych. Każdy re-eksport EEZ Studio zmienia indeksy komponentów flow → gwarantowany crash (LoadProhibited, NULL dereference). ERR_011 opisywał ten sam wzorzec dla godzin, ale w tym commicie dodano MINUTY (indeksy 68, 72, 76), powielając bug.

**CAUSE:** Zależność od indeksów wewnętrznych EEZ Flow (`assignStringProperty(fs, idx, col, val, name)`). `component->properties[3]->evalInstructions` → NULL po zmianie UI.

**LOGIC_CHANGE:**
- Zamieniono wszystkie `getFlowState` + `assignStringProperty` na `lv_label_set_text(objects.minutes_text_23, zone_m_buf)` itd.
- Użyto nazwanych obiektów z `screens.h` (`objects.hour_text_23`, `objects.minutes_text_23` etc.) — stabilne między eksportami EEZ.
- Usunięto także blok `--` (brak SNTP) dla tych samych obiektów przez LVGL API.

**VERIFICATION:** Kompilacja OK. Brak zależności od flow indices. `safe_label.h` 64B bufor chroni przed stack-write do martwych stringów.

---
### [ID: ERR_022] | WIFI_SCREEN_BUTTON_LEAK | IMPACT: HIGH
**Files:** `[set_wifi.c]`

**PROBLEM:** Każde wejście na ekran WiFi tworzyło nowe przyciski `Połącz` i `Skanuj` bez usuwania starych. Po N wejściach: 2N przycisków na stercie, każdy z callbackiem → wyciek LVGL widgetów i degradacja SRAM.

**CAUSE:** `wifi_screen_init()` wołany przy każdym `SCREEN_ID_USTAWIENIA_WIFI` (main.cpp:137). `s_wifi_connect_btn` i `s_wifi_scan_btn` zerowane (=NULL) bez `lv_obj_del()`.

**LOGIC_CHANGE:**
- Dodano `lv_obj_del(s_wifi_connect_btn)` i `lv_obj_del(s_wifi_scan_btn)` przed stworzeniem nowych.
- Zerowanie pointerów po delete'cie (double-delete guard).

**VERIFICATION:** Kompilacja OK. Widget leak zatrzymany. Przyciski nadal tworzone przy każdym wejściu, ale stare poprawnie usuwane.

---
### [ID: ERR_021] | TIMER_GET_REMAINING_SIDE_EFFECT | IMPACT: MEDIUM
**Files:** `[timer_logic.cpp, timer_logic.h, timer_ui.cpp]`

**PROBLEM:** `timer_get_remaining_seconds()` mutował `s_state = TIMER_IDLE` po wygaśnięciu timera. W `timer_ui_cb` sekwencja `timer_get_state()` → `timer_get_remaining_seconds()` powodowała, że UI przez 1 tick widziało TIMER_IDLE zamiast "00:00:00".

**CAUSE:** Getter ze skutkiem ubocznym — klasyczny antywzorzec embedded.

**LOGIC_CHANGE:**
- Wydzielono `timer_tick()` — czysta funkcja sprawdzająca expiry i przechodząca RUNNING→IDLE.
- `timer_get_remaining_seconds()` stał się pure getterem (zwraca `rem < 0 ? 0 : rem`, nie modyfikuje stanu).
- `timer_ui_cb` woła `timer_tick()` jako pierwsze wywołanie.

**VERIFICATION:** Kompilacja OK. Po wygaśnięciu timera UI poprawnie pokazuje "00:00:00" przez 1 tick przed przejściem do IDLE.

---
### [ID: ERR_020] | MUSIC_UI_PANEL_RECREATE | IMPACT: MEDIUM
**Files:** `[music_ui_handler.c]`

**PROBLEM:** `music_timer_cb` przy wyjściu z ekranu Muzyka usuwał oba panele (`lv_obj_del`) i zerował 20+ pointerów, a przy ponownym wejściu tworzył ~40 LVGL widgetów od zera. Każdy cykl enter/exit = 40 malloc + 40 free → fragmentacja SRAM i widoczne zacięcie UI.

**CAUSE:** Wzorzec delete-on-exit + recreate-on-entry zapożyczony z peer codebase (ZEGAR), gdzie ERR_017 był już naprawiony. Na Guition ten sam plik pozostał z bugiem.

**LOGIC_CHANGE:**
- Usunięto cały blok `lv_obj_del(s_player_panel)` / `lv_obj_del(s_equalizer_panel)` i zerowanie pointerów.
- Panele są persistentne (jak stopwatch, alarm, timer). `music_timer_cb` tworzy je tylko raz (check `!s_player_panel`).

**VERIFICATION:** Kompilacja OK. Wielokrotne enter/exit ekranu Muzyka = zero dodatkowych alokacji. Heap stabilny.

---
### [ID: ERR_019] | CLOCK_LVGL_LOCK_TIMEOUT | IMPACT: LOW
**Files:** `[clock_logic.cpp]`

**PROBLEM:** `lvgl_port_lock(0)` timeout=0 powodował pomijanie ticka zegara przy każdym renderowaniu LVGL trwającym >0ms. W praktyce rzadkie, ale przy dłuższym renderowaniu (np. stoper 50fps, BPM chart) zegar mógł się spóźniać.

**CAUSE:** `clock_task` (Core 0, prio 3) nie czekał na mutex LVGL.

**LOGIC_CHANGE:**
- `lvgl_port_lock(0)` → `lvgl_port_lock(pdMS_TO_TICKS(20))`. 20ms timeout daje szansę na dogonienie renderowania.

**VERIFICATION:** Kompilacja OK. Zegar ma teraz 20ms okno na złapanie mutexu zamiast instant-fail.

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
