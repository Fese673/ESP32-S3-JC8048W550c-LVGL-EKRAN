#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_SCAN_RESULTS 20
#define WIFI_CONNECT_TIMEOUT_MS 15000

extern volatile bool g_wifi_nvs_save_complete;

typedef enum {
    WIFI_IDLE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_DISCONNECTED,
    WIFI_CONNECT_FAILED
} wifi_connect_state_t;

typedef struct {
    char ssid[33];
    int8_t rssi;
    uint8_t authmode;
} wifi_ap_list_item_t;

typedef void (*wifi_connect_callback_t)(wifi_connect_state_t state, const char *ssid, int8_t rssi);

void wifi_service_init(void);
bool wifi_service_is_connected(void);
wifi_connect_state_t wifi_service_get_state(void);
esp_err_t wifi_service_scan_start(void);
bool wifi_service_is_scan_done(void);
uint16_t wifi_service_get_scan_results(wifi_ap_list_item_t *results, uint16_t max_results);
esp_err_t wifi_service_connect(const char* ssid, const char* password);
void wifi_service_disconnect(void);
const char* wifi_service_get_signal_desc(int8_t rssi);
void wifi_service_set_connect_callback(wifi_connect_callback_t cb);

#ifdef __cplusplus
}
#endif

#endif
