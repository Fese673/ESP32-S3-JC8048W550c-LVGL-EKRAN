#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ui.h"
#include "screens.h"

#define LOG_BUFFER_SIZE 8192
#define MAX_LOG_LINES 100

static log_config_t log_config = {
    .enable_lvgl = true,
    .enable_wifi = true,
    .enable_comm = true
};

static char log_buffer[LOG_BUFFER_SIZE];
static size_t log_buffer_pos = 0;
static bool log_initialized = false;
static int log_change_counter = 0;
static int log_last_update_counter = 0;

static void log_add_to_buffer(const char *tag, const char *message) {
    if (!log_initialized) return;

    log_change_counter++;

    char line[256];
    int len = snprintf(line, sizeof(line), "[%s] %s\n", tag, message);

    if (log_buffer_pos + len >= LOG_BUFFER_SIZE - 1) {
        log_buffer[0] = '\0';
        log_buffer_pos = 0;
    }

    memcpy(log_buffer + log_buffer_pos, line, len);
    log_buffer_pos += len;
    log_buffer[log_buffer_pos] = '\0';
}

void log_init(void) {
    log_buffer[0] = '\0';
    log_buffer_pos = 0;
    log_change_counter = 0;
    log_last_update_counter = 0;
    log_initialized = true;
    log_info("LOG", "System logow zainicjalizowany");
}

void log_set_enabled(log_type_t type, bool enabled) {
    switch (type) {
        case LOG_TYPE_LVGL:
            log_config.enable_lvgl = enabled;
            break;
        case LOG_TYPE_WIFI:
            log_config.enable_wifi = enabled;
            break;
        default:
            break;
    }
}

bool log_is_enabled(log_type_t type) {
    switch (type) {
        case LOG_TYPE_LVGL:
            return log_config.enable_lvgl;
        case LOG_TYPE_WIFI:
            return log_config.enable_wifi;
        case LOG_TYPE_COMM:
            return log_config.enable_comm;
        default:
            return false;
    }
}

void log_info(const char *tag, const char *format, ...) {
    if (!log_initialized) return;

    if (strcmp(tag, "LVGL") == 0 && !log_config.enable_lvgl) return;
    if (strcmp(tag, "WIFI") == 0 && !log_config.enable_wifi) return;
    if (strcmp(tag, "COMM") == 0 && !log_config.enable_comm) return;

    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_add_to_buffer(tag, message);
}

void log_warn(const char *tag, const char *format, ...) {
    if (!log_initialized) return;

    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_add_to_buffer(tag, message);
}

void log_error(const char *tag, const char *format, ...) {
    if (!log_initialized) return;

    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    log_add_to_buffer(tag, message);
}

const char* log_get_all_text(void) {
    return log_buffer;
}

void log_clear(void) {
    log_buffer[0] = '\0';
    log_buffer_pos = 0;
    log_change_counter = 0;
    log_last_update_counter = 0;
}

void log_update_textarea(void) {
    if (objects.log_terminal == NULL) return;
    if (log_change_counter == log_last_update_counter) return;
    if (log_buffer_pos == 0) return;

    static int64_t last_update = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_update < 500000) return;
    last_update = now;

    lv_textarea_set_max_length(objects.log_terminal, 0);
    lv_textarea_set_text(objects.log_terminal, log_buffer);
    log_last_update_counter = log_change_counter;
}

void log_boot_snapshot(const char *where) {
    log_info("LOG", "boot: %s", where);
}

void log_ui_snapshot(const char *where, int16_t screen_id) {
    log_info("LOG", "screen: %s id=%d", where, (int)screen_id);
}

void log_ui_tick_begin(int16_t screen_id) {
}

void log_ui_tick_end(int16_t screen_id, int64_t elapsed_us) {
    if (elapsed_us > 5000) {
        log_warn("LVGL", "tick slow: screen=%d duration=%lld us", (int)screen_id, (long long)elapsed_us);
    }
}