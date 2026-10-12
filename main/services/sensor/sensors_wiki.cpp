#include "services/sensor/sensors_wiki.h"
#include "screens.h"
#include "fonts.h"
#include "images.h"
#include "eez-flow.h"
#include "communication/CommunicationProtocol.h"
#include "utils/gesture/gesture.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Private State
// ---------------------------------------------------------------------------
static lv_obj_t *s_parent_screen = NULL;

static uint8_t s_active_sensor = 0; // 0: PMS5003, 1: ENS160, 2: AHT21, 3: BMP280
static uint8_t s_active_mode   = 0; // 0: PARAMETERS, 1: THEORY

// Sidebar UI references
static lv_obj_t *s_sidebar_panel = NULL;
static lv_obj_t *s_sensor_btns[4] = {NULL, NULL, NULL, NULL};

// Content Panel UI references
static lv_obj_t *s_content_panel = NULL;
static lv_obj_t *s_mode_param_btn = NULL;
static lv_obj_t *s_mode_theory_btn = NULL;
static lv_obj_t *s_mode_indicator = NULL;

static lv_obj_t *s_workspace = NULL; // Inner workspace of the content panel

// Live Data Label References (for real-time ticks)
static lv_obj_t *s_lbl_val1 = NULL;
static lv_obj_t *s_lbl_val2 = NULL;
static lv_obj_t *s_lbl_val3 = NULL;

static lv_obj_t *s_lbl_trend1 = NULL;
static lv_obj_t *s_lbl_trend2 = NULL;
static lv_obj_t *s_lbl_trend3 = NULL;

