# Guition JC8048W550 — Panel sterujący

Firmware dla wyświetlacza **Guition JC8048W550** (800×480 RGB, GT911 touch) z **ESP32-S3** i **LVGL 9**, sterujący pracą systemu w składzie dwóch płyt ESP32.

## Architektura

```
┌──────────────────────┐    UART 115200     ┌──────────────────────┐
│   Guition ESP32-S3   │ ◄─── COBS/CRC ───► │   Sensor ESP32       │
│   (panel, GUI, UI)   │                    │ (MAX30102, BMP280,   │
│                      │                    │  PMS5003, WiFi/BLE)  │
└──────────────────────┘                    └──────────────────────┘
```

**Panel** — odpowiada za wyświetlanie GUI (LVGL + EEZ Flow), zarządzanie ekranami, animacje i interakcję z użytkownikiem.

**Sensor** (osobne repo `ZEGAR-ESP32`) — zbiera dane z czujników (tętno, SpO₂, jakość powietrza, pogoda, WiFi/BLE) i przesyła je przez UART w ramkach COBS z sumą CRC16.

## Wymagania

- ESP-IDF ≥5.2 (docelowo 6.0.1)
- Python + narzędzia ESP-IDF w PATH
- Target: `esp32s3`

## Szybki start

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx -b 115200 flash
idf.py -p COMx monitor
```

Poza środowiskiem ESP-IDF (Windows):

```bash
python build_guition.py        # build
python build_guition_flash.py  # build + flash
```

## Projektowanie UI

Interfejs edytowany w **EEZ Studio** (projekt w `ekran_zegar_kopia/`).  
Export → `copy_ui.py` → build.

## Kluczowe pliki

| Plik | Opis |
|------|------|
| `main/app/` | Logika aplikacji (zegar, powiadomienia, stany) |
| `main/app/alarm/` | Popup alarmu/minutnika — bell, drzemka, wyłącz |
| `main/communication/` | Protokół UART COBS/CRC, parsowanie ramek, mapowanie na UI |
| `main/services/` | PPG/BPM (MAX30102), WiFi |
| `main/hal/` | Inicjalizacja LCD, dotyku, podświetlenia |
| `main/ui/ui/` | Auto-generowane z EEZ Studio — nie edytować ręcznie |

## Uwagi

- Dwa rdzenie: Core 1 — wyłącznie LVGL, Core 0 — wszystko inne
- Komunikacja UART z sensorami przez oddzielny task (RX/TX na Core 0)
- PSRAM + RGB — flash write blokuje PSRAM; NVS zapisywany asynchronicznie w worker task
- Popup alarmu: budzik → drzemka + wyłącz, minutnik → tylko wyłącz
- SRAM: `safe_label.h` zamienia `lv_label_set_text` na statyczny bufor (64B/label), eliminuje fragmentację
