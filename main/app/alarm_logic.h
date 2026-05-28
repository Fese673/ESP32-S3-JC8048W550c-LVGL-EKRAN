#ifndef ALARM_LOGIC_H
#define ALARM_LOGIC_H

#include <stdbool.h>

#define ALARM_MAX_COUNT 5

typedef struct {
    bool enabled;
    int hour;
    int minute;
    bool days[7]; // 0: Pon, 1: Wt, 2: Śr, 3: Cz, 4: Pt, 5: So, 6: Nd
} alarm_t;

#ifdef __cplusplus
extern "C" {
#endif

void alarm_logic_init(void);
int alarm_get_count(void);
bool alarm_get(int index, alarm_t *out_alarm);
bool alarm_add(int hour, int minute, const bool days[7]);
bool alarm_edit(int index, int hour, int minute, const bool days[7]);
bool alarm_delete(int index);
bool alarm_toggle(int index, bool enabled);

#ifdef __cplusplus
}
#endif

#endif // ALARM_LOGIC_H
