#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void wifi_screen_init(void);
void wifi_timers_pause(void);
const char* wifi_screen_get_selected_ssid(void);

#ifdef __cplusplus
}
#endif
