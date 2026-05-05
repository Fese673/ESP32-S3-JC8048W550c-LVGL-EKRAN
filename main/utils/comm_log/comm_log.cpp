#include "comm_log.h"
#include "log.h"
#include "communication/CommunicationProtocol.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

typedef struct {
    uint16_t hello;
    uint16_t hello_ack;
    uint16_t weather;
    uint16_t pms;
    uint16_t time;
    uint16_t settings;
    uint16_t set_settings;
    uint16_t request;
    uint16_t config;
    uint16_t ack;
    uint16_t unknown;
} frame_counts_t;

static struct {
    frame_counts_t tx = {};
    frame_counts_t rx = {};
    int64_t window_start_us = 0;
    bool initialized = false;
} s_state;

static void increment_counter(frame_counts_t *counts, uint8_t type) {
    switch (type) {
        case CommunicationProtocol::kTypeHello: counts->hello++; break;
        case CommunicationProtocol::kTypeHelloAck: counts->hello_ack++; break;
        case CommunicationProtocol::kTypeWeather: counts->weather++; break;
        case CommunicationProtocol::kTypePms: counts->pms++; break;
        case CommunicationProtocol::kTypeTime: counts->time++; break;
        case CommunicationProtocol::kTypeSettings: counts->settings++; break;
        case CommunicationProtocol::kTypeSetSettings: counts->set_settings++; break;
        case CommunicationProtocol::kTypeRequest: counts->request++; break;
        case CommunicationProtocol::kTypeConfig: counts->config++; break;
        case CommunicationProtocol::kTypeAck: counts->ack++; break;
        default: counts->unknown++; break;
    }
}

void comm_log_init(void) {
    memset(&s_state, 0, sizeof(s_state));
    s_state.window_start_us = esp_timer_get_time();
    s_state.initialized = true;
}

void comm_log_increment_tx(uint8_t frame_type) {
    if (!s_state.initialized) return;
    increment_counter(&s_state.tx, frame_type);
}

void comm_log_increment_rx(uint8_t frame_type) {
    if (!s_state.initialized) return;
    increment_counter(&s_state.rx, frame_type);
}

static int append_frame_if_nonzero(char *buf, size_t remaining, const char *name, uint16_t count) {
    if (count == 0) return 0;
    return snprintf(buf, remaining, "%dx%s ", count, name);
}

void comm_log_flush_now(void) {
    if (!s_state.initialized) return;

    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_ms = (now_us - s_state.window_start_us) / 1000;

    char tx_buf[256] = {0};
    char rx_buf[256] = {0};
    int tx_pos = 0;
    int rx_pos = 0;

    #define APPEND_TX(name) tx_pos += append_frame_if_nonzero(tx_buf + tx_pos, sizeof(tx_buf) - tx_pos, #name, s_state.tx.name)
    #define APPEND_RX(name) rx_pos += append_frame_if_nonzero(rx_buf + rx_pos, sizeof(rx_buf) - rx_pos, #name, s_state.rx.name)

    APPEND_TX(hello); APPEND_TX(hello_ack); APPEND_TX(weather);
    APPEND_TX(pms); APPEND_TX(time); APPEND_TX(settings);
    APPEND_TX(set_settings); APPEND_TX(request); APPEND_TX(config);
    APPEND_TX(ack); APPEND_TX(unknown);

    APPEND_RX(hello); APPEND_RX(hello_ack); APPEND_RX(weather);
    APPEND_RX(pms); APPEND_RX(time); APPEND_RX(settings);
    APPEND_RX(set_settings); APPEND_RX(request); APPEND_RX(config);
    APPEND_RX(ack); APPEND_RX(unknown);

    #undef APPEND_TX
    #undef APPEND_RX

    if (tx_pos > 0 || rx_pos > 0) {
        if (tx_pos == 0) tx_pos = snprintf(tx_buf, sizeof(tx_buf), "- ");
        if (rx_pos == 0) rx_pos = snprintf(rx_buf, sizeof(rx_buf), "- ");

        log_info("COMM", "TX: %s| RX: %s(%lldms)", tx_buf, rx_buf, (long long)elapsed_ms);
    }

    memset(&s_state.tx, 0, sizeof(s_state.tx));
    memset(&s_state.rx, 0, sizeof(s_state.rx));
    s_state.window_start_us = esp_timer_get_time();
}

void comm_log_flush_if_needed(void) {
    if (!s_state.initialized) return;

    int64_t now_us = esp_timer_get_time();
    int64_t elapsed_ms = (now_us - s_state.window_start_us) / 1000;

    if (elapsed_ms >= COMM_LOG_FLUSH_INTERVAL_MS) {
        comm_log_flush_now();
    }
}
