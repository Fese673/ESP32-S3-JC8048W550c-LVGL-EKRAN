#include "alarm_popup.h"
#include "lvgl.h"
#include "fonts.h"
#include "images.h"
#include "screens.h"
#include "eez-flow.h"
#include "communication/time/timer_synchro.h"
#include "esp_log.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

#define TAG "AlarmPopup"

// Toggle mockup mode here: 1 = enabled, 0 = disabled (production)
#define ALARM_POPUP_MOCKUP_MODE 0

static lv_obj_t *s_popup_container = NULL;
static lv_obj_t *s_bell_icon = NULL;
static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_msg_label = NULL;
static lv_obj_t *s_snooze_btn = NULL;
static lv_obj_t *s_dismiss_btn = NULL;

static char s_static_time_buf[16] = "07:00";
static char s_static_msg_buf[128] = "WSTAWAJ SZKODA DNIA";

static bool s_popup_active = false;
static uint32_t s_show_time_sec = 0;

static void set_y_anim_cb(void *var, int32_t v) {
    lv_obj_set_y((lv_obj_t *)var, v);
}

static void set_opa_anim_cb(void *var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, LV_PART_MAIN);
}

static void anim_dismiss_completed_cb(lv_anim_t *a) {
    lv_obj_t *obj = (lv_obj_t *)a->var;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    s_popup_active = false;
}

static void on_snooze_click(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "Snooze button clicked");
    CommunicationProtocol::TimerSync::sendAlarmAction(1); // SNOOZE
    alarm_popup_dismiss();
}

static void on_dismiss_click(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "Dismiss button clicked");
    CommunicationProtocol::TimerSync::sendAlarmAction(0); // DISMISS
    alarm_popup_dismiss();
}

