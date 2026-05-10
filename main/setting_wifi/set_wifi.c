#include "set_wifi.h"
#include "ui/ui/screens.h"
#include "wifi_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static const char *TAG = "WIFI_SET";
static char selected_ssid[64] = "";
static lv_timer_t * scan_timer = NULL;
static lv_timer_t * results_poll_timer = NULL;
static lv_timer_t * connect_poll_timer = NULL;
static int64_t s_connect_start_us = 0;
static wifi_ap_list_item_t s_last_scan_results[MAX_SCAN_RESULTS];
static uint16_t s_last_scan_count = 0;
static bool s_wifi_callbacks_registered = false;
static bool s_connect_done_shown = false;
static char s_status_buf[128];

static void list_btn_event_handler(lv_event_t * e);
static void set_status(const char *fmt, ...) {
    if (!objects.wifi_status_labe) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_status_buf, sizeof(s_status_buf), fmt, args);
    va_end(args);
    lv_label_set_text_static(objects.wifi_status_labe, s_status_buf);
}

static int compare_rssi(const void * a, const void * b) {
    int ra = ((const wifi_ap_list_item_t*)a)->rssi;
    int rb = ((const wifi_ap_list_item_t*)b)->rssi;
    return (rb > ra) - (rb < ra);
}

static void refresh_wifi_list_ui(void) {
    if (!wifi_service_is_scan_done()) return;

    s_last_scan_count = wifi_service_get_scan_results(s_last_scan_results, MAX_SCAN_RESULTS);
    qsort(s_last_scan_results, s_last_scan_count, sizeof(wifi_ap_list_item_t), compare_rssi);

    if (objects.wifi_list) {
        lv_obj_clean(objects.wifi_list);
        for (int i = 0; i < s_last_scan_count; i++) {
            const char* symbol = (s_last_scan_results[i].authmode == 0) ? LV_SYMBOL_WIFI : LV_SYMBOL_SETTINGS;
            lv_obj_t * btn = lv_list_add_button(objects.wifi_list, symbol, s_last_scan_results[i].ssid);
            lv_obj_add_event_cb(btn, list_btn_event_handler, LV_EVENT_CLICKED, NULL);
        }
    }
    set_status("Skanowanie zako\u0144czone.");
    if (results_poll_timer) lv_timer_pause(results_poll_timer);
}

static void results_poll_cb(lv_timer_t * t) {
    if (wifi_service_is_scan_done()) {
        refresh_wifi_list_ui();
    }
}

static void scan_timer_cb(lv_timer_t * t) {
    ESP_LOGI(TAG, "Periodic scan start...");
    wifi_service_scan_start();
    if (results_poll_timer) lv_timer_resume(results_poll_timer);
    set_status("Od\u015bwie\u017canie listy...");
}

static void connect_poll_cb(lv_timer_t * t) {
    if (g_wifi_nvs_save_complete) {
        g_wifi_nvs_save_complete = false;
        lv_obj_invalidate(lv_scr_act());
    }

    wifi_connect_state_t state = wifi_service_get_state();

    if (state == WIFI_CONNECTED) {
        if (!s_connect_done_shown) {
            s_connect_done_shown = true;
            int8_t rssi = -100;
            for (int i = 0; i < s_last_scan_count; i++) {
                if (strcmp(s_last_scan_results[i].ssid, selected_ssid) == 0) {
                    rssi = s_last_scan_results[i].rssi;
                    break;
                }
            }
            set_status("Sukces! Po\u0142\u0105czy\u0142e\u015b si\u0119 z %s.\nJako\u015b\u0107 sygna\u0142u: %s.",
                       selected_ssid, wifi_service_get_signal_desc(rssi));
        }
        if (g_wifi_nvs_save_complete) {
            g_wifi_nvs_save_complete = false;
            lv_obj_invalidate(lv_scr_act());
            lv_timer_pause(connect_poll_timer);
        }
    } else if (state == WIFI_CONNECT_FAILED) {
        set_status("Nie uda\u0142o si\u0119 po\u0142\u0105czy\u0107 z %s.\nSprawd\u017a has\u0142o i zasi\u0119g sieci.",
                   selected_ssid);
        lv_timer_pause(connect_poll_timer);
    } else if (state == WIFI_CONNECTING) {
        int elapsed_ms = (int)((esp_timer_get_time() - s_connect_start_us) / 1000);
        int remaining_s = (WIFI_CONNECT_TIMEOUT_MS - elapsed_ms + 500) / 1000;
        if (remaining_s <= 0) {
            set_status("Timeout \u2013 nie uda\u0142o si\u0119 po\u0142\u0105czy\u0107 z %s.", selected_ssid);
            wifi_service_disconnect();
            lv_timer_pause(connect_poll_timer);
        } else {
            set_status("\u0141\u0105czenie z %s... (%d s)", selected_ssid, remaining_s);
        }
    } else {
        lv_timer_pause(connect_poll_timer);
    }
}

