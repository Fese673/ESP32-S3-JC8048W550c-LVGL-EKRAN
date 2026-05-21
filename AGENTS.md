# AGENTS.md — ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ

## Project identity

ESP32-S3 firmware for Gution JC8048W550 800×480 RGB display (GT911 touch), LVGL 9.4 GUI, EEZ Flow code-generated UI. Communicates via UART/COBS/CRC16 with a separate ESP32 sensor board (`../ZEGAR-ESP32`). Fork of https://github.com/mr-sven/esp32-8048S050C.

## Build & flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COM12 -b 115200 flash
idf.py -p COM12 monitor
```

Default COM port in `.vscode/settings.json` is COM12. Change as needed.

Python helper `build_guition.py` exists for Windows builds outside the ESP-IDF PowerShell environment (hardcodes IDF 6.0.1 paths, Ninja, CMake, toolchains).

## EEZ Studio UI workflow

1. Edit UI in EEZ Studio (project in `ekran_zegar_kopia/`)
2. Export → generates files to `ekran_zegar_kopia/src/ui/`
3. Run `python copy_ui.py` to sync into `main/ui/ui/`
4. Build with `idf.py build`

`main/ui/ui/` is **auto-generated** — never hand-edit (except for the scroll-reset patches listed below). Hand-written code lives in `main/app/`, `main/communication/`, `main/utils/`, `main/services/`, `main/hal/`.

## Critical gotchas

### lv_textarea O(N²) watchdog kill

`lv_textarea_set_text()` has two code paths in LVGL 9.4:
- `max_length > 0` → **O(N²)** slow path (calls `lv_textarea_add_char()` per character → recalculates layout each time → triggers `taskWDT` reset)
- `max_length == 0` → **O(N)** fast path (direct `lv_label_set_text()`)

Always set `lv_textarea_set_max_length(obj, 0)` before `lv_textarea_set_text()`. The log viewer in `main/utils/log/log.cpp` already does this — do not "fix" max_length to a non-zero value.

### SRAM fragmentation fix — `main/app/safe_label.h`

`main/CMakeLists.txt` dodaje `-include safe_label.h` do flag kompilatora (`target_compile_options`).  
To wstrzykuje `#define lv_label_set_text(obj, txt) safe_lv_label_set_text(obj, txt)` do **każdego** pliku w komponencie `main/` — bez modyfikacji `screens.c`.

`safe_lv_label_set_text()`:
- Alokuje **jeden** 64-bajtowy bufor na label (przy pierwszym użyciu, przez `lv_obj_set_user_data`)
- Używa `lv_label_set_text_static()` — LVGL nie kopiuje stringa, nie ma alloc/free na tick
- Eliminuje fragmentację SRAM związaną z cykliczną aktualizacją etykiet

**Zero zmian w workflow EEZ → export → `copy_ui.py`.** Działa automatycznie.



### Architecture — Task Layout

| Task | File | Core | Priority | Stack | Opis |
|------|------|------|----------|-------|------|
| **LVGL** (`esp_lvgl_port`) | `sunton_esp32_8048s050c.c` | **1** | **4** | 8192 | Jedyny task z LVGL API. Nigdy nie blokowany |
| **UART RX** (`comm_uart1_rx`) | `UartTransport.cpp` | 0 | 4 | 4096 | Odbiera dane z UART. Nigdy nie woła LVGL |
| **UART TX** (`comm_uart1_tx`) | `UartTransport.cpp` | 0 | **3** | 3072 | Zapisuje do UART. Kolejka z LVGL taska, async |
| **Clock** (`clock_task`) | `clock_logic.cpp` | 0 | 3 | 4096 | Aktualizuje czas. Ma `lvgl_port_lock` |
| **WiFi worker** (`wifi_worker`) | `wifi_service.c` | 0 | **2** | 4096 | WiFi connect/disconnect. Queue od LVGL. Async |
| `app_main` idle | `main.cpp` | 0 | 1 | — | `vTaskDelay(1000ms)`, bez efektu |

**Core 1** = LVGL tylko. **Core 0** = wszystko inne. Zero LVGL API z Core 0 bez `lvgl_port_lock`.

### Krytyczne: async patterny

#### WiFi Connect — w pełni asynchroniczny
```
User klika "Połącz"
  → LVGL task: wifi_service_connect(ssid, pwd) → xQueueSend() → return (< 1ms)
  → Worker task (Core 0): xQueueReceive() → esp_wifi_set_storage(RAM)
                                              → esp_wifi_set_config()
                                              → esp_wifi_connect()
  → Event handler (event loop): GOT_IP → state = CONNECTED
                                              → SNTP start
                                              → nvs_save_pending = true
  → Worker (3s timeout): save_config_to_nvs() → nvs_set_blob("wifi", "sta")
  → connect_poll_cb (LVGL, 1s tick): g_wifi_nvs_save_complete → lv_obj_invalidate(scr)
```

#### UART TX — w pełni asynchroniczny
```
LVGL task: sendRawFrame() → xQueueSend() → return (< 1ms)
  → UART TX task (Core 0): xQueueReceive() → uart_write_bytes()
```

#### SNTP start — dopiero po GOT_IP
```
GOT_IP → esp_sntp_init() w event handlerze (a nie w app_main)
```

### Tearing (PSRAM + RGB) — przyczyny i zapobieganie

