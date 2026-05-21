# Protokół transmisji PPG/BPM — STM32 → ZEGAR-ESP32 → Gution

## 1. Architektura systemu

```
┌──────────────────┐   UART (115200 baud)  ┌──────────────────┐   UART (115200 baud)    ┌─────────────────────┐
│   STM32-BPM      │   binary 0xAA frame   │   ZEGAR-ESP32    │   COBS + CRC16        │   GUTION (ESP32-S3)  │
│   MAX30102       │──────────────────────►│   (hub/bridge)   │──────────────────────►│   LVGL 9.4 + EEZ     │
│   50Hz, 18-bit   │   co 20ms, 4B/ramkę   │   WiFi, BT, MQTT │                        │   800×480 RGB        │
│   I2C, FIFO      │                       │   własne sensory  │                        │                     │
└──────────────────┘                       └──────────────────┘                        └─────────────────────┘
```

### Urządzenia

| Urządzenie | MCU | Platforma | Rola |
|-----------|-----|-----------|------|
| **STM32-BPM** | STM32F103 | HAL/STM32Cube | Akwizycja MAX30102 (50Hz), obliczenia BPM/SpO2, wysyłka diff |
| **ZEGAR-ESP32** | ESP32 | Arduino/PlatformIO | Hub: odbiera dane z STM32, przekazuje 1:1 do Gution przez COBS/CRC16 |
| **Gution** | ESP32-S3 | ESP-IDF 6.0.1 | Wyświetlacz: LVGL 9.4, EEZ Flow, wykres PPG |

### Łańcuch danych PPG

```
MAX30102 FIFO (50Hz)
  → STM32: odczyt IR/Red, filtracja (FILTER_LEVEL=8), diff = prev_filtered - current_filtered
    → UART (9600): binary frame z diff (int16_t) — 4B, każda próbka
      → ZEGAR-ESP32: SoftwareSerial rx, przekazuje 1:1
        → UART (115200): COBS frame z 1x int16_t = 2B payload — każda próbka osobno
          → Gution: UartTransport → CommunicationState → bpm_service
            → bpm_chart_handler: lv_chart_set_next_value()
              → LVGL: rysowanie wykresu PPG
```

---

## 2. Link 1: STM32 → ZEGAR-ESP32 (UART 115200 baud)

### 2.1 Stan obecny

STM32 wysyła tekstowo tylko przy wykrytym pulsie:
```
BPM:64,SPO2:97\n   (~16 bajtów, ~1-2 razy/sek)
```

### 2.2 Stan docelowy — binarny strumień diff 50Hz

MAX30102 skonfigurowany:
- `SPO2_CONFIG = 0x63` → 50Hz sample rate, 18-bit ADC, 16384nA range
- Co 20ms czyta FIFO, filtruje, oblicza `eachSampleDiff`

**Format ramki binarnej (z STM32 do ZEGAR-ESP32):**

```
┌──────┬──────────┬──────┐
│ 0xAA │ int16_t  │ 0x0A │
│ HEAD │ diff     │ LF   │
│ 1B   │ 2B       │ 1B   │
└──────┴──────────┴──────┘
Razem: 4 bajty na próbkę
```

**Legenda:**
- `0xAA` (HEAD) — znacznik początku ramki
- `int16_t diff` — little-endian, wartość `eachSampleDiff` (zakres ~ -200..+200)
- `0x0A` (LF) — znacznik końca ramki

**Wykorzystanie łącza 115200 baud:**
- 4 bajty/ramkę × 50 ramek/s = 200 B/s = **1.7% z 115200 baud** ✅✅✅

### 2.3 Co wysyłać

Każda próbka z MAX30102 po filtracji daje `diff = prev_filtered - current_filtered`.
Wysyłamy go **natychmiast po odczycie FIFO + filtracji**, niezależnie od wykrycia pulsu.

### 2.4 Obsługa braku palca

Gdy MAX30102 nie wykrywa palca (`iRed < 40000`):
- `heartRate = 0`, `spo2 = 0`, `diff = 0`
- Wysyłamy `diff = 0` (ramka `0xAA 0x00 0x00 0x0A`)
- ZEGAR-ESP32 interpretuje serię zer jako brak sygnału

### 2.5 Parsowanie po stronie ZEGAR-ESP32

ZEGAR-ESP32 odbiera przez SoftwareSerial (piny GPIO16-RX, GPIO17-TX, 115200 baud).

