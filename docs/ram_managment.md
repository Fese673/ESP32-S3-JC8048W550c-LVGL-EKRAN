# Raport i Audyt Zarządzania Pamięcią RAM

Projekt wykorzystuje układ ESP32-S3 z **512 KB SRAM** (pamięć wewnętrzna) oraz **8 MB PSRAM** (pamięć zewnętrzna Octal SPI). Poprawne rozlokowanie danych między tymi obszarami jest kluczem do wydajności.

## 1. Mapa Pamięci PSRAM (8 MB total)

PSRAM jest wolniejszy niż SRAM, ale znacznie większy. Przechowujemy tu dane "ciężkie" (grafikę).

| Moduł / Przeznaczenie | Rozmiar (Bajty) | Alokacja | Opis |
| :--- | :--- | :--- | :--- |
| **LCD Frame Buffers** | ~1 536 000 | Statyczna | Podwójne buforowanie (2 * 800x480 * 2 bajty). Podstawa płynności bez tearingu. |
| **LVGL Layer Buffer** | 2 304 000 | Dynamiczna (PSRAM) | Nasz bufor 2.2 MB na skomplikowane operacje skalowania/transformacji. |
| **Grafika (Images)** | Zmienna | RODATA (PSRAM) | Wszystkie obrazy z EEZ Studio (`img_...`) dzięki `CONFIG_SPIRAM_RODATA=y`. |
| **LVGL Objects** | Zmienna | CLIB_MALLOC | Przyciski, etykiety i kontenery są teraz alokowane z reszty PSRAM. |

**Razem zajęte (szacunkowo):** ~4.5 MB - 5.5 MB (zależnie od ilości obrazów). Pozostaje ok. 2.5 MB wolnego na logikę aplikacji i Flow.

## 2. Mapa Pamięci SRAM (512 KB total)

Pamięć wewnętrzna (Internal RAM) jest krytyczna. Jeśli spadnie poniżej ~50 KB wolnego, system może stać się niestabilny.

| Moduł / Przeznaczenie | Rozmiar | Priorytet | Uwagi |
| :--- | :--- | :--- | :--- |
| **System Stacks** | ~64-128 KB | Krytyczny | Stosy zadań FreeRTOS (Main, LVGL task, IDLE). |
| **DMA Buffers** | ~32 KB | Wysoki | Bufory dla I2C (Touch) i komunikacji flash. |
| **WiFi/BT Stack** | ~64-100 KB | Średni | Rezerwowane przez system po włączeniu łączności. |
| **Krytyczne zmienne** | Zmienna | - | Zmienne globalne i statyczne (`.data`, `.bss`). |

## 3. Strategia Alokacji (Nasza Konfiguracja)

Dzięki wprowadzonym zmianom, Twój system działa w trybie **"PSRAM First for Graphics"**:

1.  **Odblokowany Malloc (`LV_USE_CLIB_MALLOC`):** Gdy LVGL prosi o pamięć powyżej 16 KB (np. dla dużego obrazka), system automatycznie kieruje go do PSRAM. Mniejsze prośby trafiają do szybkiego SRAM.
2.  **Instruction Cache:** Instrukcje programu są mapowane z Flash do PSRAM, co oszczędza SRAM na dane zmienne.
3.  **Optymalizacja:** `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768` – system zawsze rezerwuje 32 KB w SRAM tylko dla operacji DMA, których PSRAM nie potrafi obsłużyć.

## 4. Rekomendacje dla FPS

*   **PCLK (16 MHz):** Twój aktualny zegar LCD ogranicza Cię do ok. 40 FPS. Jeśli wydajność obliczeniowa na to pozwoli, można go podnieść do 18-20 MHz w pliku `sunton_esp32_8048s050c.c`.
*   **Image Cache:** Jeśli zauważysz, że częste otwieranie tych samych ikon spowalnia system, możemy włączyć `CONFIG_LV_CACHE_DEF_SIZE` (np. na 512 KB), aby LVGL nie musiał za każdym razem dekodować nagłówków obrazów z PSRAM.

---
*Dokument wygenerowany na podstawie aktualnej konfiguracji `sdkconfig` oraz kodu źródłowego sterownika.*
