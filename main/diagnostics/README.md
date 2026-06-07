# lv_perf_diag — diagnostyka LVGL + heap (UART)

Moduł mierzy wydajność LVGL i stan heap **bez wpływu na UI**. Wyłączony w release (`CONFIG_LV_PERF_DIAG=n`) = zero narzutu (no-op stuby).

## Włączenie

`menuconfig` → **Sunton ESP32-S3 Package config** → **LVGL Performance Diagnostics**

| Opcja | Domyślnie | Opis |
|-------|-----------|------|
| `LV_PERF_DIAG_INTERVAL_MS` | 1000 | Okres próbkowania (ms) |
| `LV_PERF_DIAG_HEARTBEAT_SKIP` | 4 | Gdy OK: log co (1+N) interwałów |
| `LV_PERF_DIAG_VERBOSE` | n | Pełny format każdego heartbeat |

## Integracja (`main.cpp`)

```cpp
lv_perf_diag_init();                          // po ui_init
lv_perf_diag_screen_change(from, to);         // przy zmianie ekranu
lv_perf_diag_tick_begin(); / tick_end();      // owijaj ui_tick_timer_cb
```

## Format logów

### Zmiana ekranu (natychmiast)

```
I DIAG: >> MENU->BPM int=25K(-8.5K blk=18K) psram=122K SRAM_LOSS
```

| Pole | Znaczenie |
|------|-----------|
| `(-8.5K)` | Spadek **internal SRAM** od poprzedniego ekranu |
| `blk` | Największy ciągły blok internal (`largest_free_block`) — fragmentacja |
| `SRAM_LOSS` | Spadek ≥ 2 KB przy przejściu |
| `SRAM_FRAG` | `blk` < 4 KB |

### Heartbeat OK (rzadki, anty-spam)

```
I DIAG: [ok] MENU int=22K blk=18K psram=120K refr=12ms tick=6ms
```

### Alert (WARN, zawsze gdy aktywny)

```
W DIAG: [WARN] BPM int=25K(-8.0K head=512B blk=4K) psram=122K(min=2K) refr=396ms tick=35ms | SRAM_DRAIN DRAW_STALL
```

| Tag | Warunek |
|-----|---------|
| `SRAM_LOW` | internal free < 25 KB |
| `SRAM_FLOOR` | free − min < 2 KB (na dnie) |
| `SRAM_DRAIN` | spadek ≥ 2 KB od ostatniego heartbeat |
| `SRAM_FRAG` | largest block < 4 KB |
| `PSRAM_SPIKE` | min PSRAM < 8 KB **i** free < 32 KB (realna presja, nie historyczny min) |
| `DRAW_STALL` | lv_timer_handler gap ≥ 150 ms **w tym interwale** |
| `TICK_SLOW` | ui_tick ≥ 20 ms w tym interwale |

WARN powtarza się tylko gdy **zmieni się zestaw tagów** lub **ekran**; ten sam alert co ~5 s max.

## Jak czytać wyniki

- **Spadek `int` przy `>>`** → permanentna alokacja na nowym ekranie (lazy init, BPM, safe_label…).
- **`SRAM_FLOOR` + mały `head`** → brak zapasu nad historycznym minimum — ryzyko `malloc` fail.
- **`SRAM_FRAG` przy dużym `int`** → fragmentacja (wolne rozproszone).
- **`PSRAM_SPIKE` przy stabilnym `psram=`** → chwilowe obciążenie (cache/decode), nie leak.
- **`DRAW_STALL`** → wąskie gardło renderera (`swdraw`), nie heap.

## Anty-spam

1. Zmiana ekranu → **1 linia** od razu  
2. Progi przekroczone → **WARN** co interwał  
3. Stan OK → **co 5 s** (przy interval=1 s, skip=4) jedna linia `[ok]`
