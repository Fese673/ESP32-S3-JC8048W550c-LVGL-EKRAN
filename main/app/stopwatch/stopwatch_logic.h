#ifndef STOPWATCH_LOGIC_H
#define STOPWATCH_LOGIC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STOPWATCH_IDLE,
    STOPWATCH_RUNNING,
    STOPWATCH_PAUSED
} stopwatch_state_t;

void stopwatch_start(void);
void stopwatch_stop(void);
void stopwatch_reset(void);
stopwatch_state_t stopwatch_get_state(void);
int64_t stopwatch_get_elapsed_us(void);
void stopwatch_set_elapsed_us(int64_t elapsed_us);

#ifdef __cplusplus
}
#endif

#endif
