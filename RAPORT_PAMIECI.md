# Raport Pamięci — ESP32-S3 JC8048W550

**Cel:** Kompletny obraz wykorzystania SRAM i PSRAM w projekcie. Stan obecny + pola do optymalizacji.
**Data:** Maj 2026
**Układ:** 8 MB PSRAM (Octal) + 512 KB SRAM (wewnętrzny)

---

## CZĘŚĆ 1: STAN OBECNY

### 1.1. Pamięć Wewnętrzna (SRAM) — 512 KB

#### Podział sprzętowy

| Obszar | Adres | Rozmiar | Przeznaczenie |
|--------|-------|---------|---------------|
| SRAM0 (Cache) | 0x40370000 | 128 KB | Cache instrukcji + IROM |
| SRAM1 (Cache) | 0x3C000000 | 128 KB | Cache danych + DROM |
| SRAM2 (CPU0) | 0x3FCB0000 | ~249 KB | Heap + BSS + stosy |
| SRAM2 (CPU1) | 0x3FCE0000 | ~21 KB | Heap + BSS |
| DRAM | 0x3FCF0000 | 32 KB | DMA + dane krytyczne |
| RTCRAM | 0x600FE000 | 7 KB | RTC + sleep |

#### Alokacje w SRAM (zweryfikowane)

