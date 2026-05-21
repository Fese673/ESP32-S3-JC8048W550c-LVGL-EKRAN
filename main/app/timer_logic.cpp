#include "timer_logic.h"
#include "esp_timer.h"

static timer_state_t s_state = TIMER_IDLE;
static int s_target_seconds = 0;
static int64_t s_end_time_us = 0;
static int s_paused_remaining = 0;

void timer_start(void) {
    if (s_state == TIMER_RUNNING) return;
    
    if (s_state == TIMER_IDLE) {
        if (s_target_seconds == 0) {
            s_target_seconds = 60;
        }
        s_paused_remaining = s_target_seconds;
    }
    
    s_end_time_us = esp_timer_get_time() + ((int64_t)s_paused_remaining * 1000000ULL);
    s_state = TIMER_RUNNING;
}

void timer_stop(void) {
    if (s_state != TIMER_RUNNING) return;
    
    int64_t now = esp_timer_get_time();
    int64_t rem = s_end_time_us - now;
    if (rem < 0) rem = 0;
    
    s_paused_remaining = (int)(rem / 1000000ULL);
    s_state = TIMER_PAUSED;
}

void timer_reset(void) {
    s_state = TIMER_IDLE;
    s_paused_remaining = 0;
    s_end_time_us = 0;
    // We do not reset target_seconds immediately so user can reuse the same time easily
}

void timer_add_hours(int h) {
    if (s_state != TIMER_IDLE) return;
    int cur_h = s_target_seconds / 3600;
    int cur_m = (s_target_seconds % 3600) / 60;
    int cur_s = s_target_seconds % 60;
    cur_h = (cur_h + h + 100) % 100; // max 99 hours
    s_target_seconds = cur_h * 3600 + cur_m * 60 + cur_s;
}

void timer_add_minutes(int m) {
    if (s_state != TIMER_IDLE) return;
    int cur_h = s_target_seconds / 3600;
    int cur_m = (s_target_seconds % 3600) / 60;
    int cur_s = s_target_seconds % 60;
    cur_m = (cur_m + m + 60) % 60;
    s_target_seconds = cur_h * 3600 + cur_m * 60 + cur_s;
}

void timer_add_seconds(int s) {
    if (s_state != TIMER_IDLE) return;
    int cur_h = s_target_seconds / 3600;
    int cur_m = (s_target_seconds % 3600) / 60;
    int cur_s = s_target_seconds % 60;
    cur_s = (cur_s + s + 60) % 60;
    s_target_seconds = cur_h * 3600 + cur_m * 60 + cur_s;
}

timer_state_t timer_get_state(void) {
    return s_state;
}

int timer_get_remaining_seconds(void) {
    if (s_state == TIMER_IDLE) {
        return s_target_seconds;
    }
    if (s_state == TIMER_RUNNING) {
        int64_t now = esp_timer_get_time();
        int64_t rem = s_end_time_us - now;
        if (rem < 0) {
            s_state = TIMER_IDLE; // timer finished
            // Todo: trigger buzzer/alarm
            return 0;
        }
        return (int)(rem / 1000000ULL);
    }
    return s_paused_remaining;
}

int timer_get_target_seconds(void) {
    return s_target_seconds;
}
