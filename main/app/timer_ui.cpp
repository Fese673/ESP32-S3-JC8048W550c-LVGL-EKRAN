#include "timer_ui.h"
#include "timer_logic.h"
#include "screens.h"
#include "fonts.h"
#include "eez-flow.h"
#include "lvgl.h"
#include <stdio.h>

static lv_timer_t *s_timer = NULL;

static lv_obj_t *s_b_h, *s_p_h, *s_a_h;
static lv_obj_t *s_b_m, *s_p_m, *s_a_m;
static lv_obj_t *s_b_s, *s_p_s, *s_a_s;
static lv_obj_t *s_start_stop_label = NULL;

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

static lv_obj_t* create_timer_label(lv_obj_t* parent, lv_coord_t x, lv_coord_t y) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, x, y);
    lv_obj_set_size(lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(lbl, &ui_font_bebas_neue35, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_label_set_text(lbl, "00");
    return lbl;
}

static void timer_ui_cb(lv_timer_t *timer) {
    (void)timer;
    if (eez_flow_get_current_screen() != SCREEN_ID_ZEGAR) return;
    if (!s_p_h) return;

    int total_sec = timer_get_remaining_seconds();
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;

    int bh = (h + 99) % 100;
    int ah = (h + 1) % 100;
    
    int bm = (m + 59) % 60;
    int am = (m + 1) % 60;

    int bs = (s + 59) % 60;
    int as = (s + 1) % 60;

    char buf[8];

    // Hours
    snprintf(buf, sizeof(buf), "%02d", bh);
    lv_label_set_text(s_b_h, buf);
    snprintf(buf, sizeof(buf), "%02d", h);
    lv_label_set_text(s_p_h, buf);
    snprintf(buf, sizeof(buf), "%02d", ah);
    lv_label_set_text(s_a_h, buf);

    // Minutes
    snprintf(buf, sizeof(buf), "%02d", bm);
    lv_label_set_text(s_b_m, buf);
    snprintf(buf, sizeof(buf), "%02d", m);
    lv_label_set_text(s_p_m, buf);
    snprintf(buf, sizeof(buf), "%02d", am);
    lv_label_set_text(s_a_m, buf);

    // Seconds
    snprintf(buf, sizeof(buf), "%02d", bs);
    lv_label_set_text(s_b_s, buf);
    snprintf(buf, sizeof(buf), "%02d", s);
    lv_label_set_text(s_p_s, buf);
    snprintf(buf, sizeof(buf), "%02d", as);
    lv_label_set_text(s_a_s, buf);

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

void timer_ui_init(void) {
    // Hide original labels
    if (objects.before_hour_minutnik) lv_obj_add_flag(objects.before_hour_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.before_minutes_minutnik) lv_obj_add_flag(objects.before_minutes_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.before_seconds_minutnik) lv_obj_add_flag(objects.before_seconds_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.pick_hour_minutnik) lv_obj_add_flag(objects.pick_hour_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.pick_minutes_minutnik) lv_obj_add_flag(objects.pick_minutes_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.pick_seconds_minutnik) lv_obj_add_flag(objects.pick_seconds_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.after_hour_minutnik) lv_obj_add_flag(objects.after_hour_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.after_minutes_minutnik) lv_obj_add_flag(objects.after_minutes_minutnik, LV_OBJ_FLAG_HIDDEN);
    if (objects.after_seconds_minutnik) lv_obj_add_flag(objects.after_seconds_minutnik, LV_OBJ_FLAG_HIDDEN);

    if (objects.pick_hour_minutnik) {
        lv_obj_t *parent = lv_obj_get_parent(objects.pick_hour_minutnik);

        s_b_h = create_timer_label(parent, 13, 0);
        s_b_m = create_timer_label(parent, 60, 0);
        s_b_s = create_timer_label(parent, 112, 0);

        s_p_h = create_timer_label(parent, 13, 41);
        s_p_m = create_timer_label(parent, 60, 40);
        s_p_s = create_timer_label(parent, 112, 39);

        s_a_h = create_timer_label(parent, 13, 78);
        s_a_m = create_timer_label(parent, 60, 77);
        s_a_s = create_timer_label(parent, 112, 77);
    }

    s_start_stop_label = objects.start_stop_label_minutnik;
    timer_ui_sync_start_stop_label();

    if (objects.start_stop_minutnik_button) {
        // Remove EEZ-generated events to prevent double-triggering
        uint32_t event_cnt = lv_obj_get_event_count(objects.start_stop_minutnik_button);
        for (uint32_t i = event_cnt; i > 0; i--) {
            lv_obj_remove_event(objects.start_stop_minutnik_button, i - 1);
        }

        // Register direct C++ callback
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
