#include "communication/CommunicationState.h"
#include "services/bpm/bpm_service.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace CommunicationProtocol {
namespace {

constexpr char kTag[] = "COMM_STATE";
SemaphoreHandle_t s_mutex = nullptr;

Snapshot s_snapshot;
OutdoorWeatherSnapshot s_outdoorWeather;
DeviceSettings s_deviceSettings;
SystemResources s_systemResources;

volatile bool s_dirty = false;
volatile bool s_outdoorDirty = false;
volatile bool s_settingsDirty = false;
volatile bool s_resourcesDirty = false;
volatile bool s_resetUiPending = false;

// Sensor WiFi status
bool s_sensorWifiConnected = false;
int8_t s_sensorWifiRssi = 0;
uint8_t s_sensorWifiIp[4] = {0, 0, 0, 0};
volatile bool s_wifiDirty = false;

// Sensor BLE status
uint8_t s_sensorBleValue = 0;
volatile bool s_bleDirty = false;

// Sensor Bell status
uint8_t s_sensorBellValue = 0;
volatile bool s_bellDirty = false;

// Music state
struct MusicMetadataState {
    char title[128] = {};
    char artist[128] = {};
    volatile bool titleDirty = false;
    volatile bool artistDirty = false;
};
MusicMetadataState s_musicMetadata;

struct MusicStatusState {
    bool connected = false;
    bool playing = false;
    volatile bool dirty = false;
};
MusicStatusState s_musicStatus;

struct MusicVolumeState {
    uint8_t volume = 50;
    volatile bool dirty = false;
};
MusicVolumeState s_musicVolume;

struct MusicEQState {
    uint8_t bass = 50;
    uint8_t mid = 50;
    uint8_t treble = 50;
    volatile bool dirty = false;
};
MusicEQState s_musicEQ;

struct RadioModeState {
    uint8_t mode = 0;  // 0=WIFI, 1=BT, 2=TRANSITIONING
    volatile bool dirty = false;
};
RadioModeState s_radioMode;

uint32_t currentMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void deriveAirQualityText(Snapshot &snapshot) {
  if (snapshot.pm25UgM3 <= 12) {
    snapshot.airQualityIndex = 1;
    snprintf(snapshot.airQualityText, sizeof(snapshot.airQualityText), "Dobra");
    return;
  }
  if (snapshot.pm25UgM3 <= 35) {
    snapshot.airQualityIndex = 2;
    snprintf(snapshot.airQualityText, sizeof(snapshot.airQualityText), "\u015arednia");
    return;
  }
  snapshot.airQualityIndex = 3;
  snprintf(snapshot.airQualityText, sizeof(snapshot.airQualityText), "Z\u0142a");
}

static void deriveOutdoorAirQualityText(OutdoorWeatherSnapshot &ow) {
  if (ow.aqi <= 20) {
    snprintf(ow.airQualityText, sizeof(ow.airQualityText), "Dobra");
  } else if (ow.aqi <= 40) {
    snprintf(ow.airQualityText, sizeof(ow.airQualityText), "\u015arednia");
  } else if (ow.aqi <= 60) {
    snprintf(ow.airQualityText, sizeof(ow.airQualityText), "Z\u0142a");
  } else {
    snprintf(ow.airQualityText, sizeof(ow.airQualityText), "Bardzo z\u0142a");
  }
}

} // namespace

void initCommunicationState() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
}

