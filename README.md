# Guition JC8048W550 — Panel sterujący

Firmware dla wyświetlacza **Guition JC8048W550** (800×480 RGB, GT911 touch) na **ESP32-S3** z **LVGL 9.4.0** i **ESP-IDF 6.0.1**. Steruje pracą systemu złożonego z dwóch płytek ESP32 (panel + sensor) przez UART COBS/CRC.

[![ESP-IDF 6.0.1](https://img.shields.io/badge/ESP--IDF-6.0.1-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![LVGL 9.4.0](https://img.shields.io/badge/LVGL-9.4.0-green?logo=lvgl)](https://docs.lvgl.io/)
[![Target: esp32s3](https://img.shields.io/badge/target-esp32s3-orange)](https://www.espressif.com/en/products/socs/esp32-s3)
[![EEZ Studio](https://img.shields.io/badge/UI-EEZ%20Studio-gray)](https://github.com/eez-open/studio)

---

## Spis treści

- [Architektura](#architektura)
- [Zadania FreeRTOS](#zadania-freertos)
- [Wymagania](#wymagania)
- [Szybki start](#szybki-start)
- [Projektowanie UI](#projektowanie-ui)
- [Struktura projektu](#struktura-projektu)
- [Kluczowe pliki](#kluczowe-pliki)
- [Instrukcja obsługi — ekrany (16)](#instrukcja-obsługi-ekrany-16)
- [Komunikacja UART](#komunikacja-uart)
- [Uwagi techniczne](#uwagi-techniczne)
- [Walidacja struktury](#walidacja-struktury)

---

## Architektura

```
┌──────────────────────┐    UART 115200     ┌──────────────────────┐
│   Guition ESP32-S3   │ ◄─── COBS/CRC ───► │   Sensor ESP32       │
│   (panel, GUI, UI)   │                    │ (MAX30102, BMP280,   │
│                      │                    │  PMS5003, WiFi/BLE)  │
└──────────────────────┘                    └──────────────────────┘
```

**Panel (ten repo)** — LVGL + EEZ Flow, zarządzanie ekranami, animacje, interakcja, alarmy, stoper, minutnik, pogoda, radar, gra, muzyka.
**Sensor** (osobne repo `ZEGAR-ESP32`) — zbiera dane z czujników i przesyła przez UART w ramkach COBS z sumą CRC16.

---

## Zadania FreeRTOS

| Task | Core | Priorytet | Stack | Opis |
|------|------|-----------|-------|------|
| LVGL | 1 | 4 | 8192 | Jedyny task wywołujący LVGL API |
| UART RX | 0 | 4 | 4096 | Odbiór danych, nigdy nie woła LVGL |
| UART TX | 0 | 3 | 3072 | Zapis UART, asynchroniczny względem LVGL |
| Clock | 0 | 3 | 4096 | Aktualizacja czasu |
| WiFi worker | 0 | 2 | 4096 | Connect/disconnect, asynchroniczny |

---

## Wymagania

- ESP-IDF 6.0.1
- Target: `esp32s3`
- Python + narzędzia ESP-IDF w PATH

---

## Szybki start

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx -b 115200 flash
idf.py -p COMx monitor
```

Skrypty pomocnicze (Windows, ścieżki twarde w plikach):

```bash
python build_guition.py        # build
python flash_guition.py        # build + flash (port COM12, 115200)
```

---

## Projektowanie UI

Interfejs edytowany w **EEZ Studio** (`PROJEKT_UI_EZZ_STUDIO/src/ui`).  
Eksport → `copy_ui.py` (sync do `main/ui/ui/`) → build.

`main/ui/ui/` jest auto-generowane — **nie edytować ręcznie**.

---

## Struktura projektu

```
.
├── CMakeLists.txt
├── build_guition.py
├── copy_ui.py
├── flash_guition.py
├── sdkconfig.defaults
├── parts.csv
├── scan_project_map.py
├── MAPA_PROJEKTU.md
├── README.md
├── ERROR_ARCHIVE/
├── PROJEKT_UI_EZZ_STUDIO/
├── PRZYKŁAD/
├── SCALE_PHOTO/
└── main/
    ├── app/               # clock, timer, stopwatch, alarm, notification,
    │   │                  # weather, game, screen_geometry, radar,
    │   │                  # zegar_mode, main_handler, zasoby_sliders
    ├── communication/     # UART COBS/CRC, handshake, CommUiMapper, timer_synchro
    ├── services/          # bpm (MAX30102), wifi, sntp, time, music, sensor
    ├── hal/display/       # LCD (sunton_esp32_8048s050c), dotyk GT911
    ├── ui/ui/             # Auto-generowane z EEZ Studio
    ├── custom_icons/      # Ikony pogody i niestandardowe
    ├── setting_wifi/      # Ekran konfiguracji WiFi (HMI tylko)
    ├── diagnostics/       # lv_perf_diag — wydajność LVGL + heap
    ├── utils/             # log, comm_log, gesture
    └── eez-framework/     # EEZ Flow runtime dla LVGL
```

---

## Kluczowe pliki

| Plik | Opis |
|------|------|
| `main/app/main.cpp` | `app_main`, inicjalizacja wyświetlacza, LVGL, UI tick (100 ms) |
| `main/communication/CommunicationProtocol.cpp` | Protokół UART, parsowanie ramek, mapowanie na UI |
| `main/communication/CobsFraming.cpp` | Ramki COBS + CRC16 |
| `main/services/bpm/bpm_service.c` | Obsługa MAX30102 (BPM/SpO₂) |
| `main/hal/display/sunton_esp32_8048s050c.c` | LCD, podświetlenie, dotyk |
| `main/ui/ui/ui.c/h` | Auto-generowane z EEZ Studio |
| `main/app/safe_label.h` | `lv_label_set_text` → statyczny bufor 64 B/label (eliminacja fragmentacji SRAM) |

---

## Instrukcja obsługi — ekrany (16)

| Ekran | Co robi |
|-------|---------|
| GŁÓWNY | Zegar, pogoda IN/OUT (przełącza co 10 s), powiadomienia |
| MENU | Nawigacja między wszystkimi ekranami |
| USTAWIENIA | Melodyjka alarmu, jasność 7-seg (7 poziomów: 100/75/50/25/10/5/2 %), przełączniki |
| ZASOBY SYSTEMU | Zużycie CPU Core 0 i Core 1, wolny i użyty flash (4 suwaki) |
| USTAWIENIA WIFI | Konfiguracja WiFi — tylko interfejs, połączenie obsługuje sensor ESP32 przez UART |
| ZEGAR | Alarmy, minutnik, stoper, tryb wyświetlacza 7-seg (zegar / stoper / minutnik) |
| LOGI | Logi systemowe i komunikacji UART |
| POGODA | Ikona pogody z kodu WMO (słonecznie, parcialmente zachmurzone, zachmurzenie, mgła, wiatr, mroźno, mżawka, deszcz, śnieg, śnieg z deszczem, burza) |
| BPM | Tętno i SpO₂ z czujnika MAX30102 (wykres na żywo) |
| GRY TEST | Gra Flappy Bird |
| MUZYKA | Odtwarzacz: play/pause, poprzedni/następny, pasek postępu, wizualizator 4 słupków, equalizer (bass/mid/treble/volume), presety FLAT/BASS/VOCAL |
| CZUJNIKI | Dane z czujników: PMS5003 (PM1/2.5/10), ENS160 (eCO2/TVOC/AQI + stopień kalibracji), AHT21 (temp./wilg.), BMP280 (temp./cisn.); tryb parametry / teoria |
| INFO | Informacje o systemie |
| GEOMETRIA EKRANU | Sterowanie silnikiem krokowym: jazda w dół / w górę (hold), STOP, presety kątów (0/15/30/45/75/90°), JEDŹ DO, kalibracja 0°/90°, radar mikrofalowy WŁ/WYŁ, auto-zjazd radaru (30/180/600/2700 s) |
| RADAR | Widok radaru LD2410C: sweep ±80°, blipy ruchu i postoju, dystanse (ruch/postój/zasieg), czulść OUT (wysoka/średnia/mała), auto-zjazd, radar WŁ/WYŁ, płynne wyłączenie 7-seg, OFFLINE po 3 s bez ramki |
| PROJECT INFO | Informacje o projekcie i firmware |

**Sterowanie:** dotyk GT911, gesty Swipe Up (dostępne na większości ekranów poza GŁÓWNYM i GRY TEST). Alarmy i minutaż sterowane z poziomu ekranu ZEGAR.

---

## Komunikacja UART

- Prędkość: 115200 bd
- Ramki: COBS + CRC16
- Sensor → Panel: dane z czujników, czas, BPM, pogoda, alarmy
- Panel → Sensor: ustawienia urządzenia, żądania stanu, komendy sterowania

---

## Uwagi techniczne

- Core 1 wyłącznie LVGL; Core 0 — UART, clock, WiFi worker, NVS.
- Flash write blokuje PSRAM przy aktywnym RGB — zapis NVS (jasność) asynchroniczny w worker task na Core 0.
- `safe_label.h` injectowane przez CMake (`-include`) do wszystkich plików w `main/`.
- `CONFIG_LV_PERF_DIAG` wyłączone w release (zero narzutu, no-op stuby); włączane ręcznie w buildzie serwisowym.
- Logi produkcji: tylko WARN+ (`CONFIG_LOG_DEFAULT_LEVEL_WARN`).
- PARTICJA: `parts.csv` (NVS 0x9000, OTA 0x10000/0x750000, SPIFFS 0xE90000).

---

## Walidacja struktury

```bash
python scan_project_map.py --validate
```
