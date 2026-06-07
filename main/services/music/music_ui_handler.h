#ifndef MUSIC_UI_HANDLER_H
#define MUSIC_UI_HANDLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void music_ui_handler_init(void);

/** Set volume slider and percentage label directly (safe even if screen is not active). */
void music_ui_set_volume(uint8_t vol);

/** Set EQ sliders and percentage labels directly (safe even if screen is not active). */
void music_ui_set_eq(uint8_t bass, uint8_t mid, uint8_t treble);

/** Reset all music sliders/labels to defaults (called from resetUi()). */
void music_ui_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_UI_HANDLER_H */
