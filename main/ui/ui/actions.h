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

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_EVENTS_H*/