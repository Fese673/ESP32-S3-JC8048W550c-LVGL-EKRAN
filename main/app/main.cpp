#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "clock_logic.h"
#include "notification_logic.h"
#include "communication/CommunicationProtocol.h"
#include "wifi_service.h"
#include "sntp_service.h"
#include "driver/i2c_master.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "ui.h"
#include "log.h"

extern "C" {
#include "sunton_esp32_8048s050c.h"
}

#define TAG "main"

#include "screens.h"

static void my_dropdown_event_cb(lv_event_t * e) {
    lv_obj_t * dropdown = (lv_obj_t *)lv_event_get_target(e);
    uint32_t selected = lv_dropdown_get_selected(dropdown);

    CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
    CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, (int)selected);
    CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, (int)selected);
}

static void init_custom_dropdown_hook() {
    if (objects.ustawienia) {
        lv_obj_t *container = lv_obj_get_child(objects.ustawienia, 0);
        if (container) {
            lv_obj_t *dropdown = lv_obj_get_child(container, 6);
            if (dropdown) {
                CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
                lv_dropdown_set_selected(dropdown, ds.alarmMelodyIndex);
                lv_obj_add_event_cb(dropdown, my_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
            }
        }
    }
}

static void ui_tick_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    static int16_t last_screen_id = -1;
    int16_t current_screen_id = eez_flow_get_current_screen();

    if (current_screen_id != last_screen_id) {
        log_ui_snapshot("screen change", current_screen_id);
        last_screen_id = current_screen_id;

        if (current_screen_id == SCREEN_ID_MENU || current_screen_id == SCREEN_ID_USTAWIENIA) {
            CommunicationProtocol::requestDeviceSettings();
            CommunicationProtocol::requestSystemResources();
        }
        if (current_screen_id == SCREEN_ID_USTAWIENIA) {
            init_custom_dropdown_hook();
        }
    }

    int64_t start_us = esp_timer_get_time();
    log_ui_tick_begin(current_screen_id);
    ui_tick();
    notification_logic_refresh();
    CommunicationProtocol::updateUi();
    log_ui_tick_end(current_screen_id, esp_timer_get_time() - start_us);
}

static void ui_heartbeat_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    log_ui_snapshot("heartbeat", eez_flow_get_current_screen());
}

extern "C" void app_main(void)
{
    log_init();
    log_boot_snapshot("boot");

    wifi_service_init();
    sntp_service_init();

    sunton_esp32s3_backlight_init();

    lv_display_t *disp = sunton_esp32s3_lcd_init();
    if (disp == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }

    i2c_master_bus_handle_t i2c_master = sunton_esp32s3_i2c_master();
    sunton_esp32s3_touch_init(i2c_master);

    if (lvgl_port_lock(0)) {
        ui_init();
        notification_logic_init();
        CommunicationProtocol::begin();
        CommunicationProtocol::updateUi();
        log_ui_snapshot("ui_init done", eez_flow_get_current_screen());
        lv_timer_create(ui_tick_timer_cb, 100, NULL);
        lv_timer_create(ui_heartbeat_timer_cb, 1000, NULL);
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
