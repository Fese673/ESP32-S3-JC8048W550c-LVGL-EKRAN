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
        
        app_time_t t;
        
        // Jeśli SNTP jest zsynchronizowane, używamy czasu systemowego
        if (sntp_service_is_synced()) {
            t = time_service_get_system_time();
        } else {
            // W przeciwnym razie licznik lokalny (boot time)
            time_service_tick();
            t = time_service_get();
        }
        
        // Przygotowanie buforów tekstowych
        snprintf(hour_buf, sizeof(hour_buf), "%02d", t.hours);
        snprintf(min_buf, sizeof(min_buf), "%02d", t.minutes);
        snprintf(sec_buf, sizeof(sec_buf), "%02d", t.seconds);
        snprintf(day_num_buf, sizeof(day_num_buf), "%d", t.day);
        
        if (lvgl_port_lock(0)) {
            // Podstawowy czas
            eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HOUR_TIME, eez::Value(hour_buf));
            eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MINUTES_TIME, eez::Value(min_buf));
            eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SECONDS_TIME, eez::Value(sec_buf));
            
            // Data (tylko jeśli mamy synchronizację SNTP)
            if (sntp_service_is_synced()) {
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NUMBER_DAY_DATE, eez::Value(day_num_buf));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_DAY_DATE, eez::Value(DAY_NAMES_PL[t.day_of_week]));
                eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_MONTH_DATE, eez::Value(MONTH_NAMES_PL[t.month]));
            }
            
            lvgl_port_unlock();
        }
    }
}

void clock_logic_start() {
    time_service_init(12, 0, 0);
    xTaskCreatePinnedToCore(clock_task, "clock_task", 4096, NULL, 3, NULL, 0);
}
