#include "project_info_ui_handler.h"
#include "screens.h"
#include "fonts.h"
#include "lvgl.h"
#include "eez-flow.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Design tokens shared with the music screen (dark premium cards).
#define PI_PANEL_BG     0x18161b
#define PI_PANEL_BORDER 0x7a7580
#define PI_BADGE_BG     0x1e1b22
#define PI_BADGE_BORDER 0x4a4750
#define PI_TXT_MAIN     0xffffff
#define PI_TXT_SUB      0xb0acb5
#define PI_TXT_MUTED    0x85808a

#define PI_FW_VERSION "V3.6"

// Hero geometry: same outer bounds as the music panels (x 27..773, below header).
#define PI_HERO_X 27
#define PI_HERO_Y 55
#define PI_HERO_W 746
#define PI_HERO_H 395

static lv_timer_t *s_timer = NULL;
static lv_obj_t *s_hero = NULL;
static lv_obj_t *s_footer_sys = NULL;
static lv_obj_t *s_footer_uptime = NULL;
static bool s_entered = false;

// Persistent buffers: bypass the 64B safe_label.h limit via set_text_static.
static char s_sys_buf[96];
static char s_uptime_buf[96];
static uint32_t s_last_uptime_sec = UINT32_MAX;

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w,
                             const lv_font_t *font, uint32_t color,
                             int letter_space, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    if (letter_space > 0) {
        lv_obj_set_style_text_letter_space(label, letter_space, LV_PART_MAIN);
    }
    lv_label_set_text_static(label, text);
    return label;
}

static lv_obj_t *make_line(lv_obj_t *parent, int x, int y, int w) {
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, 1);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(line, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(line, lv_color_hex(PI_PANEL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(line, 0, LV_PART_MAIN);
    return line;
}

static void build_hero(void) {
    s_hero = lv_obj_create(objects.project_info);
    lv_obj_set_pos(s_hero, PI_HERO_X, PI_HERO_Y);
    lv_obj_set_size(s_hero, PI_HERO_W, PI_HERO_H);
    lv_obj_remove_flag(s_hero, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_hero, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_hero, lv_color_hex(PI_PANEL_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_hero, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_hero, lv_color_hex(PI_PANEL_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_hero, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_hero, 12, LV_PART_MAIN);

    make_label(s_hero, 23, 22, 700, &ui_font_montserrat_regular_14,
               PI_TXT_MUTED, 2, "P R O J E K T");

    // Bebas 56 carries the display title. Manual inner spaces give the premium
    // tracking (LVGL letter-space would double it and overflow the card).
    // Bullet U+2022 lives only in Bebas_Neue56 -- keep this font for the title.
    make_label(s_hero, 23, 48, 700, &ui_font_bebas_neue56,
               PI_TXT_MAIN, 0, "T I C K I N G \u2022 B O M B");

    make_line(s_hero, (PI_HERO_W - 460) / 2, 124, 460);

    make_label(s_hero, 23, 140, 700, &ui_font_montserrat_regular_16,
               PI_TXT_MAIN, 0, "Remigiusz Wozniak  |  Dominik Melcer");

    lv_obj_t *badge = lv_obj_create(s_hero);
    lv_obj_set_pos(badge, (PI_HERO_W - 120) / 2, 172);
    lv_obj_set_size(badge, 120, 32);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(badge, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(badge, lv_color_hex(PI_BADGE_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(badge, lv_color_hex(PI_BADGE_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(badge, 6, LV_PART_MAIN);

    lv_obj_t *badge_label = lv_label_create(badge);
    lv_obj_align(badge_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(badge_label, lv_color_hex(PI_TXT_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(badge_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(badge_label, "KLASA 5cT");

    make_label(s_hero, 53, 224, 640, &ui_font_montserrat_regular_14,
               PI_TXT_SUB, 0, "Zwykly zegar z bajerami.");

    make_line(s_hero, (PI_HERO_W - 200) / 2, 280, 200);

    // Quiet footer in small monospace Consolas: single-space pipes keep it
    // compact, fixed-width digits keep the centered uptime line from jumping.
    snprintf(s_sys_buf, sizeof(s_sys_buf), "FW %s | LVGL %d.%d | ESP32-S3 800x480",
             PI_FW_VERSION, LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR);
    s_footer_sys = make_label(s_hero, 23, 300, 700, &ui_font_consolai_12,
                              PI_TXT_MUTED, 0, s_sys_buf);

    // Project website shares the second footer line, highlighted in sub color.
    s_footer_uptime = make_label(s_hero, 23, 320, 700, &ui_font_consolai_12,
                                 PI_TXT_SUB, 0, "");
    s_last_uptime_sec = UINT32_MAX;
}

static void update_uptime(void) {
    if (!s_footer_uptime) {
        return;
    }
    uint32_t sec = (uint32_t)(esp_timer_get_time() / 1000000LL);
    if (sec == s_last_uptime_sec) {
        return;
    }
    s_last_uptime_sec = sec;
    snprintf(s_uptime_buf, sizeof(s_uptime_buf),
             "UPTIME %02u:%02u:%02u | strona internetowa: https://iot-esp32-zegar.web.app/",
             (unsigned)((sec / 3600) % 100), (unsigned)((sec / 60) % 60),
             (unsigned)(sec % 60));
    lv_label_set_text_static(s_footer_uptime, s_uptime_buf);
}

static void project_info_timer_cb(lv_timer_t *timer) {
    (void)timer;

    if (eez_flow_get_current_screen() != SCREEN_ID_PROJECT_INFO || !objects.project_info) {
        s_entered = false;
        if (!objects.project_info) {
            // Screen was deleted: drop dangling handles so the next visit rebuilds.
            s_hero = NULL;
            s_footer_sys = NULL;
            s_footer_uptime = NULL;
            s_last_uptime_sec = UINT32_MAX;
        }
        return;
    }

    if (!s_entered || !s_hero ||
        !lv_obj_is_valid(s_hero) || lv_obj_get_parent(s_hero) != objects.project_info) {
        s_hero = NULL;
        s_footer_sys = NULL;
        s_footer_uptime = NULL;
        build_hero();
        s_entered = true;
    }

    update_uptime();
}

void project_info_ui_handler_init(void) {
    if (s_timer) {
        return;
    }
    // 500 ms is plenty: the card is static except for the 1 s uptime footer.
    s_timer = lv_timer_create(project_info_timer_cb, 500, NULL);
}
