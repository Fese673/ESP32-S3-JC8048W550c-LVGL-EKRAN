#pragma once
#include <stddef.h>
#include <stdint.h>
#include "communication/CommunicationProtocol.h"
#include "communication/CommunicationState.h"

namespace CommunicationProtocol {

void formatWithComma(char *buffer, size_t bufferSize, float value, const char *suffix);
const char* wmoCodeToText(uint8_t code);
void resetUi();
void applySnapshotToMainScreen(const Snapshot &snapshot);
void applyOutdoorSnapshotToMainScreen(const OutdoorWeatherSnapshot &ow);
void applySnapshotToWeatherScreen(const OutdoorWeatherSnapshot &ow);
void applyResourcesToUi(const SystemResources &sr);
void applySettingsToUi(const DeviceSettings &ds);
void applyBpmStatusToUi(int bpm, int spo2);
void applyWifiStatusToUi(bool connected, int8_t rssi, const uint8_t *ip);
void applyStatusBleToUi(uint8_t value);
void applyStatusBellToUi(uint8_t value);
void applyMusicMetadataToUi(const char *title, const char *artist);
void applyMusicStatusToUi(bool connected, bool playing);
void applyMusicVolumeToUi(uint8_t volume);
void applyMusicEQToUi(uint8_t bass, uint8_t mid, uint8_t treble);
void applyRadioModeToUi(uint8_t mode);

} // namespace CommunicationProtocol
