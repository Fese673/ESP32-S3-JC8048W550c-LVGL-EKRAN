#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_init(void);
void log_info(const char *tag, const char *format, ...);
void log_warn(const char *tag, const char *format, ...);
void log_error(const char *tag, const char *format, ...);

void log_boot_snapshot(const char *where);
void log_ui_snapshot(const char *where, int16_t screen_id);
void log_ui_tick_begin(int16_t screen_id);
void log_ui_tick_end(int16_t screen_id, int64_t elapsed_us);

#ifdef __cplusplus
}
#endif