// Spectrometer micro-labels (PMS5003 only)
static lv_obj_t *s_lbl_micro_vals[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
static lv_obj_t *s_lbl_micro_trends[6] = {NULL, NULL, NULL, NULL, NULL, NULL};

// Pre-allocated panel containers (created once, shown/hidden)
static lv_obj_t *s_sensor_panels[4] = {NULL, NULL, NULL, NULL};
static lv_obj_t *s_theory_panel = NULL;
static lv_obj_t *s_section_title = NULL;
static lv_obj_t *s_ens_stage_label = NULL; // etykieta stopnia kalibracji ENS160
static bool s_ui_initialized = false;

// Per-sensor label references (stable pointers, set once during panel creation)
static lv_obj_t *s_val_labels[4][3] = {{NULL}};
static lv_obj_t *s_trend_labels[4][3] = {{NULL}};
static lv_obj_t *s_micro_val_labels[6] = {NULL};
static lv_obj_t *s_micro_trend_labels[6] = {NULL};

// ---------------------------------------------------------------------------
// Historical Tracking for Trend Arrows (10 samples buffer per variable)
// ---------------------------------------------------------------------------
#define HIST_SIZE 10
struct VarHistory {
    float buffer[HIST_SIZE];
    uint8_t count;
    uint8_t index;
};

static VarHistory s_hist_pm10 = { {0}, 0, 0 };
static VarHistory s_hist_pm25 = { {0}, 0, 0 };
static VarHistory s_hist_pm1_0 = { {0}, 0, 0 };
static VarHistory s_hist_eco2 = { {0}, 0, 0 };
static VarHistory s_hist_tvoc = { {0}, 0, 0 };
static VarHistory s_hist_temp = { {0}, 0, 0 };
static VarHistory s_hist_hum  = { {0}, 0, 0 };
static VarHistory s_hist_pres = { {0}, 0, 0 };
static VarHistory s_hist_count0p3 = { {0}, 0, 0 };
static VarHistory s_hist_count0p5 = { {0}, 0, 0 };
static VarHistory s_hist_count1p0 = { {0}, 0, 0 };
static VarHistory s_hist_count2p5 = { {0}, 0, 0 };
static VarHistory s_hist_count5p0 = { {0}, 0, 0 };
static VarHistory s_hist_count10p0 = { {0}, 0, 0 };

static void push_history(VarHistory *h, float val) {
    h->buffer[h->index] = val;
    h->index = (h->index + 1) % HIST_SIZE;
    if (h->count < HIST_SIZE) h->count++;
}

static const char* calculate_trend(VarHistory *h, float current_val, float epsilon) {
    if (h->count < 3) return "=";
    float sum = 0.0f;
    for (uint8_t i = 0; i < h->count; i++) {
        sum += h->buffer[i];
    }
    float avg = sum / h->count;
    if (current_val > avg + epsilon) return "+";
    if (current_val < avg - epsilon) return "-";
    return "=";
}



// ---------------------------------------------------------------------------
// Forward declarations for view switches
// ---------------------------------------------------------------------------
static void render_sidebar(void);


// ---------------------------------------------------------------------------
// Forward declarations for view switches
// ---------------------------------------------------------------------------
static void render_sidebar(void);
static void render_right_panel(void);
static void update_active_highlight(void);

// ---------------------------------------------------------------------------
// Button Callbacks
// ---------------------------------------------------------------------------
static void on_sensor_btn_click(lv_event_t *e) {
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (s_active_sensor != index) {
        s_active_sensor = index;
        update_active_highlight();
        render_right_panel();
    }
}

static void on_mode_change_click(lv_event_t *e) {
    uint8_t mode = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (s_active_mode != mode) {
        s_active_mode = mode;
        
        // Update Pills highlight state
        if (s_active_mode == 0) {
            lv_obj_add_state(s_mode_param_btn, LV_STATE_CHECKED);
            lv_obj_clear_state(s_mode_theory_btn, LV_STATE_CHECKED);
            if (s_mode_indicator) {
                lv_obj_align(s_mode_indicator, LV_ALIGN_LEFT_MID, 2, 0);
            }
        } else {
            lv_obj_clear_state(s_mode_param_btn, LV_STATE_CHECKED);
            lv_obj_add_state(s_mode_theory_btn, LV_STATE_CHECKED);
            if (s_mode_indicator) {
                lv_obj_align(s_mode_indicator, LV_ALIGN_RIGHT_MID, -2, 0);
            }
        }
        
        render_right_panel();
    }
}

// ---------------------------------------------------------------------------
// UI Builders
// ---------------------------------------------------------------------------
static void render_sidebar(void) {
    if (s_sidebar_panel) {
        update_active_highlight();
        return;
    }

    s_sidebar_panel = lv_obj_create(s_parent_screen);
    lv_obj_set_pos(s_sidebar_panel, 15, 55);
    lv_obj_set_size(s_sidebar_panel, 200, 410);
    lv_obj_remove_flag(s_sidebar_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_sidebar_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sidebar_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sidebar_panel, 0, LV_PART_MAIN);

    const char* names[4] = {"PMS5003", "ENS160", "AHT21", "BMP280"};
    const char* subs[4] = {"Detektor PM1/2.5/10", "Jako\u015b\u0107 TVOC & eCO2", "Temp. & Wilgotno\u015b\u0107", "Ci\u015bnienie & Klimat"};

    for (uint8_t i = 0; i < 4; i++) {
        s_sensor_btns[i] = lv_obj_create(s_sidebar_panel);
        lv_obj_set_pos(s_sensor_btns[i], 0, i * 105);
        lv_obj_set_size(s_sensor_btns[i], 200, 95);
        lv_obj_remove_flag(s_sensor_btns[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_sensor_btns[i], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_sensor_btns[i], lv_color_hex(0x1e1b22), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_sensor_btns[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_sensor_btns[i], 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_sensor_btns[i], 16, LV_PART_MAIN);

        lv_obj_t *lbl_title = lv_label_create(s_sensor_btns[i]);
        lv_obj_set_pos(lbl_title, 15, 18);
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_title, &ui_font_montserrat_regular_16, LV_PART_MAIN);
        lv_label_set_text_static(lbl_title, names[i]);

        lv_obj_t *lbl_sub = lv_label_create(s_sensor_btns[i]);
        lv_obj_set_pos(lbl_sub, 15, 45);
        lv_obj_set_style_text_color(lbl_sub, lv_color_hex(0x8d8896), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_sub, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(lbl_sub, subs[i]);

        lv_obj_add_flag(s_sensor_btns[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_sensor_btns[i], on_sensor_btn_click, LV_EVENT_RELEASED, (void*)(uintptr_t)i);
    }

    update_active_highlight();
}

static void update_active_highlight(void) {
    uint32_t active_border_colors[4] = {0x00e5ff, 0xff9f00, 0x00ff7f, 0xda70d6}; // custom matching colors

    for (uint8_t i = 0; i < 4; i++) {
        if (!s_sensor_btns[i]) continue;
        if (i == s_active_sensor) {
            lv_obj_set_style_border_color(s_sensor_btns[i], lv_color_hex(active_border_colors[i]), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_sensor_btns[i], 2, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_sensor_btns[i], lv_color_hex(0x28232e), LV_PART_MAIN);
        } else {
            lv_obj_set_style_border_color(s_sensor_btns[i], lv_color_hex(0x3d3945), LV_PART_MAIN);
            lv_obj_set_style_border_width(s_sensor_btns[i], 1, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_sensor_btns[i], lv_color_hex(0x1e1b22), LV_PART_MAIN);
        }
    }
}

static lv_obj_t* create_param_card_stable(lv_obj_t* parent, int x, int y, int w, int h, const char* title, const char* unit, lv_obj_t **val_lbl_out, lv_obj_t **trend_lbl_out) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x18161b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x312e38), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 16, LV_PART_MAIN);

    lv_obj_t *lbl_title = lv_label_create(card);
    lv_obj_set_pos(lbl_title, 15, 12);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xb0acb5), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_title, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(lbl_title, title);

    *val_lbl_out = lv_label_create(card);
    lv_obj_set_pos(*val_lbl_out, 15, 35);
    lv_obj_set_width(*val_lbl_out, w - 30);
    lv_obj_set_style_text_color(*val_lbl_out, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(*val_lbl_out, &ui_font_bebas_neue35, LV_PART_MAIN);
    lv_label_set_long_mode(*val_lbl_out, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text_static(*val_lbl_out, "--");

    if (unit && strlen(unit) > 0) {
        lv_obj_t *lbl_unit = lv_label_create(card);
        lv_obj_align(lbl_unit, LV_ALIGN_TOP_RIGHT, -15, 12);
        lv_obj_set_style_text_color(lbl_unit, lv_color_hex(0x8d8896), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_unit, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(lbl_unit, unit);
    }

    *trend_lbl_out = lv_label_create(card);
    lv_obj_set_pos(*trend_lbl_out, w - 30, h - 30);
    lv_obj_set_style_text_color(*trend_lbl_out, lv_color_hex(0x6a6570), LV_PART_MAIN);
    lv_obj_set_style_text_font(*trend_lbl_out, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(*trend_lbl_out, "=");

    return card;
}

static lv_obj_t* create_micro_card_stable(lv_obj_t* parent, int x, int y, int w, int h, const char* title, lv_obj_t **val_lbl_out, lv_obj_t **trend_lbl_out) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x131116), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2a2731), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);

    lv_obj_t *lbl_title = lv_label_create(card);
    lv_obj_set_pos(lbl_title, 8, 6);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x8d8896), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_title, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(lbl_title, title);

    *val_lbl_out = lv_label_create(card);
    lv_obj_set_pos(*val_lbl_out, 8, 24);
    lv_obj_set_style_text_color(*val_lbl_out, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(*val_lbl_out, &ui_font_bebas_neue30, LV_PART_MAIN);
    lv_label_set_text_static(*val_lbl_out, "--");

    *trend_lbl_out = lv_label_create(card);
    lv_obj_set_pos(*trend_lbl_out, w - 20, 24);
    lv_obj_set_style_text_color(*trend_lbl_out, lv_color_hex(0x6a6570), LV_PART_MAIN);
    lv_obj_set_style_text_font(*trend_lbl_out, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(*trend_lbl_out, "=");

    return card;
}

static void create_pms_panel(void) {
    s_sensor_panels[0] = lv_obj_create(s_workspace);
    lv_obj_set_pos(s_sensor_panels[0], 0, 0);
    lv_obj_set_size(s_sensor_panels[0], 525, 335);
    lv_obj_remove_flag(s_sensor_panels[0], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_sensor_panels[0], 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sensor_panels[0], 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sensor_panels[0], 0, LV_PART_MAIN);

    create_param_card_stable(s_sensor_panels[0], 0, 0, 165, 115, "PM 1.0", "ug", &s_val_labels[0][0], &s_trend_labels[0][0]);
    create_param_card_stable(s_sensor_panels[0], 180, 0, 165, 115, "PM 2.5", "ug", &s_val_labels[0][1], &s_trend_labels[0][1]);
    create_param_card_stable(s_sensor_panels[0], 360, 0, 165, 115, "PM 10", "ug", &s_val_labels[0][2], &s_trend_labels[0][2]);

    lv_obj_t *lbl_spec_title = lv_label_create(s_sensor_panels[0]);
    lv_obj_set_pos(lbl_spec_title, 0, 125);
    lv_obj_set_style_text_color(lbl_spec_title, lv_color_hex(0x8d8896), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_spec_title, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(lbl_spec_title, "Fizyczna ilo\u015b\u0107 cz\u0105stek w 0,1 L powietrza:");

    const char* size_names[6] = {"> 0.3 um", "> 0.5 um", "> 1.0 um", "> 2.5 um", "> 5.0 um", "> 10 um"};
    int x_coords[3] = {0, 180, 360};
    
    for (int i = 0; i < 6; i++) {
        int row = i / 3;
        int col = i % 3;
        create_micro_card_stable(s_sensor_panels[0], x_coords[col], 144 + row * 64, 165, 58, size_names[i], &s_micro_val_labels[i], &s_micro_trend_labels[i]);
    }
    
    lv_obj_t *badge = lv_label_create(s_sensor_panels[0]);
    lv_obj_set_pos(badge, 0, 280);
    lv_obj_set_style_text_color(badge, lv_color_hex(0x6a6570), LV_PART_MAIN);
    lv_obj_set_style_text_font(badge, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(badge, "Magistrala: UART | Zasilanie: 5V | Czas: Real-Time");
}

static void create_ens_panel(void) {
    s_sensor_panels[1] = lv_obj_create(s_workspace);
    lv_obj_set_pos(s_sensor_panels[1], 0, 0);
    lv_obj_set_size(s_sensor_panels[1], 525, 335);
    lv_obj_remove_flag(s_sensor_panels[1], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_sensor_panels[1], 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sensor_panels[1], 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sensor_panels[1], 0, LV_PART_MAIN);

    create_param_card_stable(s_sensor_panels[1], 0, 15, 165, 125, "Indeks eCO2", "ppm", &s_val_labels[1][0], &s_trend_labels[1][0]);
    create_param_card_stable(s_sensor_panels[1], 180, 15, 165, 125, "LZO / TVOC", "ppb", &s_val_labels[1][1], &s_trend_labels[1][1]);
    create_param_card_stable(s_sensor_panels[1], 360, 15, 165, 125, "Indeks AQI", "", &s_val_labels[1][2], &s_trend_labels[1][2]);

    // Stopien kalibracji gazow — mowi, na ile ufac wartosciom powyzej
    // (dane plyna od razu, ale przed 1 h / 24 h maja inna wiarygodnosc).
    s_ens_stage_label = lv_label_create(s_sensor_panels[1]);
    lv_obj_set_pos(s_ens_stage_label, 0, 158);
    lv_obj_set_style_text_color(s_ens_stage_label, lv_color_hex(0x6a6570), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_ens_stage_label, &ui_font_montserrat_regular_16, LV_PART_MAIN);
    lv_label_set_text_static(s_ens_stage_label, "");

    lv_obj_t *badge = lv_label_create(s_sensor_panels[1]);
    lv_obj_set_pos(badge, 0, 200);
    lv_obj_set_style_text_color(badge, lv_color_hex(0x6a6570), LV_PART_MAIN);
    lv_obj_set_style_text_font(badge, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(badge, "Interfejs: I2C (0x53) | Typ: MOX | Komp: AHT21");
}

static void create_aht_panel(void) {
    s_sensor_panels[2] = lv_obj_create(s_workspace);
    lv_obj_set_pos(s_sensor_panels[2], 0, 0);
    lv_obj_set_size(s_sensor_panels[2], 525, 335);
    lv_obj_remove_flag(s_sensor_panels[2], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_sensor_panels[2], 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sensor_panels[2], 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sensor_panels[2], 0, LV_PART_MAIN);

    lv_obj_t *t_card = lv_obj_create(s_sensor_panels[2]);
    lv_obj_set_pos(t_card, 0, 15);
    lv_obj_set_size(t_card, 250, 150);
    lv_obj_remove_flag(t_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(t_card, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t_card, lv_color_hex(0x18161b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(t_card, lv_color_hex(0x312e38), LV_PART_MAIN);
    lv_obj_set_style_border_width(t_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(t_card, 16, LV_PART_MAIN);

    lv_obj_t *icon_t = lv_image_create(t_card);
    lv_obj_set_pos(icon_t, 15, 20);
    lv_image_set_src(icon_t, &img_temp_icon);
    lv_image_set_scale(icon_t, 220);
    lv_obj_set_style_image_recolor(icon_t, lv_color_hex(0xff7a00), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(icon_t, 255, LV_PART_MAIN);

    lv_obj_t *lbl_t_title = lv_label_create(t_card);
    lv_obj_set_pos(lbl_t_title, 80, 20);
    lv_obj_set_style_text_color(lbl_t_title, lv_color_hex(0xb0acb5), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_t_title, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(lbl_t_title, "Temperatura");

    s_val_labels[2][0] = lv_label_create(t_card);
    lv_obj_set_pos(s_val_labels[2][0], 80, 42);
    lv_obj_set_style_text_color(s_val_labels[2][0], lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_val_labels[2][0], &ui_font_bebas_neue58, LV_PART_MAIN);
    lv_label_set_text_static(s_val_labels[2][0], "--");

    lv_obj_t *lbl_t_unit = lv_label_create(t_card);
    lv_obj_align(lbl_t_unit, LV_ALIGN_TOP_RIGHT, -15, 20);
    lv_obj_set_style_text_color(lbl_t_unit, lv_color_hex(0x8d8896), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_t_unit, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(lbl_t_unit, "C");

    s_trend_labels[2][0] = lv_label_create(t_card);
    lv_obj_set_pos(s_trend_labels[2][0], 220, 110);
    lv_obj_set_style_text_color(s_trend_labels[2][0], lv_color_hex(0x6a6570), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_trend_labels[2][0], &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(s_trend_labels[2][0], "=");

    lv_obj_t *h_card = lv_obj_create(s_sensor_panels[2]);
    lv_obj_set_pos(h_card, 275, 15);
    lv_obj_set_size(h_card, 250, 150);
    lv_obj_remove_flag(h_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(h_card, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(h_card, lv_color_hex(0x18161b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(h_card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(h_card, lv_color_hex(0x312e38), LV_PART_MAIN);
    lv_obj_set_style_border_width(h_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(h_card, 16, LV_PART_MAIN);

    lv_obj_t *icon_h = lv_image_create(h_card);
    lv_obj_set_pos(icon_h, 15, 20);
    lv_image_set_src(icon_h, &img_water_icon);
    lv_image_set_scale(icon_h, 240);
    lv_obj_set_style_image_recolor(icon_h, lv_color_hex(0x00c5ff), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(icon_h, 255, LV_PART_MAIN);

    lv_obj_t *lbl_h_title = lv_label_create(h_card);
    lv_obj_set_pos(lbl_h_title, 80, 20);
    lv_obj_set_style_text_color(lbl_h_title, lv_color_hex(0xb0acb5), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_h_title, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(lbl_h_title, "Wilgotno\u015b\u0107");

    s_val_labels[2][1] = lv_label_create(h_card);
    lv_obj_set_pos(s_val_labels[2][1], 80, 42);
    lv_obj_set_style_text_color(s_val_labels[2][1], lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_val_labels[2][1], &ui_font_bebas_neue58, LV_PART_MAIN);
    lv_label_set_text_static(s_val_labels[2][1], "--");

    lv_obj_t *lbl_h_unit = lv_label_create(h_card);
    lv_obj_align(lbl_h_unit, LV_ALIGN_TOP_RIGHT, -15, 20);
    lv_obj_set_style_text_color(lbl_h_unit, lv_color_hex(0x8d8896), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_h_unit, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(lbl_h_unit, "%");

    s_trend_labels[2][1] = lv_label_create(h_card);
    lv_obj_set_pos(s_trend_labels[2][1], 220, 110);
    lv_obj_set_style_text_color(s_trend_labels[2][1], lv_color_hex(0x6a6570), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_trend_labels[2][1], &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(s_trend_labels[2][1], "=");

    lv_obj_t *badge = lv_label_create(s_sensor_panels[2]);
    lv_obj_set_pos(badge, 0, 200);
    lv_obj_set_style_text_color(badge, lv_color_hex(0x6a6570), LV_PART_MAIN);
    lv_obj_set_style_text_font(badge, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(badge, "Interfejs: I2C (0x38) | Typ: Pojemno\u015bciowy");
}

static void create_bme_panel(void) {
    s_sensor_panels[3] = lv_obj_create(s_workspace);
    lv_obj_set_pos(s_sensor_panels[3], 0, 0);
    lv_obj_set_size(s_sensor_panels[3], 525, 335);
    lv_obj_remove_flag(s_sensor_panels[3], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_sensor_panels[3], 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sensor_panels[3], 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_sensor_panels[3], 0, LV_PART_MAIN);

    create_param_card_stable(s_sensor_panels[3], 0, 15, 245, 125, "Temperatura", "C", &s_val_labels[3][0], &s_trend_labels[3][0]);
    create_param_card_stable(s_sensor_panels[3], 260, 15, 245, 125, "Ci\u015bnienie", "hPa", &s_val_labels[3][2], &s_trend_labels[3][2]);

    lv_obj_t *badge = lv_label_create(s_sensor_panels[3]);
    lv_obj_set_pos(badge, 0, 200);
    lv_obj_set_style_text_color(badge, lv_color_hex(0x6a6570), LV_PART_MAIN);
    lv_obj_set_style_text_font(badge, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(badge, "Interfejs: I2C (0x77) | Typ: MEMS +-1 hPa");
}

static void create_theory_panel(void) {
    s_theory_panel = lv_obj_create(s_workspace);
    lv_obj_set_pos(s_theory_panel, 0, 0);
    lv_obj_set_size(s_theory_panel, 525, 335);
    lv_obj_remove_flag(s_theory_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_theory_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_theory_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_theory_panel, 0, LV_PART_MAIN);

    lv_obj_t *scroll = lv_obj_create(s_theory_panel);
    lv_obj_set_pos(scroll, 0, 0);
    lv_obj_set_size(scroll, 525, 270);
    lv_obj_set_style_pad_all(scroll, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(0x18161b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(scroll, lv_color_hex(0x312e38), LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(scroll, 12, LV_PART_MAIN);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t *wiki_h1 = lv_label_create(scroll);
    lv_obj_set_width(wiki_h1, 480);
    lv_obj_set_style_text_color(wiki_h1, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(wiki_h1, &ui_font_montserrat_regular_16, LV_PART_MAIN);
    
    lv_obj_t *wiki_body = lv_label_create(scroll);
    lv_obj_set_width(wiki_body, 480);
    lv_obj_set_style_text_color(wiki_body, lv_color_hex(0xd0cbd5), LV_PART_MAIN);
    lv_obj_set_style_text_font(wiki_body, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_long_mode(wiki_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(wiki_body, LV_ALIGN_TOP_LEFT, 0, 24);

    static const char* wiki_titles[4] = {
        "ZASADA DZIA\u0141ANIA SENSORA LASEROWEGO PMS5003",
        "ZASADA DZIA\u0141ANIA CZUJNIKA JAKO\u015aCI ENS160",
        "ZASADA DZIA\u0141ANIA SENSORA KLIMATU AHT21",
        "ZASADA DZIA\u0141ANIA CZUJNIKA CI\u015aNIENIA BMP280"
    };
    static const char* wiki_bodies[4] = {
        "Sensor PMS5003 dzia\u0142a na zasadzie rozpraszania laserowego. Dioda laserowa emituje wi\u0105zk\u0119 \u015bwiat\u0142a wewn\u0105trz komory pomiarowej, do kt\u00f3rej wentylator zasysa powietrze. Gdy cz\u0105stka py\u0142u (np. PM2.5) przecina laser, rozprasza \u015bwiat\u0142o. Fotodetektor rejestruje nat\u0119\u017cenie rozb\u0142ysk\u00f3w, a mikrokontroler oblicza ilo\u015b\u0107 cz\u0105stek oraz st\u0119\u017cenie py\u0142u w ug/m\u00b3.\n\n"
        "KLASYFIKACJA FRAKCJI I ICH POCHODZENIE:\n"
        "- > 0.3 um do 0.5 um (Ultrafrakcja):\n"
        "Najmniejsze drobiny. Pochodz\u0105 g\u0142\u00f3wnie z proces\u00f3w spalania (silniki diesla, dym tytoniowy, kominy). Przenikaj\u0105 bezpo\u015brednio do uk\u0142adu krwiono\u015bnego.\n"
        "- > 0.5 um do 1.0 um (Sadza):\n"
        "G\u0142\u00f3wnie sadza w\u0119glowa i drobny py\u0142 metaliczny ze \u015bcierania klock\u00f3w hamulcowych.\n"
        "- > 1.0 um do 2.5 um (Smog PM2.5):\n"
        "Odpowiada za klasyczny smog. Zawiera toksyczne zwi\u0105zki chemiczne i zanieczyszczenia organiczne.\n"
        "- > 2.5 um do 5.0 um (Py\u0142 mineralny):\n"
        "Drobiny powsta\u0142e mechanicznie - py\u0142 z opon, budowy, unoszenie py\u0142u z dr\u00f3g przez wiatr.\n"
        "- > 5.0 um do 10.0 um (Olbrzymy):\n"
        "Py\u0142ki ro\u015blin, roztocza, zarodniki ple\u015bni i kurzu. Powoduj\u0105 alergie, lecz s\u0105 filtrowane w nosie.",
        "ENS160 wykorzystuje zaawansowan\u0105 technologi\u0119 tlenk\u00f3w metali (MOX). Posiada cztery niezale\u017cne grza\u0142ki, kt\u00f3re podgrzewaj\u0105 cztery piksele sensoryczne do temp. ok. 350 stopni C. Tlen adsorbuje na warstwie sensorycznej, tworz\u0105c barier\u0119 potencja\u0142u. Obecne w powietrzu Lotne Zwi\u0105zki Organiczne (TVOC) wchodz\u0105 w reakcje chemiczne z tlenem, drastycznie zmieniaj\u0105c op\u00f3r elektryczny czujnika. Zmiany te s\u0105 analizowane i przeliczane na TVOC (w ppb) oraz eCO2 (w ppm).\n\n"
        "TVOC A ECO2:\n"
        "TVOC to lotne substancje organiczne (np. od\u015bwie\u017cacze, lakiery, perfumy, opary kuchenne). eCO2 (ekwiwalent CO2) wyliczany jest na podstawie gaz\u00f3w towarzysz\u0105cych oddychaniu, stanowi\u0105c idealn\u0105 miar\u0119 duszno\u015bci w pokoju i potrzebie wietrzenia.",
        "Sensor AHT21 integruje w sobie dwa zaawansowane przetworniki fizyczne.\n\n"
        "Czujnik wilgotno\u015bci dzia\u0142a w oparciu o metod\u0119 pojemno\u015bciow\u0105. Posiada mikroskopijn\u0105 warstw\u0119 polimerow\u0105 jako dielektryk kondensatora pomiarowego. Woda zawarta w powietrzu wnika w ten polimer, co drastycznie zmienia jego przenikalno\u015b\u0107 elektryczn\u0105 i tym samym pojemno\u015b\u0107 kondensatora. Wbudowany scalak ASIC mierzy pojemno\u015b\u0107 i zamienia j\u0105 na wilgotno\u015b\u0107 wzgl\u0119dn\u0105 (%).\n\n"
        "Pomiar temperatury opiera si\u0119 na krzemowym z\u0142\u0105czu p-n (diodzie), kt\u00f3rego spadek napi\u0119cia w kierunku przewodzenia zmienia si\u0119 liniowo wraz z temperatur\u0105.\n\n"
        "W\u0142a\u015bciwy poziom wilgotno\u015bci w pokoju to 40-60%. Zbyt niska podra\u017cnia \u015bluz\u00f3wk\u0119 oka i dr\u00f3g oddechowych, a zbyt wysoka sprzyja rosn\u0105cym zarodnikom ple\u015bni.",
        "BMP280 firmy Bosch to zintegrowany sensor MEMS realizuj\u0105cy precyzyjny pomiar ci\u015bnienia barometrycznego i temperatury.\n\n"
        "Sercem czujnika ci\u015bnienia jest mikroskopijna silikonowa membrana wytrawiona nad komor\u0105 pr\u00f3\u017cniow\u0105. W t\u0119 membran\u0119 wbudowano piezorezystory. Zmiana ci\u015bnienia powietrza ugina membran\u0119, co wywo\u0142uje napr\u0119\u017cenia i drastycznie zmienia op\u00f3r elektryczny piezorezystor\u00f3w. Zjawisko to (piezorezystancja) pozwala na wykrycie zmian wysoko\u015bci nawet o kilkana\u015bcie centymetr\u00f3w (dzia\u0142a jako wysoko\u015bciomierz).\n\n"
        "Wbudowana pami\u0119\u0107 ROM zawiera fabryczne wsp\u00f3\u0142czynniki kalibracji, przez kt\u00f3re uk\u0142ad ASIC kompensuje wp\u0142yw temperatury na pomiar ci\u015bnienia.\n\n"
        "Szybkie zmiany ci\u015bnienia barometrycznego pozwalaj\u0105 wykry\u0107 nadchodz\u0105ce fronty atmosferyczne (burza, deszcz) zanim przyjd\u0105 na horyzoncie."
    };

    lv_label_set_text_static(wiki_h1, wiki_titles[0]);
    lv_label_set_text_static(wiki_body, wiki_bodies[0]);
}

static void ensure_panels_created(void) {
    if (s_sensor_panels[0]) return;
    create_pms_panel();
    create_ens_panel();
    create_aht_panel();
    create_bme_panel();
    create_theory_panel();
}

static void render_right_panel(void) {
    if (!s_content_panel) return;

    ensure_panels_created();

    if (s_section_title) {
        const char *titles[4] = {
            "PMS5003 - Laserowy pomiar",
            "ENS160 - Pomiar jako\u015bci",
            "AHT21 - Klimat pokoju",
            "BMP280 - Pomiar otoczenia"
        };
        lv_label_set_text_static(s_section_title, titles[s_active_sensor]);
    }

    for (int i = 0; i < 4; i++) {
        lv_obj_add_flag(s_sensor_panels[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(s_theory_panel, LV_OBJ_FLAG_HIDDEN);

    if (s_active_mode == 0) {
        lv_obj_clear_flag(s_sensor_panels[s_active_sensor], LV_OBJ_FLAG_HIDDEN);
        s_lbl_val1 = s_val_labels[s_active_sensor][0];
        s_lbl_val2 = s_val_labels[s_active_sensor][1];
        s_lbl_val3 = s_val_labels[s_active_sensor][2];
        s_lbl_trend1 = s_trend_labels[s_active_sensor][0];
        s_lbl_trend2 = s_trend_labels[s_active_sensor][1];
        s_lbl_trend3 = s_trend_labels[s_active_sensor][2];
        for (int i = 0; i < 6; i++) {
            s_lbl_micro_vals[i] = (s_active_sensor == 0) ? s_micro_val_labels[i] : NULL;
            s_lbl_micro_trends[i] = (s_active_sensor == 0) ? s_micro_trend_labels[i] : NULL;
        }
    } else {
        lv_obj_clear_flag(s_theory_panel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *scroll = lv_obj_get_child(s_theory_panel, 0);
        if (scroll) {
            lv_obj_t *wiki_h1 = lv_obj_get_child(scroll, 0);
            lv_obj_t *wiki_body = lv_obj_get_child(scroll, 1);

            static const char* wiki_titles[4] = {
                "ZASADA DZIA\u0141ANIA SENSORA LASEROWEGO PMS5003",
                "ZASADA DZIA\u0141ANIA CZUJNIKA JAKO\u015aCI ENS160",
                "ZASADA DZIA\u0141ANIA SENSORA KLIMATU AHT21",
                "ZASADA DZIA\u0141ANIA CZUJNIKA CI\u015aNIENIA BMP280"
            };
            static const char* wiki_bodies[4] = {
                "Sensor PMS5003 dzia\u0142a na zasadzie rozpraszania laserowego. Dioda laserowa emituje wi\u0105zk\u0119 \u015bwiat\u0142a wewn\u0105trz komory pomiarowej, do kt\u00f3rej wentylator zasysa powietrze. Gdy cz\u0105stka py\u0142u (np. PM2.5) przecina laser, rozprasza \u015bwiat\u0142o. Fotodetektor rejestruje nat\u0119\u017cenie rozb\u0142ysk\u00f3w, a mikrokontroler oblicza ilo\u015b\u0107 cz\u0105stek oraz st\u0119\u017cenie py\u0142u w ug/m\u00b3.\n\n"
                "KLASYFIKACJA FRAKCJI I ICH POCHODZENIE:\n"
                "- > 0.3 um do 0.5 um (Ultrafrakcja):\n"
                "Najmniejsze drobiny. Pochodz\u0105 g\u0142\u00f3wnie z proces\u00f3w spalania (silniki diesla, dym tytoniowy, kominy). Przenikaj\u0105 bezpo\u015brednio do uk\u0142adu krwiono\u015bnego.\n"
                "- > 0.5 um do 1.0 um (Sadza):\n"
                "G\u0142\u00f3wnie sadza w\u0119glowa i drobny py\u0142 metaliczny ze \u015bcierania klock\u00f3w hamulcowych.\n"
                "- > 1.0 um do 2.5 um (Smog PM2.5):\n"
                "Odpowiada za klasyczny smog. Zawiera toksyczne zwi\u0105zki chemiczne i zanieczyszczenia organiczne.\n"
                "- > 2.5 um do 5.0 um (Py\u0142 mineralny):\n"
                "Drobiny powsta\u0142e mechanicznie - py\u0142 z opon, budowy, unoszenie py\u0142u z dr\u00f3g przez wiatr.\n"
                "- > 5.0 um do 10.0 um (Olbrzymy):\n"
                "Py\u0142ki ro\u015blin, roztocza, zarodniki ple\u015bni i kurzu. Powoduj\u0105 alergie, lecz s\u0105 filtrowane w nosie.",
                "ENS160 wykorzystuje zaawansowan\u0105 technologi\u0119 tlenk\u00f3w metali (MOX). Posiada cztery niezale\u017cne grza\u0142ki, kt\u00f3re podgrzewaj\u0105 cztery piksele sensoryczne do temp. ok. 350 stopni C. Tlen adsorbuje na warstwie sensorycznej, tworz\u0105c barier\u0119 potencja\u0142u. Obecne w powietrzu Lotne Zwi\u0105zki Organiczne (TVOC) wchodz\u0105 w reakcje chemiczne z tlenem, drastycznie zmieniaj\u0105c op\u00f3r elektryczny czujnika. Zmiany te s\u0105 analizowane i przeliczane na TVOC (w ppb) oraz eCO2 (w ppm).\n\n"
                "TVOC A ECO2:\n"
                "TVOC to lotne substancje organiczne (np. od\u015bwie\u017cacze, lakiery, perfumy, opary kuchenne). eCO2 (ekwiwalent CO2) wyliczany jest na podstawie gaz\u00f3w towarzysz\u0105cych oddychaniu, stanowi\u0105c idealn\u0105 miar\u0119 duszno\u015bci w pokoju i potrzebie wietrzenia.",
                "Sensor AHT21 integruje w sobie dwa zaawansowane przetworniki fizyczne.\n\n"
                "Czujnik wilgotno\u015bci dzia\u0142a w oparciu o metod\u0119 pojemno\u015bciow\u0105. Posiada mikroskopijn\u0105 warstw\u0119 polimerow\u0105 jako dielektryk kondensatora pomiarowego. Woda zawarta w powietrzu wnika w ten polimer, co drastycznie zmienia jego przenikalno\u015b\u0107 elektryczn\u0105 i tym samym pojemno\u015b\u0107 kondensatora. Wbudowany scalak ASIC mierzy pojemno\u015b\u0107 i zamienia j\u0105 na wilgotno\u015b\u0107 wzgl\u0119dn\u0105 (%).\n\n"
                "Pomiar temperatury opiera si\u0119 na krzemowym z\u0142\u0105czu p-n (diodzie), kt\u00f3rego spadek napi\u0119cia w kierunku przewodzenia zmienia si\u0119 liniowo wraz z temperatur\u0105.\n\n"
                "W\u0142a\u015bciwy poziom wilgotno\u015bci w pokoju to 40-60%. Zbyt niska podra\u017cnia \u015bluz\u00f3wk\u0119 oka i dr\u00f3g oddechowych, a zbyt wysoka sprzyja rosn\u0105cym zarodnikom ple\u015bni.",
                "BMP280 firmy Bosch to zintegrowany sensor MEMS realizuj\u0105cy precyzyjny pomiar ci\u015bnienia barometrycznego i temperatury.\n\n"
                "Sercem czujnika ci\u015bnienia jest mikroskopijna silikonowa membrana wytrawiona nad komor\u0105 pr\u00f3\u017cniow\u0105. W t\u0119 membran\u0119 wbudowano piezorezystory. Zmiana ci\u015bnienia powietrza ugina membran\u0119, co wywo\u0142uje napr\u0119\u017cenia i drastycznie zmienia op\u00f3r elektryczny piezorezystor\u00f3w. Zjawisko to (piezorezystancja) pozwala na wykrycie zmian wysoko\u015bci nawet o kilkana\u015bcie centymetr\u00f3w (dzia\u0142a jako wysoko\u015bciomierz).\n\n"
                "Wbudowana pami\u0119\u0107 ROM zawiera fabryczne wsp\u00f3\u0142czynniki kalibracji, przez kt\u00f3re uk\u0142ad ASIC kompensuje wp\u0142yw temperatury na pomiar ci\u015bnienia.\n\n"
                "Szybkie zmiany ci\u015bnienia barometrycznego pozwalaj\u0105 wykry\u0107 nadchodz\u0105ce fronty atmosferyczne (burza, deszcz) zanim przyjd\u0105 na horyzoncie."
            };

            if (wiki_h1) lv_label_set_text_static(wiki_h1, wiki_titles[s_active_sensor]);
            if (wiki_body) lv_label_set_text_static(wiki_body, wiki_bodies[s_active_sensor]);
        }

        s_lbl_val1 = s_lbl_val2 = s_lbl_val3 = NULL;
        s_lbl_trend1 = s_lbl_trend2 = s_lbl_trend3 = NULL;
        for (int i = 0; i < 6; i++) {
            s_lbl_micro_vals[i] = NULL;
            s_lbl_micro_trends[i] = NULL;
        }
    }

    sensors_wiki_update();
    gesture_register_recursive(s_workspace);
}

// ---------------------------------------------------------------------------
// Public Interface Implementation
// ---------------------------------------------------------------------------

void sensors_wiki_init(lv_obj_t *parent_screen) {
    if (!parent_screen) return;
    
    s_parent_screen = parent_screen;
    lv_obj_set_style_bg_color(s_parent_screen, lv_color_hex(0x28252c), LV_PART_MAIN);

    if (!s_ui_initialized) {
        s_ui_initialized = true;

        render_sidebar();

        s_content_panel = lv_obj_create(s_parent_screen);
        lv_obj_set_pos(s_content_panel, 230, 55);
        lv_obj_set_size(s_content_panel, 555, 410);
        lv_obj_remove_flag(s_content_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_content_panel, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_content_panel, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_content_panel, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_content_panel, lv_color_hex(0x312e38), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_content_panel, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_content_panel, 20, LV_PART_MAIN);

        lv_obj_t *pill_bg = lv_obj_create(s_content_panel);
        lv_obj_set_pos(pill_bg, 300, 15);
        lv_obj_set_size(pill_bg, 240, 36);
        lv_obj_remove_flag(pill_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(pill_bg, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(pill_bg, lv_color_hex(0x110f14), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pill_bg, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(pill_bg, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(pill_bg, 18, LV_PART_MAIN);

        s_mode_indicator = lv_obj_create(pill_bg);
        lv_obj_set_size(s_mode_indicator, 116, 32);
        lv_obj_remove_flag(s_mode_indicator, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_mode_indicator, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_mode_indicator, lv_color_hex(0x2d2a36), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_mode_indicator, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_mode_indicator, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_mode_indicator, 16, LV_PART_MAIN);
        if (s_active_mode == 0) {
            lv_obj_align(s_mode_indicator, LV_ALIGN_LEFT_MID, 2, 0);
        } else {
            lv_obj_align(s_mode_indicator, LV_ALIGN_RIGHT_MID, -2, 0);
        }

        s_mode_param_btn = lv_btn_create(pill_bg);
        lv_obj_set_size(s_mode_param_btn, 116, 32);
        lv_obj_align(s_mode_param_btn, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_remove_flag(s_mode_param_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(s_mode_param_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_mode_param_btn, 0, LV_PART_MAIN);
        lv_obj_t *lbl_param = lv_label_create(s_mode_param_btn);
        lv_obj_align(lbl_param, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_color(lbl_param, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_param, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(lbl_param, "PARAMETRY");
        lv_obj_add_event_cb(s_mode_param_btn, on_mode_change_click, LV_EVENT_RELEASED, (void*)0);

        s_mode_theory_btn = lv_btn_create(pill_bg);
        lv_obj_set_size(s_mode_theory_btn, 116, 32);
        lv_obj_align(s_mode_theory_btn, LV_ALIGN_RIGHT_MID, -2, 0);
        lv_obj_remove_flag(s_mode_theory_btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(s_mode_theory_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_mode_theory_btn, 0, LV_PART_MAIN);
        lv_obj_t *lbl_theory = lv_label_create(s_mode_theory_btn);
        lv_obj_align(lbl_theory, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_color(lbl_theory, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_theory, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(lbl_theory, "TEORIA");
        lv_obj_add_event_cb(s_mode_theory_btn, on_mode_change_click, LV_EVENT_RELEASED, (void*)1);

        s_section_title = lv_label_create(s_content_panel);
        lv_obj_set_pos(s_section_title, 15, 20);
        lv_obj_set_style_text_color(s_section_title, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_section_title, &ui_font_montserrat_regular_16, LV_PART_MAIN);

        s_workspace = lv_obj_create(s_content_panel);
        lv_obj_set_pos(s_workspace, 15, 60);
        lv_obj_set_size(s_workspace, 525, 335);
        lv_obj_remove_flag(s_workspace, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_workspace, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_workspace, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_workspace, 0, LV_PART_MAIN);

        ensure_panels_created();
    }

    render_right_panel();
    gesture_register_recursive(parent_screen);
}

void sensors_wiki_update(void) {
    if (eez_flow_get_current_screen() != SCREEN_ID_CZUJNIKI || !s_content_panel) {
        return;
    }

    const auto &sn = CommunicationProtocol::currentSnapshot();

    char buf[16];

    // Push values into history buffers for trend calculation
    if (sn.valid) {
        push_history(&s_hist_pm1_0, sn.pm01UgM3);
        push_history(&s_hist_pm25, sn.pm25UgM3);
        push_history(&s_hist_pm10, sn.pm10UgM3);
        push_history(&s_hist_eco2, sn.eco2);
        push_history(&s_hist_tvoc, sn.tvoc);
        push_history(&s_hist_temp, sn.temperatureC);
        push_history(&s_hist_hum, sn.humidityPct);
        push_history(&s_hist_pres, sn.pressureHpa);
        push_history(&s_hist_count0p3, sn.count0p3);
        push_history(&s_hist_count0p5, sn.count0p5);
        push_history(&s_hist_count1p0, sn.count1p0);
        push_history(&s_hist_count2p5, sn.count2p5);
        push_history(&s_hist_count5p0, sn.count5p0);
        push_history(&s_hist_count10p0, sn.count10p0);
    }

    // Refresh Parameter mode variables
    if (s_active_mode == 0) {
        if (s_active_sensor == 0) { // PMS5003
            // PM 1.0
            if (s_lbl_val1 && sn.valid) {
                snprintf(buf, sizeof(buf), "%d", sn.pm01UgM3);
                lv_label_set_text(s_lbl_val1, buf);
                
                // Color thresholds
                if (sn.pm01UgM3 > 25) {
                    lv_obj_set_style_text_color(s_lbl_val1, lv_color_hex(0xff3b30), LV_PART_MAIN); // Red
                } else if (sn.pm01UgM3 > 15) {
                    lv_obj_set_style_text_color(s_lbl_val1, lv_color_hex(0xff9f00), LV_PART_MAIN); // Orange
                } else {
                    lv_obj_set_style_text_color(s_lbl_val1, lv_color_hex(0xffffff), LV_PART_MAIN); // White
                }
            }
            if (s_lbl_trend1) {
                lv_label_set_text_static(s_lbl_trend1, calculate_trend(&s_hist_pm1_0, sn.pm01UgM3, 1.0f));
            }

            // PM 2.5
            if (s_lbl_val2 && sn.valid) {
                snprintf(buf, sizeof(buf), "%d", sn.pm25UgM3);
                lv_label_set_text(s_lbl_val2, buf);
                
                // Color thresholds
                if (sn.pm25UgM3 > 35) {
                    lv_obj_set_style_text_color(s_lbl_val2, lv_color_hex(0xff3b30), LV_PART_MAIN); // Red
                } else if (sn.pm25UgM3 > 20) {
                    lv_obj_set_style_text_color(s_lbl_val2, lv_color_hex(0xff9f00), LV_PART_MAIN); // Orange
                } else {
                    lv_obj_set_style_text_color(s_lbl_val2, lv_color_hex(0xffffff), LV_PART_MAIN); // White
                }
            }
            if (s_lbl_trend2) {
                lv_label_set_text_static(s_lbl_trend2, calculate_trend(&s_hist_pm25, sn.pm25UgM3, 1.0f));
            }

            // PM 10
            if (s_lbl_val3 && sn.valid) {
                snprintf(buf, sizeof(buf), "%d", sn.pm10UgM3);
                lv_label_set_text(s_lbl_val3, buf);
                
                // Color thresholds
                if (sn.pm10UgM3 > 50) {
                    lv_obj_set_style_text_color(s_lbl_val3, lv_color_hex(0xff3b30), LV_PART_MAIN); // Red
                } else if (sn.pm10UgM3 > 35) {
                    lv_obj_set_style_text_color(s_lbl_val3, lv_color_hex(0xff9f00), LV_PART_MAIN); // Orange
                } else {
                    lv_obj_set_style_text_color(s_lbl_val3, lv_color_hex(0xffffff), LV_PART_MAIN); // White
                }
            }
            if (s_lbl_trend3) {
                lv_label_set_text_static(s_lbl_trend3, calculate_trend(&s_hist_pm10, sn.pm10UgM3, 2.0f));
            }

            // Spectrometer Particles counts from UART (No local synthesis!)
            if (s_lbl_micro_vals[0] && sn.valid) {
                uint32_t counts[6] = {
                    sn.count0p3,
                    sn.count0p5,
                    sn.count1p0,
                    sn.count2p5,
                    sn.count5p0,
                    sn.count10p0
                };
                VarHistory *hist[6] = {
                    &s_hist_count0p3,
                    &s_hist_count0p5,
                    &s_hist_count1p0,
                    &s_hist_count2p5,
                    &s_hist_count5p0,
                    &s_hist_count10p0
                };
                for (int i = 0; i < 6; i++) {
                    if (s_lbl_micro_vals[i]) {
                        snprintf(buf, sizeof(buf), "%d", (int)counts[i]);
                        lv_label_set_text(s_lbl_micro_vals[i], buf);
                    }
                    if (s_lbl_micro_trends[i]) {
                        lv_label_set_text_static(s_lbl_micro_trends[i], calculate_trend(hist[i], counts[i], 1.0f));
                    }
                }
            }

        } else if (s_active_sensor == 1) { // ENS160
            // Etykieta stopnia kalibracji (bajt 15 ramki 0x01 z ZEGAR-a).
            if (s_ens_stage_label) {
                const char *stage_txt = "";
                uint32_t stage_col = 0x6a6570; // szary = brak danych
                switch (sn.gasStage) {
                    case 0:
                        stage_txt = "ROZGRZEWANIE - dane nieskalibrowane";
                        stage_col = 0xff9f00;
                        break;
                    case 1:
                        stage_txt = "NIESKALIBROWANE - trwa pierwsza godzina pracy";
                        stage_col = 0xff9f00;
                        break;
                    case 2:
                        stage_txt = "WST\u0118PNIE SKALIBROWANE - po 1 h pracy";
                        stage_col = 0xffffff;
                        break;
                    case 3:
                        stage_txt = "W PE\u0141NI SKALIBROWANE - 24 h ci\u0105g\u0142ej pracy";
                        stage_col = 0x00ff7f;
                        break;
                    default:
                        stage_txt = "OCZEKIWANIE NA DANE ENS160...";
                        break;
                }
                lv_label_set_text(s_ens_stage_label, stage_txt);
                lv_obj_set_style_text_color(s_ens_stage_label, lv_color_hex(stage_col), LV_PART_MAIN);
            }

            // eCO2
            if (s_lbl_val1 && sn.valid) {
                snprintf(buf, sizeof(buf), "%d", sn.eco2);
                lv_label_set_text(s_lbl_val1, buf);
                
                if (sn.eco2 > 1200) {
                    lv_obj_set_style_text_color(s_lbl_val1, lv_color_hex(0xff3b30), LV_PART_MAIN); // Red
                } else if (sn.eco2 > 800) {
                    lv_obj_set_style_text_color(s_lbl_val1, lv_color_hex(0xff9f00), LV_PART_MAIN); // Orange
                } else {
                    lv_obj_set_style_text_color(s_lbl_val1, lv_color_hex(0xffffff), LV_PART_MAIN); // White
                }
            }
            if (s_lbl_trend1) {
                lv_label_set_text_static(s_lbl_trend1, calculate_trend(&s_hist_eco2, sn.eco2, 10.0f));
            }

            // TVOC
            if (s_lbl_val2 && sn.valid) {
                snprintf(buf, sizeof(buf), "%d", sn.tvoc);
                lv_label_set_text(s_lbl_val2, buf);
                
                if (sn.tvoc > 500) {
                    lv_obj_set_style_text_color(s_lbl_val2, lv_color_hex(0xff3b30), LV_PART_MAIN); // Red
                } else if (sn.tvoc > 250) {
                    lv_obj_set_style_text_color(s_lbl_val2, lv_color_hex(0xff9f00), LV_PART_MAIN); // Orange
                } else {
                    lv_obj_set_style_text_color(s_lbl_val2, lv_color_hex(0xffffff), LV_PART_MAIN); // White
                }
            }
            if (s_lbl_trend2) {
                lv_label_set_text_static(s_lbl_trend2, calculate_trend(&s_hist_tvoc, sn.tvoc, 5.0f));
            }

            // AQI Text
            if (s_lbl_val3 && sn.valid) {
                if (sn.airQualityIndex >= 4) {
                    lv_label_set_text_static(s_lbl_val3, "BARDZO ZŁA");
                    lv_obj_set_style_text_color(s_lbl_val3, lv_color_hex(0xff3b30), LV_PART_MAIN); // Red
                } else if (sn.airQualityIndex >= 3) {
                    lv_label_set_text_static(s_lbl_val3, "UMIARKOWANA");
                    lv_obj_set_style_text_color(s_lbl_val3, lv_color_hex(0xff9f00), LV_PART_MAIN); // Orange
                } else {
                    lv_label_set_text_static(s_lbl_val3, "DOBRA");
                    lv_obj_set_style_text_color(s_lbl_val3, lv_color_hex(0x00ff7f), LV_PART_MAIN); // Green
                }
            }
            if (s_lbl_trend3) {
                lv_label_set_text_static(s_lbl_trend3, "=");
            }

        } else if (s_active_sensor == 2) { // AHT21 (2 large panels)
            // Temp
            if (s_lbl_val1 && sn.valid) {
                snprintf(buf, sizeof(buf), "%.1f", sn.temperatureC);
                lv_label_set_text(s_lbl_val1, buf);
            }
            if (s_lbl_trend1) {
                lv_label_set_text_static(s_lbl_trend1, calculate_trend(&s_hist_temp, sn.temperatureC, 0.1f));
            }

            // Hum
            if (s_lbl_val2 && sn.valid) {
                snprintf(buf, sizeof(buf), "%d", sn.humidityPct);
                lv_label_set_text(s_lbl_val2, buf);
            }
            if (s_lbl_trend2) {
                lv_label_set_text_static(s_lbl_trend2, calculate_trend(&s_hist_hum, sn.humidityPct, 1.0f));
            }

        } else if (s_active_sensor == 3) { // BMP280
            // Temp
            if (s_lbl_val1 && sn.valid) {
                snprintf(buf, sizeof(buf), "%.1f", sn.temperatureC);
                lv_label_set_text(s_lbl_val1, buf);
            }
            if (s_lbl_trend1) {
                lv_label_set_text_static(s_lbl_trend1, calculate_trend(&s_hist_temp, sn.temperatureC, 0.1f));
            }

            // Pressure
            if (s_lbl_val3 && sn.valid) {
                snprintf(buf, sizeof(buf), "%d", sn.pressureHpa);
                lv_label_set_text(s_lbl_val3, buf);
            }
            if (s_lbl_trend3) {
                lv_label_set_text_static(s_lbl_trend3, calculate_trend(&s_hist_pres, sn.pressureHpa, 0.5f));
            }
        }
    }
}

void sensors_wiki_deinit(void) {
    s_lbl_val1 = s_lbl_val2 = s_lbl_val3 = NULL;
    s_lbl_trend1 = s_lbl_trend2 = s_lbl_trend3 = NULL;
    s_ens_stage_label = NULL;
    for (int i = 0; i < 6; i++) {
        s_lbl_micro_vals[i] = NULL;
        s_lbl_micro_trends[i] = NULL;
    }
    s_parent_screen = NULL;
}
