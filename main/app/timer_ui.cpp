#include "timer_ui.h"
#include "timer_logic.h"
#include "screens.h"
#include "fonts.h"
#include "eez-flow.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

static lv_timer_t *s_timer = NULL;

static lv_obj_t *s_roller_h = NULL;
static lv_obj_t *s_roller_m = NULL;
static lv_obj_t *s_roller_s = NULL;
static lv_obj_t *s_colon1 = NULL;
static lv_obj_t *s_colon2 = NULL;
static lv_obj_t *s_countdown_label = NULL;
static lv_obj_t *s_start_stop_label = NULL;

static const char *s_roller_options_60 = 
    "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
    "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
    "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
    "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"
    "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"
    "50\n51\n52\n53\n54\n55\n56\n57\n58\n59";

static const char *s_roller_options_24 = 
    "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
    "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
    "20\n21\n22\n23";

void timer_ui_sync_start_stop_label(void) {
    if (eez_flow_get_current_screen() != SCREEN_ID_ZEGAR) {
        return;
    }

    if (!s_start_stop_label) {
        s_start_stop_label = objects.start_stop_label_minutnik;
    }
    if (!s_start_stop_label) {
        return;
    }

    const char *expected_text = timer_get_state() == TIMER_IDLE ? "START" : "ZERUJ";
    const char *current_text = lv_label_get_text(s_start_stop_label);
    if (strcmp(current_text, expected_text) != 0) {
        lv_label_set_text(s_start_stop_label, expected_text);
    }
}

