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

/* Etap 3: mapowanie dropdownu jasnosci 7-seg. Dropdown ma 7 opcji:
 * 100/75/50/25/10/5/2% (indeksy 0..6). brightnessToDropdownIndex()
 * wybiera najblizszy indeks dla wartosci 0..100.
 * dropdownIndexToBrightness() odwraca operacje. */
uint8_t brightnessToDropdownIndex(uint8_t brightness);
uint8_t dropdownIndexToBrightness(uint8_t index);

} // namespace CommunicationProtocol
