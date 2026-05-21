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

timer_state_t timer_get_state(void);
int timer_get_remaining_seconds(void);
int timer_get_target_seconds(void);
