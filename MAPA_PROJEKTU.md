# Mapa projektu

Mapa jest zorganizowana wg domen funkcjonalnych.
Ma ułatwiać nawigację i być łatwa do rozszerzania wraz z rozwojem firmware.

## Pliki nadrzędne

```
├── CMakeLists.txt
├── README
├── MAPA_PROJEKTU.md
├── build_guition.py
├── copy_ui.py
├── flash_guition.py
├── scan_project_map.py
├── sdkconfig.defaults
├── parts.csv
├── .clangd
└── .gitignore
```

## main/

```
main/
├── CMakeLists.txt
├── Kconfig.projbuild
├── idf_component.yml
├── app/
│   ├── main.cpp
│   ├── main_handler.cpp
│   ├── main_handler.h
│   ├── alarm_logic.cpp
│   ├── alarm_logic.h
│   ├── alarm_ui.cpp
│   ├── alarm_ui.h
│   ├── safe_label.h
│   ├── font_stubs.c
│   ├── alarm/
│   │   ├── alarm_popup.cpp
│   │   └── alarm_popup.h
│   ├── clock/
│   │   ├── clock_logic.cpp
│   │   ├── clock_logic.h
│   │   ├── clock_timezone_tiles.c
│   │   └── clock_timezone_tiles.h
│   ├── timer/
│   │   ├── timer_logic.cpp
│   │   ├── timer_logic.h
│   │   ├── timer_ui.cpp
│   │   └── timer_ui.h
│   ├── stopwatch/
│   │   ├── stopwatch_logic.cpp
│   │   ├── stopwatch_logic.h
│   │   ├── stopwatch_ui.cpp
│   │   └── stopwatch_ui.h
│   ├── notification/
│   │   ├── notification_logic.cpp
│   │   └── notification_logic.h
│   ├── weather/
│   │   ├── weather_icon_handler.c
│   │   └── weather_icon_handler.h
│   └── game/
│       ├── flappy_bird.cpp
│       └── flappy_bird.h
├── communication/
│   ├── CobsFraming.cpp
│   ├── CobsFraming.h
│   ├── CommHandshake.cpp
│   ├── CommHandshake.h
│   ├── CommUiMapper.cpp
│   ├── CommUiMapper.h
│   ├── CommunicationProtocol.cpp
│   ├── CommunicationProtocol.h
│   ├── CommunicationState.cpp
│   ├── CommunicationState.h
│   ├── UartTransport.cpp
│   ├── UartTransport.h
│   ├── communication.md
│   └── time/
│       ├── timer_synchro.cpp
│       └── timer_synchro.h
├── services/
│   ├── bpm/
│   │   ├── bpm_service.c
│   │   ├── bpm_service.h
│   │   ├── bpm_chart_handler.c
│   │   └── bpm_chart_handler.h
│   ├── wifi/
│   │   ├── wifi_service.c
│   │   └── wifi_service.h
│   ├── sntp/
│   │   ├── sntp_service.c
│   │   └── sntp_service.h
│   ├── time/
│   │   ├── time_service.c
│   │   └── time_service.h
│   ├── music/
│   │   ├── music_ui_handler.c
│   │   └── music_ui_handler.h
│   └── sensor/
│       ├── sensors_wiki.cpp
│       └── sensors_wiki.h
├── hal/
│   └── display/
│       ├── sunton_esp32_8048s050c.c
│       └── sunton_esp32_8048s050c.h
├── utils/
│   ├── log/
│   │   ├── log.cpp
│   │   └── log.h
│   ├── comm_log/
│   │   ├── comm_log.cpp
│   │   └── comm_log.h
│   └── gesture/
│       ├── gesture.cpp
│       └── gesture.h
├── setting_wifi/
│   ├── set_wifi.c
│   └── set_wifi.h
├── custom_icons/
│   ├── generate_icons.py
│   ├── weather_icons.h
│   └── wi_*.c
├── diagnostics/
│   ├── lv_perf_diag.cpp
│   ├── lv_perf_diag.h
│   └── README.md
├── ui/
│   └── ui/
│       ├── screens.c/h
│       ├── images.c/h
│       ├── fonts.h
│       ├── styles.c/h
│       ├── actions.h
│       ├── eez-flow.cpp/h
│       ├── structs.h
│       ├── vars.h
│       ├── ui.c/h
│       ├── ui_font_*.c
│       └── ui_image_*.c
└── eez-framework/
    └── src/eez/
```

## Inne katalogi

```
├── ekran_zegar_kopia/
│   ├── ekran_zegar.eez-project
│   └── src/ui/
├── ERROR_ARCHIVE/
│   ├── ERROR_LOG.md
│   └── ERR_*.md
├── SKILLS/
│   └── bug-verify-fix.md
└── SCALE_PHOTO/
    ├── scale_photos.py
    ├── zdjęcia przed/
    └── zdjęcia po/
```

## Architektura (zadania)

```
┌──────────────────────┐    UART 115200     ┌──────────────────────┐
│   Guition ESP32-S3   │ ◄─── COBS/CRC ───► │   Sensor ESP32       │
│   (panel, GUI, UI)   │                    │ (MAX30102, BMP280,   │
│                      │                    │  PMS5003, WiFi/BLE)  │
└──────────────────────┘                    └──────────────────────┘
```

| Task | Core | Priorytet | Stack | Opis |
|------|------|-----------|-------|------|
| **LVGL** | 1 | 4 | 8192 | Jedyny task z LVGL API |
| **UART RX** | 0 | 4 | 4096 | Odbiór danych, nigdy nie woła LVGL |
| **UART TX** | 0 | 3 | 3072 | Zapis UART, async z LVGL |
| **Clock** | 0 | 3 | 4096 | Aktualizacja czasu |
| **WiFi worker** | 0 | 2 | 4096 | Connect/disconnect, async |

---

Uwagi:
- `main/ui/ui/` jest auto-generowane — nigdy nie edytować ręcznie
- `safe_label.h` jest injectowane przez CMake do każdego pliku w `main/`
- Walidacja: `python scan_project_map.py --validate`
