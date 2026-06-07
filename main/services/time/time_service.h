#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include <stdint.h>

typedef struct {
    int hours;
    int minutes;
    int seconds;
    int day;
    int month;
    int year;
    int day_of_week;
} app_time_t;

extern const char* DAY_NAMES_PL[];
extern const char* MONTH_NAMES_PL[];

#ifdef __cplusplus
extern "C" {
#endif

// Pobiera aktualny czas systemowy (z RTC/SNTP)
app_time_t time_service_get_system_time();

#ifdef __cplusplus
}
#endif

#endif // TIME_SERVICE_H
