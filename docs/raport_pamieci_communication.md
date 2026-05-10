# Raport Zarządzania Pamięcią — Moduł `communication/`

**Projekt:** ESP32-S3-JC8048W550-LVGL-ESPIDF-EEZ  
**Data:** 2026-05-08  
**Zakres:** `main/communication/` + `main/utils/comm_log/` + `main/app/safe_label.h`  
**Płytka:** ESP32-S3 (8 MB Octal PSRAM, 512 KB SRAM)

---

## 1. Architektura Pamięci ESP32-S3

```
┌──────────────────────────────────────────────────────────────┐
│  SRAM (512 KB)                                                │
│  ├─ .text (code cache)                    ~180 KB             │
│  ├─ .rodata (constant data)               ~80 KB              │
│  ├─ .data (init'd globals)                ~10 KB              │
│  ├─ .bss (zero-init'd globals)            ~30 KB              │
│  ├─ Heap (malloc/free)                    ~32 KB (reserved)   │
│  ├─ FreeRTOS kernel objects               ~8 KB               │
│  ├─ UART DMA ring buffers                 ~4 KB               │
│  ├─ Task stacks (5-8 tasks)               ~40 KB              │
│  └─ Free internal SRAM                    ~128 KB             │
├──────────────────────────────────────────────────────────────┤
│  PSRAM (8 MB, cached via Cache bus)                           │
│  ├─ LVGL framebuffers (2× 800×480×2)      ~1.5 MB             │
│  ├─ LVGL draw buffer layer                ~2.2 MB             │
│  ├─ LVGL cache (images, fonts)            ~1 MB               │
│  ├─ WiFi/LWIP heap                        ~1 MB               │
│  ├─ Malloc heap (>=16 KB allocs)          ~2 MB               │
│  └─ Free PSRAM                            ~0.3 MB             │
└──────────────────────────────────────────────────────────────┘
```

**Konfiguracja** (`CONFIG_SPIRAM_USE_MALLOC=y`):  
- `malloc()` → PSRAM (dla allocacji ≥ 16 KB)  
- Allokacje < 16 KB → SRAM (internal)  
- 32 KB rezerwy SRAM dla małych alloc (`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`)  
- WiFi/LWIP → PSRAM (`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`)  

---

## 2. Analiza Statyczna — Moduł Communication

### 2.1. BSS / Data (alokacje globalne)

| Plik | Symbol | Typ | Rozmiar | Lokalizacja |
|------|--------|-----|---------|-------------|
| `CommunicationState.cpp` | `s_mutex` | `SemaphoreHandle_t` | 4 B | SRAM (ISR-safe) |
| `CommunicationState.cpp` | `s_snapshot` | `Snapshot` | ~56 B | SRAM |
| `CommunicationState.cpp` | `s_outdoorWeather` | `OutdoorWeatherSnapshot` | ~96 B | SRAM |
| `CommunicationState.cpp` | `s_deviceSettings` | `DeviceSettings` | ~12 B | SRAM |
| `CommunicationState.cpp` | `s_systemResources` | `SystemResources` | ~32 B | SRAM |
| `CommunicationState.cpp` | `s_dirty` (×5) | `volatile bool` | 5 B | SRAM |
| `CommunicationState.cpp` | `s_peerBootIdLocal` | `uint32_t` | 4 B | SRAM |
| `CommunicationState.cpp` | `s_lastFrameRxMsLocal` | `uint32_t` | 4 B | SRAM |
| `CommHandshake.cpp` | `s_bootId` | `uint32_t` | 4 B | SRAM |
| `CommHandshake.cpp` | `s_syncState` | `PeerSyncState` | 4 B | SRAM |
| `CommHandshake.cpp` | `s_lastHelloSentMs` | `uint32_t` | 4 B | SRAM |
| `CommHandshake.cpp` | `s_lastFrameRxMs` | `uint32_t` | 4 B | SRAM |
| `UartTransport.cpp` | `s_parser` | `FrameParser` (buf[256] + size) | ~264 B | SRAM |
| `UartTransport.cpp` | `s_taskHandle` | `TaskHandle_t` | 4 B | SRAM |
| `UartTransport.cpp` | `s_started` | `volatile bool` | 1 B | SRAM |
| `comm_log.cpp` | `s_state` | `{frame_counts_t×2, int64, bool}` | ~56 B | SRAM |

