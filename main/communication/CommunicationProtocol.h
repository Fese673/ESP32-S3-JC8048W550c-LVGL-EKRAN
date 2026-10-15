#pragma once

#include <stddef.h>
#include <stdint.h>
#include "communication/time/timer_synchro.h"

namespace CommunicationProtocol {
 
 enum FrameType {
   kTypeIndoorWeather = 0x01,
   kTypePms = 0x02,
   kTypeTime = 0x03,
   kTypeSettings = 0x04,
   kTypeSetSettings = 0x05,
   kTypeSystemResources = 0x06,
   kTypeWifiStatus = 0x07,
    kTypeOutdoorWeather = 0x08,
    kTypePpg = 0x09,
    kTypeBpmStatus = 0x0A,
    kTypeStatusBle = 0x0B,
    kTypeStatusBell = 0x0C,
    kTypeRequest = 0x10,
    kTypeConfig = 0x11,
    kTypeHello    = 0x20,
    kTypeHelloAck = 0x21,
    kTypeMusicMetadata = 0x30,
    kTypeMusicStatus = 0x31,
    kTypeMusicCommand = 0x32,
    kTypeMusicVolume = 0x33,
    kTypeMusicEQ = 0x34,
    kTypeMusicRequest = 0x35,
    kTypeMusicVolumeState = 0x36,
    kTypeMusicEQState = 0x37,
    kTypeRadioMode = 0x38,
    // Radar LD2410 (ZEGAR ESP32 -> Guition). Lustro STM32 0xA3 + 18 B bramek:
    // [presence][movD u16 LE][movE][statD u16 LE][statE][detect][movGate0..8][statGate0..8],
    // 28 B = + [detect_lo,detect_hi] append-only na [26..27] (pelny dystans u16 LE).
    kTypeRadar = 0x39,       // Z -> G, 8, 26 lub 28 B
    // Silnik ekranu (Guition -> ZEGAR ESP32 -> STM32). Lustro kontraktu
    // ZEGAR-ESP32 (Esptogution.h: kTypeStepperCmd/State + sub-komendy).
    // Zmieniasz tu = zmieniasz tam.
    kTypeStepperCmd = 0x3A,   // G -> Z: [sub][args]
    kTypeStepperState = 0x3B, // Z -> G: [busy][pos i32 LE][targetDeg u16 LE][flags][speed u16 LE][homeDeg u16 LE]
    // TimeSyncProtocol — alarmy + minutnik
    kTypeAlarmList      = 0x0D,
    kTypeSetAlarm       = 0x0E,
    kTypeTimerState     = 0x0F,
    kTypeTimerCmd       = 0x12,
    kTypeEditLock       = 0x13,
    kTypeAlarmAction    = 0x14,
    kTypeStopwatchState = 0x15,
    kTypeStopwatchCmd   = 0x16,
    kTypeAck = 0xFF
 };

 // 0x11 kTypeConfig — panel "Ustawienia WiFi" (lustro ZEGAR-ESP32
 // Esptogution.h: stale kWifiSub / kWifiResp — zmieniasz tu = zmieniasz tam).
 // G -> Z: [sub][args]; Z -> G: [sub|0x80][args].
 constexpr uint8_t kCfgSubScanReq = 0x01; // [] prosba o skan
 constexpr uint8_t kCfgSubSetCred = 0x02; // [len][ssid][len][pass] test+zapis
 constexpr uint8_t kCfgSubInfoReq = 0x03; // [] stan WiFi + zapisany SSID
 constexpr uint8_t kCfgRespScan = 0x81;   // [status][total][index][count]+n*[rssi i8][flags][len][ssid]
 constexpr uint8_t kCfgRespCred = 0x82;   // [code][reason u16 LE][len][ssid]
 constexpr uint8_t kCfgRespInfo = 0x83;   // [wifiOn][btMode][connected][len][ssid]
 constexpr uint8_t kCfgNetOpen = 0x01;    // flaga sieci: otwarta (bez hasla)

 // Stany maszyny synchronizacji
 enum PeerSyncState {
   kSyncBooting          = 0,
   kSyncUartReady        = 1,
   kSyncPeerDetected     = 2,
   kSyncFirstSyncSent    = 3,
   kSyncFirstSyncConfirmed = 4,
   kSyncSteadyState      = 5
 };

