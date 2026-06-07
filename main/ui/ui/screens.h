#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    SCREEN_ID_MENU = 2,
    SCREEN_ID_USTAWIENIA = 3,
    SCREEN_ID_ZASOBY_SYSTEMU = 4,
    SCREEN_ID_USTAWIENIA_WIFI = 5,
    SCREEN_ID_ZEGAR = 6,
    SCREEN_ID_LOGI = 7,
    SCREEN_ID_POGODA = 8,
    SCREEN_ID_BPM = 9,
    SCREEN_ID_GRY_TEST = 10,
    SCREEN_ID_MUZYKA = 11,
    SCREEN_ID_CZUJNIKI = 12,
    _SCREEN_ID_LAST = 12
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *menu;
    lv_obj_t *ustawienia;
    lv_obj_t *zasoby_systemu;
    lv_obj_t *ustawienia_wifi;
    lv_obj_t *zegar;
    lv_obj_t *logi;
    lv_obj_t *pogoda;
    lv_obj_t *bpm;
    lv_obj_t *gry_test;
    lv_obj_t *muzyka;
    lv_obj_t *czujniki;
    lv_obj_t *weather_in;
    lv_obj_t *pressure_in_5;
    lv_obj_t *co2_in_6;
    lv_obj_t *pm10_in_7;
    lv_obj_t *tvoc_in_8;
    lv_obj_t *hm_in_1;
    lv_obj_t *txt_in_4;
    lv_obj_t *tm_in_2;
    lv_obj_t *pm25_in_3;
    lv_obj_t *obj0;
    lv_obj_t *hour_text;
    lv_obj_t *minutes_text;
    lv_obj_t *obj1;
    lv_obj_t *sun_icon;
    lv_obj_t *moon_icon;
    lv_obj_t *obj2;
    lv_obj_t *obj3;
    lv_obj_t *obj4;
    lv_obj_t *obj5;
    lv_obj_t *obj6;
    lv_obj_t *obj7;
    lv_obj_t *info_bell_of_icon_on;
    lv_obj_t *info_bell_of_icon_off;
    lv_obj_t *info_ble_on_icon;
    lv_obj_t *info_ble_off_icon;
    lv_obj_t *info_wifi_on_icon;
    lv_obj_t *info_wifi_off_icon;
    lv_obj_t *name_day_text;
    lv_obj_t *name_day_text_1;
    lv_obj_t *name_day_text_2;
    lv_obj_t *name_month_date;
    lv_obj_t *weather_out;
    lv_obj_t *pressure_out_5;
    lv_obj_t *co2_out_6;
    lv_obj_t *pm10_out_7;
    lv_obj_t *no2_out_8;
    lv_obj_t *hm_out_1;
    lv_obj_t *txt_out_4;
    lv_obj_t *tm_out_2;
    lv_obj_t *pm25_out_3;
    lv_obj_t *weather_in_label;
    lv_obj_t *weather_out_label;
    lv_obj_t *weather_in_label_1;
    lv_obj_t *obj8;
    lv_obj_t *menu_kfe_log_6;
    lv_obj_t *menu_kfe_tools_3;
    lv_obj_t *menu_kfe_statystyk_1;
    lv_obj_t *menu_kfe_harmonogram_8;
    lv_obj_t *menu_kfe_usieci_5;
    lv_obj_t *menu_kfe_info_2;
    lv_obj_t *menu_kfe_gry_7;
    lv_obj_t *menu_kfe_zasoby_4;
    lv_obj_t *menu_kfe_clock_9;
    lv_obj_t *obj9;
    lv_obj_t *seconds_text_12;
    lv_obj_t *obj10;
    lv_obj_t *minutes_text_13;
    lv_obj_t *obj11;
    lv_obj_t *hour_text_13;
    lv_obj_t *info_bell_of_icon_on_1;
    lv_obj_t *info_bell_of_icon_off_1;
    lv_obj_t *info_ble_on_icon_1;
    lv_obj_t *info_ble_off_icon_1;
    lv_obj_t *info_wifi_on_icon_1;
    lv_obj_t *info_wifi_off_icon_1;
    lv_obj_t *settings_melody_dropdown;
    lv_obj_t *obj12;
    lv_obj_t *obj13;
    lv_obj_t *obj14;
    lv_obj_t *obj15;
    lv_obj_t *obj16;
    lv_obj_t *obj17;
    lv_obj_t *seconds_text_21;
    lv_obj_t *obj18;
    lv_obj_t *minutes_text_22;
    lv_obj_t *obj19;
    lv_obj_t *hour_text_22;
    lv_obj_t *info_bell_of_icon_on_2;
    lv_obj_t *info_bell_of_icon_off_2;
    lv_obj_t *info_ble_on_icon_2;
    lv_obj_t *info_ble_off_icon_2;
    lv_obj_t *info_wifi_on_icon_2;
    lv_obj_t *info_wifi_off_icon_2;
    lv_obj_t *free_flash_slider;
    lv_obj_t *used_flash_slider;
    lv_obj_t *cpu_core1_slider;
    lv_obj_t *cpu_core0_slider;
    lv_obj_t *obj20;
    lv_obj_t *obj21;
    lv_obj_t *obj22;
    lv_obj_t *obj23;
    lv_obj_t *obj24;
    lv_obj_t *obj25;
    lv_obj_t *obj26;
    lv_obj_t *obj27;
    lv_obj_t *obj28;
    lv_obj_t *obj29;
    lv_obj_t *obj30;
    lv_obj_t *obj31;
    lv_obj_t *obj32;
    lv_obj_t *seconds_text_19;
    lv_obj_t *obj33;
    lv_obj_t *minutes_text_20;
    lv_obj_t *obj34;
    lv_obj_t *hour_text_20;
    lv_obj_t *info_bell_of_icon_on_3;
    lv_obj_t *info_bell_of_icon_off_3;
    lv_obj_t *info_ble_on_icon_3;
    lv_obj_t *info_ble_off_icon_3;
    lv_obj_t *info_wifi_on_icon_3;
    lv_obj_t *info_wifi_off_icon_3;
    lv_obj_t *wifi_keyboard;
    lv_obj_t *wifi_password_input;
    lv_obj_t *wifi_list;
    lv_obj_t *wifi_status_labe;
    lv_obj_t *obj35;
    lv_obj_t *seconds_text_16;
    lv_obj_t *obj36;
    lv_obj_t *minutes_text_17;
    lv_obj_t *obj37;
    lv_obj_t *hour_text_17;
    lv_obj_t *info_bell_of_icon_on_4;
    lv_obj_t *info_bell_of_icon_off_4;
    lv_obj_t *info_ble_on_icon_4;
    lv_obj_t *info_ble_off_icon_4;
    lv_obj_t *info_wifi_on_icon_4;
    lv_obj_t *info_wifi_off_icon_4;
    lv_obj_t *obj38;
    lv_obj_t *seconds_text_13;
    lv_obj_t *obj39;
    lv_obj_t *minutes_text_14;
    lv_obj_t *obj40;
    lv_obj_t *hour_text_14;
    lv_obj_t *start_stop_minutnik_button;
    lv_obj_t *start_stop_stoper_button;
    lv_obj_t *reset_stoper_button;
    lv_obj_t *minutes_stoper_label;
    lv_obj_t *seconds_stoper_label;
    lv_obj_t *obj41;
    lv_obj_t *ms_stoper_label;
    lv_obj_t *obj42;
    lv_obj_t *obj43;
    lv_obj_t *add_alarm;
    lv_obj_t *start_stop_label_stoper;
    lv_obj_t *start_stop_label_minutnik;
    lv_obj_t *alarm_list;
    lv_obj_t *minutnik_container;
    lv_obj_t *info_bell_of_icon_on_6;
    lv_obj_t *info_bell_of_icon_off_6;
    lv_obj_t *info_ble_on_icon_6;
    lv_obj_t *info_ble_off_icon_6;
    lv_obj_t *info_wifi_on_icon_6;
    lv_obj_t *info_wifi_off_icon_6;
    lv_obj_t *log_terminal;
    lv_obj_t *obj44;
    lv_obj_t *seconds_text_14;
    lv_obj_t *obj45;
    lv_obj_t *minutes_text_15;
    lv_obj_t *obj46;
    lv_obj_t *hour_text_15;
    lv_obj_t *info_bell_of_icon_on_7;
    lv_obj_t *info_bell_of_icon_off_7;
    lv_obj_t *info_ble_on_icon_7;
    lv_obj_t *info_ble_off_icon_7;
    lv_obj_t *info_wifi_on_icon_7;
    lv_obj_t *info_wifi_off_icon_7;
    lv_obj_t *obj47;
    lv_obj_t *seconds_text_10;
    lv_obj_t *obj48;
    lv_obj_t *minutes_text_11;
    lv_obj_t *obj49;
    lv_obj_t *hour_text_11;
    lv_obj_t *tm_odczuwalna_weather;
    lv_obj_t *_wind_weather;
    lv_obj_t *rain_weather;
    lv_obj_t *humidity_weather;
    lv_obj_t *pressure_weather;
    lv_obj_t *uv_weather;
    lv_obj_t *sunrise__weather;
    lv_obj_t *sunset__weather;
    lv_obj_t *temp_sensor_out;
    lv_obj_t *text_weather;
    lv_obj_t *pm25_weather;
    lv_obj_t *info_bell_of_icon_on_8;
    lv_obj_t *info_bell_of_icon_off_8;
    lv_obj_t *info_ble_on_icon_8;
    lv_obj_t *info_ble_off_icon_8;
    lv_obj_t *info_wifi_on_icon_8;
    lv_obj_t *info_wifi_off_icon_8;
    lv_obj_t *obj50;
    lv_obj_t *seconds_text_17;
    lv_obj_t *obj51;
    lv_obj_t *minutes_text_18;
    lv_obj_t *obj52;
    lv_obj_t *hour_text_18;
    lv_obj_t *bpm_chart;
    lv_obj_t *info_bell_of_icon_on_9;
    lv_obj_t *info_bell_of_icon_off_9;
    lv_obj_t *info_ble_on_icon_9;
    lv_obj_t *info_ble_off_icon_9;
    lv_obj_t *info_wifi_on_icon_9;
    lv_obj_t *info_wifi_off_icon_9;
    lv_obj_t *obj53;
    lv_obj_t *seconds_text_18;
    lv_obj_t *obj54;
    lv_obj_t *minutes_text_26;
    lv_obj_t *obj55;
    lv_obj_t *hour_text_26;
    lv_obj_t *info_bell_of_icon_on_10;
    lv_obj_t *info_bell_of_icon_off_10;
    lv_obj_t *info_ble_on_icon_10;
    lv_obj_t *info_ble_off_icon_10;
    lv_obj_t *info_wifi_on_icon_10;
    lv_obj_t *info_wifi_off_icon_10;
    lv_obj_t *obj56;
    lv_obj_t *seconds_text_22;
    lv_obj_t *obj57;
    lv_obj_t *minutes_text_27;
    lv_obj_t *obj58;
    lv_obj_t *hour_text_27;
    lv_obj_t *info_bell_of_icon_on_11;
    lv_obj_t *info_bell_of_icon_off_11;
    lv_obj_t *info_ble_on_icon_11;
    lv_obj_t *info_ble_off_icon_11;
    lv_obj_t *info_wifi_on_icon_11;
    lv_obj_t *info_wifi_off_icon_11;
} objects_t;

