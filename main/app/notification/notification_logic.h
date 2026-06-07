#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void notification_logic_init();
void notification_logic_set_wifi(bool enabled);
void notification_logic_set_ble(bool enabled);
void notification_logic_set_bell(bool enabled);
void notification_logic_set_sun_moon(bool is_day);
void notification_logic_refresh();

#ifdef __cplusplus
}
#endif
