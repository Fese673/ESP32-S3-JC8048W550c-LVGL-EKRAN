#include "communication/CommunicationProtocol.h"
#include "communication/CobsFraming.h"
#include "communication/CommunicationState.h"
#include "communication/UartTransport.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "screens.h"
#include "eez-flow.h"
#include "vars.h"

namespace CommunicationProtocol {
namespace {

constexpr char kTag[] = "COMM";
constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kDeviceRoleDisplay = 0;
constexpr uint32_t kHelloIntervalMs  = 250;    // Agresywne pytanie co 250ms
constexpr uint32_t kKeepaliveTimeoutMs = 60000; // Po tym czasie uznajemy peer za zgubinego
constexpr uint32_t kKeepaliveIntervalMs = 30000; // Zegar wysyła HELLO jako keep-alive

// --- Stan handshake ---
static uint32_t      s_bootId          = 0;
static PeerSyncState s_syncState       = kSyncBooting;
static uint32_t      s_lastHelloSentMs = 0;
static uint32_t      s_lastFrameRxMs   = 0;
static uint32_t      s_peerBootId      = 0;

uint32_t currentMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void sendHello() {
  uint8_t buf[11];
  uint8_t *c = buf;
  *c++ = kProtocolVersion;
  *c++ = kDeviceRoleDisplay;
  // bootId little-endian
  *c++ = static_cast<uint8_t>(s_bootId & 0xFF);
  *c++ = static_cast<uint8_t>((s_bootId >> 8) & 0xFF);
  *c++ = static_cast<uint8_t>((s_bootId >> 16) & 0xFF);
  *c++ = static_cast<uint8_t>((s_bootId >> 24) & 0xFF);
  // uptimeMs little-endian
  uint32_t uptime = currentMs();
  *c++ = static_cast<uint8_t>(uptime & 0xFF);
  *c++ = static_cast<uint8_t>((uptime >> 8) & 0xFF);
  *c++ = static_cast<uint8_t>((uptime >> 16) & 0xFF);
  *c++ = static_cast<uint8_t>((uptime >> 24) & 0xFF);
  *c++ = static_cast<uint8_t>(s_syncState);
  sendRawFrame(kTypeHello, 0, buf, static_cast<uint16_t>(c - buf));
  ESP_LOGI(kTag, "[HELLO] sent state=%d bootId=0x%08X", (int)s_syncState, (unsigned int)s_bootId);
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
  s_bootId = esp_random();
  s_syncState = kSyncBooting;
  s_peerBootId = 0;
  s_lastFrameRxMs = currentMs();
  reset(); // Wyczyść UI przed startem
  beginUart();
  s_syncState = kSyncUartReady;
  ESP_LOGI(kTag, "UART ready. bootId=0x%08X", (unsigned int)s_bootId);
  // Pierwsze HELLO od razu – nie czekamy 2s
  sendHello();
  s_lastHelloSentMs = currentMs();
}

void reset() {
  Snapshot emptySnapshot{};
  emptySnapshot.valid = false;
  emptySnapshot.updatedAtMs = currentMs();
  setLocalSnapshot(emptySnapshot);

  SystemResources emptyResources{};
  emptyResources.valid = false;
  setLocalResources(emptyResources);

  // Wyczyść zmienne EEZ Flow na starcie, aby nie było "preview" danych
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT, eez::Value("Oczekiwanie..."));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM1_0_SENSOR, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR, eez::Value("---"));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_FREE_RAM_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HEAP_RAM_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_DMA_RAM_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CORE0_CPU_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CORE1_CPU_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_FREE_FLASH_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_USED_FLASH_SYS, eez::Value("---"));
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

