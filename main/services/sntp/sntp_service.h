#ifndef SNTP_SERVICE_H
#define SNTP_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sprawdza czy czas został już zsynchronizowany
bool sntp_service_is_synced(void);

#ifdef __cplusplus
}
#endif

#endif // SNTP_SERVICE_H
