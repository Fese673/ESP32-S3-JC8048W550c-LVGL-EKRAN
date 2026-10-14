#include "gesture.h"
#include "esp_log.h"
#include "lvgl.h"
#include "ui.h"
#include "screens.h"

namespace {

constexpr float kSwipeMinDistanceRatio = 0.073f;
constexpr uint32_t kSwipeMaxTimeMs = 1500;
constexpr float kSwipeMaxHorizontalRatio = 0.7f;
constexpr float kSwipeBottomZoneRatio = 1.0f / 6.0f;

static const char * const kTag = "CUSTOM_GESTURE";

struct GestureState {
    bool tracking;
    lv_point_t start_point;
    uint32_t start_time;
};

static GestureState g_gesture { false, { 0, 0 }, 0 };

static inline int abs_int(int value) {
    return (value < 0) ? -value : value;
}

static int get_screen_height() {
    const int height = lv_display_get_vertical_resolution(nullptr);
    return (height > 0) ? height : 480;
}

static int get_min_swipe_distance() {
    return static_cast<int>(get_screen_height() * kSwipeMinDistanceRatio);
}

static int get_bottom_zone_px() {
    return static_cast<int>(get_screen_height() * kSwipeBottomZoneRatio);
}

static lv_indev_t * get_active_pointer_indev() {
    lv_indev_t * indev = lv_indev_active();
    if (indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
        return indev;
    }
    return nullptr;
}

static bool is_bottom_zone_press(const lv_point_t &point) {
    const int bottom_limit = get_screen_height() - get_bottom_zone_px();
    return point.y >= bottom_limit;
}

static bool is_control_surface(lv_obj_t *obj) {
    if (!obj) {
        return false;
    }

    return lv_obj_check_type(obj, &lv_slider_class) ||
           lv_obj_check_type(obj, &lv_switch_class) ||
           lv_obj_check_type(obj, &lv_roller_class);
}

static void start_tracking(const lv_point_t &point) {
    g_gesture.tracking = true;
    g_gesture.start_point = point;
    g_gesture.start_time = lv_tick_get();
}

static void stop_tracking() {
    g_gesture.tracking = false;
}

static bool is_valid_swipe(const lv_point_t &end_point) {
    const int dy = g_gesture.start_point.y - end_point.y;
    const int dx = abs_int(end_point.x - g_gesture.start_point.x);
    const uint32_t duration_ms = lv_tick_elaps(g_gesture.start_time);

    return (dy >= get_min_swipe_distance()) &&
           (dx <= static_cast<int>(dy * kSwipeMaxHorizontalRatio)) &&
           (duration_ms <= kSwipeMaxTimeMs);
}

static int16_t get_back_target_screen(int16_t current_screen) {
    switch (current_screen) {
        case SCREEN_ID_MENU:
            return SCREEN_ID_MAIN;
        case SCREEN_ID_USTAWIENIA:
            return SCREEN_ID_MENU;
        case SCREEN_ID_ZASOBY_SYSTEMU:
            return SCREEN_ID_MENU;
        case SCREEN_ID_USTAWIENIA_WIFI:
            return SCREEN_ID_MENU;
        case SCREEN_ID_ZEGAR:
            return SCREEN_ID_MENU;
        case SCREEN_ID_LOGI:
            return SCREEN_ID_MENU;
        case SCREEN_ID_POGODA:
            return SCREEN_ID_MAIN;
        case SCREEN_ID_BPM:
            return SCREEN_ID_MENU;
        case SCREEN_ID_GRY_TEST:
            return SCREEN_ID_MENU;
        case SCREEN_ID_MUZYKA:
            return SCREEN_ID_MAIN;
        case SCREEN_ID_CZUJNIKI:
            return SCREEN_ID_MAIN;
        case SCREEN_ID_GEOMETRIA_EKRANU:
            return SCREEN_ID_MENU;
        case SCREEN_ID_RADAR:
            return SCREEN_ID_MENU;
        default:
            return SCREEN_ID_MAIN;
    }
}

extern "C" void action_action_swipe_up(lv_event_t * e);

static void handle_press_event(lv_indev_t * indev) {
    lv_point_t start_point;
    lv_indev_get_point(indev, &start_point);

    if (!is_bottom_zone_press(start_point)) {
        return;
    }

    if (is_control_surface(lv_indev_get_active_obj())) {
        return;
    }

    start_tracking(start_point);
}

static void handle_release_event(lv_indev_t * indev, lv_event_t * e) {
    if (!g_gesture.tracking) {
        return;
    }

    lv_point_t end_point;
    lv_indev_get_point(indev, &end_point);

    if (is_valid_swipe(end_point)) {
        action_action_swipe_up(e);
        return;
    }

    stop_tracking();
}

static void gesture_event_cb(lv_event_t * e) {
    lv_indev_t * indev = get_active_pointer_indev();
    if (!indev) {
        return;
    }

    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:
            handle_press_event(indev);
            break;
        case LV_EVENT_RELEASED:
            handle_release_event(indev, e);
            break;
        case LV_EVENT_PRESS_LOST:
            // Utrata trackingu (np. panel przestal sledzic palec) NIE jest
            // swipe'em — bez tego nawigacja odpalala sie przypadkowo.
            stop_tracking();
            break;
        default:
            break;
    }
}

} // namespace

extern "C" void action_action_swipe_up(lv_event_t * e) {
    lv_indev_t * indev = get_active_pointer_indev();
    if (!indev) {
        return;
    }

    if (e && lv_event_get_code(e) == LV_EVENT_GESTURE) {
        if (lv_indev_get_gesture_dir(indev) != LV_DIR_TOP) {
            return;
        }
    }

    if (!g_gesture.tracking) {
        return;
    }

    stop_tracking();

    int16_t current_screen = eez_flow_get_current_screen();
    int16_t target_screen = get_back_target_screen(current_screen);

    ESP_LOGI(kTag, "Gest Swipe Up zatwierdzony. Przejscie: %d -> %d (bez animacji)", (int)current_screen, (int)target_screen);
    eez_flow_set_screen(target_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0);
}

extern "C" void gesture_register_recursive(lv_obj_t * obj) {
    if (!obj) {
        return;
    }

    lv_obj_remove_event_cb(obj, gesture_event_cb);
    lv_obj_add_event_cb(obj, gesture_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, gesture_event_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(obj, gesture_event_cb, LV_EVENT_PRESS_LOST, NULL);

    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        gesture_register_recursive(lv_obj_get_child(obj, i));
    }
}
