#include "bpm_chart_handler.h"
#include "bpm_service.h"
#include "screens.h"
#include "eez-flow.h"
#include "lvgl.h"
#include "esp_timer.h"

static lv_obj_t *s_chart = NULL;
static lv_chart_series_t *s_series = NULL;
static lv_timer_t *s_timer = NULL;

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