**Suma SRAM (BSS): ~560 B** — akceptowalne, ale `s_parser.buffer[256]` to największy pojedynczy kawałek.

---

### 2.2. Stack (alokacje tymczasowe w wywołaniach)

#### Funkcja: `UartTransport::sendRawFrame()` — **największy stack footprint (~412 B)**

```cpp
// BEFORE (obecnie) — 3 osobne bufory na stacku:
uint8_t frame[4 + kMaxPayloadBytes + 2];    // 134 B (linia 143)
uint8_t cobsBuffer[sizeof(frame) + 4];      // 138 B (linia 159)
uint8_t txBuffer[sizeof(cobsBuffer) + 2];   // 140 B (linia 162)
// Całkowity stack: 134 + 138 + 140 = ~412 B
```

**Problem:** Każdy z trzech buforów jest używany sekwencyjnie — nie ma potrzeby trzymać ich wszystkich jednocześnie na stacku. Po zakodowaniu COBS, `frame` nie jest już potrzebny. Po dodaniu delimiterów, `cobsBuffer` nie jest już potrzebny.

#### Funkcja: `UartTransport::ingestByte()` — **256 B na stacku**

```cpp
// BEFORE: alokacja na każdym 0x00 delimiterze
uint8_t decodedBuffer[256];   // linia 174
```

**Problem:** `decodedBuffer[256]` alokowany za każdym razem gdy pojawi się `0x00`. Chociaż parser opróżnia bufor, to 256 B na stack przy każdym frame to sporo.

#### Funkcja: `UartTransport::communicationTask()` — **128 B na stacku**

```cpp
uint8_t buffer[128];    // linia 86
```

**Problem:** 128 B odczytywane za każdym tickiem (50 ms). Zazwyczaj tylko 1-30 B przychodzi. Można zmniejszyć do 64 B.

#### Funkcja: `comm_log_flush_now()` — **~512 B na stacku**

```cpp
char tx_buf[256];    // linia 74
char rx_buf[256];    // linia 75
// Razem: 512 B
```

**Problem:** `comm_log_flush_if_needed()` wołane z pętli UI (tick 100 ms). 512 B stack przy logowaniu co 5 sekund. To największy stack footprint w całym module — wywoływany z kontekstu LVGL tickera!

---

### 2.3. Statyczne zmienne lokalne (static locals)

W `CommUiMapper.cpp` — wzorzec `static char buf[24/16]` w funkcjach:

```cpp
void applySnapshotToMainScreen(const Snapshot &snapshot) {
    static char temperatureBuffer[24];   // BSS (nie stack)
    static char humidityBuffer[16];
    static char pressureBuffer[16];
    static char pm01Buffer[16];
    static char pm10Buffer[16];
    static char pm25Buffer[16];
    static char co2Buffer[16];
    static char airQualityTextBuffer[24];
    // Suma BSS: ~136 B
}
```

**Zaleta:** Zero stack usage dla tych buforów.  
**Wada:** Zajęte na stałe w BSS. Ale to OK — bufory są małe i zawsze potrzebne.

Analogicznie w `applyOutdoorSnapshotToMainScreen`, `applySnapshotToWeatherScreen`, `applyResourcesToUi`.

**Suma BSS dla static locals w CommUiMapper: ~6 × 136 B = ~200 B** (ale tylko jeden zestaw aktywny na raz).

---

### 2.4. Porównanie: Zegar vs Guition

