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

static void clock_task(void *arg) {
    (void)arg;
    
    static char hour_buf[4];
    static char min_buf[4];
    static char sec_buf[4];
    static char day_num_buf[4];
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        if (lvgl_port_lock(0)) {
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

                // Światowe strefy czasowe
                if (eez_flow_get_current_screen() == SCREEN_ID_ZEGAR) {
                    void *fs = getFlowState(0, 7);
                    if (fs) {
                        char world_buf[4];
                        int usa_h = (t.hours - 5 + 24) % 24;
                        snprintf(world_buf, sizeof(world_buf), "%02d", usa_h);
                        assignStringProperty(fs, 68, 3, world_buf, "USA hour");

                        int japan_h = (t.hours + 9) % 24;
                        snprintf(world_buf, sizeof(world_buf), "%02d", japan_h);
                        assignStringProperty(fs, 72, 3, world_buf, "Japan hour");

                        int russia_h = (t.hours + 3) % 24;
                        snprintf(world_buf, sizeof(world_buf), "%02d", russia_h);
                        assignStringProperty(fs, 76, 3, world_buf, "Russia hour");
                    }
                }
            } else {
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HOUR_TIME, eez::Value("--"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MINUTES_TIME, eez::Value("--"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SECONDS_TIME, eez::Value("--"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NUMBER_DAY_DATE, eez::Value("--"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_DAY_DATE, eez::Value("---"));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_MONTH_DATE, eez::Value("---"));
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

