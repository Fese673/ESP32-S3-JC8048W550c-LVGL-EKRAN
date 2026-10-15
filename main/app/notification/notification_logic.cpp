#include "notification_logic.h"
#include "lvgl.h"
#include "screens.h"
#include "eez-flow.h"
#include "esp_lvgl_port.h"

extern "C" {

static bool s_wifi_enabled = false;
static bool s_ble_enabled = false;
static bool s_bell_enabled = false;
static bool s_is_day = true;

struct ScreenNotificationWidgets {
  lv_obj_t *bell_on;
  lv_obj_t *bell_off;
  lv_obj_t *ble_on;
  lv_obj_t *ble_off;
  lv_obj_t *wifi_on;
  lv_obj_t *wifi_off;
};

static ScreenNotificationWidgets get_widgets_for_screen(int16_t screen_id) {
  ScreenNotificationWidgets w = {NULL, NULL, NULL, NULL, NULL, NULL};
  switch (screen_id) {
    case SCREEN_ID_MAIN:
      w.bell_on = objects.info_bell_of_icon_on;
      w.bell_off = objects.info_bell_of_icon_off;
      w.ble_on = objects.info_ble_on_icon;
      w.ble_off = objects.info_ble_off_icon;
      w.wifi_on = objects.info_wifi_on_icon;
      w.wifi_off = objects.info_wifi_off_icon;
      break;
    case SCREEN_ID_MENU:
      w.bell_on = objects.info_bell_of_icon_on_1;
      w.bell_off = objects.info_bell_of_icon_off_1;
      w.ble_on = objects.info_ble_on_icon_1;
      w.ble_off = objects.info_ble_off_icon_1;
      w.wifi_on = objects.info_wifi_on_icon_1;
      w.wifi_off = objects.info_wifi_off_icon_1;
      break;
    case SCREEN_ID_USTAWIENIA:
      w.bell_on = objects.info_bell_of_icon_on_2;
      w.bell_off = objects.info_bell_of_icon_off_2;
      w.ble_on = objects.info_ble_on_icon_2;
      w.ble_off = objects.info_ble_off_icon_2;
      w.wifi_on = objects.info_wifi_on_icon_2;
      w.wifi_off = objects.info_wifi_off_icon_2;
      break;
    case SCREEN_ID_ZASOBY_SYSTEMU:
      w.bell_on = objects.info_bell_of_icon_on_3;
      w.bell_off = objects.info_bell_of_icon_off_3;
      w.ble_on = objects.info_ble_on_icon_3;
      w.ble_off = objects.info_ble_off_icon_3;
      w.wifi_on = objects.info_wifi_on_icon_3;
      w.wifi_off = objects.info_wifi_off_icon_3;
      break;
    case SCREEN_ID_USTAWIENIA_WIFI:
      w.bell_on = objects.info_bell_of_icon_on_4;
      w.bell_off = objects.info_bell_of_icon_off_4;
      w.ble_on = objects.info_ble_on_icon_4;
      w.ble_off = objects.info_ble_off_icon_4;
      w.wifi_on = objects.info_wifi_on_icon_4;
      w.wifi_off = objects.info_wifi_off_icon_4;
      break;
    case SCREEN_ID_ZEGAR:
      w.bell_on = objects.info_bell_of_icon_on_6;
      w.bell_off = objects.info_bell_of_icon_off_6;
      w.ble_on = objects.info_ble_on_icon_6;
      w.ble_off = objects.info_ble_off_icon_6;
      w.wifi_on = objects.info_wifi_on_icon_6;
      w.wifi_off = objects.info_wifi_off_icon_6;
      break;
    case SCREEN_ID_LOGI:
      w.bell_on = objects.info_bell_of_icon_on_7;
      w.bell_off = objects.info_bell_of_icon_off_7;
      w.ble_on = objects.info_ble_on_icon_7;
      w.ble_off = objects.info_ble_off_icon_7;
      w.wifi_on = objects.info_wifi_on_icon_7;
      w.wifi_off = objects.info_wifi_off_icon_7;
      break;
    case SCREEN_ID_POGODA:
      w.bell_on = objects.info_bell_of_icon_on_8;
      w.bell_off = objects.info_bell_of_icon_off_8;
      w.ble_on = objects.info_ble_on_icon_8;
      w.ble_off = objects.info_ble_off_icon_8;
      w.wifi_on = objects.info_wifi_on_icon_8;
      w.wifi_off = objects.info_wifi_off_icon_8;
      break;
    case SCREEN_ID_BPM:
      w.bell_on = objects.info_bell_of_icon_on_9;
      w.bell_off = objects.info_bell_of_icon_off_9;
      w.ble_on = objects.info_ble_on_icon_9;
      w.ble_off = objects.info_ble_off_icon_9;
      w.wifi_on = objects.info_wifi_on_icon_9;
      w.wifi_off = objects.info_wifi_off_icon_9;
      break;
    case SCREEN_ID_GRY_TEST:
      break;
    case SCREEN_ID_MUZYKA:
      w.bell_on = objects.info_bell_of_icon_on_10;
      w.bell_off = objects.info_bell_of_icon_off_10;
      w.ble_on = objects.info_ble_on_icon_10;
      w.ble_off = objects.info_ble_off_icon_10;
      w.wifi_on = objects.info_wifi_on_icon_10;
      w.wifi_off = objects.info_wifi_off_icon_10;
      break;
    case SCREEN_ID_CZUJNIKI:
      w.bell_on = objects.info_bell_of_icon_on_11;
      w.bell_off = objects.info_bell_of_icon_off_11;
      w.ble_on = objects.info_ble_on_icon_11;
      w.ble_off = objects.info_ble_off_icon_11;
      w.wifi_on = objects.info_wifi_on_icon_11;
      w.wifi_off = objects.info_wifi_off_icon_11;
      break;
    case SCREEN_ID_INFO:
      w.bell_on = objects.info_bell_of_icon_on_12;
      w.bell_off = objects.info_bell_of_icon_off_12;
      w.ble_on = objects.info_ble_on_icon_12;
      w.ble_off = objects.info_ble_off_icon_12;
      w.wifi_on = objects.info_wifi_on_icon_12;
      w.wifi_off = objects.info_wifi_off_icon_12;
      break;
    case SCREEN_ID_GEOMETRIA_EKRANU:
      w.bell_on = objects.info_bell_of_icon_on_13;
      w.bell_off = objects.info_bell_of_icon_off_13;
      w.ble_on = objects.info_ble_on_icon_13;
      w.ble_off = objects.info_ble_off_icon_13;
      w.wifi_on = objects.info_wifi_on_icon_13;
      w.wifi_off = objects.info_wifi_off_icon_13;
      break;
    case SCREEN_ID_RADAR:
      w.bell_on = objects.info_bell_of_icon_on_14;
      w.bell_off = objects.info_bell_of_icon_off_14;
      w.ble_on = objects.info_ble_on_icon_14;
      w.ble_off = objects.info_ble_off_icon_14;
      w.wifi_on = objects.info_wifi_on_icon_14;
      w.wifi_off = objects.info_wifi_off_icon_14;
      break;
    case SCREEN_ID_PROJECT_INFO:
      w.bell_on = objects.info_bell_of_icon_on_15;
      w.bell_off = objects.info_bell_of_icon_off_15;
      w.ble_on = objects.info_ble_on_icon_15;
      w.ble_off = objects.info_ble_off_icon_15;
      w.wifi_on = objects.info_wifi_on_icon_15;
      w.wifi_off = objects.info_wifi_off_icon_15;
      break;
    default:
      break;
  }
  return w;
}

void notification_logic_init() {
  s_wifi_enabled = false;
  s_ble_enabled = false;
  s_bell_enabled = false;
  s_is_day = true;
  notification_logic_refresh();
}

void notification_logic_set_wifi(bool enabled) {
  s_wifi_enabled = enabled;
  notification_logic_refresh();
}

void notification_logic_set_ble(bool enabled) {
  s_ble_enabled = enabled;
  notification_logic_refresh();
}

void notification_logic_set_bell(bool enabled) {
  s_bell_enabled = enabled;
  notification_logic_refresh();
}

void notification_logic_set_sun_moon(bool is_day) {
  s_is_day = is_day;
  notification_logic_refresh();
}

void notification_logic_refresh() {
  if (!lvgl_port_lock(0)) return;
  int16_t screen_id = eez_flow_get_current_screen();
  ScreenNotificationWidgets w = get_widgets_for_screen(screen_id);

  if (w.wifi_on && w.wifi_off) {
    if (s_wifi_enabled) {
      lv_obj_remove_flag(w.wifi_on, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(w.wifi_off, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(w.wifi_on, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(w.wifi_off, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (w.ble_on && w.ble_off) {
    if (s_ble_enabled) {
      lv_obj_remove_flag(w.ble_on, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(w.ble_off, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(w.ble_on, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(w.ble_off, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (w.bell_on && w.bell_off) {
    if (s_bell_enabled) {
      lv_obj_remove_flag(w.bell_on, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(w.bell_off, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(w.bell_on, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(w.bell_off, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (screen_id == SCREEN_ID_MAIN) {
    if (objects.sun_icon && objects.moon_icon) {
      if (s_is_day) {
        lv_obj_remove_flag(objects.sun_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(objects.moon_icon, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(objects.sun_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(objects.moon_icon, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  lvgl_port_unlock();
}

} // extern "C"



