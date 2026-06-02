# Zdalna aktualizacja – plan i analiza

## Disclaimer

**To nie jest pewny plan, a jedynie koncepcja.** Poniższe podejście nie jest w pełni efektywne – nakład pracy jest duży, a zysk (zdalna aktualizacja) nie zawsze współmierny do charakteru projektu (hobby/embedded). Decyzja o wdrożeniu wymaga świadomości ograniczeń i ryzyka opisanego poniżej.

---

## Architektura docelowa

```
INTERNET (GitHub Releases)
      │ HTTPS
      ▼
┌─────────────────────────────────────────┐
│ GUITION (ESP32-S3, 16MB flash)          │
│ WiFi → własny OTA przez GitHub          │
│ Dual partycje app0/app1 (2× 7.25MB)    │
│ FW obecnie: 6.65MB → zmieści się       │
└────────────┬────────────────────────────┘
             │ UART (COBS/CRC16) – dane czujników
             ▼
┌─────────────────────────────────────────┐
│ ZEGAR-ESP32 (po wymianie flash)         │
│ 4MB → 16MB (ESP32-WROVER / ESP32-S3)   │
│ Własny OTA przez WiFi + GitHub          │
│ Dual partycje po ~6-7MB                 │
│ FW obecnie: 2.17MB → luz                │
└────────────┬────────────────────────────┘
             │ UART1 – dane PPG
             ▼
┌─────────────────────────────────────────┐
│ STM32F103C8 (Blue Pill)                 │
│ BPM/SpO2, OLED, enkoder, buzzer        │
│ Update: ST-Link (SWD)                   │
│ Brak SD, brak IAP, brak własnej sieci  │
└─────────────────────────────────────────┘
```

---

## 1. OTA na Guition (ESP32-S3) – najbardziej realne

**Po co:** Guition ma już dual partycje OTA (app0/app1 po 7.25 MB), WiFi, PSRAM, 16MB flash. Wymaga tylko implementacji.

**Co zrobić:**
- Serwis OTA (`services/ota/ota_service.c`) – sprawdzanie wersji, pobieranie, flash
- `esp_https_ota` – ESP-IDF ma gotowe API
- UI postępu – progress bar + komunikat
- Sprawdzanie wersji przez GitHub Releases API (lub json na GitHub Pages)
- Rollback – jeśli nowy FW nie wystartuje, bootloader wraca do poprzedniej partycji

**Problemy:**
- PSRAM tearing przy zapisie flash (znany problem w projekcie) → OTA task na Core 0, pauza LVGL
- Certyfikat SSL Let's Encrypt – musi być wgrany do firmware
- Firmware 6.65MB / partition 7.25MB → po dodaniu kodu OTA zostanie ~400-500KB zapasu

**Szansa sukcesu: ~90%**

---

## 2. ZEGAR-ESP32 – wymaga zmiany sprzętu

**Problem:** Obecny moduł ESP32 ma 4MB flash, partycja `huge_app.csv` daje tylko 1× app slot (3MB). Brak drugiej partycji = brak OTA. Firmware 2.17MB nie zmieści się w dual OTA (2× 1.5MB).

**Rozwiązanie:** Wymiana na ESP32 z 16MB flash (ESP32-WROVER, ESP32-S3, lub ESP32 z zewnętrznym flash).

- Koszt modułu: ~15-25 zł
- Nowa partycja: dual OTA ~2× 6-7MB, SPIFFS ~1MB, reszta
- Własne OTA przez WiFi (Arduino IDE ma `ESPhttpUpdate`, albo `esp_https_ota` jeśli przejście na ESP-IDF)

**Alternatywa bez zmiany sprzętu:** Wyciąć Bluetooth A2DP + gry (ok. 600KB) → firmware < 1.5MB → dual OTA na 4MB. Ale tracisz funkcje.

**Szansa sukcesu: ~70% (zależy od decyzji sprzętowej)**

---

## 3. STM32 – zostaje przy ST-Link (odrzucone pomysły)

### 3.1 STM32 + SD Card jako programator ZEGARa ✗ ODRZUCONE

**Pomysł:** STM32 czyta firmware ZEGARa z SD i programuje go przez UART (esptool).

**Dlaczego odpada:**
- STM32F103 ma 20KB RAM – nie pomieści całego 2.17MB firmware, musi streamować sektor po sektorze
- Ryzyko bricka ZEGARa przy przerwaniu transmisji
- Wymaga 4 pinów SPI na STM32 + 2 piny do kontroli GPIO0/EN ZEGARa
- UART1 jest współdzielony – normalnie PPG, podczas flashowania esptool
- Wymaga pełnej implementacji protokołu esptool po stronie STM32
- Testowanie jest trudne (każda próba = reset ZEGARa w tryb bootloadera)

### 3.2 STM32 IAP self-flash ✗ ODRZUCONE

**Pomysł:** Bootloader + SD + FATFS do samodzielnej aktualizacji STM32.

**Dlaczego odpada:**
- 64KB flash total – bootloader ~8-12KB, FATFS ~6KB, SPI ~2KB → bootloader zajmie 16-20KB
- Zostaje ~44KB na app (obecna app ~35KB) – teoretycznie tak, ale zero marginesu
- Ryzyko bricka przy przerwaniu zasilania w trakcie flashowania
- Po co? ST-Link jest tańszy, szybszy i bezpieczniejszy

### 3.3 STM32 + SD jako uniwersalny programator ✗ ODRZUCONE

Obie wyżej wymienione wady + efekt kaskadowy. **Zbyt skomplikowane, zbyt ryzykowne, zbyt mało efektywne.**

---

## 4. Alternatywne podejścia rozważone i odrzucone

| Pomysł | Powód odrzucenia |
|--------|------------------|
| MQTT OTA (przesyłanie FW przez MQTT) | Payload MQTT za mały (zwykle <256KB), FW ma 2-6MB, brak gwarancji dostarczenia |
| Lokalny serwer HTTP na Guition (web upload) | Nie jest zdalne – wymaga LAN i przeglądarki |
| W5500 Ethernet do OTA | W5500 nie jest używany w projekcie, wymaga inicjalizacji ETH + driverów |
| STM32 przez UART z PC (bootloader DFU) | Wymaga bootloadera, FTDI, kabla – to samo co ST-Link, tylko gorzej |
| Cloud OTA (AWS IoT, ESP RainMaker) | Overkill dla tego projektu, vendor lock-in |

---

## Podsumowanie – co realnie można zrobić

| Element | Decyzja | Nakład pracy | Ryzyko |
|---------|---------|-------------|--------|
| Guition OTA przez GitHub | **ROBIĆ** | Średni (~2-3 dni) | Niskie |
| ZEGAR – wymiana flash na 16MB | **ROBIĆ** | Niski (zakup + PCB) + średni (OTA) | Niskie |
| STM32 – zostawić ST-Link | **TAK** | Zerowy | Zerowe |
| STM32 + SD + esptool | **NIE** | – | – |

**Całość:** ~1 tydzień pracy, koszt ~20 zł za moduł ESP32. Efekt: 2 z 3 urządzeń z prawdziwą zdalną aktualizacją przez WiFi. STM32 aktualizowany lokalnie przez ST-Link (jak do tej pory).