```cpp
// W STM32_Data.cpp, zastąpić obecny parser tekstowy:
// Szukamy 0xAA, czytamy 2 bajty diff, szukamy 0x0A
if (c == 0xAA) { start_frame = true; frame_index = 0; }
else if (start_frame && frame_index < 2) { raw_bytes[frame_index++] = c; }
else if (start_frame && c == 0x0A && frame_index == 2) {
    int16_t diff = raw_bytes[0] | (raw_bytes[1] << 8);
    stm32_diff = diff;
    stm32_data_updated = true;
    start_frame = false;
}
```

---

## 3. Link 2: ZEGAR-ESP32 → Gution (UART 115200 baud)

### 3.1 Istniejący protokół

- COBS framing z delimiterem `0x00`
- CRC16 CCITT na payloadzie
- Sekwencjonowanie (1 bajt sequence number)
- Maszyna stanów: BOOTING → UART_READY → PEER_DETECTED → FIRST_SYNC_SENT → FIRST_SYNC_CONFIRMED → STEADY_STATE

**Istniejące FrameType:**

| Type | Hex | Opis | Payload | Priorytet |
|------|-----|------|---------|-----------|
| kTypeIndoorWeather | 0x01 | Temperatura, wilgotność, ciśnienie, CO2 wewnątrz | ~14 B | Reactive |
| kTypePms | 0x02 | PM1, PM2.5, PM10, liczniki cząstek | ~23 B | Reactive |
| kTypeTime | 0x03 | Unix timestamp | 5 B | Reactive/Periodic 1s |
| kTypeSettings | 0x04 | Konfiguracja (buzzer, MQTT, itd.) | 6 B | Initial sync |
| kTypeSystemResources | 0x06 | RAM, CPU, flash, audio underruns | ~31 B | Periodic 2s |
| kTypeWifiStatus | 0x07 | Stan WiFi, RSSI, IP | 6 B | Reactive |
| kTypeOutdoorWeather | 0x08 | Pogoda zewnętrzna (API) | ~43 B | Periodic 5s |
| kTypeRequest | 0x10 | Żądanie danych od peer'a | zmienny | — |
| kTypeHello | 0x20 | Handshake | 11 B | Boot |
| kTypeHelloAck | 0x21 | Potwierdzenie handshake | 11 B | Boot |

### 3.2 Nowy FrameType: kTypePpg = 0x09

**Każda próbka wysyłana osobno** — priorytet: płynność w czasie rzeczywistym. Żadnego batchowania, zero dodatkowego opóźnienia buforowania.

```
┌──────────┬──────────┐
│ seq (1B) │ diff     │
│          │ int16_t  │
│          │ 2B       │
└──────────┴──────────┘
Payload: 3 bajty (1 seq + 1 × 2B diff)
```

**Obciążenie łącza 115200 baud:**
- Payload: 3B
- Nagłówek przed COBS: Type(1) + Seq(1) + Payload(3) + CRC16(2) = 7B
- COBS (7B → ~8B) + delimiter 0x00 = ~9B na wire
- 50 ramek/s × 9B = **450 B/s** = 450/11520 = **3.9%** z 115200 baud ✅✅✅

### 3.3 Realizacja po stronie ZEGAR-ESP32

Dodać do `EsptoGuition`:

```cpp
constexpr uint8_t kTypePpg = 0x09;

void sendPpg(uint8_t sequence);

// Każda odebrana próbka z STM32 → natychmiast wysłana (50Hz, brak batchowania)
static void sendPpgImpl(int16_t diff) {
    uint8_t buffer[3];
    buffer[0] = s_sequence++;
    buffer[1] = diff & 0xFF;        // lo
    buffer[2] = (diff >> 8) & 0xFF; // hi
    sendRawFrame(kTypePpg, buffer, sizeof(buffer));
}
```

**Wywołanie z `STM32_Data.cpp`** po odebraniu ramki 0xAA...0x0A:
```cpp
if (stm32_data_updated) {
    EsptoGuition::sendPpgImpl(stm32_diff);
    stm32_data_updated = false;
}
```

### 3.4 Realizacja po stronie Gution

Dodać do `CommunicationProtocol`:

```cpp
// W CommunicationProtocol.h
enum FrameType {
    // ... istniejące ...
    kTypePpg = 0x09,
};

// W CommunicationState — bufor cyrkularny PPG
struct PpgSnapshot {
    int16_t buffer[PPG_BUFFER_SIZE]; // circular buffer
    uint16_t head;
    uint16_t count;
    bool valid;
};

PpgSnapshot g_ppg;

// W ingestFrame() — pojedyncza próbka na ramkę
case kTypePpg: {
    uint8_t seq = data[0];              // sequence number
    int16_t diff = data[1] | (data[2] << 8); // little-endian
    bpm_service_feed_sample(diff);      // push do bpm_service
    break;
}
```

