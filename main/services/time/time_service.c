#include "time_service.h"
#include <time.h>
#include <sys/time.h>

static app_time_t current_time;

const char* DAY_NAMES_PL[] = {
    "Niedziela", "Poniedziałek", "Wtorek", "Środa", "Czwartek", "Piątek", "Sobota"
};

const char* MONTH_NAMES_PL[] = {
    "Styczeń", "Luty", "Marzec", "Kwiecień", "Maj", "Czerwiec", 
    "Lipiec", "Sierpień", "Wrzesień", "Październik", "Listopad", "Grudzień"
};

void time_service_init(int h, int m, int s) {
    current_time.hours = h;
    current_time.minutes = m;
    current_time.seconds = s;
}

void time_service_tick() {
    if (current_time.seconds > 0) {
        current_time.seconds--;
    } else {
        if (current_time.minutes > 0) {
            current_time.minutes--;
            current_time.seconds = 59;
        } else {
            if (current_time.hours > 0) {
                current_time.hours--;
                current_time.minutes = 59;
                current_time.seconds = 59;
            }
        }
    }
}

app_time_t time_service_get() {
    return current_time;
}

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
