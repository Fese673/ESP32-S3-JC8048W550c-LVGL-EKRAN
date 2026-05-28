#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TIMER_IDLE,
    TIMER_RUNNING,
    TIMER_PAUSED
} timer_state_t;

void timer_start(void);
void timer_stop(void);
void timer_reset(void);

void timer_add_hours(int h);
void timer_add_minutes(int m);
void timer_add_seconds(int s);
void timer_set_time(int h, int m, int s);

timer_state_t timer_get_state(void);
int timer_get_remaining_seconds(void);
int timer_get_target_seconds(void);

/// Check expiry and transition RUNNING→IDLE. Returns true if timer just expired.
/// Must be called from the LVGL task (or under lvgl_port_lock) at ~100ms interval.
bool timer_tick(void);
