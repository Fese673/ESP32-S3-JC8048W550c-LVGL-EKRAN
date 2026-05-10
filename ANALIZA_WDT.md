# ANALIZA WDT (Task Watchdog Timeout) — wersja 2.0

## 1. Dekodowanie backtrace

```
Backtrace:
esp_crosscore_isr
  → _xt_lowint1
    → _xtos_set_intlevel (ROM)
      → vPortClearInterruptMaskFromISR
        → vPortExitCritical
          → vPortExitCriticalSafe
            → multi_heap_internal_unlock
              → multi_heap_malloc_impl
                → heap_caps_aligned_alloc_base
                  → heap_caps_malloc_base
                    → heap_caps_malloc_default
                      → malloc
                        → lv_malloc_core (clib)
                          → lv_malloc
                            → buf_malloc (draw_buf.c:563)
                              → draw_buf_malloc
                                → lv_draw_buf_create_ex
                                  → lv_draw_buf_create
                                    → lv_draw_layer_alloc_buf (draw.c:501)
                                      → dispatch (lv_draw_sw.c:291)
                                        → lv_draw_dispatch_layer
                                          → lv_draw_dispatch
                                            → draw_buf_flush
                                              → refr_invalid_areas
                                                → lv_display_refr_timer
                                                  → lv_timer_exec
                                                    → lv_timer_handler
                                                      → lvgl_port_task
```

**KRYTYCZNA LINIA backtrace:**
```
multi_heap_malloc_impl  ← alokator heap-a ESP-IDF
  ↓
lv_draw_layer_alloc_buf ← alokacja bufora dla warstwy LVGL
  ↓
dispatch (lv_draw_sw.c:291) ← silnik SW draw dispatch
```

**WATCHDOG ZOSTAJE WYWOLANY WEWNĄTRZ malloc.**

To nie jest błąd w kodzie LVGL ani aplikacji. To alokator pamięci nie może znaleźć ciągłego bloku odpowiedniego rozmiaru i przeszukuje/listę/scala pamięć zbyt długo (>5s).

---

## 2. Dlaczego zmiana SIMPLE_BUF_SIZE na 131072 NIE POMOGŁA

`CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE` kontroluje rozmiar pojedynczego chunka warstwy. Po zmianie z 786432 (768 KB) na 131072 (128 KB):

| Parametr | Przed | Po |
|---|---|---|
| SIMPLE_BUF_SIZE | 786432 (768 KB) | 131072 (128 KB) |
| max_rgb_row_height | 491 wierszy (cały ekran) | 81 wierszy |
| max_argb_row_height (z alpha) | 245 wierszy | 40 wierszy |
| Rozmiar alokacji per chunk (ARGB) | ~768 KB | ~128 KB |

**128 KB ciągłego PSRAM to wciąż za dużo dla mocno sfragmentowanej pamięci.**

Przyczyna: 128 KB to nie jest mało, gdy PSRAM wygląda jak szwajcarski ser.

---

## 3. Prawdziwe źródło fragmentacji — "Seryjny Zabójca NR 1"

`CONFIG_LV_CACHE_DEF_SIZE=1048576` — **1 MB cache obrazów LVGL**

### Co robi ten cache:
- Przechowuje zdekodowane dane pikseli obrazów (PNG, JPG, wpisane w kod)
- Każdy obraz po zdekodowaniu zajmuje: `width × height × bytes_per_pixel`
- Przykład: ikona 64×64 w ARGB8888 = 64×64×4 = **16 KB**
- Gdy cache się zapełni (1 MB), usuwa najstarsze obrazy — **Zwalnia ich pamięć**
- Nowe obrazy są dekodowane i dodawane — **Alokują nową pamięć**

### Efekt na PSRAM:
```
Czas:     │ Obraz A (16K) │ Obraz B (16K) │ Obraz C (16K) │ … │ 64 obrazów po 16K
Alokacje: │ A1 A2 A3 … A64 │ (1 MB zapełniony)
Zwolnienia gdy nowy obraz wchodzi:
          │ Zwalnia A1 → Alokuje A65
          │ Zwalnia A2 → Alokuje A66
          │ ...
```

Po pewnym czasie PSRAM zawiera SETKI małych, poprzeplatanych bloków alokowanych i zwolnionych. To klasyczna fragmentacja zewnętrzna.

### Dowód:
```
free PSRAM:  ~5 MB
max free block:  ???  (prawdopodobnie < 128 KB)
```

Mimo że jest 5 MB wolnego PSRAM, największy CIĄGŁY blok może mieć tylko kilkadziesiąt KB.

---

## 4. Drugi zabójca — Warstwy ARGB8888 dla skalowanych obiektów

Gdy przycisk jest skalowany (`transform_scale_x/y` ≠ 256), LVGL:
1. Renderuje przycisk do WARSTWY (layer)
2. Warstwa wymaga formatu ARGB8888 (4 bajty/piksel) — dla alfa blendowania przy transformacji
3. Rozmiar warstwy: 800 × 40 × 4 = **128 KB** (z SIMPLE_BUF_SIZE=131072)

**128 KB × 12 chunków = 1.5 MB dla pełnego ekranu z efektem skalowania.**

Każdy chunk wymaga CIĄGŁEGO bloku 128 KB w PSRAM. Przy fragmencie na poziomie 16-32 KB, alokacja 128 KB wymaga:
- Skanowania długiej listy wolnych bloków
- Scalania sąsiednich wolnych bloków (coalescing)
- To trwa milisekundy... ale na PSRAM (80 MHz, ~125 ns latency) × tysiące bloków = **sekundy**

---

## 5. Dlaczego problem występuje tylko przy skalowaniu

Bez skalowania:
- Renderowanie idzie prosto do framebuffera (direct_mode)
- Żadnych warstw, żadnych dodatkowych alokacji — szybko i stabilnie

Ze skalowaniem:
- LVGL tworzy WARSTWĘ dla każdego skalowanego obiektu
- Warstwy wymagają ARGB8888 (×2 więcej pamięci niż RGB565)
- Alokacja 128 KB w sfragmentowanym PSRAM → WDT

---

## 6. Plan naprawy (3 kroki)

### Krok 1: Zmniejsz LV_CACHE_DEF_SIZE z 1 MB do 64 KB

W `sdkconfig.defaults`:
```diff
- CONFIG_LV_CACHE_DEF_SIZE=1048576
+ CONFIG_LV_CACHE_DEF_SIZE=65536
```

Efekt:
- Cache pomieści ~4 ikony 64×64 zamiast ~64
- Mniej równoczesnych alokacji → mniejsza fragmentacja
- Obrazy będą częściej dekodowane od nowa, ale dekodowanie jest szybsze niż szukanie 5 sekund w pamięci

### Krok 2: Zmniejsz SIMPLE_BUF_SIZE do 32768 (32 KB)

W `sdkconfig.defaults`:
```diff
- CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=131072
+ CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=32768
```

Efekt:
- Per chunk: 32768 / (800 × 4) = 10 wierszy
- Alokacja per chunk: 800 × 10 × 4 = **32 KB**
- Znacznie łatwiej znaleźć 32 KB ciągłego bloku w sfragmentowanym PSRAM
- Więcej chunków (48 zamiast 12), ale każdy trywialny do zaalokowania

### Krok 3: (Opcjonalnie) Routing alokacji warstw przez dedykowany allocator

W `main.cpp` po inicjalizacji LVGL:
```c
#include "lvgl.h"

// Custom allocator dla warstw (zawsze PSRAM, pomija cache fragmentację)
static void * layer_malloc(size_t size, lv_color_format_t cf) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}
static void layer_free(void * buf) {
    heap_caps_free(buf);
}

void my_lvgl_post_init() {
    lv_draw_buf_handlers_t * handlers = lv_draw_buf_get_handlers();
    handlers->buf_malloc_cb = layer_malloc;
    handlers->buf_free_cb = layer_free;
}
```

---

## 7. Podsumowanie

| Przyczyna | Waga | Opis |
|---|---|---|
| LV_CACHE_DEF_SIZE=1048576 (1 MB) | 🔴 KRYTYCZNA | Główny źródło fragmentacji PSRAM |
| SIMPLE_BUF_SIZE=786432 (768 KB) | 🔴 KRYTYCZNA | 768 KB alokacji w sfrag. PSRAM → WDT |
| Skalowanie → ARGB8888 warstwy | 🟡 ŚREDNIA | ×2 pamięci vs RGB565 |
| Brak watchdog feed w renderowaniu | 🟢 NISKA | LVGL nie karmi WDT podczas długiego rysowania |

**Kolejność naprawy:**
1. `LV_CACHE_DEF_SIZE=65536` — usuwa główne źródło fragmentacji
2. `SIMPLE_BUF_SIZE=32768` — zmniejsza rozmiar alokacji do łatwego do znalezienia 32 KB
3. Opcjonalnie: custom allocator dla warstw

Po tych zmianach skalowane przyciski w menu będą działać bez WDT, bo każda pojedyncza alokacja (max 32 KB) znajdzie ciągły blok w ułamku milisekundy.
