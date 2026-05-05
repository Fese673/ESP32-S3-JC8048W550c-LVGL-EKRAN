#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "communication/CommunicationProtocol.h"

namespace CommunicationProtocol {

void lockSnapshot();
void unlockSnapshot();

Snapshot getLocalSnapshot();
void setLocalSnapshot(const Snapshot& s);

DeviceSettings getLocalSettings();
void setLocalSettings(const DeviceSettings& ds);

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

// Handshake
void onHelloAckReceived(const uint8_t *payload, uint16_t payloadLength);
void notifyFrameReceived(); // Aktualizuje znacznik czasu ostatniej odebranej ramki

} // namespace CommunicationProtocol
