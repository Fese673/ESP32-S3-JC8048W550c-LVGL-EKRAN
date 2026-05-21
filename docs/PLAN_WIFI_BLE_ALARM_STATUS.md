# Plan: WiFi / BLE / Alarm (Bell) Status — Rozszerzenie protokołu UART

## 1. Stan obecny

| Status | ZEGAR-ESP32 (sender) | Guition ESP32-S3 (receiver) |
|--------|-------------------|---------------------------|
| **WiFi** | `kTypeWifiStatus` (0x07) wysyłany w `broadcastSnapshots()` co 2s. Payload: connected(1B)+rssi(1B)+ip(4B). | `case kTypeWifiStatus: break;` — ramka odebrana, ZIGNOROWANA. `FLOW_GLOBAL_VARIABLE_WIFI_STATE=11` istnieje w vars.h ale ustawiane z lokalnego Wi-Fi (Guition), nie z remote. |
| **BLE** | `audioBT_isConnected()` (atomic bool) + `ModeManager::isBtOn()` (radioMode == BT_ONLY). | `FLOW_GLOBAL_VARIABLE_BLE_STATE=12` istnieje. **Brak frame type** dla BLE → nie ma jak odebrać. |
| **Alarm/Bell** | `AlarmRuntime::state().alarmEnabled` + `alarmRinging`. | `FLOW_GLOBAL_VARIABLE_BELL_STATE=13` istnieje. **Brak frame type** dla alarmu → nie ma jak odebrać. |

## 2. Koncepcja — rozszerzenie protokołu

### 2.1 Nowe frame type

```c
kTypeStatusBle    = 0x0B   // BLE connection status (1 bajt: 0/1)
kTypeStatusBell   = 0x0C   // Alarm/bell status (1 bajt: 0=off, 1=armed, 2=ringing)
```

### 2.2 Modyfikacja istniejącego `kTypeWifiStatus` (0x07)

Obecny payload: `connected(1B) + rssi(1B) + ip[4](4B) = 6B`  
Nie zmieniamy — dodajemy tylko parsowanie po stronie Guition.

### 2.3 Nowe payloads

| Frame | Bajty | Znaczenie |
|-------|-------|-----------|
| `kTypeStatusBle` (0x0B) | 1 | `0` = BLE OFF / disconnected, `1` = BLE ON + connected |
| `kTypeStatusBell` (0x0C) | 1 | `0` = alarm wyłączony, `1` = alarm uzbrojony (alarmEnabled), `2` = dzwoni (alarmRinging) |

## 3. Miejsca hooków — ZEGAR-ESP32

### 3.1 Źródła danych

| Dane | Skąd wziąć | Plik |
|------|-----------|------|
| WiFi connected | `WiFi.status() == WL_CONNECTED` | już jest w `buildWifiPayload()` |
| BLE connected | `audioBT_isConnected()` | `AudioBT.cpp:205` |
| BLE mode ON | `ModeManager::isBtOn()` | `ModeManager.cpp:167` |
| Alarm enabled | `AlarmRuntime::state().alarmEnabled` | `AlarmRuntime.h:14` |
| Alarm ringing | `AlarmRuntime::state().alarmRinging` | `AlarmRuntime.h:15` |

### 3.2 Nowe funkcje w `EsptoGuitionState.h/.cpp`

```cpp
struct StatusBlePayload {
    uint8_t bleOn : 1;    // 0/1 — radio BT włączone
    uint8_t connected : 1; // 0/1 — połączenie A2DP
};

struct StatusBellPayload {
    uint8_t alarmEnabled : 1;
    uint8_t alarmRinging : 1;
};

bool buildStatusBlePayload(StatusBlePayload &out);
bool buildStatusBellPayload(StatusBellPayload &out);
```

### 3.3 Nowe funkcje send w `Esptogution.h/.cpp`

```cpp
void sendStatusBle(uint8_t sequence = 0);
void sendStatusBell(uint8_t sequence = 0);
```

Wzorować się na `sendWifiStatus()`.