---

## 4. Strona Gution — obsługa i wizualizacja

### 4.1 Architektura modułów w `services/bpm/`

```
services/bpm/
├── bpm_service.h           ← Publiczne API
├── bpm_service.c           ← Silnik symulacji + bufor + mock/real switch
├── bpm_chart_handler.h     ← Inicjalizacja
└── bpm_chart_handler.c     ← LVGL timer, integracja z wykresem
```

### 4.2 bpm_service — tryby pracy

```
tryb REAL (domyślny, brak auto-generacji)
  → bpm_service czeka na dane z zewnątrz
  → bpm_service_feed_sample(diff) ← wołane z CommunicationState po odebraniu ramki PPG
  → bpm_service_update() oddaje ostatni odebrany diff (hold)

tryb MOCK (jawnie włączony w kodzie przez developera)
  → bpm_service_set_mock_mode(true) ← tylko i wyłącznie do testów / debugowania
  → bpm_service_update() generuje realistyczną falę PPG wewnętrznie
  → Źródło: wewnętrzny generator (PPG IR → filtr → diff)
```

**Mock NIE jest domyślny. Nie jest autonomiczny. Nie jest fallbackiem.**
Jest narzędziem programisty — włącza się go w kodzie na czas testów. W normalnej pracy Gution wyświetla tylko to, co przychodzi z UART.

```cpp
// Tylko do testów / debugowania:
// bpm_service_set_mock_mode(true);

// Produkcja — zawsze false:
bpm_service_set_mock_mode(false);  // ← stan domyślny
```

### 4.3 bpm_chart_handler — wizualizacja

- LVGL timer 100ms
- Sprawdza `eez_flow_get_current_screen() == SCREEN_ID_BPM`
- Znajduje chart EEZ lub tworzy własny (pozycja 27,313, rozmiar 747,141)
- Konfiguruje: LINE, SHIFT, range Y: -100..200, point_count: 747, kolor czerwony 0xff3333
- Woła `bpm_service_update(now_ms)` → `lv_chart_set_next_value()`

---

## 5. Przepustowość — podsumowanie

### Link 1: STM32 → ZEGAR-ESP32 (115200 baud)

| Scenariusz | B/s | % wykorzystania |
|-----------|-----|-----------------|
| Istniejący: tekst BPM/SpO2 tylko przy pulsie | ~16 B/s | 0.1% |
| PPG binary diff 50Hz (4B/ramkę) | 200 B/s | **1.7%** |
| **Razem (oba strumienie)** | **~216 B/s** | **1.9%** |

### Link 2: ZEGAR-ESP32 → Gution (115200 baud)

| Składowa | B/s | % |
|----------|-----|---|
| Istniejący ruch (weather, time, wifi, itd.) | ~33 B/s | 0.3% |
| PPG 1 próbka/ramka (50 ramek/s × ~9B) | ~450 B/s | 3.9% |
| **Razem** | **~483 B/s** | **4.2%** |

→ **96% łącza wciąż wolne.** Batchowanie byłoby optymalizacją oszczędzającą 2.3% pasma kosztem opóźnienia i złożoności — nie warto.

---

## 6. Plan implementacji krok po kroku

### Krok 1: STM32 — zmiana firmware

1. Dodać funkcję wysyłającą binarną ramkę diff co 20ms:
```c
void send_ppg_sample(int16_t diff) {
    uint8_t frame[4] = {0xAA, diff & 0xFF, (diff >> 8) & 0xFF, 0x0A};
    HAL_UART_Transmit(&huart1, frame, 4, 10);
}
```

2. W pętli głównej `max30102_cal()`, po obliczeniu `eachSampleDiff`, wywołać:
```c
send_ppg_sample(eachSampleDiff);
```

3. Zachować wysyłkę `BPM:64,SPO2:97\n` przy pulsie dla kompatybilności wstecznej.

### Krok 2: ZEGAR-ESP32 — nowy parser

1. W `STM32_Data.cpp` zastąpić parser tekstowy binarnym (szukanie `0xAA` + 2B + `0x0A`).
2. Po odebraniu każdej ramki → natychmiast wywołać `sendPpgImpl(diff)`.
3. W `EsptoGuition` dodać `kTypePpg = 0x09`, funkcję `sendPpgImpl(int16_t diff)`.

### Krok 3: Gution — nowy FrameType + integracja

