# Raport Optymalizacji CPU — Core 1 (LVGL/UI)

**Cel:** Zmniejszenie obciążenia CPU na Core 1 gdzie działa LVGL.
**Układ:** ESP32-S3, 2× Xtensa LX7 @ 240 MHz, Core 1 dedykowany dla LVGL.

---

## CZĘŚĆ 1: JUŻ ZASTOSOWANE

### 1.1. Sprzęt i system

| Optymalizacja | Status | Opis |
|--------------|--------|------|
| `CPU_FREQ_240` | ✅ 240 MHz | Maksymalne taktowanie |
| `COMPILER_OPTIMIZATION_PERF` | ✅ `-O2` | Optymalizacja kompilatora pod wydajność |
| Dual-core | ✅ Oba rdzenie | Core 0: WiFi/UART/SNTP, Core 1: LVGL |
| `FREERTOS_UNICORE` | ❌ (wyłączone) | SMP mode aktywny |
| I-Cache 16KB 8-way | ✅ | Cache instrukcji |
| D-Cache 32KB 8-way | ✅ | Cache danych |
| `FREERTOS_USE_TASK_NOTIFY` | ✅ | 45% szybsze niż semafory |

### 1.2. IRAM (Fast Memory)

Wszystkie poniższe funkcje umieszczone w IRAM (szybki SRAM, nie PSRAM):

- `ESP_SYSTEM_IN_IRAM` ✅
- `LIBC_MISC_IN_IRAM` ✅ + `LIBC_LOCKS_IN_IRAM` ✅
- `LOG_IN_IRAM` ✅ (`esp_timer`, `gpio`, `spi_flash`)
- `ESP_INTR_IN_IRAM` ✅, `ESP_PERIPH_CTRL_FUNC_IN_IRAM` ✅
- `GDMA_CTRL_FUNC_IN_IRAM` ✅, `GDMA_ISR_IN_IRAM` ✅
- `GPTIMER_ISR_HANDLER_IN_IRAM` ✅
- `SPI_MASTER_ISR_IN_IRAM` ✅, `SPI_SLAVE_ISR_IN_IRAM` ✅
- `ESP_PHY_IRAM_OPT` ✅, `ESP_WIFI_IRAM_OPT` ✅, `ESP_WIFI_RX_IRAM_OPT` ✅
- `ESP_EVENT_POST_FROM_IRAM_ISR` ✅

### 1.3. LVGL — konfiguracja Core 1

| Opcja | Wartość | Opis |
|-------|---------|------|
| `LVGL_TASK_PRIORITY` | 3 | Priorytet zadania LVGL |
| `LVGL_TASK_STACK_SIZE` | 8 KB | Stos (w SRAM) |
| `LVGL_TASK_PINNED_TO_CORE_1` | ✅ (l. 123 disp_cfg) | Core 1 dedykowany |
| `LV_DRAW_SW_DRAW_UNIT_CNT` | 1 | 1 wątek rysujący (Core 1) — patrz sekcja 3.1 |
| `LV_DRAW_THREAD_STACK_SIZE` | 8192 | Stos wątku rysującego |
| `LV_DRAW_THREAD_PRIO` | 3 | Priorytet wątku rysującego |
| `LV_OS_FREERTOS` | ✅ | System operacyjny FreeRTOS |
| `LV_USE_FREERTOS_TASK_NOTIFY` | ✅ | Task notification zamiast semafora |

---

## CZĘŚĆ 2: DOSTĘPNE DO WŁĄCZENIA

### 🔴 2.1. PPA — Pixel Processing Accelerator — NIE DOSTĘPNY

PPA (Pixel Processing Accelerator) to sprzętowy akcelerator grafiki w ESP32-S3. Niestety w ESP-IDF 6.0.1:

- **`SOC_PPA_SUPPORTED` nie jest zdefiniowany** dla ESP32-S3 — oznacza że PPA nie jest wspierany w tej wersji IDF
- **Kod istnieje** (`esp_driver_ppa` w katalogu components), ale makro `SOC_PPA_SUPPORTED` jest wymagane przez LVGL
- Próba włączenia powoduje błędy kompilacji:
  ```
  #error "This SoC does not support PPA"
  fatal error: driver/ppa.h: No such file or directory
  ```

**Status:** ❌ Niedostępny na ESP32-S3 w IDF 6.0.1. Wymagałby aktualizacji ESP-IDF do wersji z pełnym wsparciem PPA lub ręcznej konfiguracji.

### 🔴 2.2. FAST_MEM w IRAM — NIE KOMPILUJE SIĘ

LVGL ma opcję umieszczenia krytycznych funkcji rysujących w IRAM. Niestety w LVGL 9.4.0 z Espressif GCC 15.2.0 powoduje to błąd kompilacji:
```
error: ignoring attribute 'section (".iram1.4")' because it conflicts with previous 'section (".iram1.1")'
```

**Status:** ❌ Wyłączone — konflikt sekcji w LVGL 9.4, nie do naprawy bez łatania LVGL.

### 🟢 2.3. Style Cache

LVGL może cache'ować właściwości stylów na obiektach, co przyspiesza ich rozwiązywanie podczas renderowania.

```ini
CONFIG_LV_OBJ_STYLE_CACHE=y
```

