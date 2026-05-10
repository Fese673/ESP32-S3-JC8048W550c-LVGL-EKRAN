# Raport Zarządzania Pamięcią (SRAM/PSRAM): Warstwa Komunikacji

## 1. Analiza wykorzystania pamięci

W projekcie ESP32-S3-JC8048W550 warstwa komunikacji (`main/communication`) opiera się niemal wyłącznie na pamięci **SRAM (Internal RAM)**. Jest to decyzja poprawna z punktu widzenia wydajności, ale wymagająca ścisłej kontroli stosu i zapobiegania fragmentacji.

### Wykorzystanie SRAM vs PSRAM
*   **SRAM**: Wykorzystywany do wszystkich buforów niskopoziomowych (UART RX/TX, bufory COBS, bufory parsowania). Pamięć wewnętrzna jest niezbędna dla operacji DMA i szybkich przerwań UART.
*   **PSRAM**: Obecnie niewykorzystywany bezpośrednio w komunikacji. Dane z komunikacji trafiają do PSRAM dopiero po przetworzeniu, gdy są kopiowane do struktur UI LVGL (które mogą znajdować się w PSRAM).

### Alokacje Statyczne vs Dynamiczne
*   **Statyczne**: Większość buforów (np. `FrameParser s_parser`) jest alokowana statycznie. Zapobiega to fragmentacji, ale na stałe rezerwuje cenne bajty SRAM (ok. 1.5 KB na samą strukturę komunikacji).
*   **Dynamiczne**: `uart_driver_install` alokuje bufory RX/TX (domyślnie 1KB + 1KB) z heapu SRAM przy starcie systemu.

---

## 2. Wskazane problemy i ryzyka

1.  **Nadmierne kopiowanie danych (Multi-copy overhead):**
    Dane z UART przechodzą przez następujące etapy:
    `Driver Buffer` -> `Stack Buffer (communicationTask)` -> `Static Buffer (s_parser)` -> `Stack Buffer (decodedBuffer)` -> `Logic Structure`.
    Powoduje to niepotrzebne obciążenie CPU i ryzyko przepełnienia stosu.

2.  **Presja na stos (Stack Pressure):**
    Funkcja `sendRawFrame` alokuje na stosie bufory:
    *   `frame[134]`
    *   `cobsBuffer[138]`
    *   `txBuffer[140]`
    Łącznie ok. 412 bajtów na jedno wywołanie. Przy głębokiej rekurencji lub wielu zagnieżdżonych wywołaniach w LVGL, może to doprowadzić do `Stack Overflow`.

3.  **Brak wyrównania do Cache (Cache Alignment):**
    Bufory statyczne nie są wyrównane do linii cache (32-64 bajty), co przy ewentualnym przejściu na DMA z PSRAM mogłoby powodować błędy spójności.

---

## 3. Proponowana Architektura Pamięci

### Model Zero-Copy / Reduced-Copy
Zamiast kopiować dane między buforami, należy dążyć do parsowania "w miejscu".

**Before (UartTransport.cpp):**
```cpp
uint8_t decodedBuffer[256];
size_t decodedLen = cobsDecode(s_parser.buffer, s_parser.length, decodedBuffer);
handleFrame(type, decodedBuffer + 4, payloadLength);
```

**After (Propozycja):**
Zastosowanie `Ring Buffer` bezpośrednio z drivera UART i dekodowanie COBS w locie do jednego, stałego bufora procesowego.

### Memory Pools dla Ramek
Wprowadzenie stałej puli pamięci dla najczęściej przesyłanych struktur (np. `Snapshot`). Dzięki temu unikamy alokacji na stosie i przesyłamy tylko wskaźniki.

---

## 4. Konkretne Refaktoryzacje (Before/After)

### Optymalizacja `sendRawFrame` (Ograniczenie stosu)

**Problem**: Trzy duże bufory na stosie.

**Rozwiązanie**: Użycie jednego wspólnego bufora (pre-allocated) lub dynamiczna alokacja z flagą `MALLOC_CAP_DMA`.

```cpp
// After: Optymalizacja stosu
void sendRawFrame(...) {
    // Zamiast 3 buforów na stosie, używamy jednego bufora statycznego 
    // chronionego mutexem lub alokowanego jednorazowo przy starcie.
    static uint8_t* s_work_buffer = (uint8_t*)heap_caps_malloc(512, MALLOC_CAP_DMA);
    
    // ... operacje na s_work_buffer ...
    uart_write_bytes(kUartPort, s_work_buffer, totalLen);
}
```

---

## 5. Checklista dobrych praktyk (Communication Memory)

- [ ] **Weryfikacja rozmiaru stosu**: Zadanie `comm_uart1` ma 4096 bajtów. Należy sprawdzić `uxTaskGetStackHighWaterMark`, czy nie jest to wartość zbyt duża (marnotrawstwo SRAM) lub zbyt mała.
- [ ] **DMA Alignment**: Wszystkie bufory przekazywane do driverów (UART, SPI) powinny być alokowane z `MALLOC_CAP_DMA`.
- [ ] **ISR Safety**: Jeśli komunikacja będzie rozszerzona o przerwania, upewnij się, że bufory i kolejki używają wersji funkcji `_FROM_ISR`.
- [ ] **Static Guard**: Używaj `const` i `constexpr` dla rozmiarów buforów, aby kompilator mógł zoptymalizować ułożenie w pamięci.

---

## 6. Priorytety Optymalizacji

1.  **PRIORYTET 1**: Redukcja rozmiaru buforów na stosie w `sendRawFrame` i `ingestByte`. (Wysokie ryzyko crashy).
2.  **PRIORYTET 2**: Migracja buforów UART do `MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA` dla stabilności transferów przy wysokich baudrate.
3.  **PRIORYTET 3**: Implementacja mechanizmu Zero-copy dla dekodowania COBS.

---

*Opracowano na podstawie analizy kodu źródłowego komponentu `main/communication`.*
