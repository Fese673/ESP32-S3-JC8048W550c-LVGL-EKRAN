#include "communication/CommUiMapper.h"
#include "communication/CommunicationState.h"
#include "app/notification/notification_logic.h"
#include "app/alarm_ui.h"
#include "app/zasoby_sliders.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "screens.h"
#include "eez-flow.h"
#include "vars.h"

// This constant is used by EEZ Flow but may be missing from vars.h after EEZ Studio export
#ifndef FLOW_GLOBAL_VARIABLE_RADIO_MODE
#define FLOW_GLOBAL_VARIABLE_RADIO_MODE 63
#endif

// Etap 3: nowa zmienna flow dla jasnosci 7-seg. Indeks 64 (po ostatnim
// enum FLOW_GLOBAL_VARIABLE_MUSIC_TREBLE=62 i RADIO_MODE=63).
#ifndef FLOW_GLOBAL_VARIABLE_7SEG_BRIGHTNESS
#define FLOW_GLOBAL_VARIABLE_7SEG_BRIGHTNESS 64
#endif

// Timer flow variables — przeniesione do timer_synchro.cpp

#include "services/music/music_ui_handler.h"
#include "main_handler.h"
#include "app/weather/weather_icon_handler.h"

namespace CommunicationProtocol {

static constexpr char kTag[] = "UI_MAPPER";

void formatWithComma(char *buffer, size_t bufferSize, float value,
                     const char *suffix) {
  if (buffer == nullptr || bufferSize == 0U) return;
  snprintf(buffer, bufferSize, "%.1f%s", value, suffix);
  for (char *cursor = buffer; *cursor != '\0'; ++cursor) {
    if (*cursor == '.') { *cursor = ','; }
  }
}

const char* wmoCodeToText(uint8_t code) {
  if (code == 0) return "S\u0142onecznie";
  if (code <= 3) return "Cz\u0119\u015b\u0107. zachm.";
  if (code <= 9) return "Zachmurzenie";
  if (code <= 19) return "Mg\u0142a";
  if (code <= 29) return "Burza piaskowa";
  if (code <= 35) return "Sucho";
  if (code <= 39) return "Mr\u00f3z";
  if (code <= 49) return "Mg\u0142a g\u0119sta";
  if (code <= 59) return "M\u017cawka";
  if (code <= 69) return "Deszcz";
  if (code <= 79) return "\u015anieg";
  if (code <= 84) return "Deszcz ze \u015bniegiem";
  if (code <= 89) return "Deszcz ze \u015bniegiem";
  if (code <= 99) return "Burza";
  return "N/A";
}

static void initLabelScroll() {
  static bool s_scrollInited = false;
  if (s_scrollInited) return;
  s_scrollInited = true;

  lv_obj_t* scrollLabels[] = {
    objects.tvoc_in_8, objects.co2_in_6, objects.pm10_in_7,
    objects.pm25_in_3, objects.hm_in_1, objects.txt_in_4,
    objects.tm_in_2, objects.pressure_in_5,
    objects.no2_out_8, objects.co2_out_6, objects.pm10_out_7,
    objects.pm25_out_3, objects.hm_out_1, objects.txt_out_4,
    objects.tm_out_2, objects.pressure_out_5,
    objects.weather_in_label, objects.weather_out_label
  };
  for (unsigned i = 0; i < sizeof(scrollLabels) / sizeof(scrollLabels[0]); ++i) {
    lv_obj_t *label = scrollLabels[i];
    if (!label) continue;
    lv_obj_t *parent = lv_obj_get_parent(label);
    lv_coord_t parentW = lv_obj_get_width(parent);
    lv_coord_t labelX = lv_obj_get_x(label);
    lv_coord_t w = parentW - labelX - 4;
    if (w < 40) w = 40;
    lv_obj_set_width(label, w);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
  }
}

void resetUi() {
  initLabelScroll();

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT_IN, eez::Value("Oczekiwanie..."));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM1_0_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CO2_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TVOC_SENSOR_IN, eez::Value("---"));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CO2_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM1_0_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NO2_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SUNRISE__WEATHER_TIME, eez::Value("--:--"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SUNSET__WEATHER_TIME, eez::Value("--:--"));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TM_ODCZUWALNA_WEATHER_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WIND_WEATHER_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEXT_WEATHER, eez::Value("Oczekiwanie..."));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_FREE_RAM_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HEAP_RAM_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_DMA_RAM_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CORE0_CPU_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CORE1_CPU_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_FREE_FLASH_SYS, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_USED_FLASH_SYS, eez::Value("---"));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BPM_VALUE, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SPO2_VALUE, eez::Value("---"));

  notification_logic_set_wifi(false);
  notification_logic_set_ble(false);
  notification_logic_set_bell(false);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WIFI_STATE, eez::Value(0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BLE_STATE, eez::Value(0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BELL_STATE, eez::Value(0));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_TITLE, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_ARTIST, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_CONNECTED, eez::Value(0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_PLAYING, eez::Value(0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_VOLUME, eez::Value(50));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_BASS, eez::Value(50));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_MID, eez::Value(50));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_TREBLE, eez::Value(50));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_RADIO_MODE, eez::Value(0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_7SEG_BRIGHTNESS, eez::Value(100));

  music_ui_reset();
}

