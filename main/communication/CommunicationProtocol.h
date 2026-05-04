#pragma once

#include <stddef.h>
#include <stdint.h>

namespace CommunicationProtocol {

struct Snapshot {
    float temperatureC = 23.2f;
    uint8_t humidityPct = 32;
    uint16_t pressureHpa = 1012;
    uint16_t pm01UgM3 = 9;
    uint16_t pm25UgM3 = 23;
    uint16_t pm10UgM3 = 31;
    uint32_t sampleAgeMs = 0;
    uint32_t timeUnixSeconds = 0;
    uint8_t airQualityIndex = 1;
    char airQualityText[16] = "Dobra";
    bool valid = true;
    unsigned long updatedAtMs = 0;
};

struct DeviceSettings {
    bool buzzerEnabled = true;
    bool mqttEnabled = true;
    bool touchTestEnabled = true;
    bool backgroundMusicEnabled = true;
    bool pmsEnabled = true;
    bool valid = false;
};

void begin();
void reset();
void setSnapshot(const Snapshot& snapshot);
const Snapshot& currentSnapshot();
const DeviceSettings& currentSettings();
bool ingestFrame(const uint8_t* data, size_t length);
void updateUi();
void requestDeviceSettings();
void sendDeviceSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms);
void updateLocalSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms);

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

#ifdef __cplusplus
}
#endif