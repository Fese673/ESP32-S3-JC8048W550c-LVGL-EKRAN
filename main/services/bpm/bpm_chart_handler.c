#include "bpm_chart_handler.h"
#include "bpm_service.h"
#include "screens.h"
#include "eez-flow.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "fonts.h"
#include "vars.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void get_flow_global_variable_text(uint32_t var_id, char *buf, size_t buf_size);

static lv_obj_t *s_chart = NULL;
static lv_chart_series_t *s_series = NULL;
static lv_timer_t *s_timer = NULL;

static lv_obj_t *s_bpm_tile = NULL;
static lv_obj_t *s_spo2_tile = NULL;
static lv_obj_t *s_desc_tile = NULL;

static lv_obj_t *s_bpm_value_label = NULL;
static lv_obj_t *s_spo2_value_label = NULL;
static lv_obj_t *s_bpm_min_max_label = NULL;

static int s_min_bpm = 999;
static int s_max_bpm = -1;

static lv_obj_t *find_chart(lv_obj_t *parent) {
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (lv_obj_has_class(child, &lv_chart_class)) {
            return child;
        }
    }
    return NULL;
}

static lv_obj_t *create_chart(lv_obj_t *parent) {
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_set_pos(chart, 27, 313);
    lv_obj_set_size(chart, 747, 141);
    return chart;
}

static void configure_chart(lv_obj_t *chart) {
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_point_count(chart, 747);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -100, 200);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    s_series = lv_chart_add_series(chart, lv_color_hex(0xff3333), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, s_series, 0);
}

