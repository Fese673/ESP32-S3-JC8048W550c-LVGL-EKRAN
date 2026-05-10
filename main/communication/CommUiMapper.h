#pragma once
#include <stddef.h>
#include <stdint.h>
#include "communication/CommunicationProtocol.h"

namespace CommunicationProtocol {

void formatWithComma(char *buffer, size_t bufferSize, float value, const char *suffix);
const char* wmoCodeToText(uint8_t code);
void resetUi();
void applySnapshotToMainScreen(const Snapshot &snapshot);
void applyOutdoorSnapshotToMainScreen(const OutdoorWeatherSnapshot &ow);
void applySnapshotToWeatherScreen(const OutdoorWeatherSnapshot &ow);
void applyResourcesToUi(const SystemResources &sr);
void applySettingsToUi(const DeviceSettings &ds);

} // namespace CommunicationProtocol