| Element | Rozmiar | Lokalizacja | Uwagi |
|---------|---------|-------------|-------|
| **Stos LVGL task** | 8192 B | SRAM (wymuszony `task_stack_caps`) | ✅ Krytyczny — zadanie GUI musi być w SRAM |
| **Stos comm_uart1** | 4096 B | SRAM (domyślnie) | ✅ UART IRQ wymaga SRAM |
| **Stos clock_task** | 4096 B | SRAM | ✅ Zegar czasu rzeczywistego |
| **Stos WiFi driver** | 6656 B | SRAM | ✅ WiFi DMA wymaga SRAM |
| **Rezerwa DMA** (`SPIRAM_MALLOC_RESERVE_INTERNAL`) | 32768 B | SRAM | ✅ Dla malloc`ów z `MALLOC_CAP_DMA` |
| **Próg małych alokacji** (`SPIRAM_MALLOC_ALWAYSINTERNAL`) | 16384 B | SRAM | Alokacje ≤16 KB lecą do SRAM |
| **LVGL task stack caps** | — | SRAM | `MALLOC_CAP_INTERNAL \| MALLOC_CAP_8BIT` |
| **IRAM (instrukcje krytyczne)** | ~50 KB | SRAM0 | ISR, scheduler, FreeRTOS |
| **BSS (dane statyczne)** | ~20 KB | SRAM | Zmienne globalne, bufory statyczne |

**Szacowane wolne SRAM po starcie:** ~200 KB (z boot logów: `heap_init: 3FCAAF70 len 0003E7A0 (249 KiB): RAM`)

### 1.2. Pamięć Zewnętrzna (PSRAM) — 8 MB (64 Mbit)

#### Stałe alokacje

| Element | Rozmiar | Status | Uwagi |
|---------|---------|--------|-------|
| **Framebuffer 1 (RGB panel)** | 768000 B | ✅ Stałe | 800×480×2B (RGB565), `num_fbs=1` |
| **Framebuffer 2 (RGB panel)** | 768000 B | ✅ Stałe | Double buffer, `num_fbs=2` |
| **LVGL draw buffers** | 0 B | ✅ Zero-copy | `avoid_tearing=true`, bufory z panelu |

**Razem stałe w PSRAM: 1,536,000 B ≈ 1.5 MB**

#### Dynamiczne alokacje

| Element | Rozmiar typowo | Maksimum | Uwagi |
|---------|---------------|----------|-------|
| **LVGL image cache** | 0-64 KB | 64 KB | `LV_CACHE_DEF_SIZE=65536` |
| **LVGL widgets** | 100-300 KB | ~500 KB | Zależne od ekranu |
| **WiFi/LWIP** | 50-100 KB | ~200 KB | `SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` |
| **Font glyph cache** | 10-50 KB | ~100 KB | LRU, zależne od użycia |
| **Layer buffer (max)** | 32 KB | 32 KB | `SIMPLE_BUF_SIZE=32768`, ARGB8888 |
| **safe_label buffers** | ~3 KB | ~20 KB | 64 B/label, ~50 labeli na ekran |
| **Log buffer** | 8 KB | 8 KB | `LOG_BUFFER_SIZE=8192` |
| **COBS/UART parser** | 256 B | 256 B | `FrameParser.buffer[256]` |
| **Struktury stanu** | ~1 KB | ~1 KB | Snapshot, settings, resources |

**Szacowane typowe użycie PSRAM:** ~2.5-3.0 MB (z 8 MB dostępnych)

**Szacowane wolne PSRAM:** ~5 MB

### 1.3. Konfiguracja kritična (sdkconfig / sdkconfig.defaults)

| Parametr | Wartość | Kiedy zmieniono | Uzasadnienie |
|----------|---------|-----------------|--------------|
| `SPIRAM_USE_MALLOC` | y | — | Duże alokacje → PSRAM |
| `SPIRAM_MALLOC_ALWAYSINTERNAL` | 16384 | — | ≤16KB w SRAM (oszczędza PSRAM) |
| `SPIRAM_MALLOC_RESERVE_INTERNAL` | 32768 | — | Rezerwa dla DMA |
| `LV_DRAW_LAYER_SIMPLE_BUF_SIZE` | **32768** | Maj 2026 | Zmniejszono z 786432 (768 KB → 32 KB) |
| `LV_CACHE_DEF_SIZE` | **65536** | Maj 2026 | Zmniejszono z 1048576 (1 MB → 64 KB) |
| `LV_DRAW_SW_DRAW_UNIT_CNT` | 1 | — | Jeden wątek rysujący (PSRAM bus) |
| `LV_DEF_REFR_PERIOD` | 15 | — | ~66 FPS odświeżania |
| `LV_USE_SYSMON` | y | — | Monitor systemu (FPS, RAM) |
| `LV_USE_PERF_MONITOR` | y | — | Monitor wydajności |
| `LV_USE_DEMO_WIDGETS` | y | — | Demo widgets w flash |

### 1.4. Mechanizmy bezpieczeństwa (wprowadzone)

| Mechanizm | Plik | Opis |
|-----------|------|------|
| **safe_label** | `safe_label.h` | `lv_label_set_text_static` — zero copy dla labeli + `LV_EVENT_DELETE` free |
| **Log mutex** | `log.cpp` | `s_logMutex` chroni `log_buffer` przed race condition |
| **CommState mutex** | `CommunicationState.cpp` | `s_mutex` chroni snapshot + dirty flags |
| **Consume atomic** | `CommunicationState.cpp` | `consumeSnapshot()` — kopiuj i czyść flagę atomowo |
| **Static TX bufory** | `UartTransport.cpp` | `sendRawFrame` bufory przeniesione ze stosu na `.bss` |
| **stopUart wait** | `UartTransport.cpp` | Czeka na zakończenie tasku przed resetem |
| **Callback guards** | `set_wifi.c`, `main.cpp` | `static bool` chroni przed duplikacją event callbacków |
| **Incremental log** | `log.cpp` | `lv_textarea_add_text` zamiast pełnego set_text |

---

## CZĘŚĆ 2: POLA DO OPTYMALIZACJI

### 2.1. Krytyczne — pilne do sprawdzenia

#### ⚠️ 2.1.1. LVGL task stack w SRAM

Obecnie LVGL task stack (8192 B) jest alokowany w **wewnętrznym SRAM** (`task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`).

LVGL rysuje głównie do PSRAM (framebuffery). Sam kod + dane stosu nie muszą być w SRAM. Po ustabilizowaniu WDT można przetestować:
```
lvgl_cfg.task_stack_caps = MALLOC_CAP_SPIRAM;  // Stos LVGL w PSRAM
```

**Zysk:** ~8 KB SRAM. **Ryzyko:** Spowolnienie LVGL (stack access przez PSRAM). Najpierw zmierz `uxTaskGetStackHighWaterMark()`.

#### ⚠️ 2.1.2. `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`

Obecnie KAŻDA alokacja ≤16 KB idzie do SRAM. To DUŻO:
- LVGL tworzy wiele małych widgetów (kilka KB każdy)
- Próg 16 KB wysyła je wszystkie do SRAM
- Z czasem SRAM się wyczerpuje, PSRAM stoi pusty

**Zalecenie:** Sprawdź boot logi — jeśli `heap_init` pokazuje mało SRAM, obniż próg:
```diff
- CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384
+ CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096   // Tylko ≤4 KB w SRAM
```

**Zysk:** ~50-100 KB SRAM dla DMA i krytycznych alokacji.

### 2.2. Średnie — rekomendowane do produkcji

#### 🔶 2.2.1. Wyłącz demo i monitory (oszczędność flash + tiny RAM)

```diff
- CONFIG_LV_USE_DEMO_WIDGETS=y
+ # CONFIG_LV_USE_DEMO_WIDGETS is not set
- CONFIG_LV_USE_SYSMON=y
- CONFIG_LV_USE_PERF_MONITOR=y
```

Demo widgets dodają ~50 KB do flash (nie RAM, ale zbędne w produkcji).  
Monitory dodają runtime overlay (pomijalne RAM). Można zostawić dla diagnostyki.

#### 🔶 2.2.2. Zredukuj fonty do potrzebnych

Obecnie wbudowane:
- Montserrat 14, 16, 18, 30, 48 + 14 jako default

Sprawdź które są faktycznie używane w UI. Każdy embedded font to ~20-60 KB we flash (flash, nie RAM — ale cache w PSRAM przy pierwszym użyciu).

```diff
- CONFIG_LV_FONT_MONTSERRAT_48=y  // Czy potrzebujesz 48px?
+ # CONFIG_LV_FONT_MONTSERRAT_48 is not set
```

#### 🔶 2.2.3. Usuń niespójność ThorVG

```
sdkconfig.defaults: CONFIG_LV_USE_THORVG_INTERNAL=y
sdkconfig:          # CONFIG_LV_USE_THORVG is not set
```

`THORVG_INTERNAL=y` kompiluje źródła ThorVG do flash (zajmuje ~100 KB flash), ale `THORVG` nie jest włączony, więc renderer nie jest używany. Niespójność — nie wpływa na RAM, ale flash jest marnowany.

```diff
- CONFIG_LV_USE_THORVG_INTERNAL=y
+ # CONFIG_LV_USE_THORVG_INTERNAL is not set
```

### 2.3. Drobne — kosmetyka/opcjonalne

#### 🟢 2.3.1. Odświeżanie LVGL w idle

`CONFIG_LV_DEF_REFR_PERIOD=15` — LVGL odświeża co 15ms (~66 FPS). Dla statycznego UI (monitor pogody) można zmniejszyć do 30-50ms bez zauważalnego efektu, oszczędzając CPU.

```diff
- CONFIG_LV_DEF_REFR_PERIOD=15
+ CONFIG_LV_DEF_REFR_PERIOD=30
```

#### 🟢 2.3.2. Redukcja stosu clock_task

```c
xTaskCreatePinnedToCore(clock_task, "clock_task", 4096, NULL, 3, NULL, 0);
```

clock_task głównie czeka (`vTaskDelay`). Po zmierzeniu HighWaterMark można zejść do 2048.

#### 🟢 2.3.3. `CONFIG_LV_USE_ASSERT_*` w produkcji

Obecnie włączone:
```c
CONFIG_LV_USE_ASSERT_NULL=y      // Sprawdza NULL pointery
CONFIG_LV_USE_ASSERT_MALLOC=y    // Sprawdza malloc failure
```

Bezpieczne w produkcji — zatrzymują system przy błędzie zamiast cichej korupcji. **Zostaw.**

### 2.4. Podsumowanie potencjalnych zysków

| Optymalizacja | Zysk SRAM | Zysk PSRAM | Zysk Flash | Ryzyko |
|---------------|-----------|------------|------------|--------|
| Stos LVGL → PSRAM | ~8 KB | 0 | 0 | Spowolnienie LVGL |
| `ALWAYSINTERNAL=4096` | ~50-100 KB | 0 | 0 | Małe alokacje w PSRAM (wolniej) |
| Wyłącz demo | 0 | 0 | ~50 KB | — |
| Wyłącz monitory | ~1 KB | 0 | ~10 KB | Brak FPS overlay |
| Zredukuj fonty | 0 | 0 | ~50-100 KB | Brakuje fonta w UI |
| Usuń ThorVG | 0 | 0 | ~100 KB | — |
| Zmniejsz refresh | 0 | 0 | 0 | Niższe FPS |
| Redukcja stosów | ~4-8 KB | 0 | 0 | Stack overflow |

---

## CZĘŚĆ 3: REKOMENDACJA KOŃCOWA (PRODUKCYJNA)

### Konfiguracja docelowa `sdkconfig.defaults`:

```ini
# === PSRAM ===
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096     # TYLKO ≤4KB w SRAM
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768  # Rezerwa DMA