1. W `CommunicationProtocol.h` dodać `kTypePpg = 0x09`.
2. W `CommunicationState` dodać bufor PPG.
3. W `ingestFrame()` dodać case `kTypePpg` → woła `bpm_service_feed_sample(diff)`.
4. Upewnić się że `bpm_service_set_mock_mode(false)` — stan domyślny.

### Krok 4: Testowanie

1. Test z real data: STM32 wysyła diff → Gution odbiera przez UART → wykres rysuje PPG.
2. Test braku palca: seria zer (0xAA 0x00 0x00 0x0A) → wykres wygaszony (linia płaska na y=0).
3. Test mock (debug): tymczasowo `bpm_service_set_mock_mode(true)` → symulacja bez STM32.

---

## 7. Podsumowanie różnic: mock (debug) vs real data

| Cecha | Mock (debug only) | Real data (produkcja) |
|-------|-------------------|----------------------|
| Źródło | Wewnętrzny generator PPG IR → filtr → diff | Rzeczywisty MAX30102 przez UART |
| Kiedy używany | Tylko jawnie: `bpm_service_set_mock_mode(true)` | Zawsze domyślnie: `bpm_service_set_mock_mode(false)` |
| Częstotliwość | 50Hz (time-based) | 50Hz (z MAX30102 FIFO) |
| Kształt fali | 6-segmentowy model PPG + respiracja + jitter | Rzeczywista fala PPG z palca |
| Brak sygnału | — | diff = 0 gdy iRed < 40000 |
| API | `bpm_service_update(now_ms)` | `bpm_service_feed_sample(diff)` |

---

## 8. Plan implementacji inżynierskiej

Podzielony na trzy urządzenia. Każdy krok zawiera: **co zmienić**, **w którym pliku**, **jakie są zależności** i **jak zweryfikować**.

---

### 8.1 STM32-BPM (firmware C, HAL/STM32Cube)

**Stan wyjściowy:**
- `main.c` — pętla główna, czeka na przerwanie MAX30102 (PB12=LOW), woła `max30102_cal()`
- `max30102.c` — `eachSampleDiff` obliczany (line 304), zerowany przy braku palca (line 286)
- `Uart1.c` — `uart1_send_vitals()` wysyła tekst `BPM:64,SPO2:97` przez interrupt-driven TX (HAL_UART_Transmit_IT)
- UART1 baud: 115200 (huart1)

**Stan wyjściowy:** `eachSampleDiff` już istnieje jako **global** w `max30102.c:266`, z getterem `max30102_getDiff()` w `max30102.h:28`. Zero przy braku palca (line 286).  
**Cel:** każda próbka diff wysłana natychmiast jako binarna ramka — **nie wymaga zmian w `max30102.c`**.

#### Krok S1 — istniejący kod (zweryfikować)

**Plik:** `max30102.c` (nie wymaga zmian)

```c
// Już istnieje (line 266):
int16_t eachSampleDiff = 0;

// Już przypisywane (line 304):
eachSampleDiff = (int16_t)(last_iRed - sampleBuffTemp[i].iRed);

// Już zerowane przy braku palca (line 286):
eachSampleDiff = 0;

// Już getter (line 328):
int16_t max30102_getDiff(void) { return eachSampleDiff; }
```

#### Krok S2 — dodać funkcję wysyłki binarnej ramki

**Plik:** `Uart1.c`

```c
// Obok uart1_send_vitals():
void uart1_send_ppg_diff(int16_t diff) {
    uint8_t frame[4] = {
        0xAA,
        (uint8_t)(diff & 0xFF),
        (uint8_t)((diff >> 8) & 0xFF),
        0x0A
    };
    uart1_write(frame, 4);  // ta sama interrupt-driven kolejka co uart1_send_vitals
}
```

**Ważne:** `uart1_write()` już istnieje i jest interrupt-driven (line 190 `HAL_UART_Transmit_IT`). Ramka 4B jest mniejsza niż tekstowa ~16B, więc nie ma ryzyka overflow kolejki.

#### Krok S3 — wysyłać diff co 20ms (50Hz) z main.c

**Plik:** `main.c`

W bloku `if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12) == GPIO_PIN_RESET)`, po pobraniu diff (line 199), dodać:

```c
int16_t diff = max30102_getDiff();

// PPG diff — KAŻDA próbka, niezależnie od pulsu (50Hz stream):
uart1_send_ppg_diff(diff);
```

To jest **przed** blokiem display + buzzer, więc wysyłka UART jest najwcześniejszą operacją po pomiarze — minimalne opóźnienie do ZEGAR-ESP32.

**Istniejący kod BPM/SpO2** (`uart1_send_vitals`) pozostaje bez zmian — wysyłany tylko przy pulsie raz na beat przez `last_diff_state` gating.

