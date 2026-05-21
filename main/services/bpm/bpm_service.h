#ifndef BPM_SERVICE_H
#define BPM_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BPM_PPG_BUFFER_SIZE 747
#define BPM_FILTER_TAPS 8

void bpm_service_init(void);

void bpm_service_set_bpm(int bpm);
int bpm_service_get_bpm(void);
void bpm_service_set_spo2(uint8_t spo2);
int bpm_service_get_spo2(void);
int16_t bpm_service_current_diff(void);

uint16_t bpm_service_update(uint32_t now_ms);

uint16_t bpm_service_sample_count(void);
int16_t bpm_service_get_sample(uint16_t index);

void bpm_service_set_mock_mode(bool enabled);
bool bpm_service_get_mock_mode(void);
void bpm_service_feed_sample(int16_t diff);

// Atomic read N samples from ring buffer (producer-consumer safe).
// Copies up to max_count samples. Returns how many were actually read.
// For use by LVGL timer (consumer) in real-data mode.
uint16_t bpm_service_read_samples(int16_t *out, uint16_t max_count);

#ifdef __cplusplus
}
#endif

#endif