const SystemResources &currentResources() {
  static SystemResources cachedResources;
  cachedResources = getLocalResources();
  return cachedResources;
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

void requestSystemResources() {
  uint8_t payload[1] = { kTypeSystemResources };
  sendRawFrame(kTypeRequest, 0, payload, 1);
}

// Wywoływane z warstwy transportu kiedy HELLO_ACK dotrze poprawnie zdekodowany
void onPeerHelloAckReceived() {
  // Odśwież znacznik keep-alive w maszynie stanów
  s_lastFrameRxMs = currentMs();

  if (s_syncState == kSyncUartReady || s_syncState == kSyncFirstSyncSent) {
    // Peer jest gotowy – przechodzimy do żądania pełnych danych
    s_syncState = kSyncPeerDetected;
    ESP_LOGI(kTag, "[HELLO_ACK] Peer detected, transitioning to PeerDetected");
  } else if (s_syncState == kSyncSteadyState) {
    // Keep-alive od Zegara – tylko aktualizujemy timestamp, stan bez zmian
    ESP_LOGD(kTag, "[HELLO_ACK] Keep-alive from peer in steady state");
  }
}

void sendDeviceSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms, int alarmMelody) {
  uint8_t payload[6];
  payload[0] = buzzer ? 1 : 0;
  payload[1] = mqtt ? 1 : 0;
  payload[2] = touch ? 1 : 0;
  payload[3] = music ? 1 : 0;
  payload[4] = pms ? 1 : 0;
  payload[5] = static_cast<uint8_t>(alarmMelody);
  sendRawFrame(kTypeSetSettings, 0, payload, sizeof(payload));
}

void updateLocalSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms, int alarmMelody) {
  DeviceSettings ds;
  ds.buzzerEnabled = buzzer;
  ds.mqttEnabled = mqtt;
  ds.touchTestEnabled = touch;
  ds.backgroundMusicEnabled = music;
  ds.pmsEnabled = pms;
  ds.alarmMelodyIndex = alarmMelody;
  ds.valid = true;
  setLocalSettings(ds);
}

