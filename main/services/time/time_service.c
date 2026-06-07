#include "time_service.h"
#include <time.h>
#include <sys/time.h>

const char* DAY_NAMES_PL[] = {
    "Niedziela", "Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota"
};

const char* MONTH_NAMES_PL[] = {
    "Styczeń", "Luty", "Marzec", "Kwiecień", "Maj", "Czerwiec", 
    "Lipiec", "Sierpień", "Wrzesień", "Październik", "Listopad", "Grudzień"
};
app_time_t time_service_get_system_time() {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    app_time_t t;
    t.hours = timeinfo.tm_hour;
    t.minutes = timeinfo.tm_min;
    t.seconds = timeinfo.tm_sec;
    t.day = timeinfo.tm_mday;
    t.month = timeinfo.tm_mon; // 0-11
    t.year = timeinfo.tm_year + 1900;
    t.day_of_week = timeinfo.tm_wday; // 0-6

    return t;
}
