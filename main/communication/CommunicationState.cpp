#include "communication/CommunicationState.h"
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace CommunicationProtocol {
namespace {

constexpr char kTag[] = "COMM_STATE";
SemaphoreHandle_t s_mutex = nullptr;

Snapshot s_snapshot;
DeviceSettings s_deviceSettings;

volatile bool s_dirty = false;
volatile bool s_settingsDirty = false;

uint32_t currentMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void initMutex() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

void deriveAirQualityText(Snapshot &snapshot) {
  if (snapshot.pm25UgM3 <= 12) {
    snapshot.airQualityIndex = 1;
    snprintf(snapshot.airQualityText, sizeof(snapshot.airQualityText), "Dobra");
    return;
  }
  if (snapshot.pm25UgM3 <= 35) {
    snapshot.airQualityIndex = 2;
    snprintf(snapshot.airQualityText, sizeof(snapshot.airQualityText), "Srednia");
    return;
  }
  snapshot.airQualityIndex = 3;
  snprintf(snapshot.airQualityText, sizeof(snapshot.airQualityText), "Zla");
}

} // namespace

void lockSnapshot() {
  initMutex();
  if (s_mutex != nullptr) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
  }
}

void unlockSnapshot() {
  if (s_mutex != nullptr) {
    xSemaphoreGive(s_mutex);
  }
}

Snapshot getLocalSnapshot() {
  lockSnapshot();
  Snapshot copy = s_snapshot;
  unlockSnapshot();
  return copy;
}

void setLocalSnapshot(const Snapshot& s) {
  lockSnapshot();
  s_snapshot = s;
  deriveAirQualityText(s_snapshot);
  s_dirty = true;
  unlockSnapshot();
}

DeviceSettings getLocalSettings() {
  lockSnapshot();
  DeviceSettings copy = s_deviceSettings;
  unlockSnapshot();
  return copy;
}

void setLocalSettings(const DeviceSettings& ds) {
  lockSnapshot();
  s_deviceSettings = ds;
  s_settingsDirty = true;
  unlockSnapshot();
}

bool isDirty() { return s_dirty; }
void clearDirty() { s_dirty = false; }

bool isSettingsDirty() { return s_settingsDirty; }
void clearSettingsDirty() { s_settingsDirty = false; }

void applyWeatherPayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payloadLength != 11U) {
    ESP_LOGW(kTag, "Weather payload length invalid");
    return;
  }
  Snapshot nextSnapshot = getLocalSnapshot();

  const int16_t tempCx100 = static_cast<int16_t>(payload[0] | (payload[1] << 8U));
  const uint16_t humPctX100 = static_cast<uint16_t>(payload[2] | (payload[3] << 8U));
  const uint16_t pressHpaX10 = static_cast<uint16_t>(payload[4] | (payload[5] << 8U));
  const uint32_t sampleAgeMs = static_cast<uint32_t>(payload[6]) |
                               (static_cast<uint32_t>(payload[7]) << 8U) |
                               (static_cast<uint32_t>(payload[8]) << 16U) |
                               (static_cast<uint32_t>(payload[9]) << 24U);
  const uint8_t flags = payload[10];

  if ((flags & 0x01U) != 0U) {
    nextSnapshot.temperatureC = static_cast<float>(tempCx100) / 100.0f;
  }
  if ((flags & 0x02U) != 0U) {
    nextSnapshot.humidityPct = static_cast<uint8_t>(humPctX100 / 100U);
  }
  if ((flags & 0x04U) != 0U) {
    nextSnapshot.pressureHpa = static_cast<uint16_t>(pressHpaX10 / 10U);
  }

  nextSnapshot.sampleAgeMs = sampleAgeMs;
  nextSnapshot.valid = true;
  nextSnapshot.updatedAtMs = currentMs();

  setLocalSnapshot(nextSnapshot);
}

void applyPmsPayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payloadLength != 23U) {
    ESP_LOGW(kTag, "PMS payload length invalid");
    return;
  }
  Snapshot nextSnapshot = getLocalSnapshot();

  nextSnapshot.pm01UgM3 = static_cast<uint16_t>(payload[0] | (payload[1] << 8U));
  nextSnapshot.pm25UgM3 = static_cast<uint16_t>(payload[2] | (payload[3] << 8U));
  nextSnapshot.pm10UgM3 = static_cast<uint16_t>(payload[4] | (payload[5] << 8U));
  nextSnapshot.sampleAgeMs = static_cast<uint32_t>(payload[18]) |
                             (static_cast<uint32_t>(payload[19]) << 8U) |
                             (static_cast<uint32_t>(payload[20]) << 16U) |
                             (static_cast<uint32_t>(payload[21]) << 24U);
  nextSnapshot.valid = true;
  nextSnapshot.updatedAtMs = currentMs();

  setLocalSnapshot(nextSnapshot);
}

void applyTimePayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payloadLength != 5U) {
    ESP_LOGW(kTag, "Time payload length invalid");
    return;
  }
  const uint32_t unixSeconds = static_cast<uint32_t>(payload[0]) |
                               (static_cast<uint32_t>(payload[1]) << 8U) |
                               (static_cast<uint32_t>(payload[2]) << 16U) |
                               (static_cast<uint32_t>(payload[3]) << 24U);
  const uint8_t valid = payload[4];

  lockSnapshot();
  s_snapshot.timeUnixSeconds = unixSeconds;
  s_snapshot.valid = s_snapshot.valid || (valid != 0U);
  s_snapshot.updatedAtMs = currentMs();
  s_dirty = true;
  unlockSnapshot();
}

void applySettingsPayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payloadLength == 5U) {
    DeviceSettings ds;
    ds.buzzerEnabled = (payload[0] != 0);
    ds.mqttEnabled = (payload[1] != 0);
    ds.touchTestEnabled = (payload[2] != 0);
    ds.backgroundMusicEnabled = (payload[3] != 0);
    ds.pmsEnabled = (payload[4] != 0);
    ds.valid = true;
    setLocalSettings(ds);
  }
}

} // namespace CommunicationProtocol