| Aspekt | Zegar (Arduino) | Guition (ESP-IDF) | Uwagi |
|--------|-----------------|-------------------|-------|
| CRC16 | Lookup table (512 B flash) | Bitwise (0 B flash, wolniejsze) | Zegar szybszy, Guition oszczędniejszy flash |
| TX buffer guard | `availableForWrite()` check | `uart_get_tx_buffer_free_size` | Po ostatnim fixie: oba mają |
| TX buffer size | 1024 B (Arduino `setTxBufferSize`) | 1024 B (ring buffer) | Po ostatnim fixie: zgodne |
| Stack frame build | `uint8_t buffer[32]+static` | `frame[134]+cobsBuffer[138]+txBuffer[140]` | **Guition: 3× stack, do optymalizacji** |
| Serialization helpers | `appendU8/U16/U32` helpers | Ręczne `memcpy` / inline | Wzór Zegara czystszy |

---

## 3. Wykryte Problemy i Ryzyka

### 🔴 P1 — Nadmiarowy stack w `sendRawFrame()` (~412 B)

`UartTransport.cpp:143-162` — trzy bufory na stacku, które żyją jednocześnie:

```
frame[134]  ←  cobsBuffer[138]  ←  txBuffer[140]
    ca. 134 B      ca. 138 B         ca. 140 B
    ────────────────────────────────────────────
    Całkowity stack: ~412 B
```

**Ryzyko:** Stack taska `comm_uart1` to 4096 B. 412 B to ~10% stacku na jedną funkcję. W skrajnym przypadku (zagnieżdżone wywołania) może doprowadzić do stack overflow. LVGL ticker task (`ui_tick_timer_cb` → `updateUi()` → `sendRawFrame`) ma podobny problem.

**Fix:** Użyć jednego bufora z offsetami lub alokować COBS buffer tylko po zbudowaniu frame.

### 🔴 P2 — Stack `comm_log_flush_now()` 512 B z LVGL tickera

`comm_log.cpp:74-75`:
```cpp
char tx_buf[256];   // stack
char rx_buf[256];   // stack
```

Wywołane z `ui_tick_timer_cb()` → `log_update_textarea()` → `comm_log_flush_if_needed()`. LVGL task stack bywa ~4096 B, ale 512 B na formatowanie stringów to dużo.

**Ryzyko:** Stack overflow w LVGL task (który już ma LVGL draw stack + framebuffer operations).

**Fix:** Użyć `static char buf[256]` lub zmniejszyć do 128 B z krótszym formatem.

### 🟡 P3 — Duplikacja `decodedBuffer` na stacku w `ingestByte()`

`UartTransport.cpp:174`:
```cpp
uint8_t decodedBuffer[256];   // stack
```

Każdy frame (nawet małe HELLO = 11 B payload = ~30 B COBS) alokuje 256 B na stack. Może to być zastąpione przez współdzielony bufor.

**Fix:** Użyć `s_parser.buffer` jako bufor wyjściowy COBS decode (in-place decode jest możliwy, bo COBS decoded data ≤ encoded data — poza ostatnim bajtem).

### 🟡 P4 — CRC16 bitwise vs lookup table

`CobsFraming.cpp:5-17` — CRC16 bitwise dla każdej ramki:
```cpp
for (size_t i = 0; i < length; ++i) {
    crc ^= data[i] << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) { ... }
}
```

Dla ramki 134 B: 134 × 8 = 1072 iteracje wewnętrznej pętli. Zegar używa lookup table (512 B flash, ~8× szybszy).

**Wpływ:** Nie wpływa na SRAM, ale marnowanie czasu CPU na Core 0 (ten sam rdzeń co UART RX task). Wysyłanie ramki 32 B to ~256 bit-loop = drobne opóźnienie.

**Fix:** Opcjonalnie — dodać lookup table jeśli CPU load na Core 0 jest problemem.

### 🟢 P5 — `safe_label.h` malloc(64) per label

`safe_label.h:15`:
```cpp
my_buf = (char *)malloc(64);
```

**Zaleta:** Eliminuje fragmentację SRAM — jedna alloc na label, potem `lv_label_set_text_static()` bez realokacji.  
**Koszt:** 64 B × liczba labeli (~30-50) = ~2-3.2 KB w PSRAM (bo malloc ≥ 16 KB → PSRAM, ale 64 B < 16 KB → **SRAM**!).  

