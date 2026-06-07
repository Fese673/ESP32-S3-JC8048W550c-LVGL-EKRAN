#ifndef EEZ_LVGL_UI_EVENTS_H
#define EEZ_LVGL_UI_EVENTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void action_action_refresh_settings(lv_event_t * e);
extern void action_action_toggle_buzzer(lv_event_t * e);
extern void action_action_toggle_mqtt(lv_event_t * e);
extern void action_action_toggle_pms(lv_event_t * e);
extern void action_action_toggle_touch(lv_event_t * e);
extern void action_action_toggle_music(lv_event_t * e);
extern void action_action_plus_hour_minutnik(lv_event_t * e);
extern void action_action_plus_minutes_minutnik(lv_event_t * e);
extern void action_action_plus_seconds_minutnik(lv_event_t * e);
extern void action_action_minus_hour_minutnik(lv_event_t * e);
extern void action_action_minus_minutes_minutnik(lv_event_t * e);
extern void action_action_minus_seconds_minutnik(lv_event_t * e);
extern void action_action_toggle_start_stop_minutnik(lv_event_t * e);
extern void action_action_swipe_up(lv_event_t * e);

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/