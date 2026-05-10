#include "communication/CommHandshake.h"
#include "communication/CommunicationProtocol.h"
#include "communication/CommunicationState.h"
#include "communication/UartTransport.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"

namespace CommunicationProtocol {
namespace {

constexpr char kTag[] = "HANDSHAKE";
constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kDeviceRoleDisplay = 0;
constexpr uint32_t kHelloIntervalMs  = 250;
constexpr uint32_t kKeepaliveTimeoutMs = 60000;

static uint32_t s_bootId = 0;
static PeerSyncState s_syncState = kSyncBooting;
static uint32_t s_lastHelloSentMs = 0;
static uint32_t s_lastFrameRxMs = 0;

} // namespace

uint32_t currentMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void sendHello() {
  uint8_t buf[11];
  uint8_t *c = buf;
  *c++ = kProtocolVersion;
  *c++ = kDeviceRoleDisplay;
  *c++ = static_cast<uint8_t>(s_bootId & 0xFF);
  *c++ = static_cast<uint8_t>((s_bootId >> 8) & 0xFF);
  *c++ = static_cast<uint8_t>((s_bootId >> 16) & 0xFF);
  *c++ = static_cast<uint8_t>((s_bootId >> 24) & 0xFF);
  uint32_t uptime = currentMs();
  *c++ = static_cast<uint8_t>(uptime & 0xFF);
  *c++ = static_cast<uint8_t>((uptime >> 8) & 0xFF);
  *c++ = static_cast<uint8_t>((uptime >> 16) & 0xFF);
  *c++ = static_cast<uint8_t>((uptime >> 24) & 0xFF);
  *c++ = static_cast<uint8_t>(s_syncState);
  sendRawFrame(kTypeHello, 0, buf, static_cast<uint16_t>(c - buf));
  ESP_LOGI(kTag, "[HELLO] sent state=%d bootId=0x%08X", (int)s_syncState, (unsigned int)s_bootId);
}

void onPeerHelloAckReceived() {
  s_lastFrameRxMs = currentMs();

  if (s_syncState == kSyncUartReady || s_syncState == kSyncFirstSyncSent) {
    s_syncState = kSyncPeerDetected;
    ESP_LOGI(kTag, "[HELLO_ACK] Peer detected, transitioning to PeerDetected");
  } else if (s_syncState == kSyncSteadyState) {
    ESP_LOGD(kTag, "[HELLO_ACK] Keep-alive from peer in steady state");
  }
}

void updateHandshake() {
  const uint32_t now = currentMs();

  switch (s_syncState) {
    case kSyncBooting:
      break;

    case kSyncUartReady:
      if (now - s_lastHelloSentMs >= kHelloIntervalMs) {
        sendHello();
        s_lastHelloSentMs = now;
      }
      break;

    case kSyncPeerDetected:
      requestDeviceSettings();
      requestSystemResources();
      s_syncState = kSyncFirstSyncSent;
      ESP_LOGI(kTag, "[SYNC] Requesting full data set");
      break;

    case kSyncFirstSyncSent:
      {
        DeviceSettings ds = getLocalSettings();
        SystemResources sr = getLocalResources();
        if (ds.valid && sr.valid) {
          s_syncState = kSyncFirstSyncConfirmed;
          ESP_LOGI(kTag, "[SYNC] First sync confirmed!");
        } else if (now - s_lastHelloSentMs >= kHelloIntervalMs) {
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
      if (now - s_lastFrameRxMs >= kKeepaliveTimeoutMs) {
        ESP_LOGW(kTag, "[SYNC] Peer timeout! Resetting sync state.");
        s_syncState = kSyncUartReady;
        reset();
        sendHello();
        s_lastHelloSentMs = now;
      }
      break;
  }
}

void initHandshake(uint32_t bootId) {
  s_bootId = bootId;
  s_syncState = kSyncBooting;
  s_lastFrameRxMs = currentMs();
}

void handshakeUartReady() {
  s_syncState = kSyncUartReady;
  ESP_LOGI(kTag, "UART ready. bootId=0x%08X", (unsigned int)s_bootId);
  sendHello();
  s_lastHelloSentMs = currentMs();
}

void onHandshakeFrameReceived() {
  s_lastFrameRxMs = currentMs();
}

} // namespace CommunicationProtocol
