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
    (void)t;
    time_t now;
    time(&now);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc); // Get exact UTC time structure
    
    // 1. USA (New York: UTC-5 / DST is UTC-4)
    // DST in USA starts on the second Sunday of March and ends on the first Sunday of November.
    bool ny_dst = false;
    int month = tm_utc.tm_mon + 1; // 1-12
    int day = tm_utc.tm_mday;
    int hour = tm_utc.tm_hour; // UTC hour
    
    if (month > 3 && month < 11) {
        ny_dst = true;
    } else if (month == 3) {
        // Find day of the second Sunday in March
        struct tm tm_march1;
        memset(&tm_march1, 0, sizeof(tm_march1));
        tm_march1.tm_year = tm_utc.tm_year;
        tm_march1.tm_mon = 2; // March
        tm_march1.tm_mday = 1;
        time_t t_march1 = mktime(&tm_march1);
        struct tm tm_temp;
        localtime_r(&t_march1, &tm_temp);
        int wday_march1 = tm_temp.tm_wday; // 0=Sunday
        
        int second_sunday = 1 + (7 - wday_march1) % 7 + 7;
        
        if (day > second_sunday) {
            ny_dst = true;
        } else if (day == second_sunday) {
            // Starts at 02:00 local time (07:00 UTC under standard time)
            if (hour >= 7) {
                ny_dst = true;
            }
        }
    } else if (month == 11) {
        // Find day of the first Sunday in November
        struct tm tm_nov1;
        memset(&tm_nov1, 0, sizeof(tm_nov1));
        tm_nov1.tm_year = tm_utc.tm_year;
        tm_nov1.tm_mon = 10; // November
        tm_nov1.tm_mday = 1;
        time_t t_nov1 = mktime(&tm_nov1);
        struct tm tm_temp;
        localtime_r(&t_nov1, &tm_temp);
        int wday_nov1 = tm_temp.tm_wday;
        
        int first_sunday = 1 + (7 - wday_nov1) % 7;
        
        if (day < first_sunday) {
            ny_dst = true;
        } else if (day == first_sunday) {
            // Ends at 02:00 local time (01:00 EST / 02:00 EDT, i.e., 06:00 UTC)
            if (hour < 6) {
                ny_dst = true;
            }
        }
    }

    int offset_ny = ny_dst ? -4 : -5;
    int h_ny = (tm_utc.tm_hour + offset_ny + 24) % 24;
    snprintf(s_usa_hour, sizeof(s_usa_hour), "%02d", h_ny);
    snprintf(s_usa_min,  sizeof(s_usa_min),  "%02d", tm_utc.tm_min);
    lv_label_set_text_static(objects.hour_text_23,  s_usa_hour);
    lv_label_set_text_static(objects.minutes_text_23, s_usa_min);

    // 2. Japonia (Tokio: UTC+9, standard time only)
    int h_jp = (tm_utc.tm_hour + 9) % 24;
    snprintf(s_japan_hour, sizeof(s_japan_hour), "%02d", h_jp);
    snprintf(s_japan_min,  sizeof(s_japan_min),  "%02d", tm_utc.tm_min);
    lv_label_set_text_static(objects.hour_text_24,  s_japan_hour);
    lv_label_set_text_static(objects.minutes_text_24, s_japan_min);

    // 3. Rosja (Moskwa: UTC+3, standard time only)
    int h_ru = (tm_utc.tm_hour + 3) % 24;
    snprintf(s_russia_hour, sizeof(s_russia_hour), "%02d", h_ru);
    snprintf(s_russia_min,  sizeof(s_russia_min),  "%02d", tm_utc.tm_min);
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
    
    static int last_sec = -1;
    static int last_min = -1;
    static int last_hour = -1;
    static int last_day = -1;
    static int last_day_of_week = -1;
    static int last_month = -1;
    static bool last_synced = false;
    static bool first_run = true;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
        
        bool is_synced = sntp_service_is_synced();
        
        if (is_synced) {
            app_time_t t = time_service_get_system_time();
            
            // Only update UI if time components, sync status, or first run changes
            if (first_run || !last_synced || 
                t.seconds != last_sec || 
                t.minutes != last_min || 
                t.hours != last_hour || 
                t.day != last_day || 
                t.month != last_month || 
                t.day_of_week != last_day_of_week) {
                
                if (lvgl_port_lock(pdMS_TO_TICKS(20))) {
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
                    
                    lvgl_port_unlock();
                    
                    last_sec = t.seconds;
                    last_min = t.minutes;
                    last_hour = t.hours;
                    last_day = t.day;
                    last_day_of_week = t.day_of_week;
                    last_month = t.month;
                    last_synced = true;
                    first_run = false;
                }
            }
        } else {
            if (first_run || last_synced) {
                if (lvgl_port_lock(pdMS_TO_TICKS(20))) {
                    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HOUR_TIME, eez::Value("--"));
                    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MINUTES_TIME, eez::Value("--"));
                    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SECONDS_TIME, eez::Value("--"));
                    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NUMBER_DAY_DATE, eez::Value("--"));
                    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_DAY_DATE, eez::Value("---"));
                    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_MONTH_DATE, eez::Value("---"));

                    if (eez_flow_get_current_screen() == SCREEN_ID_ZEGAR) {
                        clear_world_clocks();
                    }
                    
                    lvgl_port_unlock();
                    
                    last_synced = false;
                    first_run = false;
                }
            }
        }
    }
}

void clock_logic_start() {
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

