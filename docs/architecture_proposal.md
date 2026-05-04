# Propozycja Architektury: Modular ESP-S3

Aby projekt był łatwy w rozbudowie, musimy podzielić go na warstwy. Obecnie wszystko miesza się w folderze `main`. Poniżej znajduje się docelowy schemat.

## 1. Struktura Folderów

```text
main/
├── hal/                # Hardware Abstraction Layer (Sprzęt)
│   ├── display/        # sunton_esp32_8048s050c.c/.h
│   ├── touch/          # Sterownik dotyku (GT911)
│   └── peripherals/    # GPIO, czujniki, przekaźniki
├── services/           # Usługi działające w tle (Core 0)
│   ├── wifi/           # Manager połączeń WiFi
│   ├── mqtt/           # Komunikacja z chmurą / Home Assistant
│   └── storage/        # Obsługa NVS i plików konfiguracyjnych
├── ui/                 # Interfejs Użytkownika (Core 1)
│   ├── ui/             # Kod wygenerowany z EEZ Studio (nie ruszamy)
│   └── actions/        # Ręcznie pisane akcje wywoływane przez przyciski
├── app/                # Logika Biznesowa (Mózg systemu)
│   ├── state_manager.c # Maszyna stanów (np. tryb AUTO, MANUAL)
│   └── app_main.cpp    # Inicjalizacja wszystkich modułów
└── utils/              # Narzędzia pomocnicze (Logging, JSON, Time)
```

## 2. Podział Odpowiedzialności (CPU Cores)

### Rdzeń 1 (Core 1): UI & Interaction
*   **Zadanie:** `taskLVGL`
*   **Odpowiedzialność:** Renderowanie klatek, animacje, dekodowanie obrazów, obsługa dotyku.
*   **Zasada:** Nie wolno tu wywoływać funkcji blokujących (np. czekanie na odpowiedź z internetu), bo UI "zamarznie".

### Rdzeń 0 (Core 0): System & Background
*   **Zadanie:** `main_task`, `wifi_task`, `mqtt_task`
*   **Odpowiedzialność:** Utrzymywanie połączenia WiFi, wysyłanie danych do bazy, odczyt ciężkich czujników, aktualizacja OTA.
*   **Komunikacja:** Przekazuje dane do UI za pomocą **LVGL Lock** lub **FreeRTOS Queues**.

## 3. Komunikacja między Modułami (Model Mediator)

Zalecam unikanie bezpośredniego odwoływania się modułów do siebie. 
*   **Zły przykład:** Sterownik czujnika bezpośrednio zmienia tekst na etykiecie LVGL.
*   **Dobry przykład:** Sterownik czujnika aktualizuje zmienną w `state_manager`, a `state_manager` (przez EEZ Flow lub LVGL timer) aktualizuje UI.

## 4. Zalety tego podejścia

1.  **Czysty EEZ Studio:** Pliki w `main/ui/ui/` mogą być nadpisywane przez generator, a Twoja logika w `main/ui/actions/` zostanie bezpieczna.
2.  **Skalowalność:** Jeśli będziesz chciał zmienić ekran na inny model, zmieniasz tylko plik w `hal/display/`, reszta kodu nie wie o zmianie.
3.  **Brak lagów:** Nawet jeśli WiFi będzie miało problem z połączeniem, Twoje animacje w UI będą nadal działać płynnie (bo są na innym rdzeniu).

---
*Czy chciałbyś, abym pomógł Ci teraz fizycznie przenieść pliki do takiej struktury i przygotować CMakeLists.txt?*
