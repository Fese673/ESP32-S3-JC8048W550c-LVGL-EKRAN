#include "wifi_service.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sntp.h"
#include "esp_lvgl_port.h"
#include <string.h>
#include <time.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "WifiService";

/* Ujednolicone WiFi (V3.0): wlascicielem sieci jest ZEGAR ESP32.
 * Ekran jest TYLKO HMI — panel "Ustawienia WiFi" wysyla dane po kablu
 * (COBS 0x11), nie laczy sie samodzielnie i nie odpala SNTP. Czas plynie
 * z ZEGARA ramka 0x03 (co 1 s, co do sekundy) i ustawia zegar systemowy.
 * 1 = stare zachowanie (wlasne WiFi ekranu, tylko do debugowania). */
#ifndef WIFI_ENABLE
#define WIFI_ENABLE 0
#endif
#define WIFI_ENABLED WIFI_ENABLE

#define WIFI_WORKER_STACK 4096
#define WIFI_WORKER_PRIORITY 2
#define WIFI_WORKER_CORE 0
#define WIFI_NVS_SAVE_DELAY_MS 3000

typedef struct {
    char ssid[33];
    char password[65];
} wifi_work_msg_t;

static bool s_connected;
static bool s_scan_done;
static int s_retry_num;
static bool s_connect_requested;
static wifi_connect_state_t s_connect_state;
static char s_connecting_ssid[33];
static wifi_connect_callback_t s_connect_cb;
static TaskHandle_t s_worker_task;
static QueueHandle_t s_work_queue;
atomic_bool g_wifi_nvs_save_complete = false;
static volatile bool s_worker_running;
static volatile bool s_nvs_save_pending;
static bool s_sntp_started;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

#define LOCK()   portENTER_CRITICAL(&s_mux)
#define UNLOCK() portEXIT_CRITICAL(&s_mux)

static bool rd_bool(const bool *v) { bool r; LOCK(); r = *v; UNLOCK(); return r; }
static void wr_bool(bool *v, bool val) { LOCK(); *v = val; UNLOCK(); }
static int rd_int(const int *v) { int r; LOCK(); r = *v; UNLOCK(); return r; }
static void wr_int(int *v, int val) { LOCK(); *v = val; UNLOCK(); }
static wifi_connect_state_t rd_state(void) { wifi_connect_state_t r; LOCK(); r = s_connect_state; UNLOCK(); return r; }
static void wr_state(wifi_connect_state_t val) { LOCK(); s_connect_state = val; UNLOCK(); }

static void notify_callback(wifi_connect_state_t state, const char *ssid, int8_t rssi) {
    wifi_connect_callback_t cb;
    LOCK(); cb = s_connect_cb; UNLOCK();
    if (cb) cb(state, ssid ? ssid : "", rssi);
}