**Ryzyko:** Każdy `malloc(64)` idzie do SRAM (bo < 16384 B). Przy 50 labelach: 50 × 64 = 3200 B SRAM. Z rezerwą 32 KB to OK, ale warto wiedzieć.

**Rekomendacja:** Brak zmian — to celowy kompromis dla eliminacji fragmentacji. Ale warto zmierzyć ile labeli faktycznie istnieje.

---

## 4. Proponowane Refaktoryzacje

### 4.1. `sendRawFrame()` — jeden bufor zamiast trzech

**BEFORE (stack: ~412 B):**
```cpp
void sendRawFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength) {
  uint8_t frame[4 + kMaxPayloadBytes + 2] = {};     // 134 B
  // ... build frame ...
  uint8_t cobsBuffer[sizeof(frame) + 4] = {};       // 138 B
  size_t cobsLen = cobsEncode(frame, cursor - frame, cobsBuffer);
  uint8_t txBuffer[sizeof(cobsBuffer) + 2] = {};    // 140 B
  txBuffer[0] = 0x00;
  memcpy(txBuffer + 1, cobsBuffer, cobsLen);
  txBuffer[1 + cobsLen] = 0x00;
  uart_write_bytes(kUartPort, txBuffer, cobsLen + 2);
}
```

**AFTER (stack: ~142 B):**
```cpp
void sendRawFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength) {
  // Jeden bufor: frame (max 134 B) + COBS overhead (max ~138 B) + 2 delimitery = ~274 B
  // Używamy go dwuetapowo:
  uint8_t workbuf[4 + kMaxPayloadBytes + 2 + 4];    // ~138 B
  
  // Etap 1: build raw frame na początku bufora
  uint8_t *cursor = workbuf;
  *cursor++ = type; *cursor++ = sequence;
  *cursor++ = payloadLength & 0xFF; *cursor++ = (payloadLength >> 8) & 0xFF;
  if (payloadLength > 0 && payload != nullptr) {
    memcpy(cursor, payload, payloadLength); cursor += payloadLength;
  }
  uint16_t crc = crc16Ccitt(workbuf, 4U + payloadLength);
  *cursor++ = crc & 0xFF; *cursor++ = (crc >> 8) & 0xFF;
  const size_t rawLen = cursor - workbuf;
  
  // Etap 2: COBS encode in-place (przesuwamy się za raw data)
  uint8_t *cobsStart = workbuf + rawLen;
  size_t cobsLen = cobsEncode(workbuf, rawLen, cobsStart);
  
  // Etap 3: wrap z delimiterami — bez kopii, write w dwóch częściach
  const uint8_t delim = 0x00;
  uart_write_bytes(kUartPort, &delim, 1);
  uart_write_bytes(kUartPort, cobsStart, cobsLen);
  uart_write_bytes(kUartPort, &delim, 1);
}
```

**Zysk:** Stack 412 B → ~142 B (redukcja o ~270 B). Dodatkowo eliminacja `memcpy` delimiterów.

### 4.2. `comm_log_flush_now()` — static bufory

**BEFORE (stack: 512 B):**
```cpp
void comm_log_flush_now(void) {
    char tx_buf[256] = {};
    char rx_buf[256] = {};
    // ...
}
```

**AFTER (stack: ~0 B, BSS: 512 B):**
```cpp
void comm_log_flush_now(void) {
    static char tx_buf[256];
    static char rx_buf[256];
    tx_buf[0] = '\0';
    rx_buf[0] = '\0';
    // ...
}
```

**Zysk:** Eliminacja 512 B stack usage z LVGL tickera. Koszt: 512 B BSS (już w SRAM).

### 4.3. `ingestByte()` — reuse parser bufora

**BEFORE (stack: 256 B per frame):**
```cpp
bool ingestByte(uint8_t byte) {
    // ...
    uint8_t decodedBuffer[256];
    size_t decodedLen = cobsDecode(s_parser.buffer, s_parser.length, decodedBuffer);
    // ...
}
```

