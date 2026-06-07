#ifndef ALARM_POPUP_H
#define ALARM_POPUP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the alarm popup UI exactly once.
 * Allocates all widgets statically on boot to prevent dynamic heap fragmentation.
 */
void alarm_popup_init(void);

/**
 * @brief Trigger the slide-down animation and display the alarm popup.
 * @param time_str The time string (e.g. "07:00")
 * @param message_str The scrolling alarm message (e.g. "WSTAWAJ SZKODA DNIA")
 * @param is_timer If true, snooze button is hidden (timer has no snooze)
 */
void alarm_popup_show(const char *time_str, const char *message_str, bool is_timer);

/**
 * @brief Trigger the slide-up animation and hide the alarm popup.
 */
void alarm_popup_dismiss(void);

/**
 * @brief Cycle tick called every 100ms in the main loop to service mockup and anim logic.
 */
void alarm_popup_tick(void);

#ifdef __cplusplus
}
#endif

#endif // ALARM_POPUP_H