static void save_config_to_nvs(void) {
    wifi_config_t config;
    if (esp_wifi_get_config(WIFI_IF_STA, &config) != ESP_OK) {
        ESP_LOGE(TAG, "NVS save: get_config failed");
        return;
    }

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open("wifi", NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS save: open failed: %d", ret);
        return;
    }

    ret = nvs_set_blob(nvs, "sta", &config, sizeof(wifi_config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS save: set_blob failed: %d", ret);
        nvs_close(nvs);
        return;
    }

    ret = nvs_commit(nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS save: commit failed: %d", ret);
    } else {
        ESP_LOGI(TAG, "Config saved to NVS (key=wifi.sta)");
        atomic_store(&g_wifi_nvs_save_complete, true);
        lvgl_port_task_wake(LVGL_PORT_EVENT_DISPLAY, NULL);
    }
    nvs_close(nvs);
}

static void queue_saved_connect(void) {
    if (!s_work_queue || !s_worker_running) return;
    LOCK(); bool manual_pending = s_connect_requested; UNLOCK();
    if (manual_pending) return;

    wifi_config_t config;
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) return;
    size_t len = sizeof(config);
    esp_err_t ret = nvs_get_blob(nvs, "sta", &config, &len);
    nvs_close(nvs);
    if (ret != ESP_OK || len == 0 || !config.sta.ssid[0]) return;

    wifi_work_msg_t msg = {};
    strlcpy(msg.ssid, (char*)config.sta.ssid, sizeof(msg.ssid));
    if (config.sta.password[0]) {
        strlcpy(msg.password, (char*)config.sta.password, sizeof(msg.password));
    }
    ESP_LOGI(TAG, "Queuing auto-connect to %s", msg.ssid);

    LOCK();
    wr_bool(&s_connect_requested, true);
    wr_bool(&s_connected, false);
    wr_state(WIFI_CONNECTING);
    wr_int(&s_retry_num, 0);
    strlcpy(s_connecting_ssid, msg.ssid, sizeof(s_connecting_ssid));
    UNLOCK();

    wifi_work_msg_t dummy;
    xQueueReceive(s_work_queue, &dummy, 0);
    xQueueSend(s_work_queue, &msg, 0);
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        queue_saved_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wr_bool(&s_connected, false);

        if (rd_bool(&s_connect_requested)) {
            ESP_LOGI(TAG, "Disconnect during manual connect (harmless)");
        } else {
            int retry = rd_int(&s_retry_num);
            if (retry < 10) {
                esp_wifi_connect();
                wr_int(&s_retry_num, retry + 1);
                ESP_LOGI(TAG, "Auto-retry to AP (%d/10)", retry + 1);
            } else {
                ESP_LOGE(TAG, "Auto-connect failed after 10 retries");
                wr_state(WIFI_DISCONNECTED);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_ap_record_t ap_info;
        int8_t rssi = 0;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            rssi = ap_info.rssi;
        }
        wr_bool(&s_connected, true);
        wr_int(&s_retry_num, 0);
        wr_bool(&s_connect_requested, false);
        wr_state(WIFI_CONNECTED);
        if (!s_sntp_started) {
            s_sntp_started = true;
            ESP_LOGI(TAG, "Starting SNTP...");
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
            tzset();
            ESP_LOGI(TAG, "SNTP started");
        }
        s_nvs_save_pending = true;
        char ssid[33];
        LOCK(); strlcpy(ssid, s_connecting_ssid, sizeof(ssid)); UNLOCK();
        notify_callback(WIFI_CONNECTED, ssid, rssi);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        wr_bool(&s_scan_done, true);
        ESP_LOGI(TAG, "WiFi Scan Done");
    }
}

static void wifi_worker_task(void *pvParameters) {
    wifi_work_msg_t msg;
    ESP_LOGI(TAG, "Worker task started");

    while (s_worker_running) {
        if (xQueueReceive(s_work_queue, &msg, pdMS_TO_TICKS(WIFI_NVS_SAVE_DELAY_MS)) == pdTRUE) {
            ESP_LOGI(TAG, "Worker: connecting to %s", msg.ssid);

            wifi_config_t wifi_config = {};
            strlcpy((char*)wifi_config.sta.ssid, msg.ssid, sizeof(wifi_config.sta.ssid));
            if (strlen(msg.password) > 0) {
                strlcpy((char*)wifi_config.sta.password, msg.password, sizeof(wifi_config.sta.password));
            }

            esp_wifi_set_storage(WIFI_STORAGE_RAM);
            esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Worker: set_config failed: %d", ret);
                wr_bool(&s_connect_requested, false);
                wr_state(WIFI_CONNECT_FAILED);
                notify_callback(WIFI_CONNECT_FAILED, msg.ssid, -100);
                continue;
            }

            ret = esp_wifi_connect();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Worker: connect failed: %d", ret);
                wr_bool(&s_connect_requested, false);
                wr_state(WIFI_CONNECT_FAILED);
                notify_callback(WIFI_CONNECT_FAILED, msg.ssid, -100);
                continue;
            }
        }

        if (s_nvs_save_pending) {
            s_nvs_save_pending = false;
            save_config_to_nvs();
        }
    }
    vTaskDelete(NULL);
}