**AFTER (stack: 0 B, zero-copy):**
```cpp
// COBS in-place decode: decoded ≤ encoded zawsze
// Używamy s_parser.buffer jako wyjścia po dekodowaniu
size_t ingestByte(uint8_t byte) {
    // ...
    // COBS in-place: rozmiar dekodowany ≤ rozmiar zakodowany
    size_t decodedLen = cobsDecode(s_parser.buffer, s_parser.length, s_parser.buffer);
    // ...
}
```

**Uwaga:** Wymaga modyfikacji `cobsDecode` aby wspierał in-place (obecnie nie wspiera, bo write_index może wyprzedzić read_index dla pierwszych bajtów, ale for normal cases it works since output <= input). 

Bezpieczniejsza alternatywa:
```cpp
// Współdzielony bufor static dla dekodowania
static uint8_t s_decodedBuffer[256];
size_t decodedLen = cobsDecode(s_parser.buffer, s_parser.length, s_decodedBuffer);
```

**Zysk:** Stack: 256 B → 0 B (przeniesione do BSS). Zero-copy przy in-place.

---

## 5. Mapa Pamięci — Cel Architektury

### Co POWINNO być w SRAM:

| Dane | Powód |
|------|-------|
| Mutexy / semafory | ISR-safe, FreeRTOS wymaga SRAM |
| Task handles | FreeRTOS API |
| Volatile flags (`s_dirty`) | ISR-safe, atomic access |
| Parser state (`s_parser.buffer`) | Hot path — dostęp co 50 ms z UART task |
| `s_snapshot`, `s_outdoorWeather` | Odczytywane z LVGL task (Core 1) i zapisywane z UART task (Core 0) — SRAM zapewnia brak stall na cache miss |
| UART DMA ring buffers | Wymagane przez hardware DMA (alokowane przez `uart_driver_install`) |
| Stacki tasków | Wymagane FreeRTOS |

### Co MOŻE być w PSRAM:

| Dane | Powód |
|------|-------|
| `comm_log_flush_now` bufory | Logowanie co 5 s, niehot path |
| `decodedBuffer` (jeśli static) | Tylko UART task, może znieść PSRAM latency |
| `FrameParser.buffer` | Opcjonalnie — przenieść jeśli SRAM jest krytyczny |

### Co MUSI być w PSRAM:

| Dane | Powód |
|------|-------|
| LVGL framebuffers | 1.5 MB — nie mieści się w SRAM |
| LVGL draw buffer (2.2 MB) | Nie mieści się w SRAM |
| Duże alokacje malloc (>16 KB) | `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` |

---

## 6. Memory Pools — Analiza

Obecnie **brak memory pools** w module komunikacji. Nie ma to większego znaczenia, bo:

1. **Brak dynamicznych alokacji** w hot path — wszystkie bufory są static lub stack.
2. **Brak transient alloc/free** — nie ma ryzyka fragmentacji.
3. **safe_label.h** to jedyny user `malloc()`, ale per-label-once, nie w pętli.

**Rekomendacja:** Nie dodawać memory pools dla komunikacji. Gdyby w przyszłości pojawiły się dynamiczne bufory (np. queue frame), rozważyć `StaticQueue` z FreeRTOS.

---

## 7. DMA Compatibility

UART na ESP32-S3 używa **internal DMA** (GDMA). Wymagania:
- Bufory DMA muszą być w **SRAM** (nie w PSRAM) — chyba że PSRAM jest w trybie `CONFIG_SPIRAM_XIP_FROM_PSRAM`, ale to niezalecane dla DMA.
- Bufory nie muszą być cache-aligned dla UART (w przeciwieństwie do I2S/SPI).

**Status:** Obecnie wszystkie bufory UART (alokowane przez `uart_driver_install`) są w SRAM — OK.  
Brak własnych buforów DMA w kodzie komunikacji — OK.

---

## 8. RTOS — Wpływ na Pamięć