### 3.4 Hook w `broadcastSnapshots()` — `Esptogution.cpp:141`

Dodać wysyłanie BLE i Bell w pętli broadcast, analogicznie do WiFI:

```
broadcastSnapshots(nowMs):
  ...
  // dodac: co 2000ms -> sendStatusBle() jesli stan sie zmienil
  // dodac: co 2000ms -> sendStatusBell() jesli stan sie zmienil
  ...
```

**Nie trzeba nowego EventBus handlera** — `broadcastSnapshots()` jest wołany z `EsptoGuition::update()` w każdym loop.

## 4. Miejsca hooków — Guition ESP32-S3

### 4.1 Rejestracja frame type w `CommunicationProtocol.h`

```cpp
kTypeStatusBle    = 0x0B,
kTypeStatusBell   = 0x0C,
```

### 4.2 Parsowanie w `UartTransport.cpp:handleFrame()`

```cpp
case kTypeStatusBle:
    // payload[0] -> bleOn, connected
    break;
case kTypeStatusBell:
    // payload[0] -> alarmEnabled, alarmRinging
    break;
```

### 4.3 Funkcje apply w `CommunicationState.h/.cpp`

```cpp
// Nowe struktury stanu
static bool s_bleConnected = false;
static bool s_bellEnabled = false;
static bool s_bellRinging = false;
static volatile bool s_bleDirty = false;
static volatile bool s_bellDirty = false;

void applyStatusBlePayload(const uint8_t *payload, uint16_t len);
void applyStatusBellPayload(const uint8_t *payload, uint16_t len);

bool consumeBleStatus(bool &bleConnected);
bool consumeBellStatus(bool &bellEnabled, bool &bellRinging);
```

### 4.4 UI mapping w `CommUiMapper.h/.cpp`

Dodać:

```cpp
void applyStatusBleToUi(bool bleConnected);
void applyStatusBellToUi(bool bellEnabled, bool bellRinging);
```

W `applyStatusBleToUi()`:
```cpp
eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BLE_STATE, eez::Value(bleConnected ? 1 : 0));
```

W `applyStatusBellToUi()`:
```cpp
int bellState = 0; // 0=off, 1=armed, 2=ringing
if (bellRinging) bellState = 2;
else if (bellEnabled) bellState = 1;
eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BELL_STATE, eez::Value(bellState));
```

**WiFi status** — analogicznie:
```cpp
void applyWifiStatusToUi(bool connected, int8_t rssi, const uint8_t *ip);
// setGlobalVariable(WIFI_STATE, connected ? 1 : 0)
```

### 4.5 Wywołanie w `CommunicationProtocol.cpp::updateUi()`

Dodać sekcje:
```cpp
{
    bool bleConnected;
    if (consumeBleStatus(bleConnected)) {
        applyStatusBleToUi(bleConnected);
    }
}
{
    bool bellEnabled, bellRinging;
    if (consumeBellStatus(bellEnabled, bellRinging)) {
        applyStatusBellToUi(bellEnabled, bellRinging);
    }
}
```

## 5. Integracja z istniejącą logiką powiadomień (notification_logic)

Na Guition istnieje `main/app/notification_logic.cpp` które zarządza ikonkami w pasku. Ono jest wywoływane z wielu miejsc i używa `FLOW_GLOBAL_VARIABLE_WIFI_STATE`, `BLE_STATE`, `BELL_STATE`.

**Po zmianach**: te flow variables będą ustawiane z danych z UART (a nie lokalnie) → notification_logic odczyta je przez EEZ Flow → ikonki zadziałają automatycznie.

## 6. Podział na etapy

### Etap 1 — ZEGAR-ESP32: wysyłanie BLE i Bell statusu

**Pliki:**
1. `src/comms/esp_to_gution/EsptoGuitionState.h` — dodać `StatusBlePayload`, `StatusBellPayload`, `buildStatusBlePayload()`, `buildStatusBellPayload()`
2. `src/comms/esp_to_gution/EsptoGuitionState.cpp` — implementacja build functions
3. `src/comms/esp_to_gution/Esptogution.h` — dodać `sendStatusBle()`, `sendStatusBell()`
4. `src/comms/esp_to_gution/Esptogution.cpp` — implementacja send + hook w `broadcastSnapshots()`

