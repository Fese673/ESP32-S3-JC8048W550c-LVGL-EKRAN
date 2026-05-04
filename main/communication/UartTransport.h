#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

namespace CommunicationProtocol {

void beginUart();
void stopUart();
void sendRawFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength);
bool ingestByte(uint8_t byte);

} // namespace CommunicationProtocol
