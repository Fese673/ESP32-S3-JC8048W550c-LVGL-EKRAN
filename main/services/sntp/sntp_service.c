#include "sntp_service.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>


bool sntp_service_is_synced(void) {
    time_t now;
    time(&now);
    // Jeśli czas systemowy jest większy niż rok 2000 (około 1 mld sekund), to znaczy, że mamy synchronizację.
    // Domyślny czas (bez sync) to 1970 rok (okolice 0-100 sekund).
    return now > 1000000000;
}