# === LVGL Memory ===
CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=32768
CONFIG_LV_CACHE_DEF_SIZE=65536
CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE=32
CONFIG_LV_DRAW_SW_CIRCLE_CACHE_SIZE=32
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1
CONFIG_LV_DEF_REFR_PERIOD=30                # 30ms = ~33 FPS (oszczędność CPU)

# === LVGL Performance (zostaw dla diagnostyki; wyłącz w final build) ===
# CONFIG_LV_USE_SYSMON=y
# CONFIG_LV_USE_PERF_MONITOR=y

# === Fonty (tylko potrzebne) ===
CONFIG_LV_FONT_MONTSERRAT_14=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_18=y
CONFIG_LV_FONT_MONTSERRAT_30=y
# CONFIG_LV_FONT_MONTSERRAT_48 is not set    # Włącz tylko jeśli UI używa 48px

# === Wyłącz zbędne ===
# CONFIG_LV_USE_DEMO_WIDGETS is not set
# CONFIG_LV_USE_THORVG_INTERNAL is not set   # Niespójne, ThorVG nieużywany

# === Bezpieczeństwo (zostaw) ===
CONFIG_LV_USE_ASSERT_NULL=y
CONFIG_LV_USE_ASSERT_MALLOC=y
```

### Task stacki do zweryfikowania

Po wdrożeniu zmian, uruchom z debug i odczytaj `uxTaskGetStackHighWaterMark()` dla każdego taska. Docelowo:

| Task | Obecnie | Po optymalizacji | Warunek |
|------|---------|------------------|---------|
| taskLVGL | 8192 | 6144 | Jeśli HighWaterMark > 2048 |
| clock_task | 4096 | 2048 | Jeśli HighWaterMark > 1024 |
| comm_uart1 | 4096 | 2048 | Jeśli HighWaterMark > 1024 |

### Podsumowanie zysków (target production)

| Zasób | Stan obecny | Po optymalizacji |
|-------|-------------|------------------|
| Wolny SRAM | ~200 KB | ~250-300 KB (+50-100 KB) |
| Wolny PSRAM | ~5 MB | ~5 MB (bez zmian) |
| Flash | ~6.5 MB | ~6.2 MB (−300 KB) |
| Stabilność WDT | ✅ Działa (bez skalowania) | ✅ Działa (ma być ze skalowaniem) |
