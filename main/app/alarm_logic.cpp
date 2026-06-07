#include "alarm_logic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
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
    // Pusta lista — dane przyjdą z Zegara przez UART
    s_alarm_count = 0;
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
        a->single_shot = false;
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

void alarm_sync_from_list(const alarm_t *list, int count) {
    if (!list) return;
    if (count > ALARM_MAX_COUNT) count = ALARM_MAX_COUNT;
    LOCK();
    s_alarm_count = count;
    for (int i = 0; i < count; ++i) {
        s_alarms[i] = list[i];
    }
    UNLOCK();
}

int alarm_get_all(alarm_t *out_list, int max_count) {
    if (!out_list) return 0;
    LOCK();
    int cnt = s_alarm_count;
    int n = (cnt < max_count) ? cnt : max_count;
    for (int i = 0; i < n; ++i) {
        out_list[i] = s_alarms[i];
    }
    UNLOCK();
    return n;
}