ESP32-S3 ma **jeden SPI1** dla flash i PSRAM. Każdy flash write blokuje PSRAM → EDMA czyta śmieci.

| Źródło tearingu | Zapobieganie |
|-----------------|--------------|
| `esp_wifi_set_config(FLASH)` | `WIFI_STORAGE_RAM` → deferred NVS save |
| `nvs_set_blob` / `nvs_commit` | W worker task (Core 0, prio 2). Po save: `g_wifi_nvs_save_complete = true` → LVGL invalidate |
| Każdy inny flash write w dowolnym tasku | Jeśli widzisz tearing, szukaj flash write |

**Flaga invalidate:** `g_wifi_nvs_save_complete` (volatile bool w `wifi_service.h`).  
Sprawdzana w `connect_poll_cb` (LVGL task). `lv_obj_invalidate(lv_scr_act())` = full redraw w 1 klatce (16ms).

### NVS — klucze WiFi

ESP-IDF WiFi driver używa klucza `"sta"` w namespace `"wifi"` dla station config.  
Do odczytu/zapisu configu używaj **zawsze**:

```c
nvs_open("wifi", NVS_READWRITE, &nvs);
nvs_set_blob(nvs, "sta", &config, sizeof(wifi_config_t));
nvs_commit(nvs);
nvs_close(nvs);
```

`esp_wifi_set_config(FLASH)` NIE gwarantuje że trafi pod ten klucz. Zawsze używaj bezpośredniego NVS API.

### Auto-connect po resecie

Auto-connect po resecie działa przez **worker task** (ta sama ścieżka co manualny connect):
1. `queue_saved_connect()` w `wifi_service_init()` (po `esp_wifi_start()`) i w `STA_START` handlerze
2. Czyta config z NVS: `nvs_get_blob("wifi", "sta")`
3. Wysyła do worker queue → `set_config(RAM)` + `connect()`

**Nigdy nie używaj `esp_wifi_connect()` z `STA_START`** — NVS-loaded config może mieć inną ścieżkę inicjalizacji i powodować 3s disconnect.

### Obsługa DISCONNECTED

- Manual connect (`s_connect_requested = true`): DISCONNECTED **ignorowany**. Tylko `connect_poll_cb` timer (15s) decyduje o fail.
- Auto-connect (`s_connect_requested = false`): DISCONNECTED → auto-retry (`esp_wifi_connect()`) do 10 razy.

### Gotchas

- `lv_textarea_set_max_length(obj, 0)` przed każdym `lv_textarea_set_text()` — watchdog
- `safe_label.h` — 64B/label, dla długich statusów użyj `lv_label_set_text_static()` z własnym buforem
- `notification_logic_refresh()` — zawiera `lvgl_port_lock()` może być wołany z każdego taska
- Perf monitor: `CONFIG_LV_USE_PERF_MONITOR=y` w sdkconfig.defaults — wyłączyć w release

### Communication protocol

- UART with COBS framing, delimiter `0x00`, CRC16 payload checksum.
- Handshake state machine: BOOTING → UART_READY → PEER_DETECTED → FIRST_SYNC_SENT → FIRST_SYNC_CONFIRMED → STEADY_STATE.
- Refs `CommunicationProtocol.h` for frame types and data structures.
- Known handshake reliability issues documented in `PLAN_NAPRAWY_KOMUNIKACJI.md`.

### Other

- **Default WiFi credentials** in `main/Kconfig.projbuild` (SSID `Orange_Swiatlowod_98E2`, password `x1Z6P(~8pry<St.`). Compiled into firmware unless overridden via `idf.py menuconfig`.
- **Compiler optimization**: `-O2`/`-O3` (performance), **not** debug. Set in `sdkconfig.defaults`.
- **Single draw unit**: `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` — two units caused PSRAM bus contention ("white screen").
- **Partition table** (`parts.csv`): OTA-capable (two 7.25 MB app slots), NVS, SPIFFS 1.375 MB, coredump.
- **Clangd**: `.clangd` filters `-f*` and `-m*` flags that clangd cannot parse.
- **No tests, no CI**: verification is on-device only via ESP-IDF monitor logs.
- **ESP-IDF version**: 6.0.1 (despite `main/idf_component.yml` declaring `>=5.2.0`; `.code-workspace` references 5.4 but this is stale).
- **`.gitignore`** excludes `build/`, `managed_components/`, `sdkconfig`, `sdkconfig.old`.

## BPM / PPG Pipeline

Full protocol and architecture document in `docs/PROTOKOL_PPG_BPM.md`.

Three services in `main/services/bpm/`:
| File | Role |
|------|------|
| `bpm_service.h` | Public API: init, set_bpm, set_mock_mode, feed_sample |
| `bpm_service.c` | PPG engine: mock generator (IR→filter→diff) + real data buffer |
| `bpm_chart_handler.h` | Init (called once from main.cpp) |
| `bpm_chart_handler.c` | LVGL timer 100ms, finds/creates chart, feeds data |

Real data (default): `bpm_service_set_mock_mode(false)` — czeka na `bpm_service_feed_sample(diff)` z UART.  
Mock (debug only): `bpm_service_set_mock_mode(true)` — jawnie włączany w kodzie do testów. Nie jest domyślny, nie jest fallbackiem.

**PPG pipeline: per-sample, no batching.** Każda próbka z MAX30102 (50Hz) idzie osobno przez COBS → zero opóźnienia.
