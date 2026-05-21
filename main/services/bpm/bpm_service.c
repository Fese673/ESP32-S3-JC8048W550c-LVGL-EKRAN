#include "bpm_service.h"
#include "esp_timer.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SAMPLE_INTERVAL_US 20000
#define MAX_SAMPLES_PER_UPDATE 10
#define PPG_DC_LEVEL 80000
#define PPG_AC_AMP 2000

static struct {
    // SPSC ring buffer: producer = feed_sample (UART RX, core 0),
    // consumer = bpm_service_update (LVGL timer, core 1)
    int16_t buffer[BPM_PPG_BUFFER_SIZE];
    uint16_t head;       // next write index (producer)
    uint16_t fill;       // total samples in buffer (capped at SIZE)

    int bpm;
    uint8_t spo2;

    uint32_t last_update_ms;
    uint32_t phase_us;
    uint32_t beat_period_us;
    int16_t last_diff;

    uint32_t filter_buf[BPM_FILTER_TAPS];
    uint8_t filter_idx;
    uint8_t filter_count;
    uint32_t prev_filtered;

    bool mock_enabled;
    uint32_t beat_count;

    // Consumer tracking
    uint16_t read_head;  // last consumed write index (consumer only)

    // Thread safety: producer (core 0) and consumer (core 1)
    portMUX_TYPE mux;

    bool initialized;
} s_ctx;

static uint32_t dc_offset_us(void) {
    return (uint32_t)(esp_timer_get_time());
}

static uint32_t generate_ir_sample(uint32_t phase_us, uint32_t period_us, uint32_t ac_amp) {
    uint32_t p = (phase_us * 1025) / period_us;
    if (p >= 1024) p = 1023;

    uint32_t shape;

    if (p < 154) {
        uint32_t t = (p * 256) / 154;
        shape = (t * t) / 64;
        if (shape > 1024) shape = 1024;
    } else if (p < 358) {
        uint32_t t = ((p - 154) * 256) / 204;
        shape = 1024 - ((t * t * 200) / (256 * 256));
    } else if (p < 614) {
        uint32_t t = ((p - 358) * 256) / 256;
        shape = 824 - ((t * t * 624) / (256 * 256));
        if (shape > 824) shape = 824;
        if (shape < 200) shape = 200;
    } else if (p < 717) {
        uint32_t t = ((p - 614) * 256) / 103;
        uint32_t half = (t < 128) ? t : (256 - t);
        shape = 200 + ((half * 150) / 128);
    } else if (p < 819) {
        uint32_t t = ((p - 717) * 256) / 102;
        shape = 350 - ((t * 200) / 256);
    } else {
        uint32_t t = ((p - 819) * 256) / 205;
        shape = 150 - ((t * 150) / 256);
        if (shape > 150) shape = 150;
    }

    return PPG_DC_LEVEL + (ac_amp * shape / 1024);
}

static uint32_t apply_filter(uint32_t raw) {
    s_ctx.filter_buf[s_ctx.filter_idx] = raw;
    s_ctx.filter_idx = (s_ctx.filter_idx + 1) % BPM_FILTER_TAPS;
    if (s_ctx.filter_count < BPM_FILTER_TAPS) {
        s_ctx.filter_count++;
    }

    uint32_t sum = 0;
    for (uint8_t i = 0; i < s_ctx.filter_count; i++) {
        sum += s_ctx.filter_buf[i];
    }
    return sum / s_ctx.filter_count;
}

static int16_t compute_diff(uint32_t filtered) {
    int32_t diff = (int32_t)s_ctx.prev_filtered - (int32_t)filtered;
    s_ctx.prev_filtered = filtered;
    if (diff > 32767) diff = 32767;
    if (diff < -32767) diff = -32767;
    return (int16_t)diff;
}

static uint32_t respiration_amp(uint32_t now_us) {
    uint32_t resp_period_us = 5000000;
    uint32_t resp_phase = now_us % resp_period_us;
    uint32_t t = (resp_phase * 2048) / resp_period_us;
    uint32_t half = (t < 1024) ? t : (2048 - t);
    return PPG_AC_AMP * (512 + (half * 512) / 1024) / 1024;
}

static uint32_t variable_period(uint32_t now_us) {
    uint32_t seed = s_ctx.beat_count * 2654435761U + (now_us >> 10);
    int32_t variation = (int32_t)(seed % 50001) - 25000;
    return (uint32_t)((int32_t)s_ctx.beat_period_us + variation);
}

static int16_t generate(uint32_t now_us) {
    uint32_t ac = respiration_amp(now_us);
    uint32_t period = variable_period(now_us);

    uint32_t raw = generate_ir_sample(s_ctx.phase_us, period, ac);
    uint32_t filtered = apply_filter(raw);
    int16_t diff = compute_diff(filtered);

    s_ctx.phase_us += SAMPLE_INTERVAL_US;
    if (s_ctx.phase_us >= period) {
        s_ctx.phase_us -= period;
        s_ctx.beat_count++;
    }

    return diff;
}

static void push_sample(int16_t sample) {
    s_ctx.buffer[s_ctx.head] = sample;
    s_ctx.head = (s_ctx.head + 1) % BPM_PPG_BUFFER_SIZE;
    if (s_ctx.fill < BPM_PPG_BUFFER_SIZE) {
        s_ctx.fill++;
    }
    s_ctx.last_diff = sample;
}

