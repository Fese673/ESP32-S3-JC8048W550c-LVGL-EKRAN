#ifndef FLAPPY_BIRD_H
#define FLAPPY_BIRD_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the Flappy Bird game elements and binds them to the parent screen.
 *        Called when transitioning to SCREEN_ID_GRY_TEST.
 * @param parent_screen The screen container object (e.g., objects.gry_test).
 */
void flappy_bird_init(lv_obj_t *parent_screen);

/**
 * @brief Cleans up game resources, stops timers, and deletes allocated game objects.
 *        Called when transitioning away from SCREEN_ID_GRY_TEST.
 */
void flappy_bird_cleanup(void);

/**
 * @brief Triggers a bird flap/jump. Can be called by touch events or remote UART frames.
 */
void flappy_bird_jump(void);

#ifdef __cplusplus
}
#endif

#endif // FLAPPY_BIRD_H
