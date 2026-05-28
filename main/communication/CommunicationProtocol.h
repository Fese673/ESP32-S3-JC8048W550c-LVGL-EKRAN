#pragma once

#include <stddef.h>
#include <stdint.h>

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
    kTypeAck = 0xFF
 };

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
    uint16_t eco2 = 0;
    uint16_t tvoc = 0;
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
void sendDeviceSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms, int alarmMelody);
void updateLocalSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms, int alarmMelody);
void sendMusicCommand(uint8_t cmd);
void sendMusicVolume(uint8_t vol);
void sendMusicEQ(uint8_t bass, uint8_t mid, uint8_t treble);
void requestMusicState();
void sendRadioModeSwitch();

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