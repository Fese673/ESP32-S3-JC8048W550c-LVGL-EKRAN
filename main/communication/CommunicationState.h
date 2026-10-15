#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "communication/CommunicationProtocol.h"

namespace CommunicationProtocol {

void initCommunicationState();
void lockSnapshot();
void unlockSnapshot();

Snapshot getLocalSnapshot();
void setLocalSnapshot(const Snapshot& s);

DeviceSettings getLocalSettings();
void setLocalSettings(const DeviceSettings& ds);

// True po pierwszym echu ustawien z ZEGARA — dopiero wtedy wolno wysylac
// zmiany (przed syncem cache ma tylko defaulty).
bool settingsEverSynced();

SystemResources getLocalResources();
void setLocalResources(const SystemResources& sr);

bool isDirty();
void clearDirty();

bool isSettingsDirty();
void clearSettingsDirty();

bool isResourcesDirty();
void clearResourcesDirty();

void applyWeatherPayload(const uint8_t *payload, uint16_t payloadLength);
void applyPmsPayload(const uint8_t *payload, uint16_t payloadLength);
void applyTimePayload(const uint8_t *payload, uint16_t payloadLength);
void applySettingsPayload(const uint8_t *payload, uint16_t payloadLength);
void applySystemResourcesPayload(const uint8_t *payload, uint16_t payloadLength);
void applyOutdoorWeatherPayload(const uint8_t *payload, uint16_t payloadLength);
void applyPpgPayload(const uint8_t *payload, uint16_t payloadLength);
void applyBpmStatusPayload(const uint8_t *payload, uint16_t payloadLength);
void applyWifiStatusPayload(const uint8_t *payload, uint16_t payloadLength);
void applyStatusBlePayload(const uint8_t *payload, uint16_t payloadLength);
void applyStatusBellPayload(const uint8_t *payload, uint16_t payloadLength);
void applyMusicMetadataPayload(const uint8_t *payload, uint16_t payloadLength);
void applyMusicStatusPayload(const uint8_t *payload, uint16_t payloadLength);
void applyMusicVolumeStatePayload(const uint8_t *payload, uint16_t payloadLength);
void applyMusicEQStatePayload(const uint8_t *payload, uint16_t payloadLength);
void applyRadioModePayload(const uint8_t *payload, uint16_t payloadLength);
void applyStepperStatePayload(const uint8_t *payload, uint16_t payloadLength);
void applyRadarPayload(const uint8_t *payload, uint16_t payloadLength);
void applyWifiConfigPayload(const uint8_t *payload, uint16_t payloadLength);

// ── Panel "Ustawienia WiFi" (0x11): wyniki provisioning z ZEGARA ──
struct WifiScanEntry {
  char ssid[33];
  int8_t rssi;
  uint8_t open;
};
// Skan (sklejony z paginowanych ramek 0x81): true RAZ na kompletna liste.
bool consumeWifiScan(uint8_t &status, uint8_t &count, WifiScanEntry *out, uint8_t outMax);
// Wynik testu hasla (0x82): true RAZ na ramke.
bool consumeWifiCredResult(uint8_t &code, uint16_t &reason, char *ssid, size_t ssidSize);
// Info o ZEGARZE (0x83): true RAZ na ramke.
bool consumeWifiInfo(uint8_t &wifiOn, uint8_t &btMode, uint8_t &connected,
                     char *ssid, size_t ssidSize);

OutdoorWeatherSnapshot getLocalOutdoorWeather();
void setLocalOutdoorWeather(const OutdoorWeatherSnapshot& ow);
bool isOutdoorDirty();
void clearOutdoorDirty();

// Atomic consume (copy + clear dirty under one lock, no race)
bool consumeSnapshot(Snapshot &out);
bool consumeSettings(DeviceSettings &out);
bool consumeResources(SystemResources &out);
bool consumeOutdoorWeather(OutdoorWeatherSnapshot &out);
bool consumeWifiStatus(bool &connected, int8_t &rssi, uint8_t *ip);
bool consumeStatusBle(uint8_t &value);
bool consumeStatusBell(uint8_t &value);
bool consumeMusicMetadata(char *title, size_t titleSize, char *artist, size_t artistSize);
bool consumeMusicStatus(bool &connected, bool &playing);
bool consumeMusicVolumeState(uint8_t &volume);
bool consumeMusicEQState(uint8_t &bass, uint8_t &mid, uint8_t &treble);
bool consumeRadioMode(uint8_t &mode);
bool consumeStepperState(bool &busy, int32_t &posSteps, uint16_t &targetDeg,
                          uint8_t &flags, uint16_t &speed, uint16_t &homeDeg,
                          uint16_t &autoCloseS);
bool consumeRadarState(uint8_t &presence, uint16_t &movDist, uint8_t &movEnergy,
                       uint16_t &statDist, uint8_t &statEnergy, uint16_t &detectDist,
                       uint8_t *movGates, uint8_t *statGates);

// Handshake
void onHelloAckReceived(const uint8_t *payload, uint16_t payloadLength);
void notifyFrameReceived(); // Aktualizuje znacznik czasu ostatniej odebranej ramki

// Peer restart/startup – defer resetUi() to LVGL task
bool isResetUiPending();
void setResetUiPending();
void clearResetUiPending();

} // namespace CommunicationProtocol
