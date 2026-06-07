/*
 * lv_perf_diag.h
 *
 * LVGL + heap diagnostics (UART-only).
 *
 * Enable via menuconfig:
 *   Component config → Sunton ESP32-S3 Package config
 *     → LVGL Performance Diagnostics
 *
 * Public API (LVGL task / Core 1 only):
 *   lv_perf_diag_init()
 *   lv_perf_diag_tick_begin() / lv_perf_diag_tick_end()  — wrap ui_tick_timer_cb
 *   lv_perf_diag_screen_change(from, to)                 — on eez screen transition
 *
 * Logging policy (anti-spam):
 *   - Screen change  → one line immediately (SRAM delta since previous screen)
 *   - Threshold breach → WARN/CRIT line every emit interval while active
 *   - Steady OK state  → compact heartbeat every (1+N)*interval (see Kconfig)
 *
 * When CONFIG_LV_PERF_DIAG=n the module compiles to inline no-op stubs.
 */

#ifndef LV_PERF_DIAG_H
#define LV_PERF_DIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_LV_PERF_DIAG) && CONFIG_LV_PERF_DIAG

void lv_perf_diag_init(void);
void lv_perf_diag_tick_begin(void);
void lv_perf_diag_tick_end(void);
void lv_perf_diag_screen_change(int16_t from_screen, int16_t to_screen);

#else

static inline void lv_perf_diag_init(void) { }
static inline void lv_perf_diag_tick_begin(void) { }
static inline void lv_perf_diag_tick_end(void) { }
static inline void lv_perf_diag_screen_change(int16_t from_screen, int16_t to_screen) {
    (void)from_screen;
    (void)to_screen;
}

#endif /* CONFIG_LV_PERF_DIAG */

#ifdef __cplusplus
}
#endif

#endif /* LV_PERF_DIAG_H */