void wifi_service_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if WIFI_ENABLED
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    s_work_queue = xQueueCreate(1, sizeof(wifi_work_msg_t));
    if (!s_work_queue) { ESP_LOGE(TAG, "Failed to create queue"); return; }

    s_worker_running = true;
    if (xTaskCreatePinnedToCore(wifi_worker_task, "wifi_worker", WIFI_WORKER_STACK,
        NULL, WIFI_WORKER_PRIORITY, &s_worker_task, WIFI_WORKER_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create worker task");
        s_worker_running = false;
    }

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    queue_saved_connect();

    wr_state(WIFI_IDLE);
    ESP_LOGI(TAG, "wifi_init_sta started");
#else
    ESP_LOGW(TAG, "WiFi DISABLED (WIFI_ENABLED=0) — saving PSRAM");
#endif
}

bool wifi_service_is_connected(void) { return rd_bool(&s_connected); }
wifi_connect_state_t wifi_service_get_state(void) { return rd_state(); }

esp_err_t wifi_service_scan_start(void) {
#if !WIFI_ENABLED
    ESP_LOGW(TAG, "WiFi disabled, scan not available");
    return ESP_ERR_NOT_SUPPORTED;
#else
    wr_bool(&s_scan_done, false);
    wifi_scan_config_t scan_config = { .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false };
    return esp_wifi_scan_start(&scan_config, false);
#endif
}

bool wifi_service_is_scan_done(void) { return rd_bool(&s_scan_done); }

uint16_t wifi_service_get_scan_results(wifi_ap_list_item_t *results, uint16_t max_results) {
    uint16_t ap_count = 0;
    if (esp_wifi_scan_get_ap_num(&ap_count) != ESP_OK || ap_count == 0) return 0;

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_records) return 0;

    if (esp_wifi_scan_get_ap_records(&ap_count, ap_records) != ESP_OK) {
        free(ap_records); return 0;
    }

    uint16_t count = (ap_count < max_results) ? ap_count : max_results;
    for (int i = 0; i < count; i++) {
        strlcpy(results[i].ssid, (char *)ap_records[i].ssid, sizeof(results[i].ssid));
        results[i].rssi = ap_records[i].rssi;
        results[i].authmode = ap_records[i].authmode;
    }
    free(ap_records);
    return count;
}

esp_err_t wifi_service_connect(const char* ssid, const char* password) {
#if !WIFI_ENABLED
    ESP_LOGW(TAG, "WiFi disabled, connect not available");
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;
    if (!s_work_queue || !s_worker_running) return ESP_ERR_INVALID_STATE;

    wr_bool(&s_connect_requested, true);
    wr_bool(&s_connected, false);
    wr_state(WIFI_CONNECTING);
    wr_int(&s_retry_num, 0);
    LOCK(); strlcpy(s_connecting_ssid, ssid, sizeof(s_connecting_ssid)); UNLOCK();

    wifi_work_msg_t msg = {};
    strlcpy(msg.ssid, ssid, sizeof(msg.ssid));
    if (password) strlcpy(msg.password, password, sizeof(msg.password));

    wifi_work_msg_t dummy;
    xQueueReceive(s_work_queue, &dummy, 0);
    if (xQueueSend(s_work_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full");
        wr_bool(&s_connect_requested, false);
        wr_state(WIFI_CONNECT_FAILED);
        return ESP_FAIL;
    }
    return ESP_OK;
#endif
}

void wifi_service_disconnect(void) {
    wr_bool(&s_connect_requested, false);
    wr_int(&s_retry_num, 11);
    wr_state(WIFI_IDLE);
    wr_bool(&s_connected, false);
#if WIFI_ENABLED
    wifi_work_msg_t dummy;
    xQueueReceive(s_work_queue, &dummy, 0);
    esp_wifi_disconnect();
#endif
}

const char* wifi_service_get_signal_desc(int8_t rssi) {
    if (rssi >= -50) return "doskona\u0142a";
    if (rssi >= -60) return "bardzo dobra";
    if (rssi >= -70) return "dobra";
    if (rssi >= -80) return "s\u0142aba";
    return "bardzo s\u0142aba";
}

void wifi_service_set_connect_callback(wifi_connect_callback_t cb) {
    LOCK(); s_connect_cb = cb; UNLOCK();
}
