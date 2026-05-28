#include "clock_logic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time_service.h"
#include "sntp_service.h"
#include "wifi_service.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "screens.h"
#include "eez-flow.h"
#include "vars.h"
#include <stdio.h>

// Persistent buffers dla światowych stref czasowych
// Używamy lv_label_set_text_static() z pominięciem safe_label.h
// (bufory w .bss, nigdy nie znikają, LVGL nie kopiuje)
#undef lv_label_set_text
#undef lv_label_set_text_static

static char s_usa_hour[4];
static char s_usa_min[4];
static char s_japan_hour[4];
static char s_japan_min[4];
static char s_russia_hour[4];
static char s_russia_min[4];

static void update_world_clocks(const app_time_t *t) {
    // 1. USA (Nowy Jork: UTC-5 / DST)
    int h = (t->hours - 5 + 24) % 24;
    snprintf(s_usa_hour, sizeof(s_usa_hour), "%02d", h);
    snprintf(s_usa_min,  sizeof(s_usa_min),  "%02d", t->minutes);
    lv_label_set_text_static(objects.hour_text_23,  s_usa_hour);
    lv_label_set_text_static(objects.minutes_text_23, s_usa_min);

    // 2. Japonia (Tokio: UTC+9)
    h = (t->hours + 9) % 24;
    snprintf(s_japan_hour, sizeof(s_japan_hour), "%02d", h);
    snprintf(s_japan_min,  sizeof(s_japan_min),  "%02d", t->minutes);
    lv_label_set_text_static(objects.hour_text_24,  s_japan_hour);
    lv_label_set_text_static(objects.minutes_text_24, s_japan_min);

    // 3. Rosja (Moskwa: UTC+3)
    h = (t->hours + 3) % 24;
    snprintf(s_russia_hour, sizeof(s_russia_hour), "%02d", h);
    snprintf(s_russia_min,  sizeof(s_russia_min),  "%02d", t->minutes);
    lv_label_set_text_static(objects.hour_text_25,  s_russia_hour);
    lv_label_set_text_static(objects.minutes_text_25, s_russia_min);
}

static void clear_world_clocks(void) {
    lv_label_set_text_static(objects.minutes_text_23, "--");
    lv_label_set_text_static(objects.hour_text_23,     "--");
    lv_label_set_text_static(objects.minutes_text_24, "--");
    lv_label_set_text_static(objects.hour_text_24,     "--");
    lv_label_set_text_static(objects.minutes_text_25, "--");
    lv_label_set_text_static(objects.hour_text_25,     "--");
}

static void clock_task(void *arg) {
    (void)arg;
    
    static char hour_buf[4];
    static char min_buf[4];
    static char sec_buf[4];
    static char day_num_buf[4];
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        if (lvgl_port_lock(pdMS_TO_TICKS(20))) {
            if (sntp_service_is_synced()) {
                app_time_t t = time_service_get_system_time();

                snprintf(hour_buf, sizeof(hour_buf), "%02d", t.hours);
                snprintf(min_buf, sizeof(min_buf), "%02d", t.minutes);
                snprintf(sec_buf, sizeof(sec_buf), "%02d", t.seconds);
                snprintf(day_num_buf, sizeof(day_num_buf), "%d", t.day);

                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HOUR_TIME, eez::Value(hour_buf));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MINUTES_TIME, eez::Value(min_buf));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SECONDS_TIME, eez::Value(sec_buf));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NUMBER_DAY_DATE, eez::Value(day_num_buf));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_DAY_DATE, eez::Value(DAY_NAMES_PL[t.day_of_week]));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_MONTH_DATE, eez::Value(MONTH_NAMES_PL[t.month]));

                if (eez_flow_get_current_screen() == SCREEN_ID_ZEGAR) {
                    update_world_clocks(&t);
                }
            } else {
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HOUR_TIME, eez::Value("--"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MINUTES_TIME, eez::Value("--"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SECONDS_TIME, eez::Value("--"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NUMBER_DAY_DATE, eez::Value("--"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_DAY_DATE, eez::Value("---"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_MONTH_DATE, eez::Value("---"));

                if (eez_flow_get_current_screen() == SCREEN_ID_ZEGAR) {
                    clear_world_clocks();
                }
            }

            lvgl_port_unlock();
        }
    }
}

void clock_logic_start() {
    time_service_init(12, 0, 0);
    xTaskCreatePinnedToCore(clock_task, "clock_task", 4096, NULL, 3, NULL, 0);
}

extern "C" {
lv_obj_t * get_usa_hour_label(void) {
    return objects.hour_text_23;
}
lv_obj_t * get_japan_hour_label(void) {
    return objects.hour_text_24;
}
lv_obj_t * get_russia_hour_label(void) {
    return objects.hour_text_25;
}
}