| Task | Core | Stack | Prio | Uwagi |
|------|------|-------|------|-------|
| `comm_uart1` | 0 | 4096 B | 4 | RX UART, parsowanie ramek, wywołuje `apply*Payload` |
| `LVGL` (taskLVGL) | 1 | ~6144 B | 4 | UI tick, `updateUi()`, `sendRawFrame()` |
| `main` (app_main) | 0-1 | ~3584 B | 1 | Głównie `vTaskDelay` |
| WiFi/SNTP | 0 | ~4096 B | 3-5 | Background |
| `Tmr Svc` | - | ~2048 B | 1 | FreeRTOS timer |

**Stack rekomendacje po refaktoryzacji:**
- `comm_uart1`: 4096 B → **3072 B** (po redukcji stack footprint w `sendRawFrame` i `ingestByte`) — oszczędność 1 KB SRAM.
- LVGL task: zachować 6144 B (LVGL draw stack może być głodny).

---

## 9. Podsumowanie — Priorytety Optymalizacji

| Priorytet | Problem | Zysk SRAM | Zysk Stack | Koszt zmiany |
|-----------|---------|-----------|------------|--------------|
| **P1** | `sendRawFrame()` 3 bufory → 1 | 0 B | ~270 B | Niski (1 funkcja) |
| **P2** | `comm_log_flush_now()` stack bufory | 0 B (lub +512 B BSS) | ~512 B | Niski (static) |
| **P3** | `ingestByte()` stack decoder | 0 B | ~256 B | Niski (static buffer) |
| **P4** | Stack taska `comm_uart1` za duży | +1 KB | 0 B | Niski (zmniejszyć stack) |
| **P5** | CRC16 lookup table | 0 B | 0 B | Niski (+512 B flash) |

**Łączny zysk stack: ~1 KB** (1024+ B freed z krytycznych ścieżek).

---

## 10. Checklista Dobrych Praktyk

- [x] **Brak `malloc`/`free` w hot path** — komunikacja nie alokuje dynamicznie w send/receive  
- [x] **Brak `new`/`delete`** — zero dynamicznych alokacji C++  
- [x] **Mutex chroni wszystkie struktury współdzielone** — `lockSnapshot()`/`unlockSnapshot()`  
- [x] **Volatile dla flag między-taskowych** — `s_dirty`, `s_started`  
- [ ] **Stack footprint < 25% per task** — **NIE**: `sendRawFrame()` ~412 B z ~4096 B = 10% w jednej funkcji + `ingestByte()` 256 B + `communicationTask` 128 B = zbyt dużo na jeden call chain  
- [x] **Static locals zamiast stack dla buforów string** — CommUiMapper już używa  
- [ ] **COBS in-place decode** — **NIE**: `ingestByte()` alokuje osobny bufor wyjściowy  
- [x] **DMA-safe bufory** — UART ring bufory w SRAM (alokowane przez driver)  
- [x] **Cache alignment** — Nie dotyczy (UART nie wymaga alignment dla danych)  
- [x] **ISR-safe** — Komunikacja nie używa ISR (polling przez task)  

---

## 11. Rekomendacje Końcowe

### Natychmiastowe (niski wysiłek, duży zysk):

1. **`UartTransport::sendRawFrame()`** — scalić 3 bufory w 1 + `uart_write_bytes` w 3 częściach. Stack: 412 B → ~142 B.
2. **`comm_log_flush_now()`** — zmienić `char buf[256]` na `static char buf[256]`. Stack: 512 B → ~0 B.
3. **`UartTransport::ingestByte()`** — użyć `static uint8_t s_decodedBuffer[256]`. Stack: 256 B → ~0 B.

### Krótkoterminowe:

4. Zmniejszyć stack `comm_uart1` task: 4096 B → 3072 B (+1 KB SRAM).
5. Zmniejszyć `communicationTask()` read buffer: 128 B → 64 B.

### Opcjonalne:

6. CRC16 lookup table jeśli Core 0 CPU jest瓶颈 (512 B flash, 8× szybszy).
7. Rozważyć przeniesienie `FrameParser.buffer[256]` do PSRAM jeśli SRAM jest krytyczny (przez `heap_caps_malloc(MALLOC_CAP_SPIRAM)`).

---

*Raport wygenerowany na podstawie kodu źródłowego `main/communication/`.*
