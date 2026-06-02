#include "alarm_ui.h"
#include "alarm_logic.h"
#include "screens.h"
#include "fonts.h"
#include "communication/time/timer_synchro.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include <stdio.h>
#include <string.h>

static bool s_edit_locked = false;
static int s_popup_alarm_idx = -1;
static bool s_popup_days[7] = {false};
static lv_obj_t *s_popup_day_btns[7] = {NULL};
static lv_obj_t *s_popup_roller_hour = NULL;
static lv_obj_t *s_popup_roller_min = NULL;
static lv_obj_t *s_popup_overlay = NULL;

static void on_save_click(lv_event_t *e);
static void on_cancel_click(lv_event_t *e);
static void on_delete_click(lv_event_t *e);
static void on_day_btn_click(lv_event_t *e);
static void on_switch_toggle(lv_event_t *e);
static void on_card_click(lv_event_t *e);
static void on_add_alarm_click(lv_event_t *e);

static void update_day_button_style(lv_obj_t *btn, bool active) {
    if (active) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x18161b), LV_PART_MAIN);
        }
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x110f14), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x312e38), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), LV_PART_MAIN);
        }
    }
}

static void publish_alarm_list_to_clock(void) {
    CommunicationProtocol::TimerSync::sendAlarmList();
}

static void on_day_btn_click(lv_event_t *e) {
    if (s_edit_locked) {
        return;
    }
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    int day_idx = (int)(uintptr_t)lv_event_get_user_data(e);
    s_popup_days[day_idx] = !s_popup_days[day_idx];
    update_day_button_style(btn, s_popup_days[day_idx]);
}

static void on_switch_toggle(lv_event_t *e) {
    if (s_edit_locked) {
        return;
    }
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (alarm_toggle(idx, enabled)) {
        publish_alarm_list_to_clock();
    }
}

