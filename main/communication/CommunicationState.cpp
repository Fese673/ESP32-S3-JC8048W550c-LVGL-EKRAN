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
SystemResources s_systemResources;

volatile bool s_dirty = false;
volatile bool s_settingsDirty = false;
volatile bool s_resourcesDirty = false;

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

SystemResources getLocalResources() {
  lockSnapshot();
  SystemResources copy = s_systemResources;
  unlockSnapshot();
  return copy;
}

void setLocalResources(const SystemResources& sr) {
  lockSnapshot();
  s_systemResources = sr;
  s_resourcesDirty = true;
  unlockSnapshot();
}

bool isDirty() { return s_dirty; }
void clearDirty() { s_dirty = false; }

bool isSettingsDirty() { return s_settingsDirty; }
void clearSettingsDirty() { s_settingsDirty = false; }

bool isResourcesDirty() { return s_resourcesDirty; }
void clearResourcesDirty() { s_resourcesDirty = false; }

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
  if (payloadLength >= 5U) {
    DeviceSettings ds;
    ds.buzzerEnabled = (payload[0] != 0);
    ds.mqttEnabled = (payload[1] != 0);
    ds.touchTestEnabled = (payload[2] != 0);
    ds.backgroundMusicEnabled = (payload[3] != 0);
    ds.pmsEnabled = (payload[4] != 0);
    ds.alarmMelodyIndex = 0;
    if (payloadLength >= 6U) {
      ds.alarmMelodyIndex = payload[5];
    }
    ds.valid = true;
    setLocalSettings(ds);
  }
}

void applySystemResourcesPayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payloadLength < 28U) {
    ESP_LOGW(kTag, "System resources payload length invalid (%u)", payloadLength);
    return;
  }
  SystemResources sr;
  sr.freeRam = static_cast<uint32_t>(payload[0]) |
               (static_cast<uint32_t>(payload[1]) << 8U) |
               (static_cast<uint32_t>(payload[2]) << 16U) |
               (static_cast<uint32_t>(payload[3]) << 24U);
  sr.heapRam = static_cast<uint32_t>(payload[4]) |
               (static_cast<uint32_t>(payload[5]) << 8U) |
               (static_cast<uint32_t>(payload[6]) << 16U) |
               (static_cast<uint32_t>(payload[7]) << 24U);
  sr.dmaRam = static_cast<uint32_t>(payload[8]) |
              (static_cast<uint32_t>(payload[9]) << 8U) |
              (static_cast<uint32_t>(payload[10]) << 16U) |
              (static_cast<uint32_t>(payload[11]) << 24U);
  sr.core0Cpu = payload[12];
  sr.core1Cpu = payload[13];
  sr.freeFlash = static_cast<uint32_t>(payload[14]) |
                 (static_cast<uint32_t>(payload[15]) << 8U) |
                 (static_cast<uint32_t>(payload[16]) << 16U) |
                 (static_cast<uint32_t>(payload[17]) << 24U);
  sr.usedFlash = static_cast<uint32_t>(payload[18]) |
                 (static_cast<uint32_t>(payload[19]) << 8U) |
                 (static_cast<uint32_t>(payload[20]) << 16U) |
                 (static_cast<uint32_t>(payload[21]) << 24U);
  sr.underrunsAudio = static_cast<uint16_t>(payload[22]) | (static_cast<uint16_t>(payload[23]) << 8U);
  sr.overflowAudio = static_cast<uint16_t>(payload[24]) | (static_cast<uint16_t>(payload[25]) << 8U);
  sr.dropsAudio = static_cast<uint16_t>(payload[26]) | (static_cast<uint16_t>(payload[27]) << 8U);
  
  if (payloadLength >= 32U) {
    sr.errorsI2c = static_cast<uint16_t>(payload[28]) | (static_cast<uint16_t>(payload[29]) << 8U);
    sr.timeoutsI2c = static_cast<uint16_t>(payload[30]) | (static_cast<uint16_t>(payload[31]) << 8U);
  }

  sr.valid = true;
  setLocalResources(sr);
}

// ─── Handshake ────────────────────────────────────────────────────────────────

// Zegar jest peerem o roli 1 (Sensor). Przechowujemy jego bootId.
static uint32_t s_peerBootIdLocal = 0;
static uint32_t s_lastFrameRxMsLocal = 0;

void notifyFrameReceived() {
  s_lastFrameRxMsLocal = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void onHelloAckReceived(const uint8_t *payload, uint16_t payloadLength) {
  if (payload == nullptr || payloadLength < 11U) {
    ESP_LOGW(kTag, "[HELLO_ACK] Payload too short (%u)", (unsigned int)payloadLength);
    return;
  }

  // uint8_t protocolVersion = payload[0];
  // uint8_t deviceRole      = payload[1];
  uint32_t peerBootId = static_cast<uint32_t>(payload[2])
                      | (static_cast<uint32_t>(payload[3]) << 8U)
                      | (static_cast<uint32_t>(payload[4]) << 16U)
                      | (static_cast<uint32_t>(payload[5]) << 24U);
  uint32_t peerUptime = static_cast<uint32_t>(payload[6])
                      | (static_cast<uint32_t>(payload[7]) << 8U)
                      | (static_cast<uint32_t>(payload[8]) << 16U)
                      | (static_cast<uint32_t>(payload[9]) << 24U);
  // uint8_t peerSyncState = payload[10];

  notifyFrameReceived();

  bool peerRestarted = (s_peerBootIdLocal != 0 && peerBootId != s_peerBootIdLocal);

  if (peerRestarted) {
    ESP_LOGW(kTag, "[HELLO_ACK] Peer restarted! oldBootId=0x%08X newBootId=0x%08X",
             (unsigned int)s_peerBootIdLocal, (unsigned int)peerBootId);
    // Unieważniamy stare dane i czyścimy UI
    reset(); 
  }

  s_peerBootIdLocal = peerBootId;
  ESP_LOGI(kTag, "[HELLO_ACK] Peer bootId=0x%08X uptime=%ums", (unsigned int)peerBootId, (unsigned int)peerUptime);
}

} // namespace CommunicationProtocol