#### Krok S4 — synchronizacja baud rate (stwierdzono mismatch)

**Stan obecny:**
- STM32 `MX_USART1_UART_Init` → `huart1.Init.BaudRate = 115200` (line 383)
- ZEGAR `STM32data_begin` default → `baudRate = 9600UL` (`STM32_Data.h:21`)
- **To jest realny mismatch.** Jeśli STM32 PA9/PA10 są fizycznie połączone z ZEGAR GPIO16/17, obecnie komunikacja nie działa.

**Decyzja:** podnieść ZEGAR do 115200 — STM32 już tam jest, zmiana na ZEGARze jest prostsza niż modyfikacja STM32.

W `STM32_Data.h` zmienić:
```cpp
void STM32data_begin(int rxPin, int txPin, uint32_t baudRate = 115200UL);
```

Jeśli 115200 okaże się niestabilne na EspSoftwareSerial (ryzyko niskie), wtedy opuścić STM32 do 9600:
```c
// main.c line 383:
huart1.Init.BaudRate = 9600;
```
Przeliczenie: 4B × 50Hz = 200 B/s = 20.8% z 9600 — wciąż OK.

#### Podsumowanie STM32

| Plik | Zmiana | Status |
|------|--------|--------|
| `max30102.c` | Brak — `eachSampleDiff` już globalny z getterem | ✅ istnieje |
| `uart1.h` | Dodać deklarację `uart1_send_ppg_diff()` | ✅ zrobione |
| `Uart1.c` | Dodać implementację `uart1_send_ppg_diff()` | ✅ zrobione |
| `main.c` | Dodać `uart1_send_ppg_diff(diff)` po `max30102_getDiff()` | ✅ zrobione |
| `STM32_Data.h` (ZEGAR) | Zmienić default baudRate 9600→115200 | ⏳ ZEGAR krok Z4 |

---

### 8.2 ZEGAR-ESP32 (Arduino/PlatformIO)

**Stan wyjściowy:**
- `STM32_Data.cpp` — parser tekstowy, szuka `BPM:xxx,SPO2:yyy\n`, aktualizuje `bpmNumber`, `spo2Number`
- `STM32_Data.h` — API `STM32data_begin()`, `STM32data_update()`
- `Esptogution.h` — frame types: 0x01-0x08, 0x10, 0x11, 0x20, 0x21, 0xFF
- `Esptogution.cpp` — `broadcastSnapshots()` co 200ms, reactive
- `EsptoGuitionTransport.h` — `sendRawFrame(type, seq, payload, len)`

**Cel:** parsować ramki binarne z STM32, forwardować każdą próbkę do Gution.

#### Krok Z1 — dodać parser binarny do STM32_Data

**Plik:** `STM32_Data.cpp`

Dodać nowy stan maszyny obok parsera tekstowego (oba mają działać równolegle, dopóki tekstowy nie zostanie wycofany):

```cpp
// Nowe stałe dla ramki binarnej PPG:
static const uint8_t PPG_FRAME_HEAD = 0xAA;
static const uint8_t PPG_FRAME_TAIL = 0x0A;

// Stan parsera binarnego:
static bool s_ppg_frame_started = false;
static uint8_t s_ppg_frame_buf[2];
static uint8_t s_ppg_frame_index = 0;
static int16_t s_ppg_diff = 0;
```

W `STM32data_update()`, w pętli `while (stmSerial->available() > 0)`, dodać **przed** parserem tekstowym:

```cpp
int cInt = stmSerial->read();
if (cInt < 0) break;
uint8_t c = (uint8_t)cInt;

// --- Parser binarny PPG (priorytet: wykrycie HEAD) ---
if (s_ppg_frame_started) {
    if (s_ppg_frame_index < 2) {
        s_ppg_frame_buf[s_ppg_frame_index++] = c;
    } else if (c == PPG_FRAME_TAIL && s_ppg_frame_index == 2) {
        // Kompletna ramka: 0xAA, lo, hi, 0x0A
        s_ppg_diff = (int16_t)(s_ppg_frame_buf[0] | (s_ppg_frame_buf[1] << 8));
        s_ppg_frame_started = false;
        s_ppg_frame_index = 0;
        
        // Forward do Gution natychmiast:
        EsptoGuition::sendPpgImpl(s_ppg_diff);
        continue; // pomiń parser tekstowy
    } else {
        // Błąd ramki — reset
        s_ppg_frame_started = false;
        s_ppg_frame_index = 0;
    }
} else if (c == PPG_FRAME_HEAD) {
    s_ppg_frame_started = true;
    s_ppg_frame_index = 0;
    continue;
}

// --- istniejący parser tekstowy (BPM:xxx,SPO2:yyy) ---
// ... bez zmian ...
```

