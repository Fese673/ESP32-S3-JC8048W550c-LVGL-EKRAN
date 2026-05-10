#include "stopwatch_ui.h"
#include "stopwatch_logic.h"
#include "screens.h"
#include "fonts.h"
#include "eez-flow.h"
#include "lvgl.h"
#include <stdio.h>

static lv_timer_t *s_timer = NULL;

static lv_obj_t *s_min_label = NULL;
static lv_obj_t *s_sec_label = NULL;
static lv_obj_t *s_cs_label = NULL;

static void on_start_stop(lv_event_t *e) {
    (void)e;
    if (stopwatch_get_state() == STOPWATCH_RUNNING) {
        stopwatch_stop();
    } else {
        stopwatch_start();
    }
}

static void on_reset(lv_event_t *e) {
    (void)e;
    stopwatch_reset();
}

static void stopwatch_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (eez_flow_get_current_screen() != SCREEN_ID_ZEGAR) return;
    if (!s_min_label) return;

    int64_t elapsed_us = stopwatch_get_elapsed_us();

    int64_t total_ms = elapsed_us / 1000;
    int minutes = (int)(total_ms / 60000);
    int seconds = (int)((total_ms / 1000) % 60);
    int centiseconds = (int)((total_ms % 1000) / 10);

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", minutes);
    lv_label_set_text(s_min_label, buf);

    snprintf(buf, sizeof(buf), "%02d", seconds);
    lv_label_set_text(s_sec_label, buf);

    snprintf(buf, sizeof(buf), "%02d", centiseconds);
    lv_label_set_text(s_cs_label, buf);
}

void stopwatch_ui_init(void) {
    lv_obj_add_flag(objects.minutes_stoper_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.seconds_stoper_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.ms_stoper_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *parent = lv_obj_get_parent(objects.minutes_stoper_label);

    s_min_label = lv_label_create(parent);
    lv_obj_set_pos(s_min_label, 9, 18);
    lv_obj_set_size(s_min_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(s_min_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_min_label, &ui_font_bebas_neue35, 0);
    lv_obj_set_style_text_letter_space(s_min_label, 2, 0);
    lv_label_set_text(s_min_label, "00");

    s_sec_label = lv_label_create(parent);
    lv_obj_set_pos(s_sec_label, 61, 18);
    lv_obj_set_size(s_sec_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(s_sec_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_sec_label, &ui_font_bebas_neue35, 0);
    lv_obj_set_style_text_letter_space(s_sec_label, 2, 0);
    lv_label_set_text(s_sec_label, "00");

    s_cs_label = lv_label_create(parent);
    lv_obj_set_pos(s_cs_label, 112, 17);
    lv_obj_set_size(s_cs_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(s_cs_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_cs_label, &ui_font_bebas_neue35, 0);
    lv_obj_set_style_text_letter_space(s_cs_label, 2, 0);
    lv_label_set_text(s_cs_label, "00");

    lv_obj_add_event_cb(objects.start_stop_stoper, on_start_stop, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(objects.reset_stoper, on_reset, LV_EVENT_PRESSED, NULL);

    s_timer = lv_timer_create(stopwatch_timer_cb, 20, NULL);
}