static void list_btn_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);

    const char * text = lv_list_get_button_text(objects.wifi_list, obj);
    if (!text) return;
    strncpy(selected_ssid, text, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';

    bool is_open = false;
    for (int i = 0; i < s_last_scan_count; i++) {
        if (strcmp(s_last_scan_results[i].ssid, selected_ssid) == 0) {
            if (s_last_scan_results[i].authmode == 0) is_open = true;
            break;
        }
    }

    if (objects.wifi_password_input) {
        lv_textarea_set_max_length(objects.wifi_password_input, 0);
        lv_textarea_set_text(objects.wifi_password_input, "");
        if (is_open) {
            lv_textarea_set_placeholder_text(objects.wifi_password_input, "Brak has\u0142a \u2014 sie\u0107 otwarta");
        } else {
            lv_textarea_set_placeholder_text(objects.wifi_password_input, "Has\u0142o...");
        }
    }
    set_status("Wybrano: %s", selected_ssid);
}

static void connect_btn_event_handler(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    if (strlen(selected_ssid) == 0) {
        set_status("B\u0141\u0104D: Wybierz sie\u0107!");
        return;
    }

    const char* pwd = lv_textarea_get_text(objects.wifi_password_input);
    esp_err_t ret = wifi_service_connect(selected_ssid, pwd);
    if (ret != ESP_OK) {
        set_status("B\u0142\u0105d po\u0142\u0105czenia: %d", (int)ret);
        return;
    }

    s_connect_start_us = esp_timer_get_time();
    if (connect_poll_timer) lv_timer_resume(connect_poll_timer);
    set_status("\u0141\u0105czenie z %s...", selected_ssid);
}

void wifi_screen_init(void) {
    ESP_LOGI(TAG, "Initializing WiFi screen...");
    if (!objects.wifi_list) return;

    if (objects.wifi_keyboard && objects.wifi_password_input) {
        lv_keyboard_set_textarea(objects.wifi_keyboard, objects.wifi_password_input);
        lv_textarea_set_max_length(objects.wifi_password_input, 0);
        lv_textarea_set_placeholder_text(objects.wifi_password_input, "Has\u0142o...");
    }

    if (!s_wifi_callbacks_registered) {
        if (objects.wifi_connect_btn) {
            lv_obj_add_event_cb(objects.wifi_connect_btn, connect_btn_event_handler, LV_EVENT_CLICKED, NULL);
        }
        s_wifi_callbacks_registered = true;
    }

    selected_ssid[0] = '\0';
    s_last_scan_count = 0;
    s_connect_done_shown = false;

    if (!results_poll_timer) {
        results_poll_timer = lv_timer_create(results_poll_cb, 500, NULL);
    }
    if (!scan_timer) {
        scan_timer = lv_timer_create(scan_timer_cb, 60000, NULL);
    }
    if (!connect_poll_timer) {
        connect_poll_timer = lv_timer_create(connect_poll_cb, 1000, NULL);
        lv_timer_pause(connect_poll_timer);
    }

    lv_timer_pause(connect_poll_timer);
    wifi_service_scan_start();
    lv_timer_resume(results_poll_timer);
    lv_timer_resume(scan_timer);
    set_status("Skanowanie sieci...");
}

void wifi_timers_pause(void) {
    if (results_poll_timer) lv_timer_pause(results_poll_timer);
    if (scan_timer) lv_timer_pause(scan_timer);
    if (connect_poll_timer) lv_timer_pause(connect_poll_timer);
}

const char* wifi_screen_get_selected_ssid(void) {
    return selected_ssid;
}