void applySnapshotToMainScreen(const Snapshot &snapshot) {
  if (!snapshot.valid) return;

  static char temperatureBuffer[24];
  static char humidityBuffer[16];
  static char pressureBuffer[16];
  static char pm01Buffer[24];
  static char pm10Buffer[24];
  static char pm25Buffer[24];
  static char co2Buffer[16];
  static char airQualityTextBuffer[24];
  static char tvocBuffer[16];

  snprintf(pressureBuffer, sizeof(pressureBuffer), "%uhPa", snapshot.pressureHpa);
  snprintf(pm01Buffer, sizeof(pm01Buffer), "[1.0] %u \u00b5g/m\u00b3", snapshot.pm01UgM3);
  snprintf(pm10Buffer, sizeof(pm10Buffer), "[10] %u \u00b5g/m\u00b3", snapshot.pm10UgM3);
  snprintf(pm25Buffer, sizeof(pm25Buffer), "[2.5] %u \u00b5g/m\u00b3", snapshot.pm25UgM3);
  snprintf(humidityBuffer, sizeof(humidityBuffer), "%u%%", snapshot.humidityPct);
  snprintf(co2Buffer, sizeof(co2Buffer), "%u ppm", snapshot.eco2);
  snprintf(airQualityTextBuffer, sizeof(airQualityTextBuffer), "%s", snapshot.airQualityText);
  formatWithComma(temperatureBuffer, sizeof(temperatureBuffer), snapshot.temperatureC, "\u00b0C");
  snprintf(tvocBuffer, sizeof(tvocBuffer), "%u ppb", snapshot.tvoc);

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR_IN, eez::Value(pressureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT_IN, eez::Value(airQualityTextBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM1_0_SENSOR_IN, eez::Value(pm01Buffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR_IN, eez::Value(pm10Buffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR_IN, eez::Value(humidityBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR_IN, eez::Value(temperatureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR_IN, eez::Value(pm25Buffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CO2_SENSOR_IN, eez::Value(co2Buffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TVOC_SENSOR_IN, eez::Value(tvocBuffer));
}

void applyOutdoorSnapshotToMainScreen(const OutdoorWeatherSnapshot &ow) {
  if (!ow.valid) return;

  static char temperatureBuffer[24];
  static char humidityBuffer[16];
  static char pressureBuffer[16];
  static char pm25Buffer[24];
  static char pm10Buffer[24];
  static char co2Buffer[16];
  static char airQualityTextBuffer[24];
  static char no2Buffer[24];

  formatWithComma(temperatureBuffer, sizeof(temperatureBuffer), ow.temperatureC, "\u00b0C");
  snprintf(humidityBuffer, sizeof(humidityBuffer), "%u%%", ow.humidityPct);
  snprintf(pressureBuffer, sizeof(pressureBuffer), "%uhPa", ow.pressureHpa);
  snprintf(pm25Buffer, sizeof(pm25Buffer), "[2.5] %u \u00b5g/m\u00b3", ow.pm25UgM3);

  const char* status = wmoCodeToText(ow.weatherCode);
  snprintf(airQualityTextBuffer, sizeof(airQualityTextBuffer), "%s", status);

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR_OUT, eez::Value(humidityBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR_OUT, eez::Value(temperatureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR_OUT, eez::Value(pressureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR_OUT, eez::Value(pm25Buffer));

  snprintf(pm10Buffer, sizeof(pm10Buffer), "[10] %u \u00b5g/m\u00b3", ow.pm10UgM3);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR_OUT, eez::Value(pm10Buffer));

  snprintf(co2Buffer, sizeof(co2Buffer), "%u ppm", ow.co2Ppm);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CO2_SENSOR_OUT, eez::Value(co2Buffer));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT_OUT, eez::Value(airQualityTextBuffer));

  snprintf(no2Buffer, sizeof(no2Buffer), "[NO\u2082] %u \u00b5g/m\u00b3", ow.no2UgM3);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_NO2_SENSOR_OUT, eez::Value(no2Buffer));
}

void applySnapshotToWeatherScreen(const OutdoorWeatherSnapshot &ow) {
  if (!ow.valid) return;

  static char appTempBuf[16];
  static char windBuf[16];
  static char rainBuf[16];
  static char uvBuf[16];
  static char sunriseBuf[16];
  static char sunsetBuf[16];
  static char textWeatherBuf[32];

  snprintf(appTempBuf, sizeof(appTempBuf), "%.1f°C", ow.apparentTempC);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TM_ODCZUWALNA_WEATHER_OUT, eez::Value(appTempBuf));

  snprintf(windBuf, sizeof(windBuf), "%.1f m/s", ow.windSpeedMs);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WIND_WEATHER_OUT, eez::Value(windBuf));

  snprintf(rainBuf, sizeof(rainBuf), "%.1f mm", ow.precipitationMm);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_RAIN_WEATHER_OUT, eez::Value(rainBuf));

  snprintf(uvBuf, sizeof(uvBuf), "%.1f", ow.uvIndex);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_UV_WEATHER_OUT, eez::Value(uvBuf));

  if (ow.sunriseHour > 0 || ow.sunriseMin > 0) {
    snprintf(sunriseBuf, sizeof(sunriseBuf), "%02u:%02u", (unsigned)ow.sunriseHour, (unsigned)ow.sunriseMin);
  } else {
    snprintf(sunriseBuf, sizeof(sunriseBuf), "--:--");
  }
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SUNRISE__WEATHER_TIME, eez::Value(sunriseBuf));

  if (ow.sunsetHour > 0 || ow.sunsetMin > 0) {
    snprintf(sunsetBuf, sizeof(sunsetBuf), "%02u:%02u", (unsigned)ow.sunsetHour, (unsigned)ow.sunsetMin);
  } else {
    snprintf(sunsetBuf, sizeof(sunsetBuf), "--:--");
  }
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SUNSET__WEATHER_TIME, eez::Value(sunsetBuf));

  if (ow.sunriseHour > 0 || ow.sunsetHour > 0) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if (local) {
      bool is_day = true; // Default to Sun icon at startup/unsynced state
      if (local->tm_year + 1900 >= 2020) { // Only calculate if system time is synced (year is valid)
        int cur = local->tm_hour * 60 + local->tm_min;
        int rise = (int)ow.sunriseHour * 60 + (int)ow.sunriseMin;
        int set  = (int)ow.sunsetHour  * 60 + (int)ow.sunsetMin;
        is_day = (cur >= rise && cur < set);
      }
      notification_logic_set_sun_moon(is_day);
    }
  }

  snprintf(textWeatherBuf, sizeof(textWeatherBuf), "%s", wmoCodeToText(ow.weatherCode));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEXT_WEATHER, eez::Value(textWeatherBuf));

  weather_icon_handler_set(ow.weatherCode);
}

