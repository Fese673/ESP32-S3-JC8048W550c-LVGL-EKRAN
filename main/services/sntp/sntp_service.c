#include "sntp_service.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "SntpService";

void sntp_service_init(void) {
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Ustawienie strefy czasowej dla Polski (CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

bool sntp_service_is_synced(void) {
    time_t now;
    time(&now);
    // Jeśli czas systemowy jest większy niż rok 2000 (około 1 mld sekund), to znaczy, że mamy synchronizację.
    // Domyślny czas (bez sync) to 1970 rok (okolice 0-100 sekund).
    return now > 1000000000;
}