static void timer_ui_cb(lv_timer_t *timer) {
    (void)timer;
    if (eez_flow_get_current_screen() != SCREEN_ID_ZEGAR) return;
    if (!s_roller_h || !s_roller_m || !s_roller_s || !s_countdown_label) return;

    timer_tick(); // czysta funkcja: przejście RUNNING→IDLE tylko tutaj, nie w getterze
    timer_state_t state = timer_get_state();
    int total_sec = timer_get_remaining_seconds();
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;

    if (state == TIMER_IDLE) {
        // Pokazujemy rollery i separatory
        if (lv_obj_has_flag(s_roller_h, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(s_roller_h, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_roller_m, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_roller_s, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_colon1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_colon2, LV_OBJ_FLAG_HIDDEN);
        }
        // Ukrywamy label odliczania
        if (!lv_obj_has_flag(s_countdown_label, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(s_countdown_label, LV_OBJ_FLAG_HIDDEN);
        }

        // Synchronizujemy wybrane wartości rollerów z czasem w logice
        if ((int)lv_roller_get_selected(s_roller_h) != h) {
            lv_roller_set_selected(s_roller_h, h, LV_ANIM_OFF);
        }
        if ((int)lv_roller_get_selected(s_roller_m) != m) {
            lv_roller_set_selected(s_roller_m, m, LV_ANIM_OFF);
        }
        if ((int)lv_roller_get_selected(s_roller_s) != s) {
            lv_roller_set_selected(s_roller_s, s, LV_ANIM_OFF);
        }
    } else {
        // TIMER_RUNNING lub TIMER_PAUSED
        // Ukrywamy rollery i separatory
        if (!lv_obj_has_flag(s_roller_h, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(s_roller_h, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_roller_m, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_roller_s, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_colon1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_colon2, LV_OBJ_FLAG_HIDDEN);
        }
        // Pokazujemy label odliczania
        if (lv_obj_has_flag(s_countdown_label, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(s_countdown_label, LV_OBJ_FLAG_HIDDEN);
        }

        // Aktualizujemy etykietę odliczania w formacie GG:MM:SS
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
        lv_label_set_text(s_countdown_label, buf);
    }

    timer_ui_sync_start_stop_label();
}

static void on_start_stop_minutnik(lv_event_t *e) {
    (void)e;
    if (timer_get_state() == TIMER_IDLE) {
        timer_start();
    } else {
        timer_reset();
    }
    timer_ui_sync_start_stop_label();
    timer_ui_cb(NULL);
}

static void on_roller_value_changed(lv_event_t *e) {
    (void)e;
    if (timer_get_state() != TIMER_IDLE) return;
    if (!s_roller_h || !s_roller_m || !s_roller_s) return;

    int h = lv_roller_get_selected(s_roller_h);
    int m = lv_roller_get_selected(s_roller_m);
    int s = lv_roller_get_selected(s_roller_s);

    timer_set_time(h, m, s);
}

void timer_ui_init(void) {
    // 1. Programowe ukrycie starej imitacji minutnika z EEZ Studio (do usunięcia z GUI w przyszłości)
    if (objects.plus_hour_minutnik) lv_obj_add_flag(objects.plus_hour_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.plus_minutes_minutnik) lv_obj_add_flag(objects.plus_minutes_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.plus_seconds_minutnik) lv_obj_add_flag(objects.plus_seconds_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.minus_hour_minutnik) lv_obj_add_flag(objects.minus_hour_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.minus_minutes_minutnik) lv_obj_add_flag(objects.minus_minutes_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.minus_seconds_minutnik) lv_obj_add_flag(objects.minus_seconds_minutnik, LV_OBJ_FLAG_HIDDEN);

    if (objects.before_hour_minutnik) lv_obj_add_flag(objects.before_hour_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.before_minutes_minutnik) lv_obj_add_flag(objects.before_minutes_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.before_seconds_minutnik) lv_obj_add_flag(objects.before_seconds_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.pick_hour_minutnik) lv_obj_add_flag(objects.pick_hour_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.pick_minutes_minutnik) lv_obj_add_flag(objects.pick_minutes_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.pick_seconds_minutnik) lv_obj_add_flag(objects.pick_seconds_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.after_hour_minutnik) lv_obj_add_flag(objects.after_hour_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.after_minutes_minutnik) lv_obj_add_flag(objects.after_minutes_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.after_seconds_minutnik) lv_obj_add_flag(objects.after_seconds_minutnik, LV_OBJ_FLAG_HIDDEN);

    // Zabezpieczenie przed ponowną inicjalizacją (SRAM leak guard)
    if (s_roller_h != NULL) {
        timer_ui_sync_start_stop_label();
        return;
    }

    lv_obj_t *parent = objects.minutnik_container;
    if (!parent) return;

    // Tworzenie rollera godzin
    s_roller_h = lv_roller_create(parent);
    lv_roller_set_options(s_roller_h, s_roller_options_24, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_h, 3);
    lv_obj_set_size(s_roller_h, 44, 130);
    lv_obj_set_pos(s_roller_h, 4, 21);
    
    // Pierwszy separator ":"
    s_colon1 = lv_label_create(parent);
    lv_label_set_text(s_colon1, ":");
    lv_obj_set_style_text_font(s_colon1, &ui_font_bebas_neue30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_colon1, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_colon1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(s_colon1, 14, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_colon1, 51, 70);

    // Tworzenie rollera minut
    s_roller_m = lv_roller_create(parent);
    lv_roller_set_options(s_roller_m, s_roller_options_60, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_m, 3);
    lv_obj_set_size(s_roller_m, 44, 130);
    lv_obj_set_pos(s_roller_m, 68, 21);

    // Drugi separator ":"
    s_colon2 = lv_label_create(parent);
    lv_label_set_text(s_colon2, ":");
    lv_obj_set_style_text_font(s_colon2, &ui_font_bebas_neue30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_colon2, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_colon2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(s_colon2, 14, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_colon2, 115, 70);

    // Tworzenie rollera sekund
    s_roller_s = lv_roller_create(parent);
    lv_roller_set_options(s_roller_s, s_roller_options_60, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_s, 3);
    lv_obj_set_size(s_roller_s, 44, 130);
    lv_obj_set_pos(s_roller_s, 132, 21);

    // Stylizacja rollerów
    lv_obj_t *rollers[] = { s_roller_h, s_roller_m, s_roller_s };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *r = rollers[i];
        
        // Część główna (nieaktywne wiersze)
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
        lv_obj_set_style_text_font(r, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(r, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        
        // Wyzerowanie dopełnień (paddingów) w celu wyeliminowania obcinania bocznego cyfr
        lv_obj_set_style_pad_left(r, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_right(r, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_top(r, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(r, 0, LV_PART_MAIN);

        // Część zaznaczona (środkowy wiersz)
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_SELECTED);
        lv_obj_set_style_border_width(r, 0, LV_PART_SELECTED);
        lv_obj_set_style_text_font(r, &ui_font_bebas_neue30, LV_PART_SELECTED);
        lv_obj_set_style_text_color(r, lv_color_hex(0xffffff), LV_PART_SELECTED);
        
        // Wyzerowanie dopełnień w części zaznaczonej
        lv_obj_set_style_pad_left(r, 0, LV_PART_SELECTED);
        lv_obj_set_style_pad_right(r, 0, LV_PART_SELECTED);
        lv_obj_set_style_pad_top(r, 0, LV_PART_SELECTED);
        lv_obj_set_style_pad_bottom(r, 0, LV_PART_SELECTED);

        // Podpięcie callbacku przewijania
        lv_obj_add_event_cb(r, on_roller_value_changed, LV_EVENT_VALUE_CHANGED, NULL);
    }

    // Tworzenie dużej etykiety odliczania
    s_countdown_label = lv_label_create(parent);
    lv_obj_set_size(s_countdown_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_countdown_label, &ui_font_bebas_neue35, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_countdown_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_countdown_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_countdown_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_countdown_label, LV_OBJ_FLAG_HIDDEN); // ukryte w trybie IDLE

    s_start_stop_label = objects.start_stop_label_minutnik;
    timer_ui_sync_start_stop_label();

    if (objects.start_stop_minutnik_button) {
        // Czyszczenie starych zdarzeń z EEZ Studio
        uint32_t event_cnt = lv_obj_get_event_count(objects.start_stop_minutnik_button);
        for (uint32_t i = event_cnt; i > 0; i--) {
            lv_obj_remove_event(objects.start_stop_minutnik_button, i - 1);
        }
        // Rejestrujemy bezpośrednie powiązanie z naszą logiką
        lv_obj_add_event_cb(objects.start_stop_minutnik_button, on_start_stop_minutnik, LV_EVENT_PRESSED, NULL);
    }

    s_timer = lv_timer_create(timer_ui_cb, 100, NULL);
}

// EEZ Flow Actions overrides
extern "C" {

void action_action_plus_hour_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_hours(1);
    timer_ui_cb(NULL);
}

void action_action_plus_minutes_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_minutes(1);
    timer_ui_cb(NULL);
}

void action_action_plus_seconds_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_seconds(1);
    timer_ui_cb(NULL);
}

void action_action_minus_hour_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_hours(-1);
    timer_ui_cb(NULL);
}

void action_action_minus_minutes_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_minutes(-1);
    timer_ui_cb(NULL);
}

void action_action_minus_seconds_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_seconds(-1);
    timer_ui_cb(NULL);
}

void action_action_toggle_start_stop_minutnik(lv_event_t * e) {
    (void)e;
    if (timer_get_state() == TIMER_IDLE) {
        timer_start();
    } else {
        timer_reset();
    }
    timer_ui_sync_start_stop_label();
    timer_ui_cb(NULL);
}

}
