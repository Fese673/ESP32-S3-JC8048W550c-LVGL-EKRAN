#ifndef ALARM_UI_H
#define ALARM_UI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void alarm_ui_init(void);
void alarm_ui_refresh_list(void);
void alarm_ui_set_edit_locked(bool locked);

#ifdef __cplusplus
}
#endif

#endif // ALARM_UI_H
