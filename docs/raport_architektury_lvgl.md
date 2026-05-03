# Raport architektury LVGL i framebufferów

## Krótka odpowiedź

Tak, w kodzie masz już konfigurację zgodną z architekturą **direct mode + dwa framebuffery** dla RGB LCD. To nie jest tylko deklaracja w opisie, ale realna konfiguracja panelu i portu LVGL.

Nie mogę jednak uczciwie potwierdzić, że jest to architektura „idealna” w sensie absolutnym. Jest bardzo sensowna i bliska docelowemu układowi dla tego sprzętu, ale nadal ma kompromisy pamięciowe i zależy od tego, czy `ui_tick()` oraz logika EEZ nie blokują LVGL w runtime.

## Co jest już dobrze ustawione

W sterowniku wyświetlacza [main/sunton_esp32_8048s050c.c](../main/sunton_esp32_8048s050c.c) konfiguracja wygląda tak:

- `num_fbs = 2`
- `fb_in_psram = true`
- `direct_mode = true`
- `double_buffer = true`
- `bb_mode = false`
- `avoid_tearing = true`

To oznacza, że:

- panel RGB pracuje z dwoma framebufferami,
- LVGL renderuje w trybie bezpośrednim,
- bufory ekranu są lokowane w PSRAM,
- nie używasz dodatkowego software backbuffera,
- redukujesz ryzyko tearingu przez konfigurację RGB portu.

To jest poprawny kierunek dla ESP32-S3 z dużym ekranem 800×480.

## Co wspiera tę architekturę

W [sdkconfig.defaults](../sdkconfig.defaults) widać dodatkowo:

- `CONFIG_SPIRAM=y`
- `CONFIG_SPIRAM_MODE_OCT=y`
- `CONFIG_SPIRAM_SPEED_80M=y`
- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`
- `CONFIG_SPIRAM_RODATA=y`
- `CONFIG_LV_USE_CLIB_MALLOC=y`
- `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=2304000`
- `CONFIG_LV_CACHE_DEF_SIZE=1048576`

To potwierdza, że projekt jest ustawiony na cięższą grafikę i przerzucanie dużej części kosztu do PSRAM. Dla takiego panelu to ma sens, ale oznacza też, że pamięć jest wykorzystywana agresywnie.

## Co nadal nie jest „idealne”

1. Nie ma jeszcze dowodu, że runtime nie blokuje LVGL. W [main/main.cpp](../main/main.cpp) `ui_tick()` jest uruchamiany z timera LVGL, więc jeśli ta funkcja robi ciężkie rzeczy pod lockiem LVGL, cały model może nadal przycinać.
2. `task_stack = 8192` i `task_max_sleep_ms = 500` w [main/sunton_esp32_8048s050c.c](../main/sunton_esp32_8048s050c.c) są rozsądne, ale nie gwarantują perfekcji. To trzeba potwierdzić pomiarem.
3. `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=2304000` jest bardzo duży. To może być świadoma optymalizacja, ale też oznacza wysoki koszt pamięciowy, który trzeba regularnie kontrolować.
4. „Idealna” architektura zależy od metryk: brak watchdogów, brak długich sekcji krytycznych, stabilny FPS, brak fragmentacji RAM i brak długich alokacji.

## Ocena końcowa

Moja ocena jest taka:

- **Tak**: masz już poprawną architekturę direct mode z dwoma framebufferami.
- **Tak**: konfiguracja jest spójna z dużym RGB LCD i PSRAM.
- **Nie do końca**: nie da się jeszcze nazwać jej idealną bez potwierdzenia w runtime.

Najważniejszy brak to dowód, że `ui_tick()` i logika EEZ nie trzymają LVGL zbyt długo. Jeśli to jest krótkie i nieblokujące, to architektura jest bardzo dobra. Jeśli nie, wtedy problemem nie są framebuffer y, tylko przebieg pracy pod mutexem LVGL.

## Co warto jeszcze sprawdzić

- czas wykonania `ui_tick()`,
- użycie wolnej pamięci wewnętrznej i PSRAM po starcie,
- czy nie pojawiają się watchdogi tasku LVGL,
- czy ekran nie ma tearingu przy dynamicznej zmianie widoków,
- czy `taskLVGL` nie jest blokowany przez ciężkie operacje w UI.

## Podsumowanie jednym zdaniem

Masz już właściwy fundament: direct mode, dwa framebuffery, PSRAM i brak software backbuffera, ale „idealność” trzeba jeszcze potwierdzić pomiarami działania, nie tylko konfiguracją.