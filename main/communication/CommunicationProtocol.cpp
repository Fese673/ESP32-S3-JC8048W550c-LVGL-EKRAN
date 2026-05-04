#include "communication/CommunicationProtocol.h"
#include "communication/CobsFraming.h"
#include "communication/CommunicationState.h"
#include "communication/UartTransport.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "screens.h"
#include "eez-flow.h"
#include "vars.h"

namespace CommunicationProtocol {
namespace {

constexpr char kTag[] = "COMM";
constexpr uint8_t kTypeSettings = 0x04;
constexpr uint8_t kTypeSetSettings = 0x05;
constexpr uint8_t kTypeRequest = 0x10;

uint32_t currentMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

static void formatWithComma(char *buffer, size_t bufferSize, float value,
                            const char *suffix) {
  if (buffer == nullptr || bufferSize == 0U) {
    return;
  }

  snprintf(buffer, bufferSize, "%.1f%s", value, suffix);
  for (char *cursor = buffer; *cursor != '\0'; ++cursor) {
    if (*cursor == '.') {
      *cursor = ',';
    }
  }
}

static void applySnapshotToMainScreen(const Snapshot &snapshot) {
  if (!snapshot.valid) {
    return;
  }

  static char temperatureBuffer[24];
  static char humidityBuffer[16];
  static char pressureBuffer[16];
  static char pm01Buffer[16];
  static char pm10Buffer[16];
  static char pm25Buffer[16];
  static char airQualityTextBuffer[24];

  snprintf(pressureBuffer, sizeof(pressureBuffer), "%uhPa", snapshot.pressureHpa);
  snprintf(pm01Buffer, sizeof(pm01Buffer), "PM1 %u", snapshot.pm01UgM3);
  snprintf(pm10Buffer, sizeof(pm10Buffer), "PM10 %u", snapshot.pm10UgM3);
  snprintf(pm25Buffer, sizeof(pm25Buffer), "PM2.5 %u", snapshot.pm25UgM3);
  snprintf(humidityBuffer, sizeof(humidityBuffer), "%u%%", snapshot.humidityPct);
  snprintf(airQualityTextBuffer, sizeof(airQualityTextBuffer), "%s", snapshot.airQualityText);
  formatWithComma(temperatureBuffer, sizeof(temperatureBuffer), snapshot.temperatureC, "°C");

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR, eez::Value(pressureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT, eez::Value(airQualityTextBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM1_0_SENSOR, eez::Value(pm01Buffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR, eez::Value(pm10Buffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR, eez::Value(humidityBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR, eez::Value(temperatureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR, eez::Value(pm25Buffer));
}

} // namespace

void begin() {
  beginUart();
  requestDeviceSettings();
}

void reset() {
  Snapshot emptySnapshot{};
  emptySnapshot.valid = false;
  emptySnapshot.updatedAtMs = currentMs();
  setLocalSnapshot(emptySnapshot);
}

void setSnapshot(const Snapshot &snapshot) {
  setLocalSnapshot(snapshot);
}

const Snapshot &currentSnapshot() {
  static Snapshot cachedSnapshot;
  cachedSnapshot = getLocalSnapshot();
  return cachedSnapshot;
}

const DeviceSettings &currentSettings() {
  static DeviceSettings cachedSettings;
  cachedSettings = getLocalSettings();
  return cachedSettings;
}

bool ingestFrame(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0U) {
    return false;
  }
  bool consumed = false;
  for (size_t i = 0; i < length; ++i) {
    consumed = ingestByte(data[i]) || consumed;
  }
  return consumed;
}

void requestDeviceSettings() {
  uint8_t payload[1] = { kTypeSettings };
  sendRawFrame(kTypeRequest, 0, payload, 1);
}

void sendDeviceSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms) {
  uint8_t payload[5];
  payload[0] = buzzer ? 1 : 0;
  payload[1] = mqtt ? 1 : 0;
  payload[2] = touch ? 1 : 0;
  payload[3] = music ? 1 : 0;
  payload[4] = pms ? 1 : 0;
  sendRawFrame(kTypeSetSettings, 0, payload, sizeof(payload));
}

void updateLocalSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms) {
  DeviceSettings ds;
  ds.buzzerEnabled = buzzer;
  ds.mqttEnabled = mqtt;
  ds.touchTestEnabled = touch;
  ds.backgroundMusicEnabled = music;
  ds.pmsEnabled = pms;
  ds.valid = true;
  setLocalSettings(ds);
}

void updateUi() {
  if (isSettingsDirty()) {
    DeviceSettings ds = getLocalSettings();
    clearSettingsDirty();

    if (ds.valid) {
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BUZZER_STATE, eez::Value(ds.buzzerEnabled ? 1 : 0));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MQTT_STATE, eez::Value(ds.mqttEnabled ? 1 : 0));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TOUCH_STATE, eez::Value(ds.touchTestEnabled ? 1 : 0));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_STATE, eez::Value(ds.backgroundMusicEnabled ? 1 : 0));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PMS_STATE, eez::Value(ds.pmsEnabled ? 1 : 0));
    }
  }

  if (!isDirty()) {
    return;
  }

  Snapshot snapshotCopy = getLocalSnapshot();
  applySnapshotToMainScreen(snapshotCopy);
  clearDirty();
}

} // namespace CommunicationProtocol

extern "C" {

void action_refresh_settings() {
  CommunicationProtocol::requestDeviceSettings();
}

void action_toggle_buzzer() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(!ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled);
  CommunicationProtocol::sendDeviceSettings(!ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled);
}

void action_toggle_mqtt() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, !ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, !ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled);
}

void action_toggle_touch() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, !ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, !ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled);
}

void action_toggle_music() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, !ds.backgroundMusicEnabled, ds.pmsEnabled);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, !ds.backgroundMusicEnabled, ds.pmsEnabled);
}

void action_toggle_pms() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, !ds.pmsEnabled);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, !ds.pmsEnabled);
}

void action_action_refresh_settings() {
  action_refresh_settings();
}

void action_action_toggle_buzzer() {
  action_toggle_buzzer();
}

void action_action_toggle_mqtt() {
  action_toggle_mqtt();
}

void action_action_toggle_touch() {
  action_toggle_touch();
}

void action_action_toggle_music() {
  action_toggle_music();
}

void action_action_toggle_pms() {
  action_toggle_pms();
}

}
