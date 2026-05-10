#include "communication/CommunicationProtocol.h"
#include "communication/CommHandshake.h"
#include "communication/CommUiMapper.h"
#include "communication/CommunicationState.h"
#include "communication/UartTransport.h"
#include "communication/CobsFraming.h"
#include "eez-flow.h"
#include "esp_log.h"
#include "esp_random.h"

namespace CommunicationProtocol {

static constexpr char kTag[] = "COMM";

void begin() {
  initCommunicationState();
  initHandshake(esp_random());

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
  setResetUiPending();

  beginUart();
  handshakeUartReady();
}

void reset() {
  Snapshot emptySnapshot{};
  emptySnapshot.valid = false;
  emptySnapshot.updatedAtMs = currentMs();
  setLocalSnapshot(emptySnapshot);

  SystemResources emptyResources{};
  emptyResources.valid = false;
  setLocalResources(emptyResources);

  resetUi();
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
  if (data == nullptr || length == 0U) return false;
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
  if (isResetUiPending()) {
    clearResetUiPending();
    resetUi();
  }

  updateHandshake();

  {
    DeviceSettings ds;
    if (consumeSettings(ds) && ds.valid) {
      applySettingsToUi(ds);
    }
  }

  {
    Snapshot snap;
    if (consumeSnapshot(snap)) {
      applySnapshotToMainScreen(snap);
    }
  }

  {
    OutdoorWeatherSnapshot ow;
    if (consumeOutdoorWeather(ow)) {
      applyOutdoorSnapshotToMainScreen(ow);
      applySnapshotToWeatherScreen(ow);
    }
  }

  {
    SystemResources sr;
    if (consumeResources(sr) && sr.valid) {
      applyResourcesToUi(sr);
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

void action_action_refresh_settings() { action_refresh_settings(); }
void action_action_toggle_buzzer()    { action_toggle_buzzer(); }
void action_action_toggle_mqtt()      { action_toggle_mqtt(); }
void action_action_toggle_touch()     { action_toggle_touch(); }
void action_action_toggle_music()     { action_toggle_music(); }
void action_action_toggle_pms()       { action_toggle_pms(); }

}
