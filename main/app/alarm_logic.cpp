#include "alarm_logic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static alarm_t s_alarms[ALARM_MAX_COUNT];
static int s_alarm_count = 0;
static SemaphoreHandle_t s_alarm_mutex = NULL;

#define LOCK()   if (s_alarm_mutex) { xSemaphoreTake(s_alarm_mutex, portMAX_DELAY); }
#define UNLOCK() if (s_alarm_mutex) { xSemaphoreGive(s_alarm_mutex); }

void alarm_logic_init(void) {
    if (s_alarm_mutex == NULL) {
        s_alarm_mutex = xSemaphoreCreateMutex();
    }
    LOCK();
    // Inicjalizacja z jednym domyślnym budzikiem (07:30, Pon/Śr/Pt) do testów UI
    s_alarm_count = 1;
    s_alarms[0].enabled = true;
    s_alarms[0].hour = 7;
    s_alarms[0].minute = 30;
    memset(s_alarms[0].days, 0, sizeof(s_alarms[0].days));
    s_alarms[0].days[0] = true; // Pon
    s_alarms[0].days[2] = true; // Śr
    s_alarms[0].days[4] = true; // Pt
    UNLOCK();
}

int alarm_get_count(void) {
    int count = 0;
    LOCK();
    count = s_alarm_count;
    UNLOCK();
    return count;
}

bool alarm_get(int index, alarm_t *out_alarm) {
    bool success = false;
    LOCK();
    if (index >= 0 && index < s_alarm_count && out_alarm) {
        *out_alarm = s_alarms[index];
        success = true;
    }
    UNLOCK();
    return success;
}

bool alarm_add(int hour, int minute, const bool days[7]) {
    bool success = false;
    LOCK();
    if (s_alarm_count < ALARM_MAX_COUNT) {
        alarm_t *a = &s_alarms[s_alarm_count];
        a->enabled = true;
        a->hour = hour;
        a->minute = minute;
        if (days) {
            memcpy(a->days, days, sizeof(a->days));
        } else {
            memset(a->days, 0, sizeof(a->days));
        }
        s_alarm_count++;
        success = true;
    }
    UNLOCK();
    return success;
}

bool alarm_edit(int index, int hour, int minute, const bool days[7]) {
    bool success = false;
    LOCK();
    if (index >= 0 && index < s_alarm_count) {
        alarm_t *a = &s_alarms[index];
        a->hour = hour;
        a->minute = minute;
        if (days) {
            memcpy(a->days, days, sizeof(a->days));
        }
        success = true;
    }
    UNLOCK();
    return success;
}

bool alarm_delete(int index) {
    bool success = false;
    LOCK();
    if (index >= 0 && index < s_alarm_count) {
        for (int i = index; i < s_alarm_count - 1; i++) {
            s_alarms[i] = s_alarms[i + 1];
        }
        s_alarm_count--;
        success = true;
    }
    UNLOCK();
    return success;
}

bool alarm_toggle(int index, bool enabled) {
    bool success = false;
    LOCK();
    if (index >= 0 && index < s_alarm_count) {
        s_alarms[index].enabled = enabled;
        success = true;
    }
    UNLOCK();
    return success;
}
