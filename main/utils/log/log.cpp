#include "log.h"

#include <cstdarg>
#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ui.h"

static const char *LOG_TAG = "diag";

static void log_vmessage(esp_log_level_t level, const char *tag,
                         const char *format, va_list args) {
  char buffer[256];
  vsnprintf(buffer, sizeof(buffer), format, args);
  esp_log_write(level, tag, "%s", buffer);
}

void log_init(void) {
  esp_log_level_set("*", ESP_LOG_INFO);
  esp_log_level_set(LOG_TAG, ESP_LOG_INFO);
}

void log_info(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  log_vmessage(ESP_LOG_INFO, tag, format, args);
  va_end(args);
}

void log_warn(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  log_vmessage(ESP_LOG_WARN, tag, format, args);
  va_end(args);
}

void log_error(const char *tag, const char *format, ...) {
  va_list args;
  va_start(args, format);
  log_vmessage(ESP_LOG_ERROR, tag, format, args);
  va_end(args);
}

static void log_memory_block(const char *label) {
  size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t min_free_internal =
      heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t min_free_psram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

  ESP_LOGI(LOG_TAG, "%s | heap int=%u KB (min=%u KB) psram=%u KB (min=%u KB)",
           label, (unsigned)(free_internal / 1024),
           (unsigned)(min_free_internal / 1024), (unsigned)(free_psram / 1024),
           (unsigned)(min_free_psram / 1024));
}

void log_boot_snapshot(const char *where) {
  ESP_LOGI(LOG_TAG, "%s | task=%s core=%d ticks=%u screen=%d", where,
           pcTaskGetName(NULL), xPortGetCoreID(), (unsigned)xTaskGetTickCount(),
           (int)eez_flow_get_current_screen());
  log_memory_block(where);
}

void log_ui_snapshot(const char *where, int16_t screen_id) {
  ESP_LOGI(LOG_TAG,
           "%s | task=%s core=%d ticks=%u screen=%d current=%d stack=%u", where,
           pcTaskGetName(NULL), xPortGetCoreID(), (unsigned)xTaskGetTickCount(),
           (int)screen_id, (int)eez_flow_get_current_screen(),
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
  log_memory_block(where);
}

void log_ui_tick_begin(int16_t screen_id) {
  ESP_LOGI(LOG_TAG, "ui_tick begin | task=%s core=%d screen=%d current=%d",
           pcTaskGetName(NULL), xPortGetCoreID(), (int)screen_id,
           (int)eez_flow_get_current_screen());
}

void log_ui_tick_end(int16_t screen_id, int64_t elapsed_us) {
  if (elapsed_us > 5000) {
    ESP_LOGW(LOG_TAG,
             "ui_tick end   | screen=%d current=%d duration=%lld us stack=%u",
             (int)screen_id, (int)eez_flow_get_current_screen(),
             (long long)elapsed_us,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
  } else {
    ESP_LOGI(LOG_TAG,
             "ui_tick end   | screen=%d current=%d duration=%lld us stack=%u",
             (int)screen_id, (int)eez_flow_get_current_screen(),
             (long long)elapsed_us,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
  }
}