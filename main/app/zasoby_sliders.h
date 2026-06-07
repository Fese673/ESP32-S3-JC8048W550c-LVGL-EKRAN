#ifndef ZASOBY_SLIDERS_H
#define ZASOBY_SLIDERS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void zasoby_sliders_create(lv_obj_t *screen);
void zasoby_sliders_update(int core0_cpu, int core1_cpu, uint32_t free_flash, uint32_t used_flash);

#ifdef __cplusplus
}
#endif

#endif // ZASOBY_SLIDERS_H