extern objects_t objects;

typedef struct {
    lv_span_t *span_0;
} screen_logi_state_t;

extern screen_logi_state_t screen_logi_state;

void create_screen_main();
void delete_screen_main();
void tick_screen_main();

void create_screen_menu();
void delete_screen_menu();
void tick_screen_menu();

void create_screen_ustawienia();
void delete_screen_ustawienia();
void tick_screen_ustawienia();

void create_screen_zasoby_systemu();
void delete_screen_zasoby_systemu();
void tick_screen_zasoby_systemu();

void create_screen_ustawienia_wifi();
void delete_screen_ustawienia_wifi();
void tick_screen_ustawienia_wifi();

void create_screen_zegar();
void delete_screen_zegar();
void tick_screen_zegar();

void create_screen_logi();
void delete_screen_logi();
void tick_screen_logi();

void create_screen_pogoda();
void delete_screen_pogoda();
void tick_screen_pogoda();

void create_screen_bpm();
void delete_screen_bpm();
void tick_screen_bpm();

void create_screen_gry_test();
void delete_screen_gry_test();
void tick_screen_gry_test();

void create_screen_muzyka();
void delete_screen_muzyka();
void tick_screen_muzyka();

void create_screen_czujniki();
void delete_screen_czujniki();
void tick_screen_czujniki();

void create_screen_by_id(enum ScreensEnum screenId);
void delete_screen_by_id(enum ScreensEnum screenId);
void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/