void updateUi() {
  const uint32_t now = currentMs();

  // ─── Maszyna stanów handshake ──────────────────────────────────────────────
  switch (s_syncState) {
    case kSyncBooting:
      // Czekamy na beginUart() – wyjdziemy z tego stanu w begin()
      break;

    case kSyncUartReady:
      // UART gotowy, nie mamy jeszcze odpowiedzi – wysyłamy HELLO co kHelloIntervalMs
      if (now - s_lastHelloSentMs >= kHelloIntervalMs) {
        sendHello();
        s_lastHelloSentMs = now;
      }
      break;

    case kSyncPeerDetected:
      // Peer wykryty (dostaliśmy HELLO_ACK), wysyłamy żądania danych
      requestDeviceSettings();
      requestSystemResources();
      s_syncState = kSyncFirstSyncSent;
      ESP_LOGI(kTag, "[SYNC] Requesting full data set");
      break;

    case kSyncFirstSyncSent:
      // Czekamy na odpowiedź – sprawdzamy czy mamy komplet danych
      {
        DeviceSettings ds = getLocalSettings();
        SystemResources sr = getLocalResources();
        if (ds.valid && sr.valid) {
          s_syncState = kSyncFirstSyncConfirmed;
          ESP_LOGI(kTag, "[SYNC] First sync confirmed!");
        } else if (now - s_lastHelloSentMs >= kHelloIntervalMs) {
          // Ponów żądanie jeśli za długo czekamy
          requestDeviceSettings();
          requestSystemResources();
          s_lastHelloSentMs = now;
          ESP_LOGW(kTag, "[SYNC] Retrying data request...");
        }
      }
      break;

    case kSyncFirstSyncConfirmed:
      s_syncState = kSyncSteadyState;
      ESP_LOGI(kTag, "[SYNC] Entering steady state");
      break;

    case kSyncSteadyState:
      // Sprawdzamy timeout keep-alive – jeśli peer milczy za długo, resetujemy
      if (now - s_lastFrameRxMs >= kKeepaliveTimeoutMs) {
        ESP_LOGW(kTag, "[SYNC] Peer timeout! Resetting sync state.");
        s_syncState = kSyncUartReady;
        s_peerBootId = 0;
        reset(); // Czyścimy dane i UI
        sendHello();
        s_lastHelloSentMs = now;
      }
      break;
  }
  // ──────────────────────────────────────────────────────────────────────────

  if (isSettingsDirty()) {
    DeviceSettings ds = getLocalSettings();
    clearSettingsDirty();

    if (ds.valid) {
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BUZZER_STATE, eez::Value(ds.buzzerEnabled ? 1 : 0));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MQTT_STATE, eez::Value(ds.mqttEnabled ? 1 : 0));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TOUCH_STATE, eez::Value(ds.touchTestEnabled ? 1 : 0));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_STATE, eez::Value(ds.backgroundMusicEnabled ? 1 : 0));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PMS_STATE, eez::Value(ds.pmsEnabled ? 1 : 0));

      if (objects.ustawienia) {
        lv_obj_t *container = lv_obj_get_child(objects.ustawienia, 0);
        if (container) {
          lv_obj_t *dropdown = lv_obj_get_child(container, 6);
          if (dropdown) {
            lv_dropdown_set_selected(dropdown, ds.alarmMelodyIndex);
          }
        }
      }
    }
  }

  if (isDirty()) {
    Snapshot snapshotCopy = getLocalSnapshot();
    applySnapshotToMainScreen(snapshotCopy);
    clearDirty();
  }

  if (isResourcesDirty()) {
    SystemResources sr = getLocalResources();
    clearResourcesDirty();

    if (sr.valid) {
      static char freeRamBuf[16];
      static char heapRamBuf[16];
      static char dmaRamBuf[16];
      static char core0CpuBuf[16];
      static char core1CpuBuf[16];
      static char freeFlashBuf[16];
      static char usedFlashBuf[16];
      
      snprintf(freeRamBuf, sizeof(freeRamBuf), "%u KB", (unsigned int)(sr.freeRam / 1024));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_FREE_RAM_SYS, eez::Value(freeRamBuf));
      
      snprintf(heapRamBuf, sizeof(heapRamBuf), "%u KB", (unsigned int)(sr.heapRam / 1024));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HEAP_RAM_SYS, eez::Value(heapRamBuf));
      
      snprintf(dmaRamBuf, sizeof(dmaRamBuf), "%u KB", (unsigned int)(sr.dmaRam / 1024));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_DMA_RAM_SYS, eez::Value(dmaRamBuf));
      
      snprintf(core0CpuBuf, sizeof(core0CpuBuf), "%u %%", (unsigned int)sr.core0Cpu);
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CORE0_CPU_SYS, eez::Value(core0CpuBuf));
      
      snprintf(core1CpuBuf, sizeof(core1CpuBuf), "%u %%", (unsigned int)sr.core1Cpu);
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CORE1_CPU_SYS, eez::Value(core1CpuBuf));
      
      snprintf(freeFlashBuf, sizeof(freeFlashBuf), "%.1f MB", sr.freeFlash / (1024.0f * 1024.0f));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_FREE_FLASH_SYS, eez::Value(freeFlashBuf));
      
      snprintf(usedFlashBuf, sizeof(usedFlashBuf), "%.1f MB", sr.usedFlash / (1024.0f * 1024.0f));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_USED_FLASH_SYS, eez::Value(usedFlashBuf));
      
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_UNDERRUNS_AUDIO_SYS, eez::Value((int)sr.underrunsAudio));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_OVERFLOW_AUDIO_SYS, eez::Value((int)sr.overflowAudio));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_DROPS_AUDIO_SYS, eez::Value((int)sr.dropsAudio));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_ERRORS_I2C_SYS, eez::Value((int)sr.errorsI2c));
      eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIMEOUTS_I2C_SYS, eez::Value((int)sr.timeoutsI2c));
    }
  }
}

} // namespace CommunicationProtocol

extern "C" {

void action_refresh_settings() {
  CommunicationProtocol::requestDeviceSettings();
}

void action_toggle_buzzer() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(!ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex);
  CommunicationProtocol::sendDeviceSettings(!ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex);
}

void action_toggle_mqtt() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, !ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, !ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex);
}

void action_toggle_touch() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, !ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, !ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex);
}

void action_toggle_music() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, !ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, !ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex);
}

void action_toggle_pms() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, !ds.pmsEnabled, ds.alarmMelodyIndex);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, !ds.pmsEnabled, ds.alarmMelodyIndex);
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