void lockSnapshot() {
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

OutdoorWeatherSnapshot getLocalOutdoorWeather() {
  lockSnapshot();
  OutdoorWeatherSnapshot copy = s_outdoorWeather;
  unlockSnapshot();
  return copy;
}

void setLocalOutdoorWeather(const OutdoorWeatherSnapshot& ow) {
  lockSnapshot();
  s_outdoorWeather = ow;
  s_outdoorDirty = true;
  unlockSnapshot();
}

bool isOutdoorDirty() { return s_outdoorDirty; }
void clearOutdoorDirty() { s_outdoorDirty = false; }

bool isSettingsDirty() { return s_settingsDirty; }
void clearSettingsDirty() { s_settingsDirty = false; }

bool isResourcesDirty() { return s_resourcesDirty; }
void clearResourcesDirty() { s_resourcesDirty = false; }

bool consumeSnapshot(Snapshot &out) {
  lockSnapshot();
  bool dirty = s_dirty;
  if (dirty) {
    out = s_snapshot;
    s_dirty = false;
  }
  unlockSnapshot();
  return dirty;
}

bool consumeSettings(DeviceSettings &out) {
  lockSnapshot();
  bool dirty = s_settingsDirty;
  if (dirty) {
    out = s_deviceSettings;
    s_settingsDirty = false;
  }
  unlockSnapshot();
  return dirty;
}

bool consumeResources(SystemResources &out) {
  lockSnapshot();
  bool dirty = s_resourcesDirty;
  if (dirty) {
    out = s_systemResources;
    s_resourcesDirty = false;
  }
  unlockSnapshot();
  return dirty;
}

bool consumeOutdoorWeather(OutdoorWeatherSnapshot &out) {
  lockSnapshot();
  bool dirty = s_outdoorDirty;
  if (dirty) {
    out = s_outdoorWeather;
    s_outdoorDirty = false;
  }
  unlockSnapshot();
  return dirty;
}

bool isResetUiPending() {
  lockSnapshot();
  bool val = s_resetUiPending;
  unlockSnapshot();
  return val;
}

void setResetUiPending() {
  lockSnapshot();
  s_resetUiPending = true;
  unlockSnapshot();
}

void clearResetUiPending() {
  lockSnapshot();
  s_resetUiPending = false;
  unlockSnapshot();
}

void applyWeatherPayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payloadLength < 11U) {
    ESP_LOGW(kTag, "Weather payload too short (%u)", payloadLength);
    return;
  }
  Snapshot nextSnapshot = getLocalSnapshot();

  const int16_t tempCx100 = static_cast<int16_t>(payload[0] | (payload[1] << 8U));
  const uint16_t humPctX100 = static_cast<uint16_t>(payload[2] | (payload[3] << 8U));
  const uint16_t pressHpaX10 = static_cast<uint16_t>(payload[4] | (payload[5] << 8U));

  uint32_t sampleAgeMs;
  uint8_t flags;

  if (payloadLength >= 13U) {
    const uint16_t eco2 = static_cast<uint16_t>(payload[6] | (payload[7] << 8U));
    sampleAgeMs = static_cast<uint32_t>(payload[8]) |
                  (static_cast<uint32_t>(payload[9]) << 8U) |
                  (static_cast<uint32_t>(payload[10]) << 16U) |
                  (static_cast<uint32_t>(payload[11]) << 24U);
    flags = payload[12];
    if ((flags & 0x40U) != 0U) {
      nextSnapshot.eco2 = eco2;
    }
    if (payloadLength >= 15U && (flags & 0x80U) != 0U) {
      nextSnapshot.tvoc = static_cast<uint16_t>(payload[13] | (payload[14] << 8U));
    }
  } else {
    sampleAgeMs = static_cast<uint32_t>(payload[6]) |
                  (static_cast<uint32_t>(payload[7]) << 8U) |
                  (static_cast<uint32_t>(payload[8]) << 16U) |
                  (static_cast<uint32_t>(payload[9]) << 24U);
    flags = payload[10];
  }

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

void applyOutdoorWeatherPayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payloadLength < 27U) {
    ESP_LOGW(kTag, "Outdoor weather payload too short (%u)", payloadLength);
    return;
  }

  const bool extended = (payloadLength >= 33U);
  const bool hasNo2 = (payloadLength >= 35U);
  const uint8_t flags = extended ? payload[32] : payload[26];

  OutdoorWeatherSnapshot ow = getLocalOutdoorWeather();

  if ((flags & 0x01U) != 0U) {
    const int16_t tempCx100 = static_cast<int16_t>(payload[0] | (payload[1] << 8U));
    const float t = static_cast<float>(tempCx100) / 100.0f;
    if (isfinite(t) && t > -50.0f && t < 60.0f) { ow.temperatureC = t; }
  }
  if ((flags & 0x02U) != 0U) {
    const uint16_t humPctX100 = static_cast<uint16_t>(payload[2] | (payload[3] << 8U));
    const uint8_t h = static_cast<uint8_t>(humPctX100 / 100U);
    if (h <= 100) { ow.humidityPct = h; }
  }
  if ((flags & 0x04U) != 0U) {
    const uint16_t pressHpaX10 = static_cast<uint16_t>(payload[4] | (payload[5] << 8U));
    const uint16_t p = static_cast<uint16_t>(pressHpaX10 / 10U);
    if (p >= 800 && p <= 1100) { ow.pressureHpa = p; }
  }
  if ((flags & 0x08U) != 0U) {
    const uint16_t windMsX100 = static_cast<uint16_t>(payload[6] | (payload[7] << 8U));
    const float ws = static_cast<float>(windMsX100) / 100.0f;
    if (isfinite(ws) && ws >= 0.0f && ws < 100.0f) { ow.windSpeedMs = ws; }
    const uint16_t gustMsX100 = static_cast<uint16_t>(payload[8] | (payload[9] << 8U));
    const float wg = static_cast<float>(gustMsX100) / 100.0f;
    if (isfinite(wg) && wg >= 0.0f && wg < 150.0f) { ow.windGustMs = wg; }
    if (payload[10] <= 180) { ow.windDeg = payload[10] * 2U; }
  }
  if ((flags & 0x10U) != 0U) {
    if (payload[11] <= 99) { ow.weatherCode = payload[11]; }
    if (payload[12] <= 100) { ow.cloudCover = payload[12]; }
    const int16_t appTempCx100 = static_cast<int16_t>(payload[13] | (payload[14] << 8U));
    const float at = static_cast<float>(appTempCx100) / 100.0f;
    if (isfinite(at) && at > -50.0f && at < 60.0f) { ow.apparentTempC = at; }
  }
  if ((flags & 0x20U) != 0U) {
    const uint16_t pm25x100 = static_cast<uint16_t>(payload[15] | (payload[16] << 8U));
    const uint16_t p25 = static_cast<uint16_t>((pm25x100 + 50U) / 100U);
    if (p25 <= 5000) { ow.pm25UgM3 = p25; }
    const uint16_t pm10x100 = static_cast<uint16_t>(payload[17] | (payload[18] << 8U));
    const uint16_t p10 = static_cast<uint16_t>((pm10x100 + 50U) / 100U);
    if (p10 <= 10000) { ow.pm10UgM3 = p10; }
    const uint16_t co2 = static_cast<uint16_t>(payload[19] | (payload[20] << 8U));
    if (co2 >= 300 && co2 <= 5000) { ow.co2Ppm = co2; }
    if (payload[21] <= 100) { ow.aqi = payload[21]; deriveOutdoorAirQualityText(ow); }
    if (hasNo2) {
      ow.no2UgM3 = static_cast<uint16_t>(payload[33] | (payload[34] << 8U));
    }
  }
  if ((flags & 0x40U) != 0U && extended) {
    ow.precipitationMm = static_cast<float>(payload[22]) / 10.0f;
    ow.uvIndex = static_cast<float>(payload[23]) / 10.0f;
  }
  if ((flags & 0x80U) != 0U && extended) {
    if (payload[24] < 24) { ow.sunriseHour = payload[24]; }
    if (payload[25] < 60) { ow.sunriseMin = payload[25]; }
    if (payload[26] < 24) { ow.sunsetHour = payload[26]; }
    if (payload[27] < 60) { ow.sunsetMin = payload[27]; }
  }

  if ((flags & 0xFFU) != 0U) {
    ow.valid = true;
  }
  ow.updatedAtMs = currentMs();

  setLocalOutdoorWeather(ow);
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

  // Extract PMS5003 Particle counts
  nextSnapshot.count0p3 = static_cast<uint16_t>(payload[6] | (payload[7] << 8U));
  nextSnapshot.count0p5 = static_cast<uint16_t>(payload[8] | (payload[9] << 8U));
  nextSnapshot.count1p0 = static_cast<uint16_t>(payload[10] | (payload[11] << 8U));
  nextSnapshot.count2p5 = static_cast<uint16_t>(payload[12] | (payload[13] << 8U));
  nextSnapshot.count5p0 = static_cast<uint16_t>(payload[14] | (payload[15] << 8U));
  nextSnapshot.count10p0 = static_cast<uint16_t>(payload[16] | (payload[17] << 8U));

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

