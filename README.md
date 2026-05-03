# ESP32-S3 JC8048W550 (Gution) — LVGL

Ten repozytorium to fork dostosowany do modułu Gution/kompatybilnego wyświetlacza 800×480 RGB z dotykiem GT911, oparty na projekcie upstream z ESP32-S3 i LVGL 9.

## Co znajdziesz w tym repo
- ESP32-S3 z wyświetlaczem RGB 800×480
- interfejs dotykowy GT911
- silnik GUI LVGL 9.x
- struktura aplikacji oparta na EEZ
- wsparcie dla PSRAM i buforowania w pamięci zewnętrznej

## Stan forka
- Ten repozytorium jest forkiem, a nie oryginalnym projektem Sunton.
- Nazwy plików nadal mogą zawierać `sunton`, ponieważ pochodzą z upstreamu.
- Fizyczną marką wyświetlacza jest Gution / kompatybilny moduł 800×480.

## Wymagania
- ESP-IDF 6.0 lub kompatybilny z 5.x
- Python z narzędziami ESP-IDF
- skonfigurowane środowisko ESP-IDF z `idf.py` dostępnym w PATH

## Szybki start
1. Ustaw target:

  idf.py set-target esp32s3

2. Zbuduj projekt:

  idf.py build

3. Wgraj na urządzenie:

  idf.py -p COMx -b 115200 flash

4. Otwórz monitor:

  idf.py -p COMx monitor

## Ważne informacje
- `sdkconfig.defaults` w tym repo zawiera ustawienia PSRAM i konfigurację LVGL.
- Jeśli zmienisz ustawienia pamięci, usuń `sdkconfig` i odbuduj projekt, aby wygenerować nowy plik konfiguracyjny.
- Upewnij się, że wybrany target to `esp32s3` przed kompilacją.

## Najważniejsze pliki
- `main/main.cpp` — punkt wejścia i inicjalizacja aplikacji
- `main/sunton_esp32_8048s050c.c/.h` — inicjalizacja LCD, podświetlenia i dotyku
- `main/ui/` — źródła interfejsu LVGL, obrazy, fonty
- `main/eez-framework/` — biblioteki EEZ używane przez UI
- `CMakeLists.txt` / `sdkconfig.defaults` — konfiguracja budowania

## Upstream
Projekt bazuje na: https://github.com/mr-sven/esp32-8048S050C


