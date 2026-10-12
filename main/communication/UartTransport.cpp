#include "communication/UartTransport.h"
#include "communication/CommunicationProtocol.h"
#include "communication/CommHandshake.h"
#include "communication/CobsFraming.h"
#include "communication/CommunicationState.h"
#include "communication/time/timer_synchro.h"
#include "services/bpm/bpm_service.h"
#include "utils/comm_log/comm_log.h"
#include "app/alarm_logic.h"
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <atomic>

namespace CommunicationProtocol {
namespace {

constexpr char kTag[] = "UART_TRANS";
constexpr uart_port_t kUartPort = UART_NUM_1;
constexpr int kRxPin = 18;
constexpr int kTxPin = 17;
constexpr uint32_t kBaudRate = 115200UL;
constexpr size_t kRxBufferSize = 1024;
constexpr size_t kTxBufferSize = 2048;
constexpr size_t kMaxPayloadBytes = 128;
constexpr size_t kMaxCobsFrame = 4 + kMaxPayloadBytes + 2 + 4;
constexpr size_t kTxQueueLen = 4;

struct UartTxMessage {
    uint8_t data[kMaxCobsFrame];
    size_t length;
};

struct FrameParser {
    uint8_t buffer[256] = {};
    size_t length = 0;
};

FrameParser s_parser;
TaskHandle_t s_rxTaskHandle = nullptr;
TaskHandle_t s_txTaskHandle = nullptr;
QueueHandle_t s_txQueue = nullptr;
std::atomic<bool> s_started{false};

void resetParser() { s_parser.length = 0; }

void handleFrame(uint8_t type, const uint8_t *payload, uint16_t payloadLength) {
    comm_log_increment_rx(type);
    notifyFrameReceived();
    onHandshakeFrameReceived();

    switch (type) {
    case kTypeIndoorWeather:
        applyWeatherPayload(payload, payloadLength);
        break;
    case kTypePms:
        applyPmsPayload(payload, payloadLength);
        break;
    case kTypeTime:
        applyTimePayload(payload, payloadLength);
        break;
    case kTypeSettings:
        applySettingsPayload(payload, payloadLength);
        break;
    case kTypeSystemResources:
        applySystemResourcesPayload(payload, payloadLength);
        break;
    case kTypeOutdoorWeather:
        applyOutdoorWeatherPayload(payload, payloadLength);
        break;
    case kTypePpg:
        applyPpgPayload(payload, payloadLength);
        break;
    case kTypeBpmStatus:
        applyBpmStatusPayload(payload, payloadLength);
        break;
    case kTypeWifiStatus:
        applyWifiStatusPayload(payload, payloadLength);
        break;
    case kTypeStatusBle:
        applyStatusBlePayload(payload, payloadLength);
        break;
    case kTypeStatusBell:
        applyStatusBellPayload(payload, payloadLength);
        break;
    case kTypeMusicMetadata:
        applyMusicMetadataPayload(payload, payloadLength);
        break;
    case kTypeMusicStatus:
        applyMusicStatusPayload(payload, payloadLength);
        break;
    case kTypeMusicVolumeState:
        applyMusicVolumeStatePayload(payload, payloadLength);
        break;
    case kTypeMusicEQState:
        applyMusicEQStatePayload(payload, payloadLength);
        break;
    case kTypeRadioMode:
        applyRadioModePayload(payload, payloadLength);
        break;
    case kTypeRadar:
        applyRadarPayload(payload, payloadLength);
        break;
    case kTypeStepperState:
        applyStepperStatePayload(payload, payloadLength);
        break;
    case kTypeAlarmList:
        TimerSync::handleAlarmListPayload(payload, payloadLength);
        break;
    case kTypeTimerState:
        TimerSync::handleTimerStatePayload(payload, payloadLength);
        break;
    case kTypeEditLock:
        TimerSync::handleEditLockPayload(payload, payloadLength);
        break;
    case kTypeStopwatchState:
        TimerSync::handleStopwatchStatePayload(payload, payloadLength);
        break;
    case kTypeHelloAck:
        onHelloAckReceived(payload, payloadLength);
        onPeerHelloAckReceived();
        break;
    case kTypeHello:
        ESP_LOGI(kTag, "Received HELLO from peer (keep-alive)");
        break;
    case kTypeRequest:
    case kTypeConfig:
    case kTypeAck:
        ESP_LOGI(kTag, "Control frame type=0x%02X", type);
        break;
    default:
        ESP_LOGW(kTag, "Unknown frame type=0x%02X", type);
        break;
    }
}

void uartRxTask(void *) {
    uint8_t buffer[128];
    while (s_started) {
        const int readCount = uart_read_bytes(kUartPort, buffer, sizeof(buffer), pdMS_TO_TICKS(50));
        if (readCount > 0) {
            for (int i = 0; i < readCount; ++i) {
                ingestByte(buffer[i]);
            }
        }
    }
    s_rxTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

void uartTxTask(void *) {
    UartTxMessage msg;
    while (s_started) {
        // Timeout 100ms zamiast portMAX_DELAY — task sprawdza s_started i może
        // się sam zakończyć bez potrzeby xTaskAbortDelay z zewnątrz.
        if (xQueueReceive(s_txQueue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            uart_write_bytes(kUartPort, msg.data, msg.length);
        }
    }
    s_txTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

void beginUart() {
    if (s_started) return;

    uart_config_t config = {};
    config.baud_rate = static_cast<int>(kBaudRate);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    const esp_err_t paramStatus = uart_param_config(kUartPort, &config);
    const esp_err_t driverStatus = uart_driver_install(kUartPort, static_cast<int>(kRxBufferSize), static_cast<int>(kTxBufferSize), 0, nullptr, 0);
    const esp_err_t pinStatus = uart_set_pin(kUartPort, kTxPin, kRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    if (paramStatus != ESP_OK || driverStatus != ESP_OK || pinStatus != ESP_OK) {
        ESP_LOGE(kTag, "UART1 init failed");
        return;
    }

    uart_flush_input(kUartPort);
    resetParser();
    s_txQueue = xQueueCreate(kTxQueueLen, sizeof(UartTxMessage));
    if (!s_txQueue) {
        ESP_LOGE(kTag, "Failed to create TX queue");
        return;
    }
    s_started = true;

    if (s_rxTaskHandle == nullptr) {
        if (xTaskCreatePinnedToCore(uartRxTask, "comm_uart1_rx", 4096, nullptr, 4, &s_rxTaskHandle, 0) != pdPASS) {
            ESP_LOGE(kTag, "Failed to create UART RX task");
        }
    }
    if (s_txTaskHandle == nullptr) {
        if (xTaskCreatePinnedToCore(uartTxTask, "comm_uart1_tx", 3072, nullptr, 3, &s_txTaskHandle, 0) != pdPASS) {
            ESP_LOGE(kTag, "Failed to create UART TX task");
        }
    }
}

void sendRawFrame(uint8_t type, uint8_t sequence, const uint8_t *payload, uint16_t payloadLength) {
    if (!s_started || !s_txQueue || payloadLength > kMaxPayloadBytes) return;

    comm_log_increment_tx(type);

    uint8_t frame[4 + kMaxPayloadBytes + 2];
    memset(frame, 0, sizeof(frame));
    uint8_t *cursor = frame;
    *cursor++ = type;
    *cursor++ = sequence;
    *cursor++ = static_cast<uint8_t>(payloadLength & 0xFFU);
    *cursor++ = static_cast<uint8_t>((payloadLength >> 8) & 0xFFU);

    if (payloadLength > 0 && payload != nullptr) {
        memcpy(cursor, payload, payloadLength);
        cursor += payloadLength;
    }

    uint16_t crc = crc16Ccitt(frame, 4U + payloadLength);
    *cursor++ = static_cast<uint8_t>(crc & 0xFFU);
    *cursor++ = static_cast<uint8_t>((crc >> 8) & 0xFFU);

    uint8_t cobsBuffer[sizeof(frame) + 4];
    memset(cobsBuffer, 0, sizeof(cobsBuffer));
    size_t cobsLen = cobsEncode(frame, cursor - frame, cobsBuffer);

    UartTxMessage msg;
    msg.data[0] = 0x00;
    memcpy(msg.data + 1, cobsBuffer, cobsLen);
    msg.data[1 + cobsLen] = 0x00;
    msg.length = cobsLen + 2;

    if (xQueueSend(s_txQueue, &msg, 0) != pdTRUE) {
        ESP_LOGW(kTag, "TX queue full, dropping frame type=0x%02X", type);
    }
}

bool ingestByte(uint8_t byte) {
    if (byte == 0x00) {
        bool consumed = false;
        if (s_parser.length > 0) {
            uint8_t decodedBuffer[256];
            size_t decodedLen = cobsDecode(s_parser.buffer, s_parser.length, decodedBuffer);

            if (decodedLen >= 4 + 2) {
                uint8_t type = decodedBuffer[0];
                uint16_t payloadLength = static_cast<uint16_t>(decodedBuffer[2]) |
                                         (static_cast<uint16_t>(decodedBuffer[3]) << 8U);

                if (payloadLength <= kMaxPayloadBytes && decodedLen == 4 + payloadLength + 2) {
                    uint16_t receivedCrc = static_cast<uint16_t>(decodedBuffer[decodedLen - 2]) |
                                           (static_cast<uint16_t>(decodedBuffer[decodedLen - 1]) << 8U);
                    uint16_t expectedCrc = crc16Ccitt(decodedBuffer, decodedLen - 2);

                    if (receivedCrc == expectedCrc) {
                        handleFrame(type, decodedBuffer + 4, payloadLength);
                        consumed = true;
                    }
                }
            }
        }
        resetParser();
        return consumed;
    } else {
        if (s_parser.length < sizeof(s_parser.buffer)) {
            s_parser.buffer[s_parser.length++] = byte;
        } else {
            resetParser();
        }
        return false;
    }
}

} // namespace CommunicationProtocol