**Build:** PlatformIO (`pio run`)

### Etap 2 — Guition: obsługa WiFi status (0x07)

**Pliki:**
1. `main/communication/CommunicationState.h` — dodać `applyWifiStatusPayload()`, `consumeWifiStatus()`
2. `main/communication/CommunicationState.cpp` — implementacja
3. `main/communication/CommUiMapper.h` — dodać `applyWifiStatusToUi()`
4. `main/communication/CommUiMapper.cpp` — implementacja + resetUi
5. `main/communication/UartTransport.cpp` — `case kTypeWifiStatus: applyWifiStatusPayload(...); break;`
6. `main/communication/CommunicationProtocol.cpp` — `updateUi()`: consume + apply

**Build:** `python build_guition.py`

### Etap 3 — Guition: obsługa BLE i Bell statusu (0x0B, 0x0C)

**Pliki:**
1. `main/communication/CommunicationProtocol.h` — dodać `kTypeStatusBle = 0x0B`, `kTypeStatusBell = 0x0C`
2. `main/communication/CommunicationState.h` — dodać apply/consume dla BLE i Bell
3. `main/communication/CommunicationState.cpp` — implementacja
4. `main/communication/CommUiMapper.h` — dodać apply functions
5. `main/communication/CommUiMapper.cpp` — implementacja + resetUi
6. `main/communication/UartTransport.cpp` — case dla 0x0B i 0x0C
7. `main/communication/CommunicationProtocol.cpp` — updateUi

**Build:** `python build_guition.py`

### Etap 4 — Testy regresji

1. Sprawdzić czy WE/WY WiFi na Guition nadal działa (`FLOW_GLOBAL_VARIABLE_WIFI_STATE` lokalny vs zdalny)
2. Sprawdzić czy ikonki BLE/bell reagują na zmiany z ZEGAR-ESP32
3. Sprawdzić czy `notification_logic_refresh()` nie migocze (już ma guard `lvgl_port_lock`)
4. Sprawdzić czy `kTypeWifiStatus` nie koliduje z lokalnym WiFi Guition

## 7. Uwagi techniczne

### 7.1 Thread safety — ZEGAR-ESP32
- `audioBT_isConnected()` używa `std::atomic<bool>` — bezpieczne
- `AlarmRuntime::state()` to zwykły struct — reader single-thread (loop task) → atomic reads
- `broadcastSnapshots()` wołany z głównego loop → ten sam wątek co reszta

### 7.2 Thread safety — Guition
- Wzorzec `consumeXxx()` z mutexem → taki sam jak istniejący dla SystemResources
- `applyXxxToUi()` wołane z `updateUi()` → LVGL task → LVGL API safe

### 7.3 Konflikt WiFi status
Obecnie `FLOW_GLOBAL_VARIABLE_WIFI_STATE=11` jest ustawiane lokalnie na Guition przez `notification_logic_set_wifi()`. Po dodaniu zdalnego WiFi statusu trzeba rozdzielić: zdalny WiFi status (z ZEGAR-ESP32) dotyczy **connectivity sensora**, lokalny (Guition) dotyczy **connectivity wyświetlacza**.

**Decyzja:** Potrzebne nowe flow variable np. `FLOW_GLOBAL_VARIABLE_SENSOR_WIFI_STATE = 53`, żeby nie mieszać z lokalnym WiFi. Analogicznie dla BLE i Bell jeśli są oddzielne.

### 7.4 Zależność od EEZ Studio
Nowe flow variable (53, 54, 55) wymagają dodania w EEZ Studio i re-eksportu → `vars.h`. Alternatywnie można pominąć EEZ Flow i robić `lv_label_set_text`/`lv_obj_add_flag` bezpośrednio — tak jak zrobiono z world clock na zegarze (ERR_011).
