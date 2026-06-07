#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the premium Sensors Wikipedia interface on the active screen.
 * Dynamically builds tabs, view selector and content area inside the provided screen.
 */
void sensors_wiki_init(lv_obj_t *parent_screen);

/**
 * Periodically called from the main tick loop (approx. 100-200ms) when the active screen is SCREEN_ID_CZUJNIKI.
 * Updates real-time parameter values, warning colors, and computes trend arrows.
 */
void sensors_wiki_update(void);

/**
 * Cleans up allocated resources, dynamic timers, and clears dynamic widgets.
 */
void sensors_wiki_deinit(void);

#ifdef __cplusplus
}
#endif