 // Payload ramki HELLO / HELLO_ACK
 struct HelloPayload {
   uint8_t  protocolVersion; // Aktualna wersja: 1
   uint8_t  deviceRole;      // 0 = Display (Guition), 1 = Sensor (Zegar)
   uint32_t bootId;          // Losowe ID generowane przy każdym starcie
   uint32_t uptimeMs;        // Czas od startu w ms
   uint8_t  syncState;       // PeerSyncState
 };

struct Snapshot {
    float temperatureC = 0.0f;
    uint8_t humidityPct = 0;
    uint16_t pressureHpa = 0;
    uint16_t pm01UgM3 = 0;
    uint16_t pm25UgM3 = 0;
    uint16_t pm10UgM3 = 0;
    uint16_t count0p3 = 0;
    uint16_t count0p5 = 0;
    uint16_t count1p0 = 0;
    uint16_t count2p5 = 0;
    uint16_t count5p0 = 0;
    uint16_t count10p0 = 0;
    uint16_t eco2 = 0;
    uint16_t tvoc = 0;
    // Stopien kalibracji ENS160 z ZEGAR (bajt 15 ramki 0x01):
    // 0=rozgrzewanie, 1=nieskalibrowane, 2=wstepnie, 3=w pelni, 0xFF=brak.
    uint8_t gasStage = 0xFF;
    uint32_t sampleAgeMs = 0;
    uint32_t timeUnixSeconds = 0;
    uint8_t airQualityIndex = 0;
    char airQualityText[16] = "";
    bool valid = false;
    unsigned long updatedAtMs = 0;
};

struct OutdoorWeatherSnapshot {
    float temperatureC = 0.0f;
    uint8_t humidityPct = 0;
    uint16_t pressureHpa = 0;
    float windSpeedMs = 0.0f;
    float windGustMs = 0.0f;
    uint8_t windDeg = 0;
    uint8_t weatherCode = 0;
    uint8_t cloudCover = 0;
    float apparentTempC = 0.0f;
    uint16_t pm25UgM3 = 0;
    uint16_t pm10UgM3 = 0;
    uint16_t co2Ppm = 0;
    uint8_t aqi = 0;
    char airQualityText[16] = "";
    float precipitationMm = 0.0f;
    float uvIndex = 0.0f;
    uint8_t sunriseHour = 0;
    uint8_t sunriseMin = 0;
    uint8_t sunsetHour = 0;
    uint8_t sunsetMin = 0;
    uint16_t no2UgM3 = 0;
    bool valid = false;
    unsigned long updatedAtMs = 0;
};

struct DeviceSettings {
    bool buzzerEnabled = true;
    bool mqttEnabled = true;
    bool touchTestEnabled = true;
    bool backgroundMusicEnabled = true;
    bool pmsEnabled = true;
    int alarmMelodyIndex = 0;
    /* Etap 3: jasnosc 7-seg (0..100). Trzymana w struct dla UI flow
     * oraz protokolu. Wartosc domyslna 100 = pelna jasnosc. */
    uint8_t sevenSegBrightness = 100;
    bool valid = false;
};

struct SystemResources {
    uint32_t freeRam = 0;
    uint32_t heapRam = 0;
    uint32_t dmaRam = 0;
    uint8_t core0Cpu = 0;
    uint8_t core1Cpu = 0;
    uint32_t freeFlash = 0;
    uint32_t usedFlash = 0;
    uint16_t underrunsAudio = 0;
    uint16_t overflowAudio = 0;
    uint16_t dropsAudio = 0;
    uint16_t errorsI2c = 0;
    uint16_t timeoutsI2c = 0;
    bool valid = false;
};

void begin();
void reset();
void setSnapshot(const Snapshot& snapshot);
const Snapshot& currentSnapshot();
const DeviceSettings& currentSettings();
const SystemResources& currentResources();
bool ingestFrame(const uint8_t* data, size_t length);
void updateUi();
void requestDeviceSettings();
void requestSystemResources();
/* Etap 3: 7-bajtowy payload kTypeSetSettings (bajt [6] = sevenSegBrightness 0..100). */
void sendDeviceSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms, int alarmMelody, uint8_t sevenSegBrightness);
void updateLocalSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms, int alarmMelody, uint8_t sevenSegBrightness);
void sendMusicCommand(uint8_t cmd);
void sendMusicVolume(uint8_t vol);
void sendMusicEQ(uint8_t bass, uint8_t mid, uint8_t treble);
void requestMusicState();
void sendRadioModeSwitch();
// Silnik ekranu — sub-komendy 0x3A (lustro Esptogution.h).
// EXECUTE: ZEGAR zapisuje kat i sam jedzie (MOVE_ABS). HOLD: chunk 2048,
// puszczenie = STOP (jak live w skrypcie PC). STOP idempotentny.
// CAL: 0 = zapisz biezacy pos jako zero, 1 = zapisz rozpietosc + wroc do 0.
void sendStepperExecute(uint16_t deg, uint16_t speed);
void sendStepperHold(int8_t dir, uint16_t speed);
void sendStepperStop();
void sendStepperCal(uint8_t step);
// CAL_MODE: [en] — wstrzymaj automat radaru na czas kalibracji (STM32 trzyma
// do en=0 albo auto-timeout). Guition: en=1 przy wejsciu w kalibracje
// (+ keepalive), en=0 po zapisie (calOk 0->1) / wyjsciu z ekranu.
void sendStepperCalMode(bool en);
// Cel powrotu po resecie (double-click presetu) — preferencja w STM32.
void sendStepperSetHome(uint16_t deg);
// Wybrany kat z UI (single-click presetu) — zapis w NVS ZEGAR-a, bez ruchu.
// Automaty (radar) zmieniaja tylko pozycje silnika; tego NIE nadpisuja.
void sendStepperSetTarget(uint16_t deg);
// Automat LD2410C (przycisk na geometrii) — preferencja we flash STM32.
void sendStepperSetRadar(bool en);
// Czas auto-zjazdu radaru (panel opcji na ekranie RADAR) — presety
// 30/180/600/2700 s; walidacja + zapis we flash STM32, echo wraca ramka 0x3B.
void sendStepperSetAutoClose(uint16_t seconds);
// Czulosc OUT radaru (ekran radaru) — presety 0=SREDNIA, 1=WYSOKA, 2=MALA;
// walidacja + zapis we flash STM32, echo wraca ramka 0x3B (bity 2-3 flags).
void sendStepperSetSens(uint8_t code);
// "Plynne wylaczenie" (panel opcji na ekranie RADAR) — bramka premium dimu
// 7-seg przy zjezdzie; zapis we flash STM32, echo wraca ramka 0x3B (bit4 flags).
void sendStepperSetOnOff(bool en);
// Predkosc auto-powrotu (suwak/presety na Geometrii) — persist we flash
// STM32, echo wraca ramka 0x3B (pole speed). Wolane tylko na commit
// (RELEASED suwaka / klik presetu), nie w trakcie przeciagania.
void sendStepperSetSpeed(uint16_t speed);
void requestStepperState();

