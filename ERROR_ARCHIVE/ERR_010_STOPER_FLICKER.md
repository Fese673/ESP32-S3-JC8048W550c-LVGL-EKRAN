# ERR_010 — Stopwatch flicker: EEZ Flow tick vs lv_timer data-binding conflict

## Metadata
- **ID:** ERR_010
- **Impact:** MEDIUM (visual flickering, stopwatch unusable)
- **Detection:** 2026-05-10
- **Fix version:** Hide EEZ Flow labels, create own labels in stopwatch_ui_init()
- **Tags:** `eez-flow`, `tick-screen`, `data-binding`, `flicker`, `stoper`, `safe-label-conflict`

## Symptom

Stopwatch labels (`minutes_stoper_label`, `seconds_stoper_label`, `ms_stoper_label`) flicker constantly on the zegar screen. The flicker looks like a "second stopwatch" overlapping the real one. Flicker persists on every screen (even non-zegar) at ~10Hz.

## Root Cause

### Warstwa 1: sprzeczne źródła danych

EEZ Flow projekt ma zdefiniowane **zmienne przepływu** (flow variables):

```
minutes_stoper (integer, default 0)
seconds_stoper (integer, default 0)
ms_stoper       (integer, default 0)
```

Etykiety stopera w EEZ Flow Designer są **zbindowane** do tych zmiennych przez wyrażenia. Auto-generowany `tick_screen_zegar()` w `screens.c` co 100ms woła:

```c
const char *new_val = evalTextProperty(flowState, 34, 3, "Failed to evaluate...");
// zwraca "0" (string z integera 0)
lv_label_set_text(objects.minutes_stoper_label, new_val);
```

Jednocześnie nasz `stopwatch_timer_cb` (lv_timer, 20ms) pisze:

```c
snprintf(buf, sizeof(buf), "%02d", minutes);  // "00"
lv_label_set_text(objects.minutes_stoper_label, buf);
```

### Warstwa 2: safe_label.h wykrywa zmianę i przepisuje

`safe_label.h` (wstrzyknięty przez `-include` do wszystkich plików) interceptuje `lv_label_set_text()` i używa `lv_label_set_text_static()` tylko gdy tekst faktywnie się zmienił:

```c
static inline void safe_lv_label_set_text(lv_obj_t *obj, const char *txt) {
    char *my_buf = lv_obj_get_user_data(obj);
    if (my_buf && strcmp(my_buf, txt) != 0) {
        strncpy(my_buf, txt, 63);
        lv_label_set_text_static(obj, my_buf);  // ← zmiana → lv_obj_invalidate()
    }
}
```

Co 100ms:
1. `tick_screen_zegar()` pisze `"0"` → safe_label porównuje z `"00"` (z poprzedniego zapisu) → różne → `lv_label_set_text_static("0")` → **`lv_obj_invalidate()`**
2. Nasz timer (w tej samej klatce LVGL, bo timery są przetwarzane batchowo) pisze `"00"` → safe_label porównuje z `"0"` → różne → `lv_label_set_text_static("00")` → **`lv_obj_invalidate()`**

Oba zapisy wykonują się w ramach jednego `lv_timer_handler()`, ale każdy wywołuje `lv_obj_invalidate()`, co powoduje niepotrzebne przetwarzanie i wizualne migotanie.

### Warstwa 3: "drugi stoper" na każdym ekranie

`tick_screen_zegar()` jest wołany tylko gdy ekran Zegara jest aktywny (`ui_tick()` → `tick_screen(g_currentScreen)` → `tick_screen_funcs[g_currentScreen]()` → tylko aktywny screen). Na innych ekranach tick Zegara NIE chodzi. Mimo to użytkownik widział migotanie na każdym ekranie.

**Przyczyna:** pierwsza wersja fixa nie miała osłony ekranu (`eez_flow_get_current_screen() != SCREEN_ID_ZEGAR`). Timer pisał na wszystkich ekranach → `lv_obj_invalidate()` na ukrytych labelkach → LVGL zbędnie przetwarzał invalidacje → objawiało się to jako migotanie na widocznym ekranie.

Dodanie screen guarda rozwiązało migotanie na innych ekranach, ale na Zegarze dalej był konflikt `"0"` vs `"00"`.

## Pattern

```
EEZ Flow Designer: label text ← flow variable (integer 0)
    → evalTextProperty() zwraca "0"
    → tick_screen_zegar() pisze "0" co 100ms
    → nasz timer pisze "00" co 20ms
    → safe_label.h: "0" != "00" → przepisuje → invalidate
    → konflikt co 100ms → flicker
```

## Fix

Zamiast walczyć z EEZ Flow tick, **całkowicie go omiń:**

```c
void stopwatch_ui_init(void) {
    // 1. Ukryj EEZ Flow etykiety — tick_screen_zegar() pisze do nich, ale są niewidoczne
    lv_obj_add_flag(objects.minutes_stoper_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.seconds_stoper_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.ms_stoper_label, LV_OBJ_FLAG_HIDDEN);

    // 2. Stwórz własne etykiety w tym samym kontenerze, te same pozycje/font/kolor
    lv_obj_t *parent = lv_obj_get_parent(objects.minutes_stoper_label);
    s_min_label = lv_label_create(parent);
    lv_obj_set_pos(s_min_label, 9, 18);
    lv_obj_set_style_text_font(s_min_label, &ui_font_bebas_neue35, 0);
    // ...

    // 3. Timer pisze TYLKO do naszych etykiet — zero konfliktu
    // tick_screen_zegar() pisze do ukrytych EEZ Flow labeli → brak efektu wizualnego
    s_timer = lv_timer_create(stopwatch_timer_cb, 20, NULL);
}
```

## Dlaczego nie zadziałały wcześniejsze fixy

| Fix | Problem |
|-----|---------|
| Screen guard (`SCREEN_ID_ZEGAR`) | Usunął flicker na innych ekranach, ale na Zegarze dalej tick vs timer |
| Timer ordering (20ms < 100ms) | Oba timery i tak wykonują się batchowo w `lv_timer_handler()`; safe_label i tak detectuje zmianę i invaliduje |
| `setGlobalVariable()` | Zmienne stopera to **flow variables** (lokalne przepływu), nie **global variables** (dostępne przez `setGlobalVariable`) |

## Podsumowanie

Jedyny sposób na całkowite wyeliminowanie konfliktu z auto-generowanym tick_screen: **nie używaj tych samych obiektów LVGL**. Ukryj EEZ Flow etykiety, stwórz własne. Tick pisze do niewidzialnych, ty piszesz do swoich — zero interakcji, zero flicker.
