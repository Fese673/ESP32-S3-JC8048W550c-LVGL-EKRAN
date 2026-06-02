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

    // Parent to top layer so it floats over any other active screen
    s_popup_container = lv_obj_create(lv_layer_top());
    
    // Set size and place off-screen initially
    lv_obj_set_size(s_popup_container, 360, 80);
    lv_obj_set_pos(s_popup_container, 220, -95);
    
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

    // Left Column: Orange-gold bell icon & time
    s_bell_icon = lv_img_create(s_popup_container);
    lv_img_set_src(s_bell_icon, &img_bell_on_icon);
    lv_img_set_zoom(s_bell_icon, 150); // Scale down the 48x48 icon to 150 (~28x28 px rendered size)
    lv_obj_set_pos(s_bell_icon, 10, 6); // Mathematically centered inside 68px wide column: (68 - 48) / 2 = 10
    lv_obj_set_style_img_recolor(s_bell_icon, lv_color_hex(0xffaa00), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(s_bell_icon, LV_OPA_COVER, LV_PART_MAIN);

    s_time_label = lv_label_create(s_popup_container);
    lv_obj_set_pos(s_time_label, 9, 46); // Mathematically centered inside 68px wide column: (68 - 50) / 2 = 9
    lv_obj_set_size(s_time_label, 50, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_time_label, &ui_font_consola_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xffaa00), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_time_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text_static(s_time_label, s_static_time_buf);

    // Separator 1
    lv_obj_t *sep1 = lv_obj_create(s_popup_container);
    lv_obj_set_pos(sep1, 68, 15);
    lv_obj_set_size(sep1, 1, 50);
    lv_obj_set_style_bg_color(sep1, lv_color_hex(0x3a3542), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep1, 0, LV_PART_MAIN);

    // Center Message: circular long scrolling
    s_msg_label = lv_label_create(s_popup_container);
    lv_obj_set_pos(s_msg_label, 78, 30);
    lv_obj_set_size(s_msg_label, 168, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_msg_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_msg_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_label_set_long_mode(s_msg_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(s_msg_label, 8000, LV_PART_MAIN);
    lv_label_set_text_static(s_msg_label, s_static_msg_buf);

    // Separator 2
    lv_obj_t *sep2 = lv_obj_create(s_popup_container);
    lv_obj_set_pos(sep2, 256, 15);
    lv_obj_set_size(sep2, 1, 50);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(0x3a3542), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep2, 0, LV_PART_MAIN);

    // Right Column: Snooze and Dismiss action buttons
    s_snooze_btn = lv_button_create(s_popup_container);
    lv_obj_set_pos(s_snooze_btn, 268, 10);
    lv_obj_set_size(s_snooze_btn, 78, 26);
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
    lv_obj_set_pos(s_dismiss_btn, 268, 44);
    lv_obj_set_size(s_dismiss_btn, 78, 26);
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
}

void alarm_popup_show(const char *time_str, const char *message_str) {
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
        alarm_popup_show(time_str, "WSTAWAJ SZKODA DNIA - MOCKUP ALARM TEST!");
    }

    // Dismiss alarm popup automatically after 15 seconds safety window
    if (s_popup_active && (now - s_show_time_sec >= 15)) {
        ESP_LOGI(TAG, "Mockup alarm timeout - automatically dismissing");
        alarm_popup_dismiss();
    }
#endif
}