**Uwaga:** `continue` po udanej ramce PPG pomija parser tekstowy — to zapobiega przypadkowemu zinterpretowaniu fragmentu ramki binarnej jako tekstu.

#### Krok Z2 — dodać kTypePpg do Esptogution.h

**Plik:** `Esptogution.h`

```cpp
constexpr uint8_t kTypePpg = 0x09;
```

Dodać deklarację:
```cpp
void sendPpgImpl(int16_t diff);
```

#### Krok Z3 — zaimplementować sendPpgImpl

**Plik:** `Esptogution.cpp`

```cpp
void sendPpgImpl(int16_t diff) {
    uint8_t buffer[3];
    buffer[0] = s_sequence++;
    buffer[1] = static_cast<uint8_t>(diff & 0xFF);        // lo
    buffer[2] = static_cast<uint8_t>((diff >> 8) & 0xFF); // hi
    sendRawFrame(kTypePpg, s_sequence, buffer, sizeof(buffer));
}
```

**Uwaga:** kolejność bajtów = little-endian (zgodna z STM32).

**Zabezpieczenie:** jeśli `sendRawFrame` jest wołane z kontekstu SoftwareSerial ISR-like, może być problem z yield. `sendRawFrame` wrzuca do kolejki UART przez `xQueueSend` (z `portMAX_DELAY` lub 0). W Arduino, jeśli SoftwareSerial używa `taskYIELD()`, można dodać flagę `s_ppg_pending` i wysłać w `broadcastSnapshots()`:

```cpp
// Alternatywa: deferred send z loop()
static int16_t s_pending_ppg = 0;
static bool s_ppg_pending = false;

// W STM32data_update:
s_pending_ppg = s_ppg_diff;
s_ppg_pending = true;

// W broadcastSnapshots (na początku):
if (s_ppg_pending) {
    sendPpgImpl(s_pending_ppg);
    s_ppg_pending = false;
}
```

#### Krok Z4 — synchronizacja baud rate

**Plik:** `STM32_Data.h`

Zmienić default baud rate:
```cpp
void STM32data_begin(int rxPin, int txPin, uint32_t baudRate = 115200UL);
```

**Weryfikacja:** monitorować `stmFramesReceived` — powinno rosnąć o ~50/s.

#### Krok Z5 — obsługa BPM/SpO2 z ramki tekstowej (istniejący kod)

Nie zmieniać. `bpmNumber`, `spo2Number` wciąż aktualizowane z ramki tekstowej. Używane do wyświetlania na segmenatach ZEGAR-ESP32.

W przyszłości można przenieść BPM/SpO2 do binarnej ramki, ale na razie tekstowa ramka kompatybilności wstecznej.

#### Podsumowanie ZEGAR-ESP32

| Plik | Zmiana | Linie |
|------|--------|-------|
| `STM32_Data.cpp` | Dodać parser binarny PPG (0xAA...0x0A) | ~60-100 |
| `STM32_Data.h` | Zmienić default baudRate na 115200 | line 21 |
| `Esptogution.h` | Dodać `kTypePpg = 0x09` | ~18 |
| `Esptogution.h` | Dodać `sendPpgImpl(int16_t)` | ~66 |
| `Esptogution.cpp` | Implementacja `sendPpgImpl()` | new function |
| `Esptogution.cpp` | Opcjonalnie: deferred send w `broadcastSnapshots()` | ~140 |

---

### 8.3 Gution (ESP32-S3, ESP-IDF 6.0.1)

**Stan wyjściowy:**
- `CommunicationProtocol.h` — enum FrameType (0x01-0x08, 0x10, 0x11, 0x20, 0x21, 0xFF)
- `UartTransport.cpp` — `handleFrame()` switch (line 53-89), `sendRawFrame()`, `ingestByte()`
- `CommunicationState.cpp` — `applyWeatherPayload()`, `applyPmsPayload()`, itd.
- `bpm_service.c` — mock/real switch, bufor cyrkularny 747, `bpm_service_feed_sample()`, `bpm_service_update()`
- `bpm_chart_handler.c` — LVGL timer 100ms, wykres PPG

**Cel:** odebrać ramkę `kTypePpg`, push do `bpm_service`.

#### Krok G1 — dodać kTypePpg do enum

**Plik:** `CommunicationProtocol.h`