/* ── Panel "Ustawienia WiFi" (0x11 kTypeConfig) ────────────────────────
 * Ujednolicone WiFi (V3.0): ekran jest TYLKO HMI (WIFI_ENABLE=0). Skan,
 * test hasla, zapis i przelaczenie robi ZEGAR; wszystko leci po kablu.
 * Lustro protokolu: ZEGAR-ESP32 Esptogution.h (stale kWifiSub / kWifiResp).
 * Odpowiedzi: 0x81 skan (paginowany), 0x82 wynik testu, 0x83 info. */
void sendWifiScanRequest();
void sendWifiSetCredentials(const char* ssid, const char* password);
void sendWifiInfoRequest();

}  // namespace CommunicationProtocol

#ifdef __cplusplus
extern "C" {
#endif

void action_refresh_settings();
void action_toggle_buzzer();
void action_toggle_mqtt();
void action_toggle_touch();
void action_toggle_music();
void action_toggle_pms();

void action_action_refresh_settings();
void action_action_toggle_buzzer();
void action_action_toggle_mqtt();
void action_action_toggle_touch();
void action_action_toggle_music();
void action_action_toggle_pms();

void action_send_music_command(uint8_t cmd);
void action_send_music_volume(uint8_t vol);
void action_send_music_eq(uint8_t bass, uint8_t mid, uint8_t treble);
void action_request_music_state();
void action_send_radio_mode_switch();
int action_get_flow_variable_int(uint32_t var_id);
void action_get_flow_variable_text(uint32_t var_id, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif