# Notatki z debugu

## 2026-06-06 — CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384→512 — efekt pozytywny ale nie rozwiązuje freezu

`sdkconfig:1904`: zmiana progu z 16384 na 512. Efekt:

- **SRAM startowa wzrosła z 42K→50K** — próg 512B wypchnął małe alokacje LVGL do PSRAM
- **Chart BPM (747 punktów) poszedł do PSRAM** — przy przejściu na BPM `psram=111K→108K` (spadek 3KB) zamiast SRAM
- **`int=37K` na BPM (vs 24K przed zmianą)** — SRAM free wzrósł o 13KB na BPM
- **SRAM_LOW ostrzeżenia zniknęły** — `head` (największy blok) utrzymuje się >10KB

**Nie rozwiązuje freezu** — watchdog nadal występuje przy screen transition. Potwierdza to, że przyczyną nie jest brak SRAM lecz przeciążenie renderowania na Core 1 (swdraw, `transform_and_recolor`).

## 2026-06-05 — CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=8 nie pomogło

`sdkconfig` miał `=0` zamiast `=8` (defaults chciał 8). Po edycji i rebuildzie watchdog dalej występuje. To znaczy że same 2 największe obrazki (kfe_weather 188 KB + kfe_sunsetrise 129 KB) wystarczają do fragmentacji PSRAM i freezu. Cache 64 KB nie mieści ich.

Następny krok: zwiększyć `LV_CACHE_DEF_SIZE` z 65536 do ~204800 (200 KB), żeby wszystkie 9 obrazków mieściły się w cache i nie było malloc/free per tick.

## 2026-06-05 — ui_tick_timer 20ms → 100ms też nie pomogło

Zmiana okresu ticka z 20ms na 100ms (`main/app/main.cpp:286`) nie zatrzymała freezu.
To znaczy że nawet 10× mniej decode'ów na sekundę (100ms zamiast 20ms) nie wystarcza —
fragmentacja PSRAM narasta mimo wolniejszego tempa, bo 2 największe obrazki (188 KB + 129 KB)
są poza cache i decode/free każdego ticka. Nawet przy 100ms → 10 malloc(188K)/free na sekundę
wystarczy do sfastrowania systemu po kilkunastu zmianach ekranu.

Wniosek: konieczne jest powiększenie LV_CACHE_DEF_SIZE albo pre-decode obrazków.

## 2026-06-05 — LV_CACHE_DEF_SIZE=204800 też nie pomogło

Zwiększenie cache do 200 KB (obejmuje kfe_weather 188 KB) nie zatrzymało freezu.
Czyli to nie fragmentacja PSRAM od decode'ów obrazków jest główną przyczyną — lub jest
jedną z wielu nakładających się przyczyn. Szukamy dalej.