```cpp
enum FrameType {
  kTypeIndoorWeather = 0x01,
  kTypePms = 0x02,
  kTypeTime = 0x03,
  kTypeSettings = 0x04,
  kTypeSetSettings = 0x05,
  kTypeSystemResources = 0x06,
  kTypeWifiStatus = 0x07,
  kTypeOutdoorWeather = 0x08,
  kTypePpg = 0x09,        // ← NOWY
  kTypeRequest = 0x10,
  kTypeConfig = 0x11,
  kTypeHello    = 0x20,
  kTypeHelloAck = 0x21,
  kTypeAck = 0xFF
};
```

#### Krok G2 — dodać handler w UartTransport

**Plik:** `UartTransport.cpp`, w `handleFrame()`, dodać case:

```cpp
case kTypePpg: {
    // payload: [seq(1)] [diff_lo(1)] [diff_hi(1)] = 3 bajty
    if (payloadLength == 3) {
        int16_t diff = static_cast<int16_t>(payload[1] | (payload[2] << 8));
        bpm_service_feed_sample(diff);
    }
    break;
}
```

**Zależność:** `UartTransport.cpp` musi includować `"services/bpm/bpm_service.h"`.

```cpp
#include "services/bpm/bpm_service.h"
```

#### Krok G3 — dodać applyPpgPayload w CommunicationState (opcjonalnie)

Jeśli chcemy trzymać architekturę (wszystkie `applyXxxPayload` w `CommunicationState.cpp`):

**Plik:** `CommunicationState.h`
```cpp
void applyPpgPayload(const uint8_t *payload, uint16_t payloadLength);
```

**Plik:** `CommunicationState.cpp`
```cpp
void applyPpgPayload(const uint8_t *payload, uint16_t payloadLength) {
    if (payloadLength != 3) return;
    int16_t diff = static_cast<int16_t>(payload[1] | (payload[2] << 8));
    bpm_service_feed_sample(diff);
}
```

Wtedy w `UartTransport.cpp`:
```cpp
case kTypePpg:
    applyPpgPayload(payload, payloadLength);
    break;
```

#### Krok G4 — upewnić się, że mock_mode = false

**Plik:** `bpm_service.c`

Obecnie (line 141):
```c
s_ctx.mock_enabled = true;
```

Zmienić na:
```c
s_ctx.mock_enabled = false;
```

**Uzasadnienie:** mock NIE jest domyślny. Gution startuje w trybie real data. Jeśli nie ma danych z UART, wykres wyświetla ostatni diff (0) — płaska linia. Deweloper włącza mock tylko do debugowania.

#### Krok G5 — obsługa BPM/SpO2 na Gution

BPM/SpO2 przychodzą obecnie w ramce tekstowej → ZEGAR-ESP32 parsuje → wyświetla na swoich segmentach.

Docelowo BPM/SpO2 trafią do Gution przez osobny kanał. Dwie opcje:

**Opcja A (prosta, zalecana):** dodać nowy frame type `kTypeBpmStatus = 0x0A`.

ZEGAR-ESP32 wysyła:
- Payload: 2 bajty (`bpmNumber`, `spo2Number`)
- Częstotliwość: przy każdej zmianie (~1Hz)

Gution odbiera i zapisuje do `eez::flow::setGlobalVariable` dla wyświetlenia na ekranie BPM.

**Opcja B (szybka, tymczasowa):** BPM/SpO2 w payloadzie IndoorWeather (dodać flagę z danymi). Ale to miesza semantykę — niezalecane.

**Implementacja (opcja A):**

**Plik:** `CommunicationProtocol.h`
```cpp
kTypeBpmStatus = 0x0A,
```

**ZEGAR-ESP32** (`Esptogution.h`):
```cpp
constexpr uint8_t kTypeBpmStatus = 0x0A;
void sendBpmStatus(uint8_t seq);
```

**ZEGAR-ESP32** (`Esptogution.cpp`), w `broadcastSnapshots()`:
```cpp
// Na początku broadcastSnapshots():
if (stmDataUpdated) {
    uint8_t buf[2] = { (uint8_t)bpmNumber, (uint8_t)spo2Number };
    sendRawFrame(kTypeBpmStatus, s_sequence++, buf, 2);
    stmDataUpdated = false;
}
```

**Gution** (`UartTransport.cpp`):
```cpp
case kTypeBpmStatus: {
    if (payloadLength == 2) {
        uint8_t bpm = payload[0];
        uint8_t spo2 = payload[1];
        // Zapisać do zmiennej globalnej EEZ Flow lub Snapshot
    }
    break;
}
```

#### Krok G6 — inicjalizacja i pierwsze uruchomienie

