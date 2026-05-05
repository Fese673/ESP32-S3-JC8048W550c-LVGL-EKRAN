#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_TYPE_LVGL = 0,
    LOG_TYPE_WIFI = 1,
    LOG_TYPE_COMM = 2,
    LOG_TYPE_COUNT
} log_type_t;

typedef struct {
    bool enable_lvgl;
    bool enable_wifi;
    bool enable_comm;
} log_config_t;

void log_init(void);
void log_set_enabled(log_type_t type, bool enabled);
bool log_is_enabled(log_type_t type);
void log_info(const char *tag, const char *format, ...);
void log_warn(const char *tag, const char *format, ...);
void log_error(const char *tag, const char *format, ...);

const char* log_get_all_text(void);
void log_clear(void);
void log_update_textarea(void);

void log_boot_snapshot(const char *where);
void log_ui_snapshot(const char *where, int16_t screen_id);
void log_ui_tick_begin(int16_t screen_id);
void log_ui_tick_end(int16_t screen_id, int64_t elapsed_us);

#ifdef __cplusplus
}
#endif