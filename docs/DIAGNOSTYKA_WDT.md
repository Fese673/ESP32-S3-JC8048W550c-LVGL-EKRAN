# Diagnostyka WDT (Task Watchdog Timeout) — Raport Techniczny

## 1. Objaw

Task Watchdog resetuje system przy wchodzeniu w menu z animacjami, cieniami lub przezroczystościami.

## 2. Co NIE jest przyczyną (wykluczone)

| Podejrzenie | Werdykt | Dowód |
|---|---|---|
| Memory Doubling (3.0 MB buforów) | ❌ Wykluczone | `avoid_tearing=true` → LVGL używa buforów panelu RGB, zero-copy. `KONFRONTACJA.md` |
| Za mało PSRAM ogólnie | ❌ Wykluczone | System ma 8 MB PSRAM, ~6.5 MB wolne po odjęciu ramek |
| `buffer_size = 0` | ❌ Wykluczone | `assert(disp_cfg->buffer_size > 0)` w `esp_lvgl_port_disp.c:266` — zcrashowałoby |

## 3. Prawdziwa przyczyna — "Szwajcarski Ser" w PSRAM

### Łańcuch zdarzeń:

```
WiFi alokacje
  → Logi (log_add_to_buffer)
    → LVGL widget alloc/free
      → safe_label malloc(64) × setki labeli
        → Poprzednie warstwy (layer alloc/free)
          → FRAGMENTACJA PSRAM ← JESTEŚMY TUTAJ
              ↓
    Użytkownik wchodzi w menu z efektami (cień, przezroczystość)
              ↓
    LVGL woła lv_draw_layer_create() → lv_draw_layer_alloc_buf()
              ↓
    Próbuje zaalokować LV_DRAW_LAYER_SIMPLE_BUF_SIZE = 786432 bajtów
              ↓
    768 KB ciągłego PSRAM — NIE MA TAKIEGO BLOKU (są tylko dziury)
              ↓
    Alokator (malloc) zaczyna skanować i scalać pamięć (heap coalescing)
              ↓
    Trwa to >5 sekund → Watchdog twardo resetuje procesor
```

### Dowód w kodzie — `lv_refr.c:528-533`:

```c
if(layer_type == LV_LAYER_TYPE_SIMPLE) {
    int32_t w = lv_area_get_width(&layer_area_full);  // 800
    uint8_t px_size = lv_color_format_get_size(disp_refr->color_format);  // 2 (RGB565)
    max_rgb_row_height = LV_DRAW_LAYER_SIMPLE_BUF_SIZE / w / px_size;
    // = 786432 / 800 / 2 = 491 rows  ← cały ekran (480) w JEDNEJ alokacji
}
```

Następnie w `lv_draw.c:501`:
```c
layer->draw_buf = lv_draw_buf_create(w, h, layer->color_format, 0);
// → alokuje 800 × 491 × 2 = 785.600 bajtów ciągłego PSRAM
```

### Dowód w sdkconfig:

```
CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=786432   ← 768 KB
CONFIG_LV_DRAW_LAYER_MAX_MEMORY=0             ← brak limitu całkowitego
```

LVGL domyślnie: `LV_DRAW_LAYER_SIMPLE_BUF_SIZE = 24 * 1024 = 24 KB` — tyle zaleca producent.

## 4. Rozwiązanie — ograniczenie warstw (Layers) w LVGL 9

### Krok 1: Zmniejsz `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE`

Zamiast 768 KB, ustaw wartość, która pozwoli na łatwe znalezienie ciągłego bloku:

| Wartość | Wierszy na chunk | Ryzyko WDT | Uwagi |
|---|---|---|---|
| **786432** (obecnie) | 491 (cały ekran) | 🔴 Bardzo wysokie | Jeden wielki blok — łatwo trafić na fragmentację |
| **262144** (256 KB) | 163 wiersze | 🟡 Średnie | Kompromis |
| **131072** (128 KB) | 81 wierszy | 🟢 Niskie | Zalecane startowo |
| **65536** (64 KB) | 40 wierszy | 🟢 Bardzo niskie | Bezpieczne, więcej passów rysowania |
| **24576** (24 KB) | 15 wierszy | 🟢 Najbezpieczniejsze | Domyślna wartość LVGL, wiele passów |

**Zalecenie:** Zacznij od **131072 (128 KB)** i testuj. Jeśli nadal WDT → zejdź do 65536.

### Krok 2: Opcjonalnie — ogranicz całkowitą pamięć warstw

`CONFIG_LV_DRAW_LAYER_MAX_MEMORY=0` oznacza brak limitu. Można ustawić rozsądny limit:

```
CONFIG_LV_DRAW_LAYER_MAX_MEMORY=2097152  ← 2 MB max na wszystkie warstwy
```

### Krok 3: Jak zmienić

Edytuj `sdkconfig.defaults` (linia 20):

```diff
- CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=786432
+ CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=131072
```

Następnie:
```bash
idf.py reconfigure   # przebuduj konfigurację z nowych defaults
idf.py build
```

### Krok 4: Weryfikacja

Po flashowaniu sprawdź logi:
```
I (xxx) LVGL: Layer memory used: XX kB
```
Przy wejściu w menu powinny pojawiać się mniejsze wartości, a system nie powinien się resetować.

## 5. Dodatkowe optymalizacje (po ustabilizowaniu WDT)

### Zmniejsz `LV_CACHE_DEF_SIZE` jeśli potrzebujesz więcej RAM

Obecnie: `CONFIG_LV_CACHE_DEF_SIZE=1048576` (1 MB cache LVGL).

Jeśli cache jest w PSRAM i też fragmentuje pamięć, można rozważyć zmniejszenie do 512 KB:
```
CONFIG_LV_CACHE_DEF_SIZE=524288
```

### Zmniejsz stosy zadań (po użyciu `uxTaskGetStackHighWaterMark()`)

Obecnie: `comm_uart1=4096`, `clock_task=4096`, LVGL=8192. Po potwierdzeniu watermarkiem można zredukować o 1-2 KB każde.

## 6. Podsumowanie

**Przyczyna:** `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=786432` zmusza LVGL do alokacji **768 KB ciągłego PSRAM** na pojedynczą warstwę. W sfragmentowanej pamięci alokacja trwa >5s → WDT.

**Rozwiązanie:** Zmniejszyć do **131072 (128 KB)**. LVGL podzieli warstwę na ~81-wierszowe chunki, które łatwo zaalokować w PSRAM. Efekt wizualny identyczny — warstwa rysowana w kilku passach zamiast jednej.

**Ryzyko:** Zerowe. LVGL 9.4 obsługuje subdivision warstw natywnie. Jedyna różnica — więcej ale mniejszych alokacji.