void bpm_service_init(void) {
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.bpm = 72;
    s_ctx.spo2 = 98;
    s_ctx.beat_period_us = 60000000 / s_ctx.bpm;
    s_ctx.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_ctx.prev_filtered = PPG_DC_LEVEL;
    s_ctx.mock_enabled = false;
    // Compound literal: (portMUX_TYPE){...} is valid C99+ for runtime init.
    s_ctx.mux = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    s_ctx.initialized = true;
}

void bpm_service_set_mock_mode(bool enabled) {
    s_ctx.mock_enabled = enabled;
}

bool bpm_service_get_mock_mode(void) {
    return s_ctx.mock_enabled;
}

void bpm_service_feed_sample(int16_t diff) {
    if (!s_ctx.initialized) {
        bpm_service_init();
    }
    if (s_ctx.mock_enabled) {
        return;
    }

    // Producer (UART RX task, core 0). Spinlock synchronizes with
    // bpm_service_update() on core 1 (LVGL task).
    // Xtensa memory ordering: critical section provides acquire/release barrier.
    portENTER_CRITICAL(&s_ctx.mux);
    push_sample(diff);
    portEXIT_CRITICAL(&s_ctx.mux);
}

void bpm_service_set_bpm(int bpm) {
    if (bpm < 30) bpm = 30;
    if (bpm > 220) bpm = 220;

    uint32_t new_period = 60000000 / (uint32_t)bpm;

    if (s_ctx.beat_period_us > 0) {
        uint32_t phase_ratio = (s_ctx.phase_us << 16) / s_ctx.beat_period_us;
        s_ctx.phase_us = (phase_ratio * new_period) >> 16;
    } else {
        s_ctx.phase_us = 0;
    }

    s_ctx.bpm = bpm;
    s_ctx.beat_period_us = new_period;
}

int bpm_service_get_bpm(void) {
    return s_ctx.bpm;
}

void bpm_service_set_spo2(uint8_t spo2) {
    if (spo2 > 100) spo2 = 100;
    s_ctx.spo2 = spo2;
}

int bpm_service_get_spo2(void) {
    return s_ctx.spo2;
}

int16_t bpm_service_current_diff(void) {
    return s_ctx.last_diff;
}

uint16_t bpm_service_update(uint32_t now_ms) {
    if (!s_ctx.initialized) {
        bpm_service_init();
    }

    uint32_t elapsed_ms = now_ms - s_ctx.last_update_ms;
    if (elapsed_ms > 200) elapsed_ms = 200;
    s_ctx.last_update_ms = now_ms;

    if (s_ctx.mock_enabled) {
        // Mock mode: generate synthetic PPG, single-task (LVGL only), no lock needed
        uint16_t generated = 0;
        uint32_t remaining_us = elapsed_ms * 1000;

        while (remaining_us >= SAMPLE_INTERVAL_US) {
            remaining_us -= SAMPLE_INTERVAL_US;
            uint32_t now_us = dc_offset_us();
            int16_t sample = generate(now_us);
            push_sample(sample);
            generated++;
            if (generated >= MAX_SAMPLES_PER_UPDATE) break;
        }
        return generated;
    }

    // Real mode: count new samples written by feed_sample() since last call.
    // These are already in the buffer — no push needed, no duplicate.
    // Spinlock provides consistent view of head across cores.
    portENTER_CRITICAL(&s_ctx.mux);
    uint16_t produced = (s_ctx.head + BPM_PPG_BUFFER_SIZE - s_ctx.read_head) % BPM_PPG_BUFFER_SIZE;
    if (produced > MAX_SAMPLES_PER_UPDATE) {
        produced = MAX_SAMPLES_PER_UPDATE;
    }
    s_ctx.read_head = (s_ctx.read_head + produced) % BPM_PPG_BUFFER_SIZE;
    portEXIT_CRITICAL(&s_ctx.mux);

    return produced;
}

uint16_t bpm_service_read_samples(int16_t *out, uint16_t max_count) {
    if (!s_ctx.initialized || !out || max_count == 0) return 0;
    if (s_ctx.mock_enabled) return 0;

    // SPSC atomic read: consumer (LVGL task) reads under spinlock.
    // Producer (UART RX task) cannot advance head while we hold the lock.
    uint16_t count = 0;
    portENTER_CRITICAL(&s_ctx.mux);

    uint16_t available = (s_ctx.head + BPM_PPG_BUFFER_SIZE - s_ctx.read_head) % BPM_PPG_BUFFER_SIZE;
    if (available > max_count) available = max_count;

    for (uint16_t i = 0; i < available; i++) {
        out[i] = s_ctx.buffer[(s_ctx.read_head + i) % BPM_PPG_BUFFER_SIZE];
    }
    s_ctx.read_head = (s_ctx.read_head + available) % BPM_PPG_BUFFER_SIZE;
    count = available;

    portEXIT_CRITICAL(&s_ctx.mux);
    return count;
}

uint16_t bpm_service_sample_count(void) {
    return s_ctx.fill;
}

int16_t bpm_service_get_sample(uint16_t index) {
    if (s_ctx.fill == 0) return 0;

    uint16_t start;
    if (s_ctx.fill < BPM_PPG_BUFFER_SIZE) {
        start = 0;
    } else {
        start = s_ctx.head;
    }

    uint16_t pos = (start + index) % BPM_PPG_BUFFER_SIZE;
    return s_ctx.buffer[pos];
}
