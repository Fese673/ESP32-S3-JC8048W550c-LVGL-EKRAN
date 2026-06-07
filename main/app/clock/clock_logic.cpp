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
#include "clock_timezone_tiles.h"
#include <stdio.h>
#include <time.h>

// Shared state — producer: clock_task (Core 0), consumer: LVGL task (Core 1)
static volatile int s_pending = 0;
static int s_hours = 0;
static int s_minutes = 0;
static int s_seconds = 0;
static int s_day = 0;
static int s_day_of_week = 0;
static int s_month = 0;
static bool s_is_synced = false;

static void clock_task(void *arg) {
    (void)arg;

    int last_sec = -1;
    int last_min = -1;
    int last_hour = -1;
    int last_day = -1;
    int last_day_of_week = -1;
    int last_month = -1;
    bool last_synced = false;
    bool first_run = true;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));

        bool is_synced = sntp_service_is_synced();

        if (is_synced) {
            app_time_t t = time_service_get_system_time();

            if (first_run || !last_synced ||
                t.seconds != last_sec ||
                t.minutes != last_min ||
                t.hours != last_hour ||
                t.day != last_day ||
                t.month != last_month ||
                t.day_of_week != last_day_of_week) {

                s_hours = t.hours;
                s_minutes = t.minutes;
                s_seconds = t.seconds;
                s_day = t.day;
                s_day_of_week = t.day_of_week;
                s_month = t.month;
                s_is_synced = true;
                s_pending = 1;

                last_sec = t.seconds;
                last_min = t.minutes;
                last_hour = t.hours;
                last_day = t.day;
                last_day_of_week = t.day_of_week;
                last_month = t.month;
                last_synced = true;
                first_run = false;
            }
        } else {
            if (first_run || last_synced) {
                s_is_synced = false;
                s_pending = 1;

                last_synced = false;
                first_run = false;
            }
        }
    }
}

void clock_logic_start() {
    xTaskCreatePinnedToCore(clock_task, "clock_task", 4096, NULL, 3, NULL, 0);
}

// ─── LVGL task callback (called from ui_tick_timer_cb, Core 1) ────────

static void update_world_clocks_from_utc(void) {
    time_t now;
    time(&now);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);

    bool ny_dst = false;
    int m = tm_utc.tm_mon + 1;
    int d = tm_utc.tm_mday;
    int h = tm_utc.tm_hour;

    if (m > 3 && m < 11) {
        ny_dst = true;
    } else if (m == 3) {
        struct tm tm_march1;
        memset(&tm_march1, 0, sizeof(tm_march1));
        tm_march1.tm_year = tm_utc.tm_year;
        tm_march1.tm_mon = 2;
        tm_march1.tm_mday = 1;
        time_t t_march1 = mktime(&tm_march1);
        struct tm tm_temp;
        localtime_r(&t_march1, &tm_temp);
        int wday_march1 = tm_temp.tm_wday;
        int second_sunday = 1 + (7 - wday_march1) % 7 + 7;
        if (d > second_sunday) ny_dst = true;
        else if (d == second_sunday && h >= 7) ny_dst = true;
    } else if (m == 11) {
        struct tm tm_nov1;
        memset(&tm_nov1, 0, sizeof(tm_nov1));
        tm_nov1.tm_year = tm_utc.tm_year;
        tm_nov1.tm_mon = 10;
        tm_nov1.tm_mday = 1;
        time_t t_nov1 = mktime(&tm_nov1);
        struct tm tm_temp;
        localtime_r(&t_nov1, &tm_temp);
        int wday_nov1 = tm_temp.tm_wday;
        int first_sunday = 1 + (7 - wday_nov1) % 7;
        if (d < first_sunday) ny_dst = true;
        else if (d == first_sunday && h < 6) ny_dst = true;
    }

    int offset_ny = ny_dst ? -4 : -5;
    int h_ny = (tm_utc.tm_hour + offset_ny + 24) % 24;
    int h_jp = (tm_utc.tm_hour + 9) % 24;
    int h_ru = (tm_utc.tm_hour + 3) % 24;

    struct tm tm_local;
    localtime_r(&now, &tm_local);
    clock_timezone_tiles_update(true,
                                tm_local.tm_hour, tm_local.tm_min,
                                h_ny, tm_utc.tm_min,
                                h_jp, tm_utc.tm_min,
                                h_ru, tm_utc.tm_min);
}

void clock_ui_update(void) {
    if (eez_flow_get_current_screen() == SCREEN_ID_ZEGAR) {
        clock_timezone_tiles_init();
    }

    if (!s_pending) return;
    s_pending = 0;

    static char hour_buf[4];
    static char min_buf[4];
    static char sec_buf[4];
    static char day_num_buf[4];

    if (s_is_synced) {
        snprintf(hour_buf, sizeof(hour_buf), "%02d", s_hours);
        snprintf(min_buf, sizeof(min_buf), "%02d", s_minutes);
        snprintf(sec_buf, sizeof(sec_buf), "%02d", s_seconds);
        snprintf(day_num_buf, sizeof(day_num_buf), "%d", s_day);

        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HOUR_TIME, eez::Value(hour_buf));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MINUTES_TIME, eez::Value(min_buf));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SECONDS_TIME, eez::Value(sec_buf));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NUMBER_DAY_DATE, eez::Value(day_num_buf));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_DAY_DATE, eez::Value(DAY_NAMES_PL[s_day_of_week]));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_MONTH_DATE, eez::Value(MONTH_NAMES_PL[s_month]));

        if (eez_flow_get_current_screen() == SCREEN_ID_ZEGAR) {
            update_world_clocks_from_utc();
        }
    } else {
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HOUR_TIME, eez::Value("--"));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MINUTES_TIME, eez::Value("--"));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SECONDS_TIME, eez::Value("--"));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NUMBER_DAY_DATE, eez::Value("--"));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_DAY_DATE, eez::Value("---"));
        eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NAME_MONTH_DATE, eez::Value("---"));

        if (eez_flow_get_current_screen() == SCREEN_ID_ZEGAR) {
            clock_timezone_tiles_update(false, 0, 0, 0, 0, 0, 0, 0, 0);
        }
    }
}
