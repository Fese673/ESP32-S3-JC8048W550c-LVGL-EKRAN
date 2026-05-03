# ESP32-S3 JC8048W550 (Sunton) — LVGL demo

Implementation for the Sunton 800×480 capacitive display using ESP32-S3, LVGL 9.x and FreeRTOS.

Key points:
- Target: ESP32-S3 (RGB LCD + GT911 touch controller support).
- Uses LVGL for UI, EEZ framework for app structure, and esp_lcd_touch_gt911 for touch.
- `sdkconfig.defaults` enables SPIRAM and configures LVGL memory (128 KB). Review and regenerate `sdkconfig` if you change memory settings.

## Upstream & Credits

This project is based on and adapted from https://github.com/mr-sven/esp32-8048S050C. Please check the upstream repository for additional context, contributions, and historical changes.

Requirements
 - ESP-IDF 6.0 (or compatible 5.x release)
 - Python (ESP-IDF tooling)
 - A configured ESP-IDF environment with `idf.py` on PATH

Quick build & run
1. Set IDF target:

  idf.py set-target esp32s3

2. Build the project:

  idf.py build

3. Flash (replace `COMx` and `115200` as appropriate):

  idf.py -p COMx -b 115200 flash

4. Monitor serial output:

  idf.py -p COMx monitor

Notes
- LVGL: The project expects at least 128 KB LVGL heap for demo widgets (`CONFIG_LV_MEM_SIZE_KILOBYTES=128` in `sdkconfig.defaults`).
- SPIRAM: The supplied `sdkconfig.defaults` enables PSRAM; keep this if you rely on framebuffer in PSRAM.
- Touch: GT911 initialization maps touch coordinates to display coordinates. See `main/sunton_esp32_8048s050c.h` for mapping details.

Project layout (important files)
- `main/main.cpp` — application entry and initialization
- `main/sunton_esp32_8048s050c.c/.h` — hardware init (LCD, backlight, touch, I2C)
- `main/ui/` — LVGL UI sources, images and fonts
- `main/eez-framework/` — included EEZ framework sources used by the UI
- `CMakeLists.txt` / `sdkconfig.defaults` — build and default configuration

Tips
- If you change memory or PSRAM settings, run `idf.py reconfigure` (or delete `sdkconfig` and rebuild) to regenerate a config.
- Ensure the IDF target is `esp32s3` before building: `idf.py set-target esp32s3`.

## License

Check for a `LICENSE` file in the repository. If none is present, assume no license is provided by default.