void applyResourcesToUi(const SystemResources &sr) {
  static char freeRamBuf[16];
  static char heapRamBuf[16];
  static char dmaRamBuf[16];
  static char core0CpuBuf[16];
  static char core1CpuBuf[16];
  static char freeFlashBuf[16];
  static char usedFlashBuf[16];

  snprintf(freeRamBuf, sizeof(freeRamBuf), "%u KB", (unsigned int)(sr.freeRam / 1024));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_FREE_RAM_SYS, eez::Value(freeRamBuf));

  snprintf(heapRamBuf, sizeof(heapRamBuf), "%u KB", (unsigned int)(sr.heapRam / 1024));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HEAP_RAM_SYS, eez::Value(heapRamBuf));

  snprintf(dmaRamBuf, sizeof(dmaRamBuf), "%u KB", (unsigned int)(sr.dmaRam / 1024));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_DMA_RAM_SYS, eez::Value(dmaRamBuf));

  snprintf(core0CpuBuf, sizeof(core0CpuBuf), "%u %%", (unsigned int)sr.core0Cpu);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CORE0_CPU_SYS, eez::Value(core0CpuBuf));

  snprintf(core1CpuBuf, sizeof(core1CpuBuf), "%u %%", (unsigned int)sr.core1Cpu);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CORE1_CPU_SYS, eez::Value(core1CpuBuf));

  snprintf(freeFlashBuf, sizeof(freeFlashBuf), "%.1f MB", sr.freeFlash / (1024.0f * 1024.0f));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_FREE_FLASH_SYS, eez::Value(freeFlashBuf));

  snprintf(usedFlashBuf, sizeof(usedFlashBuf), "%.1f MB", sr.usedFlash / (1024.0f * 1024.0f));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_USED_FLASH_SYS, eez::Value(usedFlashBuf));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_UNDERRUNS_AUDIO_SYS, eez::Value((int)sr.underrunsAudio));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_OVERFLOW_AUDIO_SYS, eez::Value((int)sr.overflowAudio));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_DROPS_AUDIO_SYS, eez::Value((int)sr.dropsAudio));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_ERRORS_I2C_SYS, eez::Value((int)sr.errorsI2c));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIMEOUTS_I2C_SYS, eez::Value((int)sr.timeoutsI2c));

  zasoby_sliders_update(sr.core0Cpu, sr.core1Cpu, sr.freeFlash, sr.usedFlash);

  if (objects.menu_kfe_zasoby_4) {
    lv_obj_remove_state(objects.menu_kfe_zasoby_4, LV_STATE_DISABLED);
  }
  /* TODO(regression-fix): menu_kfe_statystyki_9 usuniete w screens.h
   * v2.1 ale referencja zostala w CommUiMapper.cpp (dodana v2.0).
   * Wymaga decyzji: przywrocic element w EEZ Studio lub usunac te linie.
   * Tymczasowo wylaczone, zeby build przeszedl. */