static void alarm_ui_open_popup(int alarm_index) {
    s_popup_alarm_idx = alarm_index;
    
    // Tworzenie pełnoekranowej, ciemnej nakładki blokującej dotyk w tle
    s_popup_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_popup_overlay, 800, 480);
    lv_obj_set_pos(s_popup_overlay, 0, 0);
    lv_obj_remove_flag(s_popup_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_popup_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_popup_overlay, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_popup_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_popup_overlay, 0, LV_PART_MAIN);
    
    // Panel centralny Popupa (450px x 380px)
    lv_obj_t *box = lv_obj_create(s_popup_overlay);
    lv_obj_set_size(box, 450, 380);
    lv_obj_center(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(box, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x1e1b22), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(box, lv_color_hex(0x312e38), LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(box, 16, LV_PART_MAIN);
    
    // Dynamiczny Tytuł
    lv_obj_t *lbl_title = lv_label_create(box);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_title, &ui_font_montserrat_regular_16, LV_PART_MAIN);
    if (s_popup_alarm_idx == -1) {
        lv_label_set_text_static(lbl_title, "NOWY BUDZIK");
    } else {
        lv_label_set_text_static(lbl_title, "EDYCJA BUDZIKA");
    }
    
    // Rząd przycisków wyboru dni (PN-ND)
    lv_obj_t *day_row = lv_obj_create(box);
    lv_obj_set_pos(day_row, 15, 65);
    lv_obj_set_size(day_row, 420, 50);
    lv_obj_remove_flag(day_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(day_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(day_row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(day_row, 0, LV_PART_MAIN);
    
    lv_obj_set_layout(day_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(day_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(day_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // Wczytanie początkowych dni
    if (s_popup_alarm_idx != -1) {
        alarm_t a;
        if (alarm_get(s_popup_alarm_idx, &a)) {
            memcpy(s_popup_days, a.days, sizeof(s_popup_days));
        }
    } else {
        memset(s_popup_days, 0, sizeof(s_popup_days));
    }
    
    const char* day_names[7] = {"PN", "WT", "ŚR", "CZ", "PT", "SO", "ND"};
    for (int i = 0; i < 7; i++) {
        s_popup_day_btns[i] = lv_button_create(day_row);
        lv_obj_set_size(s_popup_day_btns[i], 36, 36);
        lv_obj_set_style_radius(s_popup_day_btns[i], 18, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_popup_day_btns[i], 0, LV_PART_MAIN);
        
        lv_obj_t *lbl_btn = lv_label_create(s_popup_day_btns[i]);
        lv_obj_center(lbl_btn);
        lv_obj_set_style_text_font(lbl_btn, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(lbl_btn, day_names[i]);
        
        lv_obj_add_event_cb(s_popup_day_btns[i], on_day_btn_click, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
        update_day_button_style(s_popup_day_btns[i], s_popup_days[i]);
    }
    
    // Kontener rolek czasu
    lv_obj_t *roller_cnt = lv_obj_create(box);
    lv_obj_set_pos(roller_cnt, 100, 130);
    lv_obj_set_size(roller_cnt, 250, 140);
    lv_obj_remove_flag(roller_cnt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(roller_cnt, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(roller_cnt, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(roller_cnt, 0, LV_PART_MAIN);
    
    // Rolka godzin
    s_popup_roller_hour = lv_roller_create(roller_cnt);
    lv_obj_set_size(s_popup_roller_hour, 80, 120);
    lv_obj_align(s_popup_roller_hour, LV_ALIGN_LEFT_MID, 10, 0);
    lv_roller_set_options(s_popup_roller_hour, 
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", 
        LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_popup_roller_hour, 3);
    lv_obj_set_style_text_font(s_popup_roller_hour, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_popup_roller_hour, &ui_font_bebas_neue30, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(s_popup_roller_hour, lv_color_hex(0x18161b), LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_popup_roller_hour, lv_color_hex(0xffffff), LV_PART_SELECTED);
    
    // Separator ":"
    lv_obj_t *lbl_sep = lv_label_create(roller_cnt);
    lv_obj_center(lbl_sep);
    lv_obj_set_style_text_color(lbl_sep, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_sep, &ui_font_bebas_neue30, LV_PART_MAIN);
    lv_label_set_text_static(lbl_sep, ":");
    
    // Rolka minut
    s_popup_roller_min = lv_roller_create(roller_cnt);
    lv_obj_set_size(s_popup_roller_min, 80, 120);
    lv_obj_align(s_popup_roller_min, LV_ALIGN_RIGHT_MID, -10, 0);
    
    char min_opts[300] = "";
    for (int i = 0; i < 60; i++) {
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "%02d\n", i);
        strcat(min_opts, tmp);
    }
    min_opts[strlen(min_opts) - 1] = '\0';
    lv_roller_set_options(s_popup_roller_min, min_opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_popup_roller_min, 3);
    lv_obj_set_style_text_font(s_popup_roller_min, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_popup_roller_min, &ui_font_bebas_neue30, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(s_popup_roller_min, lv_color_hex(0x18161b), LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_popup_roller_min, lv_color_hex(0xffffff), LV_PART_SELECTED);
    
    // Ustawienie początkowego czasu
    if (s_popup_alarm_idx != -1) {
        alarm_t a;
        if (alarm_get(s_popup_alarm_idx, &a)) {
            lv_roller_set_selected(s_popup_roller_hour, a.hour, LV_ANIM_OFF);
            lv_roller_set_selected(s_popup_roller_min, a.minute, LV_ANIM_OFF);
        }
    } else {
        lv_roller_set_selected(s_popup_roller_hour, 7, LV_ANIM_OFF);
        lv_roller_set_selected(s_popup_roller_min, 30, LV_ANIM_OFF);
    }
    
    // Przyciski akcji na dole (y = 305)
    if (s_popup_alarm_idx == -1) {
        // Tryb Nowy alarm: ANULUJ, ZATWIERDŹ
        lv_obj_t *btn_cancel = lv_button_create(box);
        lv_obj_set_pos(btn_cancel, 35, 305);
        lv_obj_set_size(btn_cancel, 170, 45);
        lv_obj_set_style_radius(btn_cancel, 22, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_obj_set_style_border_color(btn_cancel, lv_color_hex(0x312e38), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_cancel, 1, LV_PART_MAIN);
        
        lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
        lv_obj_center(lbl_cancel);
        lv_obj_set_style_text_font(lbl_cancel, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_label_set_text_static(lbl_cancel, "ANULUJ");
        lv_obj_add_event_cb(btn_cancel, on_cancel_click, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *btn_ok = lv_button_create(box);
        lv_obj_set_pos(btn_ok, 245, 305);
        lv_obj_set_size(btn_ok, 170, 45);
        lv_obj_set_style_radius(btn_ok, 22, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xffffff), LV_PART_MAIN);
        
        lv_obj_t *lbl_ok = lv_label_create(btn_ok);
        lv_obj_center(lbl_ok);
        lv_obj_set_style_text_font(lbl_ok, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_ok, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_label_set_text_static(lbl_ok, "ZATWIERDŹ");
        lv_obj_add_event_cb(btn_ok, on_save_click, LV_EVENT_CLICKED, NULL);
    } else {
        // Tryb Edycja: USUŃ (czerwony), ANULUJ (ciemny), ZATWIERDŹ (jasny)
        lv_obj_t *btn_delete = lv_button_create(box);
        lv_obj_set_pos(btn_delete, 25, 305);
        lv_obj_set_size(btn_delete, 110, 45);
        lv_obj_set_style_radius(btn_delete, 22, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn_delete, lv_color_hex(0xff3b30), LV_PART_MAIN);
        
        lv_obj_t *lbl_delete = lv_label_create(btn_delete);
        lv_obj_center(lbl_delete);
        lv_obj_set_style_text_font(lbl_delete, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_delete, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_label_set_text_static(lbl_delete, "USUŃ");
        lv_obj_add_event_cb(btn_delete, on_delete_click, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *btn_cancel = lv_button_create(box);
        lv_obj_set_pos(btn_cancel, 150, 305);
        lv_obj_set_size(btn_cancel, 120, 45);
        lv_obj_set_style_radius(btn_cancel, 22, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_obj_set_style_border_color(btn_cancel, lv_color_hex(0x312e38), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_cancel, 1, LV_PART_MAIN);
        
        lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
        lv_obj_center(lbl_cancel);
        lv_obj_set_style_text_font(lbl_cancel, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_cancel, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_label_set_text_static(lbl_cancel, "ANULUJ");
        lv_obj_add_event_cb(btn_cancel, on_cancel_click, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *btn_ok = lv_button_create(box);
        lv_obj_set_pos(btn_ok, 285, 305);
        lv_obj_set_size(btn_ok, 140, 45);
        lv_obj_set_style_radius(btn_ok, 22, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xffffff), LV_PART_MAIN);
        
        lv_obj_t *lbl_ok = lv_label_create(btn_ok);
        lv_obj_center(lbl_ok);
        lv_obj_set_style_text_font(lbl_ok, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_ok, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_label_set_text_static(lbl_ok, "ZATWIERDŹ");
        lv_obj_add_event_cb(btn_ok, on_save_click, LV_EVENT_CLICKED, NULL);
    }
}

static void alarm_ui_close_popup(void) {
    if (s_popup_overlay) {
        lv_obj_del(s_popup_overlay);
        s_popup_overlay = NULL;
    }
    // Wyzeruj wszystkie wiszące pointery — chroni przed double-tap i eventami po close
    s_popup_roller_hour = NULL;
    s_popup_roller_min = NULL;
    memset(s_popup_day_btns, 0, sizeof(s_popup_day_btns));
}

static void on_cancel_click(lv_event_t *e) {
    (void)e;
    alarm_ui_close_popup();
}

static void on_save_click(lv_event_t *e) {
    (void)e;
    if (s_edit_locked) {
        return;
    }
    if (!s_popup_roller_hour || !s_popup_roller_min) return;
    
    int hour = lv_roller_get_selected(s_popup_roller_hour);
    int min = lv_roller_get_selected(s_popup_roller_min);
    
    if (s_popup_alarm_idx == -1) {
        if (alarm_add(hour, min, s_popup_days)) {
            publish_alarm_list_to_clock();
        }
    } else {
        if (alarm_edit(s_popup_alarm_idx, hour, min, s_popup_days)) {
            publish_alarm_list_to_clock();
        }
    }
    
    alarm_ui_close_popup();
    alarm_ui_refresh_list();
}

static void on_delete_click(lv_event_t *e) {
    (void)e;
    if (s_edit_locked) {
        return;
    }
    if (s_popup_alarm_idx >= 0) {
        if (alarm_delete(s_popup_alarm_idx)) {
            publish_alarm_list_to_clock();
        }
    }
    
    alarm_ui_close_popup();
    
    alarm_ui_refresh_list();
}

static void on_card_click(lv_event_t *e) {
    if (s_edit_locked) return;
    int idx = (int)(uintptr_t)lv_event_get_user_data(e);
    alarm_ui_open_popup(idx);
}

static void on_add_alarm_click(lv_event_t *e) {
    (void)e;
    if (s_edit_locked) return;
    if (alarm_get_count() >= ALARM_MAX_COUNT) {
        return;
    }
    alarm_ui_open_popup(-1);
}

void alarm_ui_refresh_list(void) {
    if (!objects.alarm_list) return;
    
    // Czyszczenie listy
    lv_obj_clean(objects.alarm_list);
    
    int count = alarm_get_count();
    for (int i = 0; i < count; i++) {
        alarm_t a;
        if (!alarm_get(i, &a)) continue;
        
        // Karta budzika (maksymalnie dopasowana do w = 176, wycentrowana)
        lv_obj_t *card = lv_obj_create(objects.alarm_list);
        lv_obj_set_size(card, 160, 60);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(card, 6, LV_PART_MAIN);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_hex(0x312e38), LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
        
        // Zdarzenie kliknięcia na całą kartę
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, on_card_click, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
        
        // Etykieta godziny (Bebas Neue 30px)
        char time_buf[16];
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", a.hour, a.minute);
        lv_obj_t *lbl_time = lv_label_create(card);
        lv_obj_set_pos(lbl_time, 5, 2);
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_time, &ui_font_bebas_neue30, LV_PART_MAIN);
        lv_label_set_text(lbl_time, time_buf);
        
        // Przełącznik stanu (Switch) bezpośrednio na karcie
        lv_obj_t *sw = lv_switch_create(card);
        lv_obj_set_size(sw, 40, 20);
        lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, -5, 2);
        
        // Stylizacja Switcha (kolorystyka premium z ekranu Muzyka)
        // 1. Tło switcha wyłączonego (MAIN)
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x28252c), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
        
        // 2. Tło switcha włączonego (INDICATOR w stanie CHECKED)
        lv_obj_set_style_bg_color(sw, lv_color_hex(0x85808a), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
        
        // 3. Suwak / Gałka (KNOB)
        lv_obj_set_style_bg_color(sw, lv_color_hex(0xffffff), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_border_color(sw, lv_color_hex(0x7a7580), LV_PART_KNOB);
        lv_obj_set_style_border_width(sw, 1, LV_PART_KNOB);
        lv_obj_set_style_pad_all(sw, -1, LV_PART_KNOB);

        if (a.enabled) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(sw, on_switch_toggle, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
        
        // Etykieta dni
        lv_obj_t *lbl_days = lv_label_create(card);
        lv_obj_set_pos(lbl_days, 5, 34);
        lv_obj_set_style_text_color(lbl_days, lv_color_hex(0x8d8896), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_days, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        
        char days_buf[64] = "";
        int active_days_count = 0;
        for (int d = 0; d < 7; d++) {
            if (a.days[d]) active_days_count++;
        }
        if (active_days_count == 7) {
            strcpy(days_buf, "Codziennie");
        } else if (active_days_count == 5 && a.days[0] && a.days[1] && a.days[2] && a.days[3] && a.days[4] && !a.days[5] && !a.days[6]) {
            strcpy(days_buf, "Dni rob.");
        } else if (active_days_count == 2 && a.days[5] && a.days[6]) {
            strcpy(days_buf, "Weekendy");
        } else if (active_days_count == 0) {
            strcpy(days_buf, "Nigdy");
        } else {
            const char* day_short[7] = {"Pn", "Wt", "Śr", "Cz", "Pt", "So", "Nd"};
            bool first = true;
            for (int d = 0; d < 7; d++) {
                if (a.days[d]) {
                    if (!first) strcat(days_buf, " ");
                    strcat(days_buf, day_short[d]);
                    first = false;
                }
            }
        }
        lv_label_set_text(lbl_days, days_buf);
    }
}

void alarm_ui_set_edit_locked(bool locked) {
    s_edit_locked = locked;
    // Wołane tylko z updateUi() (LVGL task, Core 1) — nie trzeba lvgl_port_lock

    if (objects.add_alarm) {
        if (locked) {
            lv_obj_add_state(objects.add_alarm, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(objects.add_alarm, LV_STATE_DISABLED);
        }
    }
    if (objects.alarm_list) {
        lv_obj_set_style_opa(objects.alarm_list, locked ? LV_OPA_40 : LV_OPA_COVER, LV_PART_MAIN);
    }
}

void alarm_ui_init(void) {
    // Inicjalizacja logiki budzika
    alarm_logic_init();
    
    // Formatowanie kontenera alarm_list, aby wyeliminować błędy przewijania i marginesów
    if (objects.alarm_list) {
        lv_obj_set_scroll_dir(objects.alarm_list, LV_DIR_VER); // Blokada przewijania w poziomie
        lv_obj_set_scrollbar_mode(objects.alarm_list, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_pad_all(objects.alarm_list, 0, LV_PART_MAIN); // Usunięcie domyślnego paddingu
        lv_obj_set_style_pad_row(objects.alarm_list, 8, LV_PART_MAIN); // Elegancka przerwa między kartami (8px)
        lv_obj_set_flex_align(objects.alarm_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); // Centrowanie kart w poziomie
        lv_obj_set_style_bg_opa(objects.alarm_list, LV_OPA_TRANSP, LV_PART_MAIN); // Wtopienie kontenera w tło (100% przezroczystości)
        lv_obj_set_style_border_width(objects.alarm_list, 0, LV_PART_MAIN); // Usunięcie ramki kontenera listy
        lv_obj_set_style_shadow_width(objects.alarm_list, 0, LV_PART_MAIN); // Usunięcie ewentualnych cieni
    }
    
    // Nadpisanie callbacku EEZ Studio dla przycisku add_alarm
    if (objects.add_alarm) {
        lv_obj_remove_event_cb(objects.add_alarm, NULL);
        lv_obj_add_event_cb(objects.add_alarm, on_add_alarm_click, LV_EVENT_CLICKED, NULL);
    }
    
    // Pierwsze załadowanie listy
    alarm_ui_refresh_list();
}
