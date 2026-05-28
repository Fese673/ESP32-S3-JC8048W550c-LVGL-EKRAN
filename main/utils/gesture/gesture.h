#ifndef GESTURE_H
#define GESTURE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Rejestruje obsluge gestu Swipe Up na obiekcie i jego dzieciach.
 */
void gesture_register_recursive(lv_obj_t * obj);

#ifdef __cplusplus
}
#endif

#endif // GESTURE_H