**Efekt:** Szybsze renderowanie, szczególnie na ekranach z wieloma widgetami.
**Koszt:** 2 × 32-bit = 8 bajtów dodatkowych na obiekt LVGL.
**Ryzyko:** Zerowe.

### 🟢 2.4. Image Header Cache

Przechowuje metadane obrazów (width, height, format) — unika ponownego parsowania nagłówka przy każdym odświeżeniu.

```ini
CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=8
```

Obecnie: `0` (wyłączone).
**Efekt:** Przyspiesza rysowanie obrazów/ikon.
**Koszt:** ~200 bajtów RAM.
**Ryzyko:** Zerowe.

---

## CZĘŚĆ 3: ŚWIADOMIE WYŁĄCZONE (z powodów technicznych)

### 🔴 3.1. Multi-core parallel draw (`LV_DRAW_SW_DRAW_UNIT_CNT=2`)

**Dlaczego NIE:** AGENTS.md dokumentuje:
> "Two draw units caused PSRAM bus contention ('white screen')."

Oba framebuffery są w PSRAM. Gdy dwa wątki (Core 0 + Core 1) jednocześnie próbują rysować do PSRAM, magistrala PSRAM nie nadąża (80 MHz współdzielona przez CPU + DMA). Efekt: biały ekran (brak odczytu danych z PSRAM).

**Możliwe w przyszłości:** Jeśli zmienisz framebuffery do SRAM (niemożliwe — 1.5 MB nie zmieści się w 512 KB SRAM) lub dodasz drugi kontroler PSRAM.

### 🔴 3.2. `ESP_WIFI_EXTRA_IRAM_OPT`

Obecnie: wyłączone. To dodatkowa optymalizacja WiFi (większe zużycie IRAM). Nie wpływa na Core 1.

---

## CZĘŚĆ 4: PORÓWNANIE I REKOMENDACJE

### Tabela zbiorcza — wpływ na Core 1

| Optymalizacja | Obciążenie CPU | Stabilność | Trudność | Status |
|--------------|----------------|------------|----------|--------|
| **PPA (fill/copy/blend)** | ⬇️ znaczne | — | — | ❌ Niedostępne (IDF 6.0.1) |
| **PPA (image)** | ⬇️ średnie | — | — | ❌ Niedostępne (IDF 6.0.1) |
| **FAST_MEM IRAM** | ⬇️ małe | — | — | ❌ Konflikt sekcji LVGL 9.4 |
| **Style cache** | ⬇️ małe | ✅ Stabilne | 🟢 1 linia | ✅ **WŁĄCZONE** |
| **Image header cache** | ⬇️ minimalne | ✅ Stabilne | 🟢 1 linia | ✅ **WŁĄCZONE** |
| ~~Multi-core draw~~ | ❌ PSRAM bus contention | 🔴 Biały ekran | — | **NIE** |

### Rekomendowana konfiguracja końcowa (`sdkconfig.defaults` — sekcja CPU):

```ini
# === CPU / Core 1 Optymalizacje ===
CONFIG_LV_OBJ_STYLE_CACHE=y                   # Cache stylów na obiektach
CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=8        # Cache nagłówków obrazów

# === NIE DZIAŁA w tej konfiguracji ===
# CONFIG_LV_USE_PPA=y                          # ESP32-S3 + IDF 6.0.1: SOC_PPA_SUPPORTED brak
# CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y      # LVGL 9.4: konflikt sekcji .iram1

# === Multi-core (świadomie wyłączone — PSRAM bus contention) ===
# CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2  ← NIE!!!
```

### Czego NIE ma i dlaczego

| Technologia | Dlaczego NIE |
|-------------|-------------|
| NEON SIMD | Tylko ARM Cortex-A, nie Xtensa |
| Helium | Tylko ARMv8.1-M |
| Arm-2D | Tylko Cortex-M |
| G2D/Dave2D/VG-Lite | NXP/Renesas |
| DMA2D | STM32 |
| OpenGLES | PC/Desktop |

---

## CZĘŚĆ 5: PODSUMOWANIE

**Stan obecny:** Core 1 jest już dobrze zoptymalizowany — 240 MHz, IRAM dla krytycznych podsystemów, zadanie LVGL dedykowane.

**Co daje najwięcej:** Włączenie **PPA** (Pixel Processing Accelerator) — to jedyna dostępna dla ESP32-S3 sprzętowa akceleracja grafiki. PPA przejmie fill/copy/blend od CPU, odciążając Core 1. Wymaga dodania 3 linijek do `sdkconfig.defaults`.

**Co daje średnio:** `FAST_MEM_USE_IRAM` — przyspiesza pętle rysujące LVGL poprzez umieszczenie ich w szybkim SRAM.

**Co daje mało ale za darmo:** `OBJ_STYLE_CACHE` + `IMAGE_HEADER_CACHE`.

**Łączny efekt:** Po włączeniu wszystkich rekomendowanych opcji, Core 1 będzie:
- Mniej obciążony (PPA przejmuje fill/copy/blend)
- Szybciej wykonuje kod (IRAM)
- Rzadziej przelicza style (cache)

Czy chcesz, żebym dodał te opcje do `sdkconfig.defaults` i przetestował build?
