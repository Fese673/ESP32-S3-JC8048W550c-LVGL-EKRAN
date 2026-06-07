#include "weather_icon_handler.h"
#include "custom_icons/weather_icons.h"
#include "screens.h"
#include "lvgl.h"
#include <stddef.h>

#define NUM_ICONS 13

static lv_obj_t *s_icon = NULL;
static bool s_inited = false;

static const lv_image_dsc_t* s_icon_map[NUM_ICONS] = {
    &img_wi_sunny,
    &img_wi_partly_cloudy,
    &img_wi_cloudy,
    &img_wi_fog,
    &img_wi_windy,
    &img_wi_dry,
    &img_wi_frost,
    &img_wi_fog,
    &img_wi_drizzle,
    &img_wi_rain,
    &img_wi_snow,
    &img_wi_sleet,
    &img_wi_storm,
};

static int wmo_code_to_index(uint8_t code) {
    if (code == 0)           return 0;
    if (code <= 3)           return 1;
    if (code <= 9)           return 2;
    if (code <= 19)          return 3;
    if (code <= 29)          return 4;
    if (code <= 35)          return 5;
    if (code <= 39)          return 6;
    if (code <= 49)          return 7;
    if (code <= 59)          return 8;
    if (code <= 69)          return 9;
    if (code <= 79)          return 10;
    if (code <= 89)          return 11;
    if (code <= 99)          return 12;
    return 0;
}

void weather_icon_handler_init(void) {
    if (s_inited) return;
    s_inited = true;
    if (!objects.pogoda) return;

    s_icon = lv_image_create(objects.pogoda);
    lv_image_set_src(s_icon, &img_wi_sunny);
    lv_obj_set_pos(s_icon, 280, 50);
    lv_obj_set_size(s_icon, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
}

void weather_icon_handler_set(uint8_t weatherCode) {
    if (!s_inited || !s_icon) return;
    int idx = wmo_code_to_index(weatherCode);
    if (idx < 0 || idx >= NUM_ICONS) idx = 0;
    lv_image_set_src(s_icon, s_icon_map[idx]);
}