void applyMusicMetadataPayload(const uint8_t *payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 2) return;
    uint8_t fieldId = payload[0];
    uint8_t strLen = payload[1];
    if (2U + strLen > payloadLength) return;
    if (strLen > 120) strLen = 120;

    lockSnapshot();
    if (fieldId == 0) {
        memset(s_musicMetadata.title, 0, sizeof(s_musicMetadata.title));
        if (strLen > 0) {
            memcpy(s_musicMetadata.title, payload + 2, strLen);
        }
        s_musicMetadata.titleDirty = true;
    } else if (fieldId == 1) {
        memset(s_musicMetadata.artist, 0, sizeof(s_musicMetadata.artist));
        if (strLen > 0) {
            memcpy(s_musicMetadata.artist, payload + 2, strLen);
        }
        s_musicMetadata.artistDirty = true;
    }
    unlockSnapshot();
}

void applyMusicStatusPayload(const uint8_t *payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 1) return;
    lockSnapshot();
    s_musicStatus.connected = (payload[0] & 0x01U) != 0;
    s_musicStatus.playing = (payload[0] & 0x02U) != 0;
    s_musicStatus.dirty = true;
    unlockSnapshot();
}

void applyMusicVolumeStatePayload(const uint8_t *payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 1) return;
    lockSnapshot();
    s_musicVolume.volume = payload[0];
    if (s_musicVolume.volume > 100) s_musicVolume.volume = 100;
    s_musicVolume.dirty = true;
    unlockSnapshot();
}

void applyMusicEQStatePayload(const uint8_t *payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 3) return;
    lockSnapshot();
    s_musicEQ.bass = payload[0];
    s_musicEQ.mid = payload[1];
    s_musicEQ.treble = payload[2];
    if (s_musicEQ.bass > 100) s_musicEQ.bass = 100;
    if (s_musicEQ.mid > 100) s_musicEQ.mid = 100;
    if (s_musicEQ.treble > 100) s_musicEQ.treble = 100;
    s_musicEQ.dirty = true;
    unlockSnapshot();
}

bool consumeMusicMetadata(char *title, size_t titleSize, char *artist, size_t artistSize) {
    lockSnapshot();
    bool bothDirty = s_musicMetadata.titleDirty && s_musicMetadata.artistDirty;
    static uint32_t lastDirtyTime = 0;
    uint32_t now = currentMs();
    bool anyDirty = s_musicMetadata.titleDirty || s_musicMetadata.artistDirty;
    
    if (anyDirty && !bothDirty) {
        if (lastDirtyTime == 0) lastDirtyTime = now;
        if (now - lastDirtyTime < 150) {
            // Wait up to 150ms for the other field to arrive over UART
            unlockSnapshot();
            return false;
        }
    }
    
    if (anyDirty) {
        snprintf(title, titleSize, "%s", s_musicMetadata.title);
        snprintf(artist, artistSize, "%s", s_musicMetadata.artist);
        s_musicMetadata.titleDirty = false;
        s_musicMetadata.artistDirty = false;
        lastDirtyTime = 0;
        unlockSnapshot();
        return true;
    }
    unlockSnapshot();
    return false;
}

bool consumeMusicStatus(bool &connected, bool &playing) {
    lockSnapshot();
    bool dirty = s_musicStatus.dirty;
    if (dirty) {
        connected = s_musicStatus.connected;
        playing = s_musicStatus.playing;
        s_musicStatus.dirty = false;
    }
    unlockSnapshot();
    return dirty;
}

bool consumeMusicVolumeState(uint8_t &volume) {
    lockSnapshot();
    bool dirty = s_musicVolume.dirty;
    if (dirty) {
        volume = s_musicVolume.volume;
        s_musicVolume.dirty = false;
    }
    unlockSnapshot();
    return dirty;
}

void applyRadioModePayload(const uint8_t *payload, uint16_t payloadLength) {
    if (payload == nullptr || payloadLength < 1) return;
    uint8_t mode = payload[0];
    if (mode > 2) mode = 0;
    lockSnapshot();
    s_radioMode.mode = mode;
    s_radioMode.dirty = true;
    unlockSnapshot();
}

