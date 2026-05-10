# Fragmentacja PSRAM i SRAM — analiza problemu

## Architektura pamięci (dwa sterty)

ESP32-S3 ma ~512 KB SRAM (wewnętrznej) i 8 MB PSRAM (zewnętrznej). Konfiguracja w `sdkconfig.defaults` + `sdkconfig`:

```
CONFIG_SPIRAM_USE_MALLOC=y                  # malloc() może zwracać PSRAM
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384   # próg: ≤16384B → SRAM, >16384B → PSRAM
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768 # 32 KB SRAM zarezerwowane dla malloc
```

**Konsekwencja:** istnieją **dwie osobne sterty**, a próg 16384 bajtów decyduje, na której ląduje alokacja. Wszystkie małe alokacje (Value, StringRef, krótkie stringi, małe obiekty LVGL) idą do **SRAM**. Duże (LVGL cache 1 MB, draw buffer 240 KB, ramki VRAM) idą do **PSRAM**.

Ponieważ `CONFIG_LV_USE_CLIB_MALLOC=y`, LVGL nie używa własnego pool-a, tylko deleguje do `malloc`/`free` — **wszystko konkuruje na tych samych dwóch stertach**.

---

## Miejsca występowania fragmentacji

### 1. `lv_label_set_text()` w tickach — 24+ wywołań na cykl (screens.c)

**Plik:** `main/ui/ui/screens.c` (funkcje `tick_screen_*`)

Każde wywołanie `lv_label_set_text(label, new_val)` powoduje:
- `free(stary_tekst)` — zwalnia poprzednio skopiowany string
- `strdup(new_val)` → `malloc(strlen+1)` — alokuje kopię nowego tekstu

Tick głównego ekranu (`tick_screen_main`, linie 1047-1230) aktualizuje 24 etykiety: ciśnienie, temperaturę wewnętrzną/zewnętrzną, wilgotność, PM1/PM2.5/PM10, datę, fazę księżyca, wiatr itd. Typowe rozmiary stringów: `"32.3 C"` (7 B), `"PM1 5"` (7 B), `"1013hPa"` (10 B), `"---"` (4 B). Są to małe, krótkożyciowe alokacje na SRAM.

**Ryzyko: EKSTREMALNE** — ~24 cykli alloc/free na tick (co ~100 ms). Zmienne rozmiary stringów powodują powstawanie trudnych do załatania dziur.

### 2. `Value::makeStringRef()` i `setGlobalVariable()` — 2 alokacje na wywołanie

**Pliki:** `main/eez-framework/src/eez/core/value.cpp:1378`, `main/communication/CommUiMapper.cpp`

Każde `setGlobalVariable(nazwa, wartość)` przechodzi przez `Value::makeStringRef()`:
1. `ObjectAllocator<StringRef>::allocate()` — alokuje ~16 B na strukturę StringRef
2. `alloc(len+1)` — alokuje kopię danych stringa (zmienny rozmiar)

Przy zwalnianiu (gdy refcounter spadnie do 0) obie alokacje są zwalniane, ale **nie w odwrotnej kolejności** (najpierw StringRef, potem dane). To zwiększa ryzywo fragmentation.

**Call chain:** `CommUiMapper.cpp` → ~64 `setGlobalVariable` przy inicjalizacji + ~20 przy każdej aktualizacji danych + ~5/sek z `clock_logic.cpp`.

**Ryzyko: EKSTREMALNE** — największa częstotliwość alokacji w projekcie. Każda aktualizacja danych z czujników generuje ~20 × 2 = 40 alokacji na SRAM.

### 3. `utf8dup_ex()` — alokacja stringów w silniku flow

**Pliki:** `main/eez-framework/src/eez/libs/utf8.h:487`, `main/ui/ui/eez-flow.h:3193`

```c
n = (utf8_int8_t *)malloc(bytes);
```

Alokacja kopii stringa o dowolnym rozmiarze. Wywoływana przy każdej operacji na stringach w EEZ Flow — konwersje, konkatenacje, przypisania.

**Ryzyko: DUŻE** — zmienne rozmiary stringów, krótki czas życia (są zaraz zwalniane), duża churn.

### 4. Log: `lv_textarea_set_text()` z 8 KB buforem

**Plik:** `main/utils/log/log.cpp:145`

```c
lv_textarea_set_text(objects.log_terminal, log_buffer);  // log_buffer = 8192 B
```

Co ~500 ms (gdy ekran loga jest widoczny) alokowane jest **8 KB na stercie SRAM**. Gdy log się zmienia, stare 8 KB jest zwalniane, nowe 8 KB alokowane. To jest **duża alokacja** na stercie, gdzie jednocześnie trwa ciągła churn małych stringów (punkty 1-3).

**Ryzyko: DUŻE** — duży blok (8 KB) rozrywa ciągłą wolną przestrzeń na małe dziury. Po kilkunastu cyklach sterta ma dziurę 8 KB zamiast 8 KB ciągłego bloku.

### 5. FlowState — alokacja o zmiennym rozmiarze

**Plik:** `main/eez-framework/src/eez/flow/private.cpp:165-172`

