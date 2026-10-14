#ifndef EEZ_LVGL_UI_IMAGES_H
#define EEZ_LVGL_UI_IMAGES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_kfe_analiza;
extern const lv_img_dsc_t img_kfe_czujniki;
extern const lv_img_dsc_t img_kfe_menu;
extern const lv_img_dsc_t img_kfe_pogoda;
extern const lv_img_dsc_t img_left_arrow;
extern const lv_img_dsc_t img_moon;
extern const lv_img_dsc_t img_right_arrow;
extern const lv_img_dsc_t img_sun;
extern const lv_img_dsc_t img_wifi_on_icon;
extern const lv_img_dsc_t img_ble_on_icon;
extern const lv_img_dsc_t img_bell_on_icon;
extern const lv_img_dsc_t img_water_icon;
extern const lv_img_dsc_t img_temp_icon;
extern const lv_img_dsc_t img_pm_icon;
extern const lv_img_dsc_t img_tvoc_icon;
extern const lv_img_dsc_t img_air_icon;
extern const lv_img_dsc_t img_kfe_info;
extern const lv_img_dsc_t img_kfe_log;
extern const lv_img_dsc_t img_kfe_tools;
extern const lv_img_dsc_t img_kfe_usieci;
extern const lv_img_dsc_t img_kfe_zasoby;
extern const lv_img_dsc_t img_kfe_statystyk;
extern const lv_img_dsc_t img_kfe_gry;
extern const lv_img_dsc_t img_kfe_alarm;
extern const lv_img_dsc_t img_kfe_buzzer;
extern const lv_img_dsc_t img_kfe_pin_dotyku;
extern const lv_img_dsc_t img_kfe_mqtt;
extern const lv_img_dsc_t img_kfe_muzykawtle;
extern const lv_img_dsc_t img_kfe_pms;
extern const lv_img_dsc_t img_wifi_off_icon;
extern const lv_img_dsc_t img_bell_of_icon;
extern const lv_img_dsc_t img_ble_off_icon;
extern const lv_img_dsc_t img_pressure_icon;
extern const lv_img_dsc_t img_kfe_clock;
extern const lv_img_dsc_t img_co2_icon;
extern const lv_img_dsc_t img_kfe_ram;
extern const lv_img_dsc_t img_kfe_flash;
extern const lv_img_dsc_t img_kfe_cpu;
extern const lv_img_dsc_t img_kfe_12c;
extern const lv_img_dsc_t img_kfe_audio;
extern const lv_img_dsc_t img_kfe_wind;
extern const lv_img_dsc_t img_kfe_weather;
extern const lv_img_dsc_t img_kfe_uvindex;
extern const lv_img_dsc_t img_kfe_sunsetrise;
extern const lv_img_dsc_t img_kfe_rainfall;
extern const lv_img_dsc_t img_kfe_pressures;
extern const lv_img_dsc_t img_kfe_perceptible;
extern const lv_img_dsc_t img_kfe_humiditys;
extern const lv_img_dsc_t img_kfe_dustpm25;
extern const lv_img_dsc_t img_kfe_czas;
extern const lv_img_dsc_t img_kfe_budzik;
extern const lv_img_dsc_t img_kfe_stoper;
extern const lv_img_dsc_t img_kfe_minutnik;
extern const lv_img_dsc_t img_kfe_bpm;
extern const lv_img_dsc_t img_kfe_ustawienia;
extern const lv_img_dsc_t img_kfe_jasno__;
extern const lv_img_dsc_t img_kfe_geometria_ekranu;
extern const lv_img_dsc_t img_kfe_radar;

#ifndef EXT_IMG_DESC_T
#define EXT_IMG_DESC_T
typedef struct _ext_img_desc_t {
    const char *name;
    const lv_img_dsc_t *img_dsc;
} ext_img_desc_t;
#endif

extern const ext_img_desc_t images[58];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_IMAGES_H*/