#ifndef SNTP_SERVICE_H
#define SNTP_SERVICE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicjalizuje SNTP i ustawia strefę czasową
void sntp_service_init(void);

// Sprawdza czy czas został już zsynchronizowany
bool sntp_service_is_synced(void);

#ifdef __cplusplus
}
#endif

#endif // SNTP_SERVICE_H
