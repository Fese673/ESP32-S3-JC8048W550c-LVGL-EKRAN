# ERR_009 — WiFi Connect Race Condition & Auto-Connect Reliability

## Metadata
- **ID:** ERR_009
- **Impact:** HIGH
- **Detection:** 2026-05-09
- **Fix version:** Refaktor wifi_service.c + set_wifi.c
- **Tags:** `race-condition`, `esp_wifi_connect`, `disconnected-handler`, `nvs-key`, `sntp-timing`, `auto-connect`

## Symptom

### Problem A — Po kliknięciu "Połącz" connection nie działa (przed fixem)
WiFi próbuje się łączyć, ale ciągle timeout/fail. Log pokazuje auto-retry nawet po manualnym kliknięciu.

### Problem B — Po resecie auto-connect nie działa (przed fixem)
WiFi łączy się z AP (assoc → run) ale po 3s disconnect z `0xf00` (ASSOC_LEAVE). Brak GOT_IP. 10 auto-retry — wszystkie fail.

### Problem C — SNTP nie synchronizuje (przed fixem)
Po manualnym połączeniu SNTP nie odpala się przez ~64s (retry interval).

## Root Causes

### Root 1 — Double `esp_wifi_connect()` race

Worker task (Core 0) wołał `esp_wifi_disconnect()` → DISCONNECTED event → event handler (Core 0, event loop) widzi `s_connect_requested = true` → ale ZAMIAST czekać, woła `esp_wifi_connect()` (retry #1). Worker kontynuuje i też woła `esp_wifi_connect()` (próba #2).

Dwa `esp_wifi_connect()` do firmware → Wi-Fi driver dostaje dwa rozkazy, gubi się, połączenie nie działa.

**Pattern:**
```
Worker: esp_wifi_disconnect() → DISCONNECTED event ──→ event handler: esp_wifi_connect() ❶
Worker: esp_wifi_set_config() → esp_wifi_connect()     ❷
```

### Root 2 — DISCONNECTED zbyt szybko ustawia FAILED

Event handler na `WIFI_EVENT_STA_DISCONNECTED` przy `s_connect_requested = true` ustawiał natychmiast `WIFI_CONNECT_FAILED`. Ale pierwszy DISCONNECTED może być z `esp_wifi_disconnect()` (config change) a nie z failed connect.

**Fix:** DISCONNECTED podczas manual connect jest IGNOROWANY. Tylko timer 15s (`connect_poll_cb`) decyduje o fail.

### Root 3 — NVS key niezgodny z ESP-IDF

`save_config_to_nvs()` używał `esp_wifi_set_config(FLASH)` do zapisu → nie gwarantuje że config trafi pod klucz `"sta"` w namespace `"wifi"` (klucz używany przez `esp_wifi_start()`).

**Fix:** bezpośrednie `nvs_set_blob("wifi", "sta", &config)` — ten sam klucz co ESP-IDF WiFi driver.

### Root 4 — NVS-loaded config ma inną ścieżkę niż set_config(RAM)

Auto-connect przez `esp_wifi_connect()` z NVS-loaded config (po `esp_wifi_start()`) → 3s disconnect, brak DHCP.
Manual connect przez `esp_wifi_set_config(RAM)` + `esp_wifi_connect()` → działa.

Różnica w ścieżce inicjalizacji configu w Wi-Fi driverze (pola zerowe w `wifi_config_t` są inaczej interpretowane przy load z NVS vs set_config).

**Fix:** auto-connect też przez worker: `queue_saved_connect()` → czyta config z NVS, wysyła do worker queue → `set_config(RAM)` + `connect` — TA SAMA ścieżka.

### Root 5 — SNTP start przed WiFi

`sntp_service_init()` wołany w `app_main()` zaraz po `wifi_service_init()` — DNS `pool.ntp.org` failuje (brak połączenia), SNTP czeka ~64s na retry.

**Fix:** SNTP startuje w `GOT_IP` handlerze (gdy mamy adres IP).

## Pattern

```
[esp_wifi_connect() z różnych tasków]                 → Double connect race
[DISCONNECTED handler ustawia FAILED]                   → Przedwczesny fail
[esp_wifi_set_config(FLASH) zamiast NVS direct]        → Config nie persistuje
[NVS-loaded config z esp_wifi_start()]                  → 3s disconnect, DHCP fail
[SNTP init przed GOT_IP]                                → ~64s delay do sync
```

## Fix (kompletny)

### plik: `wifi_service.c`

| Co | Kod |
|----|-----|
| Worker: **usunięty** `esp_wifi_disconnect()` | `set_config(RAM)` + `connect()` tylko |
| Event handler: **IGNORUJE** DISCONNECTED dla manual | `if (s_connect_requested) { /* nothing */ }` |
| **Auto-connect przez worker** | `queue_saved_connect()` → czyta NVS → worker → `set_config(RAM)` + `connect()` |
| `STA_START` handler: zamiast `esp_wifi_connect()` | `queue_saved_connect()` |
| Worker + queue tworzone PRZED `esp_wifi_start()` | Kolejka gotowa na STA_START event |
| **NVS key zgodny z ESP-IDF** | `nvs_get/set_blob("wifi", "sta")` |
| **SNTP start przy GOT_IP** | `esp_sntp_init()` w GOT_IP handlerze |

### plik: `set_wifi.c`

| Co | Kod |
|----|-----|
| DISCONNECTED = ignorowane | Timer 15s decyduje o fail |
| NVS save flag → full invalidate | `lv_obj_invalidate(lv_scr_act())` |

### plik: `main.cpp`

| Co | Kod |
|----|-----|
| SNTP init usunięty | Robi to GOT_IP handler |

## Diagram działania

```
Boot (no saved config):
    wifi_service_init()
    → tworzy queue + worker
    → esp_wifi_start()
    → STA_START → queue_saved_connect() → brak NVS → nic
    → User wchodzi na ekran WiFi, klika Connect
    → worker: set_config(RAM, "Orange") + connect()
    → GOT_IP
    → nvs_save_pending = true
    → worker (3s timeout): save_config_to_nvs() → nvs_set_blob("wifi", "sta")

Boot z saved config (po resecie):
    wifi_service_init()
    → queue + worker ready
    → esp_wifi_start()
    → STA_START → queue_saved_connect() → NVS ma "Orange"
    → worker: set_config(RAM, "Orange") + connect()
    → GOT_IP ← działa, bo TA SAMA ścieżka co manual
```

## Podsumowanie dla inżyniera

| Objaw | Root Cause | Fix | Plik |
|-------|-----------|-----|------|
| Nie łączy po kliknięciu "Połącz" | Double `esp_wifi_connect()` race | Worker: bez `esp_wifi_disconnect()`. Event handler: ignoruje DISCONNECTED | `wifi_service.c` |
| 3s disconnect po resecie | NVS-loaded config inna ścieżka | `queue_saved_connect()` → worker jak manual | `wifi_service.c` |
| Config nie persistuje | `esp_wifi_set_config(FLASH)` nie gwarantuje klucza | `nvs_set_blob("wifi", "sta", ...)` | `wifi_service.c` |
| SNTP nie sync po manual connect | Start przed WiFi | `esp_sntp_init()` w GOT_IP | `wifi_service.c` + `main.cpp` |