static void bpm_timer_cb(lv_timer_t *timer) {
    (void)timer;

    if (eez_flow_get_current_screen() != SCREEN_ID_BPM) {
        return;
    }

    if (!objects.bpm) {
        return;
    }

    if (!s_bpm_tile) {
        // Create BPM tile container
        s_bpm_tile = lv_obj_create(objects.bpm);
        lv_obj_set_pos(s_bpm_tile, 27, 55);
        lv_obj_set_size(s_bpm_tile, 240, 115);
        lv_obj_remove_flag(s_bpm_tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_bpm_tile, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_bpm_tile, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bpm_tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_bpm_tile, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_bpm_tile, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_bpm_tile, 12, LV_PART_MAIN);

        // BPM Title
        lv_obj_t *bpm_title = lv_label_create(s_bpm_tile);
        lv_obj_set_pos(bpm_title, 15, 12);
        lv_obj_set_style_text_color(bpm_title, lv_color_hex(0xb0acb5), LV_PART_MAIN);
        lv_obj_set_style_text_font(bpm_title, &ui_font_montserrat_regular_16, LV_PART_MAIN);
        lv_label_set_text_static(bpm_title, "BPM");

        // BPM Value
        s_bpm_value_label = lv_label_create(s_bpm_tile);
        lv_obj_set_pos(s_bpm_value_label, 15, 30);
        lv_obj_set_style_text_color(s_bpm_value_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_bpm_value_label, &ui_font_bebas_neue58, LV_PART_MAIN);
        lv_label_set_text_static(s_bpm_value_label, "--");

        // BPM Normal Range
        lv_obj_t *bpm_norm = lv_label_create(s_bpm_tile);
        lv_obj_set_pos(bpm_norm, 15, 92);
        lv_obj_set_style_text_color(bpm_norm, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(bpm_norm, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(bpm_norm, "Norma: 60-100");

        // BPM Min/Max
        s_bpm_min_max_label = lv_label_create(s_bpm_tile);
        lv_obj_set_pos(s_bpm_min_max_label, 132, 92);
        lv_obj_set_style_text_color(s_bpm_min_max_label, lv_color_hex(0x6a6570), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_bpm_min_max_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_bpm_min_max_label, "Min/Max: --/--");

        // Create SpO2 tile container
        s_spo2_tile = lv_obj_create(objects.bpm);
        lv_obj_set_pos(s_spo2_tile, 27, 185);
        lv_obj_set_size(s_spo2_tile, 240, 115);
        lv_obj_remove_flag(s_spo2_tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_spo2_tile, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_spo2_tile, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_spo2_tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_spo2_tile, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_spo2_tile, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_spo2_tile, 12, LV_PART_MAIN);

        // SpO2 Title
        lv_obj_t *spo2_title = lv_label_create(s_spo2_tile);
        lv_obj_set_pos(spo2_title, 15, 12);
        lv_obj_set_style_text_color(spo2_title, lv_color_hex(0xb0acb5), LV_PART_MAIN);
        lv_obj_set_style_text_font(spo2_title, &ui_font_montserrat_regular_16, LV_PART_MAIN);
        lv_label_set_text_static(spo2_title, "SpO2 (%)");

        // SpO2 Value
        s_spo2_value_label = lv_label_create(s_spo2_tile);
        lv_obj_set_pos(s_spo2_value_label, 15, 30);
        lv_obj_set_style_text_color(s_spo2_value_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_spo2_value_label, &ui_font_bebas_neue58, LV_PART_MAIN);
        lv_label_set_text_static(s_spo2_value_label, "--");

        // SpO2 Normal Range
        lv_obj_t *spo2_norm = lv_label_create(s_spo2_tile);
        lv_obj_set_pos(spo2_norm, 15, 92);
        lv_obj_set_style_text_color(spo2_norm, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(spo2_norm, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(spo2_norm, "Norma: 95-100%");

        // Create Sensor Description tile container
        s_desc_tile = lv_obj_create(objects.bpm);
        lv_obj_set_pos(s_desc_tile, 282, 55);
        lv_obj_set_size(s_desc_tile, 492, 245);
        lv_obj_remove_flag(s_desc_tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_desc_tile, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_desc_tile, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_desc_tile, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_desc_tile, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_desc_tile, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_desc_tile, 12, LV_PART_MAIN);

        // Title inside Description Tile
        lv_obj_t *desc_title = lv_label_create(s_desc_tile);
        lv_obj_set_pos(desc_title, 20, 15);
        lv_obj_set_style_text_color(desc_title, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(desc_title, &ui_font_montserrat_regular_16, LV_PART_MAIN);
        lv_label_set_text_static(desc_title, "OPIS DZIA\u0141ANIA CZUJNIKA PPG");

        // Body Text inside Description Tile
        lv_obj_t *desc_body = lv_label_create(s_desc_tile);
        lv_obj_set_pos(desc_body, 20, 48);
        lv_obj_set_width(desc_body, 452);
        lv_obj_set_style_text_color(desc_body, lv_color_hex(0xd0cbd5), LV_PART_MAIN);
        lv_obj_set_style_text_font(desc_body, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_long_mode(desc_body, LV_LABEL_LONG_WRAP);
        lv_label_set_text_static(desc_body, 
            "Sensor MAX30102 dzia\u0142a na zasadzie fotopletyzmografii (PPG). "
            "Emituje \u015bwiat\u0142o czerwone oraz podczerwone (IR) w g\u0142\u0105b tkanki sk\u00f3rnej, "
            "a nast\u0119pnie mierzy stopie\u0144 jego absorpcji przez pulsuj\u0105c\u0105 krew za pomoc\u0105 fotodetektora.\n\n"
            "Zmiany nat\u0119\u017cenia \u015bwiat\u0142a podczerwonego odzwierciedlaj\u0105 t\u0119tno (BPM), "
            "natomiast stosunek absorpcji \u015bwiat\u0142a czerwonego do podczerwonego pozwala wyznaczy\u0107 "
            "natlenienie krwi (SpO2).");
    }

    if (s_bpm_value_label) {
        char bpm_buf[16];
        get_flow_global_variable_text(FLOW_GLOBAL_VARIABLE_BPM_VALUE, bpm_buf, sizeof(bpm_buf));
        
        if (strcmp(bpm_buf, lv_label_get_text(s_bpm_value_label)) != 0) {
            lv_label_set_text(s_bpm_value_label, bpm_buf);
        }

        int bpm_val = atoi(bpm_buf);
        if (bpm_val > 0) {
            // Alarm dynamic color warnings: red when out of [60 - 100] range
            if (bpm_val < 60 || bpm_val > 100) {
                lv_obj_set_style_text_color(s_bpm_value_label, lv_color_hex(0xff3333), LV_PART_MAIN);
            } else {
                lv_obj_set_style_text_color(s_bpm_value_label, lv_color_hex(0xffffff), LV_PART_MAIN);
            }

            // Min/Max session calculation
            if (s_min_bpm == 999 || bpm_val < s_min_bpm) s_min_bpm = bpm_val;
            if (bpm_val > s_max_bpm) s_max_bpm = bpm_val;

            char min_max_buf[32];
            snprintf(min_max_buf, sizeof(min_max_buf), "Min/Max: %d/%d", s_min_bpm, s_max_bpm);
            lv_label_set_text(s_bpm_min_max_label, min_max_buf);
        } else {
            // Finger removed: reset extremes and text color
            lv_obj_set_style_text_color(s_bpm_value_label, lv_color_hex(0xffffff), LV_PART_MAIN);
            s_min_bpm = 999;
            s_max_bpm = -1;
            lv_label_set_text_static(s_bpm_min_max_label, "Min/Max: --/--");
        }
    }

    if (s_spo2_value_label) {
        char spo2_buf[16];
        get_flow_global_variable_text(FLOW_GLOBAL_VARIABLE_SPO2_VALUE, spo2_buf, sizeof(spo2_buf));
        
        if (strcmp(spo2_buf, lv_label_get_text(s_spo2_value_label)) != 0) {
            lv_label_set_text(s_spo2_value_label, spo2_buf);
        }

        int spo2_val = atoi(spo2_buf);
        if (spo2_val > 0) {
            // Alarm dynamic color warnings: red when below 95%
            if (spo2_val < 95) {
                lv_obj_set_style_text_color(s_spo2_value_label, lv_color_hex(0xff3333), LV_PART_MAIN);
            } else {
                lv_obj_set_style_text_color(s_spo2_value_label, lv_color_hex(0xffffff), LV_PART_MAIN);
            }
        } else {
            lv_obj_set_style_text_color(s_spo2_value_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        }
    }

    if (!s_chart) {
        s_chart = find_chart(objects.bpm);
        if (!s_chart) {
            s_chart = create_chart(objects.bpm);
        }
        configure_chart(s_chart);
    }

    if (!s_chart || !s_series) {
        return;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (bpm_service_get_mock_mode()) {
        uint16_t n = bpm_service_update(now_ms);
        for (uint16_t i = 0; i < n; i++) {
            int16_t val = bpm_service_get_sample(bpm_service_sample_count() - n + i);
            lv_chart_set_next_value(s_chart, s_series, val);
        }
    } else {
        int16_t buf[10];
        uint16_t n = bpm_service_read_samples(buf, 10);
        for (uint16_t i = 0; i < n; i++) {
            lv_chart_set_next_value(s_chart, s_series, buf[i]);
        }
    }
}

void bpm_chart_handler_init(void) {
    if (s_timer) {
        return;
    }
    s_timer = lv_timer_create(bpm_timer_cb, 100, NULL);
}