```c
sizeof(FlowState) + nValues * sizeof(Value) +
    flow->components.count * sizeof(ComponentExecutionState*) +
    flow->components.count * sizeof(bool)
```

Każdy ekran dostaje jeden FlowState — alokowany raz, zwalniany przy przejściu do innego ekranu. Zmienny rozmiar (zależy od liczby komponentów/widgetów na ekranie).

**Ryzyko: ŚREDNIE** — niska częstotliwość alokacji, ale po zwolnieniu zostawia dziurę o specyficznym rozmiarze, która może być trudna do wypełnienia (prawdopodobnie inny ekran ma inną liczbę komponentów).

### 6. Obiektowy alokator EEZ (alloc/free) — brak podziału na size class

**Plik:** `main/eez-framework/src/eez/core/alloc.cpp:38-53`

Wszystkie alokacje frameworka EEZ (łącznie z Value, obiektami flow, właściwościami widgetów) przechodzą przez `lv_malloc()` → `malloc()`. Nie ma podziału na pule rozmiarów (size classes). Alokacje 4-bajtowe i 400-bajtowe konkurują na tym samym obszarze.

W kodzie jest alternatywny alokator blokowy (`AllocBlock`, linia 132-177) z TODO: `// TODO merge multiple free consecutive blocks into one that has enough size`, ale **nie jest używany** (`EEZ_FOR_LVGL` jest zdefiniowane, więc kompiluje się ścieżka z `lv_malloc`).

**Ryzyko: DUŻE** — brak segregacji rozmiarów powoduje, że małe alokacje wypełniają wolne przestrzenie między większymi, tworząc nieużyteczne mikro-dziury.

### 7. LVGL draw layer buffer (240 KB)

**sdkconfig:** `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=245760`

240 KB alokowane (na PSRAM, bo >16384 B) przy każdym rysowaniu ekranu. Gdy rysowanie się kończy, bufor jest zwalniany. Przy dynamicznych ekranach (częste odświeżanie) to cykl alokuj-zwolnij dużego bloku na PSRAM.

**Ryzyko: ŚREDNIE** — na PSRAM, który ma 8 MB, więc ryzyko jest mniejsze niż na SRAM. Ale przy bardzo długim czasie działania (dni/tygodnie) i wielu cyklach, PSRAM też fragmentuje.

---

## Dlaczego to jest problem na ESP32-S3

| Czynnik | Wpływ |
|---------|-------|
| SRAM ~300-350 KB dostępne | Mało pamięci = fragmentacja szybciej prowadzi do failed alloc |
| Próg 16384 B | Wszystkie małe alokacje (stringi, Value) tłoczą się na SRAM |
| Brak pool allocatora | malloc/free ogólnego przeznaczenia nie radzi sobie z mieszanką rozmiarów |
| LVGL + EEZ na tym samym heap | Obiekty długożyciowe (widgety, style) mieszają się z krótkożyciowymi (stringi) |
| 24+ labeli/tick | Wysoka częstotliwość alloc/free małych bloków |
| 8 KB log okresowo | Duży blok rozrywający ciągłość na SRAM |

---

## Symptomy fragmentacji w praktyce

1. `malloc` zwraca NULL mimo że `heap_caps_get_free_size()` pokazuje wystarczająco dużo wolnej pamięci (ale brak ciągłego bloku)
2. Watchdog reset po długim czasie działania (alokacja czegoś, co wcześniej działało)
3. Coraz wolniejsze działanie UI (LVGL musi defragmentować wewnętrznie lub alokacje trwają dłużej)
4. Dziwne artefakty na ekranie (alokacja bufora rysowania się nie powiodła)

---

## Rekomendacje

### Krótkoterminowe (łatwe do wdrożenia)

1. **Dodać monitorowanie fragmentacji** — cykliczny odczyt `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` i logowanie ostrzeżenia jeśli największy wolny blok spadnie poniżej progu (np. 16 KB). To pozwoli wykryć problem zanim wystąpi crash.

2. **Zwiększyć `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`** z 32768 do 65536 — da to więcej SRAM na małe alokacje, opóźniając fragmentację.

3. **Użyć `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` dla log textarea** — 8 KB logu > 16384? Nie, to tylko 8192, więc ląduje na SRAM. Można wymusić PSRAM dla bufora loga.

### Długoterminowe

4. **Pool allocator dla StringRef i Value** — te struktury mają stały rozmiar (~16 B). Osobna pula na nie wyeliminowałaby mieszanie z variadic-size alloc.

5. **Zmniejszyć częstotliwość tick** — jeśli dane z czujników zmieniają się co 1-2 sekundy, nie ma potrzeby odświeżać labeli co 100 ms.

6. **Użyć `lv_label_set_text_static()` z własnym buforem** — zamiast pozwalać LVGL alokować kopię stringa, utrzymywać własny bufor (statyczny) i używać tekstu statycznego. Wtedy `lv_label_set_text_static` nie alokuje.

7. **Defragmentacja przez `heap_caps_get_info()`** — okresowo sprawdzać stan sterty i logować liczbę wolnych bloków. Jeśli liczba wolnych bloków jest duża przy małym największym bloku → fragmentacja.
