#include "communication/CommunicationProtocol.h"
#include "communication/CommHandshake.h"
#include "communication/CommUiMapper.h"
#include "communication/CommunicationState.h"
#include "communication/UartTransport.h"
#include "communication/CobsFraming.h"
#include "communication/time/timer_synchro.h"
#include "eez-flow.h"
#include "esp_log.h"
#include "esp_random.h"
#include "services/bpm/bpm_service.h"
#include "app/alarm_logic.h"
#include "app/alarm_ui.h"
#include "app/alarm/alarm_popup.h"
#include <time.h>

namespace CommunicationProtocol {

static constexpr char kTag[] = "COMM";

void begin() {
  initCommunicationState();
  TimerSync::init();
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

void sendDeviceSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms, int alarmMelody, uint8_t sevenSegBrightness) {
  // Dopoki ZEGAR nie potwierdzil stanu (echo 0x04), cache to same defaulty —
  // wyslanie ich nadpisaloby prawdziwe ustawienia (jasnosc, melodie itd.).
  // Zamiast tego dopraszamy o sync; kolejna proba przejdzie po echu.
  if (!settingsEverSynced()) {
    requestDeviceSettings();
    return;
  }
  uint8_t payload[7];
  payload[0] = buzzer ? 1 : 0;
  payload[1] = mqtt ? 1 : 0;
  payload[2] = touch ? 1 : 0;
  payload[3] = music ? 1 : 0;
  payload[4] = pms ? 1 : 0;
  payload[5] = static_cast<uint8_t>(alarmMelody);
  payload[6] = sevenSegBrightness;
  sendRawFrame(kTypeSetSettings, 0, payload, sizeof(payload));
}

void sendMusicCommand(uint8_t cmd) {
  uint8_t payload[1] = { cmd };
  sendRawFrame(kTypeMusicCommand, 0, payload, 1);
}

void sendMusicVolume(uint8_t vol) {
  if (vol > 100) vol = 100;
  uint8_t payload[1] = { vol };
  sendRawFrame(kTypeMusicVolume, 0, payload, 1);
}

void sendMusicEQ(uint8_t bass, uint8_t mid, uint8_t treble) {
  if (bass > 100) bass = 100;
  if (mid > 100) mid = 100;
  if (treble > 100) treble = 100;
  uint8_t payload[3] = { bass, mid, treble };
  sendRawFrame(kTypeMusicEQ, 0, payload, 3);
}

void requestMusicState() {
  uint8_t payload[1] = { 0x0F };
  sendRawFrame(kTypeMusicRequest, 0, payload, 1);
}

void sendRadioModeSwitch() {
  uint8_t payload[1] = { 0xFF };
  sendRawFrame(kTypeRadioMode, 0, payload, 1);
}

void sendStepperExecute(uint16_t deg, uint16_t speed) {
  uint8_t payload[5];
  payload[0] = 0x01;
  payload[1] = static_cast<uint8_t>(deg & 0xFFU);
  payload[2] = static_cast<uint8_t>((deg >> 8) & 0xFFU);
  payload[3] = static_cast<uint8_t>(speed & 0xFFU);
  payload[4] = static_cast<uint8_t>((speed >> 8) & 0xFFU);
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperHold(int8_t dir, uint16_t speed) {
  uint8_t payload[4];
  payload[0] = 0x02;
  payload[1] = static_cast<uint8_t>(dir);
  payload[2] = static_cast<uint8_t>(speed & 0xFFU);
  payload[3] = static_cast<uint8_t>((speed >> 8) & 0xFFU);
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperStop() {
  uint8_t payload[1] = { 0x03 };
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperCal(uint8_t step) {
  uint8_t payload[1] = { (step == 0) ? (uint8_t)0x05 : (uint8_t)0x06 };
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperCalMode(bool en) {
  uint8_t payload[2] = { (uint8_t)0x0A, (uint8_t)(en ? 1 : 0) };
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperSetHome(uint16_t deg) {
  uint8_t payload[3];
  payload[0] = 0x07;
  payload[1] = static_cast<uint8_t>(deg & 0xFFU);
  payload[2] = static_cast<uint8_t>((deg >> 8) & 0xFFU);
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperSetTarget(uint16_t deg) {
  uint8_t payload[3];
  payload[0] = 0x09;
  payload[1] = static_cast<uint8_t>(deg & 0xFFU);
  payload[2] = static_cast<uint8_t>((deg >> 8) & 0xFFU);
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperSetRadar(bool en) {
  uint8_t payload[2];
  payload[0] = 0x08;
  payload[1] = en ? 1 : 0;
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperSetAutoClose(uint16_t seconds) {
  uint8_t payload[3];
  payload[0] = 0x0B;
  payload[1] = static_cast<uint8_t>(seconds & 0xFFU);
  payload[2] = static_cast<uint8_t>((seconds >> 8) & 0xFFU);
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperSetSens(uint8_t code) {
  uint8_t payload[2];
  payload[0] = 0x0C;
  payload[1] = static_cast<uint8_t>(code <= 2U ? code : 0U);
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperSetOnOff(bool en) {
  uint8_t payload[2];
  payload[0] = 0x0D;
  payload[1] = en ? 1 : 0;
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void sendStepperSetSpeed(uint16_t speed) {
  uint8_t payload[3];
  payload[0] = 0x0E;
  payload[1] = static_cast<uint8_t>(speed & 0xFFU);
  payload[2] = static_cast<uint8_t>((speed >> 8) & 0xFFU);
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void requestStepperState() {
  uint8_t payload[1] = { 0x04 };
  sendRawFrame(kTypeStepperCmd, 0, payload, sizeof(payload));
}

void updateLocalSettings(bool buzzer, bool mqtt, bool touch, bool music, bool pms, int alarmMelody, uint8_t sevenSegBrightness) {
  DeviceSettings ds;
  ds.buzzerEnabled = buzzer;
  ds.mqttEnabled = mqtt;
  ds.touchTestEnabled = touch;
  ds.backgroundMusicEnabled = music;
  ds.pmsEnabled = pms;
  ds.alarmMelodyIndex = alarmMelody;
  ds.sevenSegBrightness = sevenSegBrightness;
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

  applyBpmStatusToUi(bpm_service_get_bpm(), bpm_service_get_spo2());

  {
    bool connected;
    int8_t rssi;
    uint8_t ip[4];
    if (consumeWifiStatus(connected, rssi, ip)) {
      applyWifiStatusToUi(connected, rssi, ip);
    }
  }
  {
    uint8_t value;
    if (consumeStatusBle(value)) {
      applyStatusBleToUi(value);
    }
  }
  {
    uint8_t value;
    if (consumeStatusBell(value)) {
      applyStatusBellToUi(value);
      // Powiadomienie popup — ta sama konsumpcja, brak duplikacji
      if (value == 2) {
        time_t now = time(nullptr);
        struct tm lt;
        char time_str[8];
        if (localtime_r(&now, &lt)) {
          snprintf(time_str, sizeof(time_str), "%02d:%02d", lt.tm_hour, lt.tm_min);
        } else {
          snprintf(time_str, sizeof(time_str), "--:--");
        }
        const char *msg = TimerSync::isTimerRinging() ? "MINUTNIK!" : "BUDZIK!";
        alarm_popup_show(time_str, msg, TimerSync::isTimerRinging());
      } else if (value == 0) {
        alarm_popup_dismiss();
      }
    }
  }
  {
    char title[128], artist[128];
    if (consumeMusicMetadata(title, sizeof(title), artist, sizeof(artist))) {
      applyMusicMetadataToUi(title, artist);
    }
  }
  {
    bool connected, playing;
    if (consumeMusicStatus(connected, playing)) {
      applyMusicStatusToUi(connected, playing);
    }
  }
  {
    uint8_t volume;
    if (consumeMusicVolumeState(volume)) {
      applyMusicVolumeToUi(volume);
    }
  }
  {
    uint8_t bass, mid, treble;
    if (consumeMusicEQState(bass, mid, treble)) {
      applyMusicEQToUi(bass, mid, treble);
    }
  }
  {
    bool busy;
    int32_t posSteps;
    uint16_t targetDeg;
    uint8_t flags;
    uint16_t speed;
    uint16_t homeDeg;
    uint16_t autoCloseS;
    if (consumeStepperState(busy, posSteps, targetDeg, flags, speed, homeDeg,
                            autoCloseS)) {
      applyStepperStateToUi(busy, posSteps, targetDeg, flags, speed, homeDeg,
                            autoCloseS);
    }
  }
  {
    uint8_t presence;
    uint16_t movDist, statDist, detectDist;
    uint8_t movEnergy, statEnergy;
    uint8_t movGates[9], statGates[9];
    if (consumeRadarState(presence, movDist, movEnergy, statDist, statEnergy,
                          detectDist, movGates, statGates)) {
      applyRadarToUi(presence, movDist, movEnergy, statDist, statEnergy,
                     detectDist, movGates, statGates);
    }
  }
  {
    uint8_t mode;
    if (consumeRadioMode(mode)) {
      applyRadioModeToUi(mode);
    }
  }

  // ─── Timer state ───────────────────────────────────────────────────
  {
    TimerSync::TimerState ts;
    if (TimerSync::consumeTimerState(ts)) {
      TimerSync::applyTimerStateToUi(ts);
    }
  }

  // ─── Stopwatch state ───────────────────────────────────────────────
  {
    TimerSync::StopwatchState ss;
    TimerSync::consumeStopwatchState(ss);
  }

  // ─── Edit lock ──────────────────────────────────────────────────────
  {
    bool locked;
    if (TimerSync::consumeEditLock(locked)) {
      TimerSync::applyEditLockToUi(locked);
    }
  }

  // ─── Alarm list sync ──────────────────────────────────────────────
  {
    if (TimerSync::consumeAlarmListDirty()) {
      alarm_ui_refresh_list();
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
  CommunicationProtocol::updateLocalSettings(!ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
  CommunicationProtocol::sendDeviceSettings(!ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
}

void action_toggle_mqtt() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, !ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, !ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
}

void action_toggle_touch() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, !ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, !ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
}

void action_toggle_music() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, !ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, !ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
}

void action_toggle_pms() {
  CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
  CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, !ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
  CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, !ds.pmsEnabled, ds.alarmMelodyIndex, ds.sevenSegBrightness);
}

void action_action_refresh_settings() { action_refresh_settings(); }
void action_action_toggle_buzzer()    { action_toggle_buzzer(); }
void action_action_toggle_mqtt()      { action_toggle_mqtt(); }
void action_action_toggle_touch()     { action_toggle_touch(); }
void action_action_toggle_music()     { action_toggle_music(); }
void action_action_toggle_pms()       { action_toggle_pms(); }

void action_send_music_command(uint8_t cmd) {
  CommunicationProtocol::sendMusicCommand(cmd);
}
void action_send_music_volume(uint8_t vol) {
  CommunicationProtocol::sendMusicVolume(vol);
}
void action_send_music_eq(uint8_t bass, uint8_t mid, uint8_t treble) {
  CommunicationProtocol::sendMusicEQ(bass, mid, treble);
}
void action_request_music_state() {
  CommunicationProtocol::requestMusicState();
}

void action_send_radio_mode_switch() {
  CommunicationProtocol::sendRadioModeSwitch();
}

int action_get_flow_variable_int(uint32_t var_id) {
  eez::Value val = eez::flow::getGlobalVariable(var_id);
  return val.getInt();
}

void action_get_flow_variable_text(uint32_t var_id, char *buf, size_t buf_size) {
  eez::Value val = eez::flow::getGlobalVariable(var_id);
  val.toText(buf, buf_size);
}

}
