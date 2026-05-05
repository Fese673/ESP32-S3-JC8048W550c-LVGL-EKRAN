#include "notification_logic.h"
#include "lvgl.h"
#include "screens.h"
#include "eez-flow.h"

extern "C" {

static bool s_wifi_enabled = true;
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
      w.bell_on = objects.obj13;
      w.bell_off = objects.obj14;
      w.ble_on = objects.obj15;
      w.ble_off = objects.obj16;
      w.wifi_on = objects.obj17;
      w.wifi_off = objects.obj18;
      break;
    case SCREEN_ID_MENU:
      w.bell_on = objects.obj26;
      w.bell_off = objects.obj27;
      w.ble_on = objects.obj28;
      w.ble_off = objects.obj29;
      w.wifi_on = objects.obj30;
      w.wifi_off = objects.obj31;
      break;
    case SCREEN_ID_USTAWIENIA:
      w.bell_on = objects.obj40;
      w.bell_off = objects.obj41;
      w.ble_on = objects.obj42;
      w.ble_off = objects.obj43;
      w.wifi_on = objects.obj44;
      w.wifi_off = objects.obj45;
      break;
    case SCREEN_ID_STATYSTYKI:
      w.bell_on = objects.obj49;
      w.bell_off = objects.obj50;
      w.ble_on = objects.obj51;
      w.ble_off = objects.obj52;
      w.wifi_on = objects.obj53;
      w.wifi_off = objects.obj54;
      break;
    case SCREEN_ID_ZASOBY_SYSTEMU:
      w.bell_on = objects.obj58;
      w.bell_off = objects.obj59;
      w.ble_on = objects.obj60;
      w.ble_off = objects.obj61;
      w.wifi_on = objects.obj62;
      w.wifi_off = objects.obj63;
      break;
    case SCREEN_ID_USTAWIENIA_WIFI:
      w.bell_on = objects.obj67;
      w.bell_off = objects.obj68;
      w.ble_on = objects.obj69;
      w.ble_off = objects.obj70;
      w.wifi_on = objects.obj71;
      w.wifi_off = objects.obj72;
      break;
    case SCREEN_ID_GRY:
      w.bell_on = objects.obj77;
      w.bell_off = objects.obj78;
      w.ble_on = objects.obj79;
      w.ble_off = objects.obj80;
      w.wifi_on = objects.obj81;
      w.wifi_off = objects.obj82;
      break;
    case SCREEN_ID_ZEGAR:
      w.bell_on = objects.obj85;
      w.bell_off = objects.obj86;
      w.ble_on = objects.obj87;
      w.ble_off = objects.obj88;
      w.wifi_on = objects.obj89;
      w.wifi_off = objects.obj90;
      break;
    default:
      break;
  }
  return w;
}

void notification_logic_init() {
  s_wifi_enabled = true;
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

  if (screen_id == SCREEN_ID_MAIN && objects.hour_text) {
    lv_obj_t *time_container = lv_obj_get_parent(objects.hour_text);
    if (time_container) {
      lv_obj_t *sun_obj = lv_obj_get_child(time_container, 5);
      lv_obj_t *moon_obj = lv_obj_get_child(time_container, 6);
      if (sun_obj && moon_obj) {
        if (s_is_day) {
          lv_obj_remove_flag(sun_obj, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(moon_obj, LV_OBJ_FLAG_HIDDEN);
        } else {
          lv_obj_add_flag(sun_obj, LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(moon_obj, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
  }
}

}


