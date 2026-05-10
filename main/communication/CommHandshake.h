#pragma once
#include <stdint.h>

namespace CommunicationProtocol {

uint32_t currentMs();
void initHandshake(uint32_t bootId);
void handshakeUartReady();
void sendHello();
void onPeerHelloAckReceived();
void onHandshakeFrameReceived();
void updateHandshake();

} // namespace CommunicationProtocol
