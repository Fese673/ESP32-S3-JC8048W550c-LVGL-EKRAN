# Raport Struktury Zadań (FreeRTOS Tasks)

Projekt wykorzystuje wielozadaniowość FreeRTOS na dwóch rdzeniach procesora ESP32-S3. Poniżej znajduje się mapa aktywnych zadań.

## 1. Aktywne Zadania Użytkownika

| Nazwa Zadania | Priorytet | Rdzeń (Affinity) | Stos (Stack) | Opis |
| :--- | :--- | :--- | :--- | :--- |
| **taskLVGL** | 4 | **Core 1** | 8192 bajty | Główne zadanie LVGL. Obsługuje rysowanie, dotyk i `lv_timer_handler`. |
| **main** | 1 | Core 0 (domyślnie) | ~4096 bajtów | Zadanie uruchamiające `app_main`. Obecnie głównie śpi (`vTaskDelay`). |

## 2. Zadania Systemowe (Tło)

Te zadania są tworzone automatycznie przez ESP-IDF:

*   **IDLE (Core 0 i 1):** Najniższy priorytet. Liczą czas wolny procesora.
*   **ipc0 / ipc1:** Służą do komunikacji międzyrdzeniowej.
*   **sys_evt:** Obsługa zdarzeń systemowych (WiFi, IP).
*   **esp_timer:** Obsługa timerów wysokiej precyzji.

## 3. Szczegóły Konfiguracji LVGL

Zadanie LVGL jest skonfigurowane w pliku `sunton_esp32_8048s050c.c`:
*   **Priorytet 4:** Jest wyższy niż standardowe zadania, co gwarantuje, że UI nie będzie "klatkować", gdy procesor coś liczy w tle.
*   **Timer Period (10ms):** LVGL wybudza się co 10ms, aby sprawdzić, czy musi coś narysować.
*   **Locking:** Używasz `lvgl_port_lock(0)`. To krytyczne! Każda funkcja LVGL wywołana z innego zadania (np. z `main`) **musi** być otoczona lockiem, inaczej system się zawiesi.

## 4. Gdzie jest miejsce na Twój kod?

Jeśli planujesz dodać np. obsługę czujników lub komunikację z serwerem:
1.  **Lekkie operacje:** Możesz je robić w `app_main` (pętla `while(1)`).
2.  **Ciężkie operacje (np. HTTP, Obliczenia):** Stwórz nowe zadanie przez `xTaskCreatePinnedToCore` i przypisz je do **Core 0**. Dzięki temu Twoje obliczenia nie spowolnią animacji na wyświetlaczu (które działają na Core 1).

## 5. Potencjalne Luki / Zagrożenia

*   **Stack Overflow:** Zadanie LVGL ma 8 KB stosu. Jeśli zaczniesz tam tworzyć ogromne tablice lokalne, system "wybuchnie". Używaj `malloc` (alokacji na stercie) dla dużych danych.
*   **Starvation:** Jeśli stworzysz własne zadanie o priorytecie 5 lub wyższym na Rdzeniu 1, UI całkowicie przestanie działać (zadanie LVGL nigdy nie dostanie czasu procesora).

---
*Dokumentacja wygenerowana na podstawie analizy plików main.cpp oraz sunton_esp32_8048s050c.c*