#if 0
  if (objects.menu_kfe_statystyki_9) {
    lv_obj_remove_state(objects.menu_kfe_statystyki_9, LV_STATE_DISABLED);
  }
#endif
}

void applyBpmStatusToUi(int bpm, int spo2) {
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BPM_VALUE, eez::Value(bpm));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_SPO2_VALUE, eez::Value(spo2));
}

void applyWifiStatusToUi(bool connected, int8_t rssi, const uint8_t *ip) {
  (void)rssi;
  (void)ip;
  ESP_LOGI(kTag, "Sensor WiFi: connected=%d rssi=%d", (int)connected, (int)rssi);
  notification_logic_set_wifi(connected);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WIFI_STATE, eez::Value(connected ? 1 : 0));
}

void applyStatusBleToUi(uint8_t value) {
  ESP_LOGI(kTag, "Sensor BLE: value=%d", (int)value);
  bool connected = (value == 2);
  notification_logic_set_ble(connected);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BLE_STATE, eez::Value((int)value));
}

void applyStatusBellToUi(uint8_t value) {
  ESP_LOGI(kTag, "Sensor Bell: value=%d", (int)value);
  bool active = (value > 0);
  notification_logic_set_bell(active);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BELL_STATE, eez::Value((int)value));
}

void applyMusicMetadataToUi(const char *title, const char *artist) {
    // WARNING: eez::Value(const char*) stores only the raw pointer, not a copy of the string!
    // The buffers MUST be static so their memory remains alive permanently in SRAM.
    static char s_titleBuf[128];
    static char s_artistBuf[128];
    snprintf(s_titleBuf, sizeof(s_titleBuf), "%s", title ? title : "");
    snprintf(s_artistBuf, sizeof(s_artistBuf), "%s", artist ? artist : "");
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_TITLE, eez::Value(s_titleBuf));
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_ARTIST, eez::Value(s_artistBuf));
}

