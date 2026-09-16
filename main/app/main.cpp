#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "clock/clock_logic.h"
#include "notification/notification_logic.h"
#include "stopwatch/stopwatch_ui.h"
#include "timer/timer_ui.h"
#include "alarm_ui.h"
#include "alarm/alarm_popup.h"
#include "main_handler.h"
#include "communication/CommunicationProtocol.h"
#include "communication/CommUiMapper.h"
#include "wifi_service.h"
#include "bpm_chart_handler.h"
#include "weather/weather_icon_handler.h"
#include "sntp_service.h"
#include "flappy_bird.h"
#include "services/sensor/sensors_wiki.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "lvgl.h"
#include "zasoby_sliders.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "ui.h"
#include "log.h"
#include "utils/comm_log/comm_log.h"
#include <stdatomic.h>
#include "nvs.h"

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
#include "gesture.h"

#include "set_wifi.h"
#include "diagnostics/lv_perf_diag.h"

static void my_dropdown_event_cb(lv_event_t * e) {
    lv_obj_t * dropdown = (lv_obj_t *)lv_event_get_target(e);
    uint32_t selected = lv_dropdown_get_selected(dropdown);

    CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
    CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, (int)selected, ds.sevenSegBrightness);
    CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, (int)selected, ds.sevenSegBrightness);
}

/* Etap 3: callback dropdownu jasnosci 7-seg. Indeks 0..6 mapowany
 * na wartosc 100/75/50/25/10/5/2% (helper w CommUiMapper). Wysyla
 * 7-bajtowy kTypeSetSettings z bajtem [6] = sevenSegBrightness. */
static void brightness_7seg_dropdown_cb(lv_event_t * e) {
    lv_obj_t * dropdown = (lv_obj_t *)lv_event_get_target(e);
    uint32_t selected = lv_dropdown_get_selected(dropdown);
    if (selected > 6U) return;   /* 7 opcji (0..6) */

    uint8_t brightness = CommunicationProtocol::dropdownIndexToBrightness((uint8_t)selected);
    CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
    CommunicationProtocol::updateLocalSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, brightness);
    CommunicationProtocol::sendDeviceSettings(ds.buzzerEnabled, ds.mqttEnabled, ds.touchTestEnabled, ds.backgroundMusicEnabled, ds.pmsEnabled, ds.alarmMelodyIndex, brightness);
}

static void style_premium_switch(lv_obj_t *sw) {
    if (!sw) return;

    // 1. Tło switcha wyłączonego (MAIN) - ciemnografitowe
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x28252c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    
    // Usuwamy outline/border z domyślnego wygenerowanego szablonu EEZ
    lv_obj_set_style_outline_width(sw, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN);
    
    // 2. Tło switcha włączonego (INDICATOR w stanie CHECKED) - stonowany szary
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x85808a), (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, (lv_style_selector_t)((uint32_t)LV_PART_INDICATOR | (uint32_t)LV_STATE_CHECKED));
    
    // 3. Suwak / Gałka (KNOB) - biała z ramką
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(sw, lv_color_hex(0x7a7580), LV_PART_KNOB);
    lv_obj_set_style_border_width(sw, 1, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -1, LV_PART_KNOB);
}

static void style_system_slider(lv_obj_t *slider) {
    if (!slider) return;

    // 1. Tło suwaka głównego (MAIN) - ciemnografitowe
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x28252c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(slider, 0, LV_PART_MAIN);

    // 2. Tło aktywnego paska postępu (INDICATOR) - stonowany szary premium
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x85808a), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(slider, 0, LV_PART_INDICATOR);

    // 3. Gałka (KNOB) - ukrywamy wskaźniki dotykowe
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
}

static void init_custom_dropdown_hook() {
    static bool s_dropdown_hook_inited = false;
    static bool s_brightness_dropdown_hook_inited = false;

    // Aplikowanie stylistyki premium na przełączniki ekranu Ustawienia
    style_premium_switch(objects.obj12);
    style_premium_switch(objects.obj13);
    style_premium_switch(objects.obj14);
    style_premium_switch(objects.obj15);
    style_premium_switch(objects.obj16);

    if (objects.settings_melody_dropdown) {
        CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
        lv_dropdown_set_selected(objects.settings_melody_dropdown, ds.alarmMelodyIndex);

        // Rejestracja callbacka TYLKO RAZ - dropdown nie jest niszczony między ekranami
        if (!s_dropdown_hook_inited) {
            lv_obj_add_event_cb(objects.settings_melody_dropdown, my_dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
            s_dropdown_hook_inited = true;
        }
    }

    /* Etap 3: dropdown jasnosci 7-seg. Sync z currentSettings (zeby po
     * wejsciu na ekran USTAWIENIA od razu byl aktualny stan z ZEGAR,
     * nie stale pierwszy element listy). */
    if (objects.settings_jasnosc_7seg) {
        CommunicationProtocol::DeviceSettings ds = CommunicationProtocol::currentSettings();
        uint8_t idx = CommunicationProtocol::brightnessToDropdownIndex(ds.sevenSegBrightness);
        lv_dropdown_set_selected(objects.settings_jasnosc_7seg, idx);

        // Rejestracja callbacka TYLKO RAZ
        if (!s_brightness_dropdown_hook_inited) {
            lv_obj_add_event_cb(objects.settings_jasnosc_7seg, brightness_7seg_dropdown_cb, LV_EVENT_VALUE_CHANGED, NULL);
            s_brightness_dropdown_hook_inited = true;
        }
    }
}

static void style_widgets_recursive(lv_obj_t * obj) {
    if (!obj) return;

    if (lv_obj_check_type(obj, &lv_switch_class)) {
        style_premium_switch(obj);
    } else if (lv_obj_check_type(obj, &lv_slider_class)) {
        style_system_slider(obj);
    }

    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        style_widgets_recursive(lv_obj_get_child(obj, i));
    }
}

