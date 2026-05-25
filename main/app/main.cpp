#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "clock_logic.h"
#include "notification_logic.h"
#include "stopwatch_ui.h"
#include "timer_ui.h"
#include "communication/CommunicationProtocol.h"
#include "wifi_service.h"
#include "bpm_chart_handler.h"
#include "sntp_service.h"
#include "flappy_bird.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "ui.h"
#include "log.h"
#include "utils/comm_log/comm_log.h"
#include <stdatomic.h>

extern "C" {
#include "sunton_esp32_8048s050c.h"
void music_ui_handler_init(void);
}

extern "C" void get_flow_global_variable_text(uint32_t var_id, char *buf, size_t buf_size) {
    eez::Value val = eez::flow::getGlobalVariable(var_id);
    val.toText(buf, buf_size);
}

#define TAG "main"

#include "screens.h"

#include "set_wifi.h"

static void my_dropdown_event_cb(lv_event_t * e) {
    lv_obj_t * dropdown = (lv_obj_t *)lv_event_get_target(e);
    uint32_t selected = lv_dropdown_get_selected(dropdown);

    CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
    CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, (int)selected);
    CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, (int)selected);
}

static void init_custom_dropdown_hook() {
    static bool s_dropdown_hook_inited = false;

    if (objects.settings_melody_dropdown) {
        CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
        lv_dropdown_set_selected(objects.settings_melody_dropdown, ds.alarmMelodyIndex);

        // Rejestracja callbacka TYLKO RAZ - dropdown nie jest niszczony między ekranami
        if (!s_dropdown_hook_inited) {
            lv_obj_add_event_cb(objects.settings_melody_dropdown, my_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
            s_dropdown_hook_inited = true;
        }
    }
}

static void ui_tick_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (atomic_exchange(&g_wifi_nvs_save_complete, false)) {
        lv_obj_invalidate(lv_scr_act());
    }

    static int16_t last_screen_id = -1;
    int16_t current_screen_id = eez_flow_get_current_screen();

    // Logika przełączania pogody (IN/OUT) co 10 sekund
    static uint32_t last_weather_toggle = 0;
    static bool show_weather_in = true;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

    if (current_screen_id == SCREEN_ID_MAIN) {
        if (now - last_weather_toggle >= 10000 || last_screen_id != current_screen_id) {
            if (now - last_weather_toggle >= 10000) {
                show_weather_in = !show_weather_in;
                last_weather_toggle = now;
            }

            if (show_weather_in) {
                if (objects.weather_in) lv_obj_remove_flag(objects.weather_in, LV_OBJ_FLAG_HIDDEN);
                if (objects.weather_in_label) lv_obj_remove_flag(objects.weather_in_label, LV_OBJ_FLAG_HIDDEN);
                if (objects.weather_out) lv_obj_add_flag(objects.weather_out, LV_OBJ_FLAG_HIDDEN);
                if (objects.weather_out_label) lv_obj_add_flag(objects.weather_out_label, LV_OBJ_FLAG_HIDDEN);
            } else {
                if (objects.weather_in) lv_obj_add_flag(objects.weather_in, LV_OBJ_FLAG_HIDDEN);
                if (objects.weather_in_label) lv_obj_add_flag(objects.weather_in_label, LV_OBJ_FLAG_HIDDEN);
                if (objects.weather_out) lv_obj_remove_flag(objects.weather_out, LV_OBJ_FLAG_HIDDEN);
                if (objects.weather_out_label) lv_obj_remove_flag(objects.weather_out_label, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    if (current_screen_id != last_screen_id) {
        // Pauzuj timery przy opuszczeniu ekranu WiFi
        if (last_screen_id == SCREEN_ID_USTAWIENIA_WIFI) {
            wifi_timers_pause();
        }
        if (last_screen_id == SCREEN_ID_GRY_TEST) {
            flappy_bird_cleanup();
        }

        last_screen_id = current_screen_id;

        if (current_screen_id == SCREEN_ID_MENU || current_screen_id == SCREEN_ID_USTAWIENIA) {
            CommunicationProtocol::requestDeviceSettings();
            CommunicationProtocol::requestSystemResources();
        }
        if (current_screen_id == SCREEN_ID_USTAWIENIA) {
            init_custom_dropdown_hook();
        }
        if (current_screen_id == SCREEN_ID_LOGI) {
            log_clear();
        }
        if (current_screen_id == SCREEN_ID_USTAWIENIA_WIFI) {
            wifi_screen_init();
        }
        if (current_screen_id == SCREEN_ID_GRY_TEST) {
            flappy_bird_init(objects.gry_test);
        }
    }

    if (current_screen_id == SCREEN_ID_LOGI) {
        log_update_textarea();
        comm_log_flush_if_needed();
    }

    int64_t start_us = esp_timer_get_time();
    log_ui_tick_begin(current_screen_id);
    ui_tick();
    stopwatch_ui_sync_start_stop_label();
    timer_ui_sync_start_stop_label();
    notification_logic_refresh();
    CommunicationProtocol::updateUi();

    log_ui_tick_end(current_screen_id, esp_timer_get_time() - start_us);
}

extern "C" void app_main(void)
{
    log_init();
    comm_log_init();

    wifi_service_init();
    // SNTP is started automatically by wifi_service when GOT_IP is received

    sunton_esp32s3_backlight_init();

    lv_display_t *disp = sunton_esp32s3_lcd_init();
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }

    i2c_master_bus_handle_t i2c_master = sunton_esp32s3_i2c_master();

    gpio_set_direction(SUNTON_ESP32_TOUCH_PIN_INT, GPIO_MODE_OUTPUT);
    gpio_set_level(SUNTON_ESP32_TOUCH_PIN_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    sunton_esp32s3_touch_init(i2c_master);

    if (lvgl_port_lock(0)) {
        ui_init();
        notification_logic_init();
        CommunicationProtocol::begin();
        CommunicationProtocol::updateUi();
        stopwatch_ui_init();
        timer_ui_init();
        lv_timer_create(ui_tick_timer_cb, 100, NULL);
        bpm_chart_handler_init();
        music_ui_handler_init();
        lvgl_port_unlock();
        clock_logic_start();
    } else {
        ESP_LOGE(TAG, "Failed to lock LVGL during UI init");
        return;
    }

    ESP_LOGI(TAG, "Application started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