void alarm_popup_init(void) {
    if (s_popup_container != NULL) return; // Already initialized

    // Popup layout constants
    #define POPUP_W 360
    #define POPUP_H 80
    #define LEFT_COL_W 68
    #define SEP_X1 68
    #define SEP_X2 256
    #define CENTER_X 78
    #define CENTER_W (SEP_X2 - CENTER_X)
    #define RIGHT_COL_X 268
    #define RIGHT_COL_W (POPUP_W - RIGHT_COL_X)
    #define BTN_W 78
    #define BTN_H 26
    #define BTN_X (RIGHT_COL_X + (RIGHT_COL_W - BTN_W) / 2)

    // Parent to top layer so it floats over any other active screen
    s_popup_container = lv_obj_create(lv_layer_top());
    
    // Set size and center horizontally on 800px screen: (800 - 360) / 2 = 220
    lv_obj_set_size(s_popup_container, POPUP_W, POPUP_H);
    lv_obj_set_pos(s_popup_container, (800 - POPUP_W) / 2, -95);
    
    // Styling the parent container card (Charcoal-violet 0x1e1a24 matching BPM tiles, border 0x3a3542, radius 16)
    lv_obj_set_style_bg_color(s_popup_container, lv_color_hex(0x1e1a24), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_popup_container, LV_OPA_90, LV_PART_MAIN);
    
    lv_obj_set_style_border_color(s_popup_container, lv_color_hex(0x3a3542), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_popup_container, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_popup_container, 16, LV_PART_MAIN);
    
    // Clear default paddings and scrollability
    lv_obj_set_style_pad_all(s_popup_container, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_popup_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Drop shadow styling
    lv_obj_set_style_shadow_width(s_popup_container, 15, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(s_popup_container, 5, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_popup_container, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_popup_container, LV_OPA_50, LV_PART_MAIN);
    
    // Initially hidden
    lv_obj_add_flag(s_popup_container, LV_OBJ_FLAG_HIDDEN);

    // Left Column: Orange-gold bell icon (22x22) & time label
    // Bell icon: horizontally centered in 68px column → (68 - 22) / 2 = 23
    // Vertically centered in top half (above time at y=46) → (46 - 22) / 2 = 12
    s_bell_icon = lv_img_create(s_popup_container);
    lv_img_set_src(s_bell_icon, &img_bell_on_icon);
    lv_obj_set_pos(s_bell_icon, (LEFT_COL_W - 22) / 2, 12);

    // Time label: horizontally centered in 68px column, below bell icon
    s_time_label = lv_label_create(s_popup_container);
    lv_obj_set_pos(s_time_label, (LEFT_COL_W - 50) / 2, 46);
    lv_obj_set_size(s_time_label, 50, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_time_label, &ui_font_consola_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xffaa00), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_time_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text_static(s_time_label, s_static_time_buf);

    // Separator 1: vertically centered in 80px container
    lv_obj_t *sep1 = lv_obj_create(s_popup_container);
    lv_obj_set_pos(sep1, SEP_X1, (POPUP_H - 50) / 2);
    lv_obj_set_size(sep1, 1, 50);
    lv_obj_set_style_bg_color(sep1, lv_color_hex(0x3a3542), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep1, 0, LV_PART_MAIN);

    // Center Message: vertically centered, circular long scrolling
    s_msg_label = lv_label_create(s_popup_container);
    lv_obj_set_pos(s_msg_label, CENTER_X, (POPUP_H - 14) / 2);
    lv_obj_set_size(s_msg_label, CENTER_W, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_msg_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_msg_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_label_set_long_mode(s_msg_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(s_msg_label, 8000, LV_PART_MAIN);
    lv_label_set_text_static(s_msg_label, s_static_msg_buf);

    // Separator 2: vertically centered in 80px container
    lv_obj_t *sep2 = lv_obj_create(s_popup_container);
    lv_obj_set_pos(sep2, SEP_X2, (POPUP_H - 50) / 2);
    lv_obj_set_size(sep2, 1, 50);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(0x3a3542), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep2, 0, LV_PART_MAIN);

    // Right Column: Snooze and Dismiss action buttons
    // Both buttons stacked: total height = 26 + 8 + 26 = 60, centered in 80px → y_start = 10
    s_snooze_btn = lv_button_create(s_popup_container);
    lv_obj_set_pos(s_snooze_btn, BTN_X, 10);
    lv_obj_set_size(s_snooze_btn, BTN_W, BTN_H);
    lv_obj_set_style_radius(s_snooze_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_snooze_btn, lv_color_hex(0x342c3d), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_snooze_btn, lv_color_hex(0x4d425a), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_snooze_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_snooze_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_snooze_btn, 0, LV_PART_MAIN);

    lv_obj_t *lbl_snooze = lv_label_create(s_snooze_btn);
    lv_obj_center(lbl_snooze);
    lv_obj_set_style_text_font(lbl_snooze, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_snooze, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_label_set_text_static(lbl_snooze, "Drzemka");
    lv_obj_add_event_cb(s_snooze_btn, on_snooze_click, LV_EVENT_CLICKED, NULL);

    s_dismiss_btn = lv_button_create(s_popup_container);
    lv_obj_set_pos(s_dismiss_btn, BTN_X, 44);
    lv_obj_set_size(s_dismiss_btn, BTN_W, BTN_H);
    lv_obj_set_style_radius(s_dismiss_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_dismiss_btn, lv_color_hex(0x5c1a24), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_dismiss_btn, lv_color_hex(0x8c2a38), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_dismiss_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_dismiss_btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_dismiss_btn, 0, LV_PART_MAIN);

    lv_obj_t *lbl_dismiss = lv_label_create(s_dismiss_btn);
    lv_obj_center(lbl_dismiss);
    lv_obj_set_style_text_font(lbl_dismiss, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_dismiss, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_label_set_text_static(lbl_dismiss, "Wyłącz");
    lv_obj_add_event_cb(s_dismiss_btn, on_dismiss_click, LV_EVENT_CLICKED, NULL);

    #undef POPUP_W
    #undef POPUP_H
    #undef LEFT_COL_W
    #undef SEP_X1
    #undef SEP_X2
    #undef CENTER_X
    #undef CENTER_W
    #undef RIGHT_COL_X
    #undef RIGHT_COL_W
    #undef BTN_W
    #undef BTN_H
    #undef BTN_X
}

void alarm_popup_show(const char *time_str, const char *message_str, bool is_timer) {
    if (s_popup_container == NULL) return;

    // Do not show the popup if Flappy Bird is active to avoid lag/stutter
    if (eez_flow_get_current_screen() == SCREEN_ID_GRY_TEST) {
        ESP_LOGI(TAG, "Alarm triggered but ignored because game screen is active");
        return;
    }

    // Safely update text fields using statically allocated buffers
    if (time_str) {
        strncpy(s_static_time_buf, time_str, sizeof(s_static_time_buf) - 1);
        s_static_time_buf[sizeof(s_static_time_buf) - 1] = '\0';
        lv_label_set_text_static(s_time_label, s_static_time_buf);
    }
    if (message_str) {
        strncpy(s_static_msg_buf, message_str, sizeof(s_static_msg_buf) - 1);
        s_static_msg_buf[sizeof(s_static_msg_buf) - 1] = '\0';
        lv_label_set_text_static(s_msg_label, s_static_msg_buf);
    }

    // Timer (minutnik) has no snooze — hide snooze button and center dismiss alone
    // Alarm (budzik) shows both buttons stacked
    #define BTN_W 78
    #define BTN_H 26
    #define RIGHT_COL_X 268
    #define RIGHT_COL_W (360 - RIGHT_COL_X)
    #define BTN_X (RIGHT_COL_X + (RIGHT_COL_W - BTN_W) / 2)

    if (is_timer) {
        lv_obj_add_flag(s_snooze_btn, LV_OBJ_FLAG_HIDDEN);
        // Center dismiss button vertically in 80px container: (80 - 26) / 2 = 27
        lv_obj_set_pos(s_dismiss_btn, BTN_X, (80 - BTN_H) / 2);
    } else {
        lv_obj_clear_flag(s_snooze_btn, LV_OBJ_FLAG_HIDDEN);
        // Stack both buttons: total 26+8+26=60, centered → y_start=10
        lv_obj_set_pos(s_snooze_btn, BTN_X, 10);
        lv_obj_set_pos(s_dismiss_btn, BTN_X, 44);
    }

    #undef BTN_W
    #undef BTN_H
    #undef RIGHT_COL_X
    #undef RIGHT_COL_W
    #undef BTN_X

    // Enable visibility
    lv_obj_clear_flag(s_popup_container, LV_OBJ_FLAG_HIDDEN);
    
    // Set slide starting parameters
    lv_obj_set_y(s_popup_container, -95);
    lv_obj_set_style_opa(s_popup_container, LV_OPA_0, LV_PART_MAIN);

    // Cancel any ongoing animation before starting new ones
    lv_anim_delete(s_popup_container, set_y_anim_cb);
    lv_anim_delete(s_popup_container, set_opa_anim_cb);

    // Slide down animation
    lv_anim_t a_y;
    lv_anim_init(&a_y);
    lv_anim_set_var(&a_y, s_popup_container);
    lv_anim_set_time(&a_y, 350);
    lv_anim_set_exec_cb(&a_y, set_y_anim_cb);
    lv_anim_set_values(&a_y, -95, 15);
    lv_anim_set_path_cb(&a_y, lv_anim_path_ease_out);
    lv_anim_start(&a_y);

    // Fade in animation
    lv_anim_t a_opa;
    lv_anim_init(&a_opa);
    lv_anim_set_var(&a_opa, s_popup_container);
    lv_anim_set_time(&a_opa, 300);
    lv_anim_set_exec_cb(&a_opa, set_opa_anim_cb);
    lv_anim_set_values(&a_opa, 0, LV_OPA_90);
    lv_anim_set_path_cb(&a_opa, lv_anim_path_ease_out);
    lv_anim_start(&a_opa);

    s_popup_active = true;
    s_show_time_sec = (uint32_t)time(NULL);
}

void alarm_popup_dismiss(void) {
    if (s_popup_container == NULL || !s_popup_active) return;

    s_popup_active = false; // Set to false immediately to prevent re-triggering during the 300ms animation transition

    // Cancel any running transition animations
    lv_anim_delete(s_popup_container, set_y_anim_cb);
    lv_anim_delete(s_popup_container, set_opa_anim_cb);

    int32_t cur_y = lv_obj_get_y(s_popup_container);
    int32_t cur_opa = lv_obj_get_style_opa(s_popup_container, LV_PART_MAIN);

    // Slide up animation
    lv_anim_t a_y;
    lv_anim_init(&a_y);
    lv_anim_set_var(&a_y, s_popup_container);
    lv_anim_set_time(&a_y, 300);
    lv_anim_set_exec_cb(&a_y, set_y_anim_cb);
    lv_anim_set_values(&a_y, cur_y, -95);
    lv_anim_set_path_cb(&a_y, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a_y, anim_dismiss_completed_cb);
    lv_anim_start(&a_y);

    // Fade out animation
    lv_anim_t a_opa;
    lv_anim_init(&a_opa);
    lv_anim_set_var(&a_opa, s_popup_container);
    lv_anim_set_time(&a_opa, 250);
    lv_anim_set_exec_cb(&a_opa, set_opa_anim_cb);
    lv_anim_set_values(&a_opa, cur_opa, 0);
    lv_anim_set_path_cb(&a_opa, lv_anim_path_ease_in);
    lv_anim_start(&a_opa);
}

void alarm_popup_tick(void) {
    int16_t current_screen_id = eez_flow_get_current_screen();

    // If alarm is active but we enter the game screen, dismiss it immediately to prevent lag
    if (s_popup_active && current_screen_id == SCREEN_ID_GRY_TEST) {
        ESP_LOGI(TAG, "Entering game screen while alarm is active - dismissing alarm");
        alarm_popup_dismiss();
        return;
    }

#if ALARM_POPUP_MOCKUP_MODE
    uint32_t now = (uint32_t)time(NULL);
    static uint32_t s_last_trigger_sec = 0;

    // Trigger mockup alarm exactly on a new minute tick (:00 seconds)
    // Only trigger if we are NOT on the game screen
    if (current_screen_id != SCREEN_ID_GRY_TEST && now % 60 == 0 && now != s_last_trigger_sec) {
        s_last_trigger_sec = now;

        struct tm timeinfo;
        localtime_r((time_t*)&now, &timeinfo);
        char time_str[16];
        snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

        ESP_LOGI(TAG, "Mockup alarm auto-triggered: %s", time_str);
        alarm_popup_show(time_str, "WSTAWAJ SZKODA DNIA - MOCKUP ALARM TEST!", false);
    }

    // Dismiss alarm popup automatically after 15 seconds safety window
    if (s_popup_active && (now - s_show_time_sec >= 15)) {
        ESP_LOGI(TAG, "Mockup alarm timeout - automatically dismissing");
        alarm_popup_dismiss();
    }
#endif
}