bool consumeRadioMode(uint8_t &mode) {
    lockSnapshot();
    bool dirty = s_radioMode.dirty;
    if (dirty) {
        mode = s_radioMode.mode;
        s_radioMode.dirty = false;
    }
    unlockSnapshot();
    return dirty;
}

bool consumeMusicEQState(uint8_t &bass, uint8_t &mid, uint8_t &treble) {
    lockSnapshot();
    bool dirty = s_musicEQ.dirty;
    if (dirty) {
        bass = s_musicEQ.bass;
        mid = s_musicEQ.mid;
        treble = s_musicEQ.treble;
        s_musicEQ.dirty = false;
    }
    unlockSnapshot();
    return dirty;
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
    // Clear data (mutex-protected, safe from any task)
    {
      Snapshot emptySnapshot{};
      emptySnapshot.valid = false;
      emptySnapshot.updatedAtMs = currentMs();
      setLocalSnapshot(emptySnapshot);
    }
    {
      SystemResources emptyResources{};
      emptyResources.valid = false;
      setLocalResources(emptyResources);
    }
    // Defer resetUi() (calls eez::flow::setGlobalVariable) to LVGL task
    setResetUiPending();
  }

  s_peerBootIdLocal = peerBootId;
  ESP_LOGI(kTag, "[HELLO_ACK] Peer bootId=0x%08X uptime=%ums", (unsigned int)peerBootId, (unsigned int)peerUptime);
}

void applyPpgPayload(const uint8_t *payload, uint16_t payloadLength) {
  // payload: [seq(1)] [diff_lo(1)] [diff_hi(1)] = 3 bajty
  if (payload == nullptr || payloadLength != 3) return;

  int16_t diff = static_cast<int16_t>(
      static_cast<uint16_t>(payload[1]) |
      (static_cast<uint16_t>(payload[2]) << 8U));
  bpm_service_feed_sample(diff);
}

void applyBpmStatusPayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payload == nullptr || payloadLength != 2) return;
  uint8_t bpm = payload[0];
  uint8_t spo2 = payload[1];
  if (bpm >= 30 && bpm <= 250) {
    bpm_service_set_bpm(bpm);
    bpm_service_set_spo2(spo2);
  }
}

void applyWifiStatusPayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payload == nullptr || payloadLength < 6) return;
  lockSnapshot();
  s_sensorWifiConnected = (payload[0] != 0);
  s_sensorWifiRssi = static_cast<int8_t>(payload[1]);
  s_sensorWifiIp[0] = payload[2];
  s_sensorWifiIp[1] = payload[3];
  s_sensorWifiIp[2] = payload[4];
  s_sensorWifiIp[3] = payload[5];
  s_wifiDirty = true;
  unlockSnapshot();
}

void applyStatusBlePayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payload == nullptr || payloadLength < 1) return;
  lockSnapshot();
  s_sensorBleValue = payload[0];
  s_bleDirty = true;
  unlockSnapshot();
}

void applyStatusBellPayload(const uint8_t *payload, uint16_t payloadLength) {
  if (payload == nullptr || payloadLength < 1) return;
  lockSnapshot();
  s_sensorBellValue = payload[0];
  s_bellDirty = true;
  unlockSnapshot();
}

bool consumeWifiStatus(bool &connected, int8_t &rssi, uint8_t *ip) {
  lockSnapshot();
  bool dirty = s_wifiDirty;
  if (dirty) {
    connected = s_sensorWifiConnected;
    rssi = s_sensorWifiRssi;
    ip[0] = s_sensorWifiIp[0];
    ip[1] = s_sensorWifiIp[1];
    ip[2] = s_sensorWifiIp[2];
    ip[3] = s_sensorWifiIp[3];
    s_wifiDirty = false;
  }
  unlockSnapshot();
  return dirty;
}

bool consumeStatusBle(uint8_t &value) {
  lockSnapshot();
  bool dirty = s_bleDirty;
  if (dirty) {
    value = s_sensorBleValue;
    s_bleDirty = false;
  }
  unlockSnapshot();
  return dirty;
}

bool consumeStatusBell(uint8_t &value) {
  lockSnapshot();
  bool dirty = s_bellDirty;
  if (dirty) {
    value = s_sensorBellValue;
    s_bellDirty = false;
  }
  unlockSnapshot();
  return dirty;
}

} // namespace CommunicationProtocol
