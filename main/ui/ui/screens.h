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
    SCREEN_ID_STATYSTYKI = 4,
    SCREEN_ID_ZASOBY_SYSTEMU = 5,
    SCREEN_ID_USTAWIENIA_WIFI = 6,
    SCREEN_ID_GRY = 7,
    SCREEN_ID_ZEGAR = 8,
    _SCREEN_ID_LAST = 8
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *menu;
    lv_obj_t *ustawienia;
    lv_obj_t *statystyki;
    lv_obj_t *zasoby_systemu;
    lv_obj_t *ustawienia_wifi;
    lv_obj_t *gry;
    lv_obj_t *zegar;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
    lv_obj_t *obj2;
    lv_obj_t *obj3;
    lv_obj_t *obj4;
    lv_obj_t *obj5;
    lv_obj_t *obj6;
    lv_obj_t *obj7;
    lv_obj_t *obj8;
    lv_obj_t *hour_text;
    lv_obj_t *minutes_text;
    lv_obj_t *obj9;
    lv_obj_t *obj10;
    lv_obj_t *obj11;
    lv_obj_t *obj12;
    lv_obj_t *obj13;
    lv_obj_t *obj14;
    lv_obj_t *obj15;
    lv_obj_t *obj16;
    lv_obj_t *obj17;
    lv_obj_t *obj18;
    lv_obj_t *name_day_text;
    lv_obj_t *name_day_text_1;
    lv_obj_t *name_day_text_2;
    lv_obj_t *name_month_date;
    lv_obj_t *obj19;
    lv_obj_t *obj20;
    lv_obj_t *obj21;
    lv_obj_t *obj22;
    lv_obj_t *obj23;
    lv_obj_t *seconds_text_4;
    lv_obj_t *obj24;
    lv_obj_t *minutes_text_5;
    lv_obj_t *obj25;
    lv_obj_t *hour_text_5;
    lv_obj_t *obj26;
    lv_obj_t *obj27;
    lv_obj_t *obj28;
    lv_obj_t *obj29;
    lv_obj_t *obj30;
    lv_obj_t *obj31;
    lv_obj_t *obj32;
    lv_obj_t *obj33;
    lv_obj_t *obj34;
    lv_obj_t *obj35;
    lv_obj_t *obj36;
    lv_obj_t *obj37;
    lv_obj_t *seconds_text_2;
    lv_obj_t *obj38;
    lv_obj_t *minutes_text_3;
    lv_obj_t *obj39;
    lv_obj_t *hour_text_3;
    lv_obj_t *obj40;
    lv_obj_t *obj41;
    lv_obj_t *obj42;
    lv_obj_t *obj43;
    lv_obj_t *obj44;
    lv_obj_t *obj45;
    lv_obj_t *obj46;
    lv_obj_t *seconds_text_3;
    lv_obj_t *obj47;
    lv_obj_t *minutes_text_4;
    lv_obj_t *obj48;
    lv_obj_t *hour_text_4;
    lv_obj_t *obj49;
    lv_obj_t *obj50;
    lv_obj_t *obj51;
    lv_obj_t *obj52;
    lv_obj_t *obj53;
    lv_obj_t *obj54;
    lv_obj_t *obj55;
    lv_obj_t *seconds_text_5;
    lv_obj_t *obj56;
    lv_obj_t *minutes_text_6;
    lv_obj_t *obj57;
    lv_obj_t *hour_text_6;
    lv_obj_t *obj58;
    lv_obj_t *obj59;
    lv_obj_t *obj60;
    lv_obj_t *obj61;
    lv_obj_t *obj62;
    lv_obj_t *obj63;
    lv_obj_t *obj64;
    lv_obj_t *seconds_text_6;
    lv_obj_t *obj65;
    lv_obj_t *minutes_text_7;
    lv_obj_t *obj66;
    lv_obj_t *hour_text_7;
    lv_obj_t *obj67;
    lv_obj_t *obj68;
    lv_obj_t *obj69;
    lv_obj_t *obj70;
    lv_obj_t *obj71;
    lv_obj_t *obj72;
    lv_obj_t *obj73;
    lv_obj_t *obj74;
    lv_obj_t *seconds_text_7;
    lv_obj_t *obj75;
    lv_obj_t *minutes_text_8;
    lv_obj_t *obj76;
    lv_obj_t *hour_text_8;
    lv_obj_t *obj77;
    lv_obj_t *obj78;
    lv_obj_t *obj79;
    lv_obj_t *obj80;
    lv_obj_t *obj81;
    lv_obj_t *obj82;
    lv_obj_t *obj83;
    lv_obj_t *seconds_text_8;
    lv_obj_t *obj84;
    lv_obj_t *minutes_text_9;
    lv_obj_t *obj85;
    lv_obj_t *hour_text_9;
    lv_obj_t *obj86;
    lv_obj_t *obj87;
    lv_obj_t *obj88;
    lv_obj_t *obj89;
    lv_obj_t *obj90;
    lv_obj_t *obj91;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void create_screen_menu();
void tick_screen_menu();

void create_screen_ustawienia();
void tick_screen_ustawienia();

void create_screen_statystyki();
void tick_screen_statystyki();

void create_screen_zasoby_systemu();
void tick_screen_zasoby_systemu();

void create_screen_ustawienia_wifi();
void tick_screen_ustawienia_wifi();

void create_screen_gry();
void tick_screen_gry();

void create_screen_zegar();
void tick_screen_zegar();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/