#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMM_LOG_FLUSH_INTERVAL_MS 5000

void comm_log_init(void);
void comm_log_increment_tx(uint8_t frame_type);
void comm_log_increment_rx(uint8_t frame_type);
void comm_log_flush_if_needed(void);
void comm_log_flush_now(void);

#ifdef __cplusplus
}
#endif