static void ui_tick_timer_cb(lv_timer_t *timer)
{
    lv_perf_diag_tick_begin();

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
        lv_perf_diag_screen_change(last_screen_id, current_screen_id);

        // Pauzuj timery przy opuszczeniu ekranu WiFi
        if (last_screen_id == SCREEN_ID_USTAWIENIA_WIFI) {
            wifi_timers_pause();
        }
        if (last_screen_id == SCREEN_ID_GRY_TEST) {
            flappy_bird_cleanup();
        }
        if (last_screen_id == SCREEN_ID_CZUJNIKI) {
            sensors_wiki_deinit();
        }

        last_screen_id = current_screen_id;

        // Apply premium styling to all switches & sliders globally on the newly loaded screen
        style_widgets_recursive(lv_scr_act());

        // Dynamiczna rejestracja gestu Swipe Up na każdym załadowanym subekranie
        if (current_screen_id != SCREEN_ID_MAIN) {
            ESP_LOGI(TAG, "Zarejestrowano gesty Swipe Up na aktywnym ekranie ID: %d", (int)current_screen_id);
            gesture_register_recursive(lv_scr_act());
        }

        if (current_screen_id == SCREEN_ID_MENU || current_screen_id == SCREEN_ID_USTAWIENIA) {
            CommunicationProtocol::requestDeviceSettings();
            CommunicationProtocol::requestSystemResources();
        }
        if (current_screen_id == SCREEN_ID_USTAWIENIA) {
            init_custom_dropdown_hook();
        }
        if (current_screen_id == SCREEN_ID_ZEGAR) {
            ESP_LOGI(TAG, "Entering ZEGAR screen, requesting alarm list from Zegar...");
            CommunicationProtocol::TimerSync::requestAlarmList();
        }
        if (current_screen_id == SCREEN_ID_ZASOBY_SYSTEMU) {
            zasoby_sliders_create(lv_scr_act());
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
        if (current_screen_id == SCREEN_ID_CZUJNIKI) {
            sensors_wiki_init(objects.czujniki);
        }
    }

    if (current_screen_id == SCREEN_ID_LOGI) {
        log_update_textarea();
        comm_log_flush_if_needed();
    }
    if (current_screen_id == SCREEN_ID_CZUJNIKI) {
        sensors_wiki_update();
    }

    int64_t start_us = esp_timer_get_time();
    log_ui_tick_begin(current_screen_id);
    ui_tick();
    clock_ui_update();
    stopwatch_ui_sync_start_stop_label();
    timer_ui_sync_start_stop_label();
    notification_logic_refresh();
    alarm_popup_tick();
    CommunicationProtocol::updateUi();

    log_ui_tick_end(current_screen_id, esp_timer_get_time() - start_us);

    lv_perf_diag_tick_end();
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
        alarm_ui_init();
        alarm_popup_init();
        lv_perf_diag_init();
        lv_timer_create(ui_tick_timer_cb, 100, NULL);
        bpm_chart_handler_init();
        weather_icon_handler_init();
        music_ui_handler_init();
        main_ui_init();
        lvgl_port_unlock();
        clock_logic_start();
    } else {
        ESP_LOGE(TAG, "Failed to lock LVGL during UI init");
        return;
    }

    ESP_LOGI(TAG, "Application started");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Deferred brightness NVS save — runs on Core 0 (idle, prio 1),
        // safely outside LVGL task to avoid SPI1/PSRAM bus contention and RGB tearing.
        // Dirty flag is set from LVGL task (Core 1) when popup closes.
        if (main_ui_brt_needs_save()) {
            uint8_t pct = main_ui_get_brt_pct();
            nvs_handle_t h;
            if (nvs_open("display", NVS_READWRITE, &h) == ESP_OK) {
                nvs_set_u8(h, "brightness", pct);
                nvs_commit(h);
                nvs_close(h);
                ESP_LOGI(TAG, "Brightness %d%% saved to NVS", (int)pct);
            }
        }
    }
}
