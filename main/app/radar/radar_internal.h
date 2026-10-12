#pragma once
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Budowa raz (persistent) na objects.radar. Tylko prymitywy LVGL.
void radar_ui_build(lv_obj_t *screen);

// Sync co 100 ms z timera logiki. Wszystko liczone na intach, zero alokacji.
// sens: aktywny preset czulosci OUT (0/1/2) — podswietlenie przyciskow.
void radar_ui_sync(uint8_t presence, uint16_t mov_dist, uint8_t mov_energy,
                   uint16_t stat_dist, uint8_t stat_energy, uint16_t detect_dist,
                   const uint8_t *mov_gates, const uint8_t *stat_gates,
                   uint8_t sens);

#ifdef __cplusplus
}
#endif