void applyMusicStatusToUi(bool connected, bool playing) {
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_CONNECTED, eez::Value(connected ? 1 : 0));
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_PLAYING, eez::Value(playing ? 1 : 0));
}

void applyMusicVolumeToUi(uint8_t volume) {
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_VOLUME, eez::Value(static_cast<int>(volume)));
    music_ui_set_volume(volume);
}

void applyMusicEQToUi(uint8_t bass, uint8_t mid, uint8_t treble) {
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_BASS, eez::Value(static_cast<int>(bass)));
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_MID, eez::Value(static_cast<int>(mid)));
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_TREBLE, eez::Value(static_cast<int>(treble)));
    music_ui_set_eq(bass, mid, treble);
}

void applyRadioModeToUi(uint8_t mode) {
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_RADIO_MODE, eez::Value(static_cast<int>(mode)));
    main_ui_set_radio_mode(mode);
}

void applySettingsToUi(const DeviceSettings &ds) {
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BUZZER_STATE, eez::Value(ds.buzzerEnabled ? 1 : 0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MQTT_STATE, eez::Value(ds.mqttEnabled ? 1 : 0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TOUCH_STATE, eez::Value(ds.touchTestEnabled ? 1 : 0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_STATE, eez::Value(ds.backgroundMusicEnabled ? 1 : 0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PMS_STATE, eez::Value(ds.pmsEnabled ? 1 : 0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_7SEG_BRIGHTNESS, eez::Value((int)ds.sevenSegBrightness));

  if (objects.settings_melody_dropdown) {
    lv_dropdown_set_selected(objects.settings_melody_dropdown, ds.alarmMelodyIndex);
  }

  /* Etap 3: dropdown jasnosci 7-seg. Opcje: 100/75/50/25/10/5/2% (indeksy 0..6).
   * Mapujemy wartosc 0..100 na najblizszy indeks, zeby zawsze trafic w
   * jedna z 7 opcji. */
  if (objects.settings_jasnosc_7seg) {
    uint8_t idx = brightnessToDropdownIndex(ds.sevenSegBrightness);
    lv_dropdown_set_selected(objects.settings_jasnosc_7seg, idx);
  }
}

/* Etap 3: mapowanie dropdown <-> brightness.
 *
 * Tablica opcji dropdowna (z screens.c):
 *   "100%\n75%\n50%\n25%\n10%\n5%\n2%" — indeksy 0..6.
 *
 * Algorytm: progi — kazda opcja pokrywa zakres [midpoint miedzy nia a
 * sasiadem]. Dla stabilnosci obrana strategia: floor do najblizszej
 * opcji (np. 80 -> indeks 1 = 75%, 24 -> indeks 3 = 25%, 1 -> indeks 6 = 2%). */
static const uint8_t kBrightnessOptions[] = {100, 75, 50, 25, 10, 5, 2};
static const uint8_t kBrightnessOptionCount = sizeof(kBrightnessOptions);

uint8_t brightnessToDropdownIndex(uint8_t brightness)
{
  if (brightness > 100U) brightness = 100U;
  /* Progi: opcja[i] obowiazuje gdy brightness >= opcja[i] (po sorted desc).
   * 100 >= 100 -> 0, 75-99 -> 1, 50-74 -> 2, 25-49 -> 3, 10-24 -> 4,
   * 5-9 -> 5, 0-4 -> 6. */
  for (uint8_t i = 0; i < kBrightnessOptionCount; ++i) {
    if (brightness >= kBrightnessOptions[i]) {
      return i;
    }
  }
  return (uint8_t)(kBrightnessOptionCount - 1U);
}

uint8_t dropdownIndexToBrightness(uint8_t index)
{
  if (index >= kBrightnessOptionCount) {
    return kBrightnessOptions[0];   /* default 100% dla nieznanego indeksu */
  }
  return kBrightnessOptions[index];
}

} // namespace CommunicationProtocol
