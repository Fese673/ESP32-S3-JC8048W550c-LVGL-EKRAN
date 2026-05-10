# ERR_008 — WiFi Flash Write powoduje persistent display tearing

## Metadata
- **ID:** ERR_008
- **Impact:** CRITICAL
- **Detection:** 2026-05-09
- **Fix version:** Refaktor wifi_service.c (worker + RAM storage + deferred NVS + full invalidate)
- **Tags:** `psram`, `framebuffer`, `tearing`, `flash-write`, `spi-bus-contention`, `esp32s3`, `nvs`

## Symptom

Po kliknięciu przycisku "Połącz" na ekranie ustawień WiFi całe UI przesuwa się w lewo/prawo, dubluje, wygląda jak "rozdarte". **Efekt utrzymuje się na wszystkich ekranach**, nawet po wyjściu z ustawień WiFi. Nie znika aż do resetu płytki.

## Root Cause

### Warstwa sprzętowa: SPI1 bus contention

ESP32-S3 ma **jeden kontroler SPI1** współdzielony między **Flash** a **PSRAM**. Gdy jakakolwiek operacja zapisu do flash jest w toku:
1. Cache na bieżącym rdzeniu wyłączany
2. SPI1 zajęty przez transmisję flash
3. **PSRAM również niedostępny** (oba na SPI1)
4. EDMA (kontroler DMA wyświetlacza RGB) nie może odczytać framebuffera z PSRAM
5. EDMA wysyła do panelu LCD śmieci/zakłócone dane z PSRAM
6. **Tearing utrwala się PSRAM** — LVGL nadpisuje tylko dirty area, reszta framebuffera skażona

### Warstwa software'owa: `esp_wifi_set_config()` z `WIFI_STORAGE_FLASH`

Zapis do NVS (sector erase ~50-100ms, write+commit ~5-10ms) blokuje SPI1 → PSRAM niedostępny → framebuffer krzaki.

## Pattern

```
[RGB + PSRAM framebuffer] + [flash write anywhere]
    → Persistent tearing na wszystkich ekranach
    → Nie znika bez resetu
```

## Fix (3 mechanizmy)

### 1. RAM storage dla connect — zero flash write

```c
// wifi_worker_task
esp_wifi_set_storage(WIFI_STORAGE_RAM);                        // ← klucz: brak flash write
esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config); // ← RAM only, <1ms
```

### 2. Deferred NVS save (persystencja)

Po GOT_IP zapisujemy config przez bezpośrednie NVS API z poprawnym kluczem:

```c
nvs_open("wifi", NVS_READWRITE, &nvs);
nvs_set_blob(nvs, "sta", &config, sizeof(wifi_config_t));  // ← klucz "sta" = zgodny z ESP-IDF
nvs_commit(nvs);
```

**Kluczowy detal:** ESP-IDF WiFi driver szuka configu pod kluczem `"sta"` w namespace `"wifi"`. `nvs_set_blob` z tym samym kluczem gwarantuje że przy starcie `esp_wifi_start()` załaduje zapisany config.

### 3. Full-screen invalidation po zapisie

Flaga `g_wifi_nvs_save_complete` ustawiana po `nvs_commit`. `connect_poll_cb` (1s tick na LVGL task) sprawdza flagę i woła `lv_obj_invalidate(lv_scr_act())` → LVGL przerysowuje cały ekran nadpisując skażone piksele. Flash write trwa ~10ms, tearing znika po 1 klatce (16ms).

## Powiązane problemy i ich rozwiązania

| Problem | ERR_009 | Opis |
|---------|---------|------|
| Double `esp_wifi_connect()` race | ERR_009 | Worker + event handler wołały dwa razy |
| DISCONNECTED ustawiał FAILED zbyt wcześnie | ERR_009 | Timer 15s decyduje, nie event handler |
| NVS key `"sta"` | ERR_009 | Zgodność z ESP-IDF WiFi driver |
| SNTP init przed WiFi | ERR_009 | Start przy GOT_IP |

## Podsumowanie

| Poziom | Problem | Fix |
|--------|---------|-----|
| Sprzęt | SPI1 bus contention | Nie do naprawienia na S3 |
| Aplikacja | Flash write przy connect | `WIFI_STORAGE_RAM` |
| Aplikacja | Persystencja configu | `nvs_set_blob("wifi", "sta")` deferred |
| Aplikacja | Tearing po flash write | `lv_obj_invalidate(lv_scr_act())` |
