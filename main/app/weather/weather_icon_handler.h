#ifndef WEATHER_ICON_HANDLER_H
#define WEATHER_ICON_HANDLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void weather_icon_handler_init(void);
void weather_icon_handler_set(uint8_t weatherCode);

#ifdef __cplusplus
}
#endif

#endif
