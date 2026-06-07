#include "stopwatch_logic.h"
#include "esp_timer.h"

static stopwatch_state_t s_state = STOPWATCH_IDLE;
static int64_t s_start_time = 0;
static int64_t s_paused_elapsed = 0;

void stopwatch_start(void) {
    if (s_state == STOPWATCH_RUNNING) return;
    if (s_state == STOPWATCH_IDLE) {
        s_paused_elapsed = 0;
    }
    s_start_time = esp_timer_get_time();
    s_state = STOPWATCH_RUNNING;
}

void stopwatch_stop(void) {
    if (s_state != STOPWATCH_RUNNING) return;
    s_paused_elapsed += esp_timer_get_time() - s_start_time;
    s_state = STOPWATCH_PAUSED;
}

void stopwatch_reset(void) {
    s_state = STOPWATCH_IDLE;
    s_paused_elapsed = 0;
    s_start_time = 0;
}

stopwatch_state_t stopwatch_get_state(void) {
    return s_state;
}

int64_t stopwatch_get_elapsed_us(void) {
    if (s_state == STOPWATCH_IDLE) {
        return 0;
    }
    if (s_state == STOPWATCH_RUNNING) {
        return s_paused_elapsed + (esp_timer_get_time() - s_start_time);
    }
    return s_paused_elapsed;
}

void stopwatch_set_elapsed_us(int64_t elapsed_us) {
    if (elapsed_us < 0) elapsed_us = 0;
    if (s_state == STOPWATCH_RUNNING) {
        int64_t run_us = esp_timer_get_time() - s_start_time;
        int64_t new_paused = elapsed_us - run_us;
        s_paused_elapsed = (new_paused > 0) ? new_paused : 0;
    } else {
        s_paused_elapsed = elapsed_us;
    }
}