**Flow uruchomienia:**
1. `app_main` → `bpm_chart_handler_init()` (już istnieje)
2. `bpm_service_init()` → mock_enabled = false
3. UART start → handshake z ZEGAR-ESP32
4. ZEGAR-ESP32 w STEADY_STATE → zaczyna wysyłać PPG
5. `handleFrame(kTypePpg)` → `bpm_service_feed_sample(diff)` → bufor
6. `bpm_timer_cb` (100ms LVGL) → `bpm_service_update()` → `lv_chart_set_next_value()`

**Bez STM32 (scenariusz offline):**
- Wykres wyświetla płaską linię (ostatni diff = 0)
- Mock włączany tylko do testów

#### Podsumowanie Gution

| Plik | Zmiana | Linie |
|------|--------|-------|
| `CommunicationProtocol.h` | Dodać `kTypePpg = 0x09` | ~17 |
| `CommunicationProtocol.h` | Dodać `kTypeBpmStatus = 0x0A` (opcja A) | ~18 |
| `UartTransport.cpp` | Dodać case `kTypePpg` + include bpm_service.h | ~53-89 |
| `UartTransport.cpp` | Dodać case `kTypeBpmStatus` (opcja A) | ~53-89 |
| `bpm_service.c` | Zmienić `mock_enabled = true` → `false` | line 141 |

---

### 8.4 Zależności między krokami

```
S1 (g_each_sample_diff) ──→ S2 (uart1_send_ppg_diff) ──→ S3 (main loop call)
                                                              │
                                                              ▼
                                              Z1 (binary parser STM32_Data.cpp) ──→ Z2 (kTypePpg) ──→ Z3 (sendPpgImpl)
                                                                    │                                        │
                                                                    ▼                                        ▼
                                                              Z4 (baud sync)                         G1 (kTypePpg enum)
                                                                                                       │
                                                                                                       ▼
                                                                                                  G2 (handleFrame PPG)
                                                                                                       │
                                                                                                       ▼
                                                                                                  G4 (mock_enabled=false)
                                                                                                       │
                                                                                                       ▼
                                                                                                  G6 (run)
```

**Kolejność wdrożenia:**
1. **G4** (mock_enabled = false) — najprostsze, bezpieczne, można zrobić od razu
2. **G1 + G2** (Gution odbiera PPG) — można testować z mock_data lub serial terminal
3. **Z1 + Z2 + Z3** (ZEGAR forwarduje PPG) — wymaga STM32, ale można testować z emulatorem
4. **S1 + S2 + S3** (STM32 wysyła PPG) — ostatnie, wymaga flashowania STM32
5. **Z4** (baud sync) — po potwierdzeniu że 115200 działa stabilnie
6. **G5** (BPM/SpO2 status) — niezależne, można zrobić w dowolnym momencie

---

### 8.5 Macierz ryzyka

| Ryzyko | Prawdopodobieństwo | Konsekwencja | Mitigacja |
|--------|-------------------|-------------|-----------|
| SoftwareSerial gubi bajty na 115200 | Niskie | Brakujące próbki → wykres "szarpie" | Test z constant diff; jeśli gubi → wrócić do 9600 i policzyć realną stratę |
| Kolejka UART TX overflow na STM32 (4B × 50Hz) | Bardzo niskie | Pominięta ramka | Interrupt-driven TX; 200 B/s << 115200 baud |
| Zły endianness (STM32 ARM vs ESP32 Xtensa) | Niskie | Odwrócone bajty → śmieci na wykresie | Oba są little-endian w domyśle; zweryfikować pierwszym testem |
| `sendRawFrame` blokuje w ISR kontekście SoftwareSerial | Średnie | Watchdog reset lub missed bytes | Użyć deferred send przez flagę + broadcastSnapshots |
| Konflikt między parsowaniem binarnym a tekstowym | Niskie | Zły parse ramki | `continue` po udanej ramce binarnej; separator 0xAA nie występuje w tekstowej ramce |

---

### 8.6 Kryteria akceptacji

1. **50 Hz steady state:** `stmFramesReceived` na ZEGAR-ESP32 rośnie o ~50/s ±2
2. **Gution wyświetla PPG:** wykres rysuje falę przyłożonego palca
3. **Bez palca:** wykres płaski na y=0 (po ~200ms serii zer)
4. **Opóźnienie ekran–do–ekran:** < 30ms od próbki MAX30102 do LVGL (mierzalne przez log z timestampem)
5. **Brake żadnego watchdog resetu:** system działa > 1h z palcem
6. **BPM/SpO2 wyświetlane poprawnie:** zgadza się z STM32 OLED (margines ±1 BPM)

