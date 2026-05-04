#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicjalizuje WiFi i próbuje się połączyć
void wifi_service_init(void);

// Sprawdza czy WiFi jest połączone
bool wifi_service_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_SERVICE_H
