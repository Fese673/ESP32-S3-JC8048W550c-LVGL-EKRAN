/**
 * @file sunton_esp32_8048s050c.c
 * @author Sven Fabricius (sven.fabricius@livediesel.de)
 * @brief ESP32-S3-8048S050C display driver using esp_lvgl_port
 * @version 0.2
 * @date 2024-12-20
 *
 * @copyright Copyright (c) 2024
 *
 */

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "sunton_esp32_8048s050c.h"

static const char *TAG = "sunton";

static esp_lcd_panel_handle_t panel_handle = NULL;
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;

void sunton_esp32s3_backlight_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = SUNTON_ESP32_BACKLIGHT_LEDC_TIMER,
        .freq_hz = 4000,
        .clk_cfg = LEDC_USE_RC_FAST_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .gpio_num = SUNTON_ESP32_PIN_BCKL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = SUNTON_ESP32_BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = SUNTON_ESP32_BACKLIGHT_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, SUNTON_ESP32_BACKLIGHT_LEDC_CHANNEL, 255));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, SUNTON_ESP32_BACKLIGHT_LEDC_CHANNEL));
    ESP_LOGI(TAG, "Backlight initialized");
}

lv_display_t *sunton_esp32s3_lcd_init(void)
{
    ESP_LOGI(TAG, "Initializing RGB LCD panel");

    // RGB panel configuration
    const esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .num_fbs = 2, // Double buffer for smooth rendering
        .clk_src = LCD_CLK_SRC_PLL160M,
        .timings = {
            .pclk_hz = (16 * 1000000),
            .h_res = SUNTON_ESP32_LCD_WIDTH,
            .v_res = SUNTON_ESP32_LCD_HEIGHT,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .hsync_idle_low = true,
                .vsync_idle_low = true,
                .de_idle_high = false,
                .pclk_active_neg = true,
                .pclk_idle_high = false,
            },
        },
        .hsync_gpio_num = GPIO_NUM_39,
        .vsync_gpio_num = GPIO_NUM_41,
        .de_gpio_num = GPIO_NUM_40,
        .pclk_gpio_num = GPIO_NUM_42,
        .data_gpio_nums = {
            GPIO_NUM_8,
            GPIO_NUM_3,
            GPIO_NUM_46,
            GPIO_NUM_9,
            GPIO_NUM_1, // B0-B4
            GPIO_NUM_5,
            GPIO_NUM_6,
            GPIO_NUM_7,
            GPIO_NUM_15,
            GPIO_NUM_16,
            GPIO_NUM_4, // G0-G5
            GPIO_NUM_45,
            GPIO_NUM_48,
            GPIO_NUM_47,
            GPIO_NUM_21,
            GPIO_NUM_14, // R0-R4
        },
        .disp_gpio_num = GPIO_NUM_NC,
        .flags = {
            .disp_active_low = false,
            .fb_in_psram = true,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ESP_LOGI(TAG, "Initializing LVGL port");

    // Initialize LVGL port
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 8192,
        .task_affinity = 1, // Pin to CPU 1
        .task_max_sleep_ms = 500,
        .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
        .timer_period_ms = 5,
    };
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    ESP_LOGI(TAG, "Adding RGB display to LVGL");

    // Add RGB display to LVGL
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = panel_handle,
        .hres = SUNTON_ESP32_LCD_WIDTH,
        .vres = SUNTON_ESP32_LCD_HEIGHT,
        .buffer_size = SUNTON_ESP32_LCD_WIDTH * SUNTON_ESP32_LCD_HEIGHT,
        .double_buffer = true,
        .flags = {
            .buff_spiram = false, // Use RGB panel's internal buffers
            .direct_mode = true,
        },
    };

    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = false,
            .avoid_tearing = true,
        },
    };

    lvgl_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (lvgl_disp == NULL)
    {
        ESP_LOGE(TAG, "Failed to add display to LVGL");
        return NULL;
    }

    ESP_LOGI(TAG, "LCD initialized successfully");
    return lvgl_disp;
}

i2c_master_bus_handle_t sunton_esp32s3_i2c_master(void)
{
    i2c_master_bus_handle_t touch_i2c_bus_handle = NULL;
    const i2c_master_bus_config_t touch_i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1,
        .scl_io_num = SUNTON_ESP32_TOUCH_PIN_I2C_SCL,
        .sda_io_num = SUNTON_ESP32_TOUCH_PIN_I2C_SDA,
        .glitch_ignore_cnt = 7,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&touch_i2c_bus_config, &touch_i2c_bus_handle));
    ESP_LOGI(TAG, "I2C master initialized");
    return touch_i2c_bus_handle;
}

void sunton_esp32s3_touch_init(i2c_master_bus_handle_t i2c_master)
{
    ESP_LOGI(TAG, "Initializing GT911 touch");

    esp_lcd_panel_io_handle_t gt911_touch_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t gt911_touch_io_config = {
        .dev_addr = SUNTON_ESP32_TOUCH_ADDRESS,
        .scl_speed_hz = 400000,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 16,
        .lcd_param_bits = 0,
        .flags = {
            .dc_low_on_data = false,
            .disable_control_phase = true,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_master, &gt911_touch_io_config, &gt911_touch_io_handle));

    esp_lcd_touch_handle_t touch_handle = NULL;
    const esp_lcd_touch_config_t gt911_touch_config = {
        .x_max = SUNTON_ESP32_LCD_WIDTH,
        .y_max = SUNTON_ESP32_LCD_HEIGHT,
        .rst_gpio_num = SUNTON_ESP32_TOUCH_PIN_RST,
        .int_gpio_num = GPIO_NUM_NC, // Disable interrupt - use polling mode
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(gt911_touch_io_handle, &gt911_touch_config, &touch_handle));

    // Add touch input to LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);
    if (lvgl_touch_indev == NULL)
    {
        ESP_LOGE(TAG, "Failed to add touch input to LVGL");
        return;
    }

    ESP_LOGI(TAG, "Touch initialized successfully");
}

// Compatibility function - not needed with esp_lvgl_port
void sunton_esp32s3_set_lvgl_task_handle(TaskHandle_t handle)
{
    (void)handle;
    // Not needed - esp_lvgl_port manages its own task
}
