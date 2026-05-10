#include "communication/CommUiMapper.h"
#include "communication/CommunicationState.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "screens.h"
#include "eez-flow.h"
#include "vars.h"

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
  if (code == 0) return "Slonecznie";
  if (code <= 3) return "Czesc. zachm.";
  if (code <= 9) return "Zachmurzenie";
  if (code <= 19) return "Mgla";
  if (code <= 29) return "Burza piaskowa";
  if (code <= 35) return "Sucho";
  if (code <= 39) return "Mraz";
  if (code <= 49) return "Mgla gesta";
  if (code <= 59) return "Mzawka";
  if (code <= 69) return "Deszcz";
  if (code <= 79) return "Snieg";
  if (code <= 84) return "Deszcz snieg";
  if (code <= 89) return "Deszcz snieg";
  if (code <= 99) return "Burza";
  return "N/A";
}

void resetUi() {
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT_IN, eez::Value("Oczekiwanie..."));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM1_0_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR_IN, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CO2_SENSOR_IN, eez::Value("---"));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CO2_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR_OUT, eez::Value("---"));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM1_0_SENSOR_OUT, eez::Value("---"));

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
}

void applySnapshotToMainScreen(const Snapshot &snapshot) {
  if (!snapshot.valid) return;

  static char temperatureBuffer[24];
  static char humidityBuffer[16];
  static char pressureBuffer[16];
  static char pm01Buffer[16];
  static char pm10Buffer[16];
  static char pm25Buffer[16];
  static char co2Buffer[16];
  static char airQualityTextBuffer[24];

  snprintf(pressureBuffer, sizeof(pressureBuffer), "%uhPa", snapshot.pressureHpa);
  snprintf(pm01Buffer, sizeof(pm01Buffer), "PM1 %u", snapshot.pm01UgM3);
  snprintf(pm10Buffer, sizeof(pm10Buffer), "PM10 %u", snapshot.pm10UgM3);
  snprintf(pm25Buffer, sizeof(pm25Buffer), "PM2.5 %u", snapshot.pm25UgM3);
  snprintf(humidityBuffer, sizeof(humidityBuffer), "%u%%", snapshot.humidityPct);
  snprintf(co2Buffer, sizeof(co2Buffer), "%u ppm", snapshot.eco2);
  snprintf(airQualityTextBuffer, sizeof(airQualityTextBuffer), "%s", snapshot.airQualityText);
  formatWithComma(temperatureBuffer, sizeof(temperatureBuffer), snapshot.temperatureC, "°C");

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR_IN, eez::Value(pressureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT_IN, eez::Value(airQualityTextBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM1_0_SENSOR_IN, eez::Value(pm01Buffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR_IN, eez::Value(pm10Buffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR_IN, eez::Value(humidityBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR_IN, eez::Value(temperatureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR_IN, eez::Value(pm25Buffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CO2_SENSOR_IN, eez::Value(co2Buffer));
}

void applyOutdoorSnapshotToMainScreen(const OutdoorWeatherSnapshot &ow) {
  if (!ow.valid) return;

  static char temperatureBuffer[24];
  static char humidityBuffer[16];
  static char pressureBuffer[16];
  static char pm25Buffer[16];
  static char pm10Buffer[16];
  static char co2Buffer[16];
  static char airQualityTextBuffer[24];

  formatWithComma(temperatureBuffer, sizeof(temperatureBuffer), ow.temperatureC, "°C");
  snprintf(humidityBuffer, sizeof(humidityBuffer), "%u%%", ow.humidityPct);
  snprintf(pressureBuffer, sizeof(pressureBuffer), "%uhPa", ow.pressureHpa);
  snprintf(pm25Buffer, sizeof(pm25Buffer), "PM2.5 %u", ow.pm25UgM3);

  const char* status = wmoCodeToText(ow.weatherCode);
  snprintf(airQualityTextBuffer, sizeof(airQualityTextBuffer), "%s", status);

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_HUMIDITY_SENSOR_OUT, eez::Value(humidityBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEMP_SENSOR_OUT, eez::Value(temperatureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PRESSURE_SENSOR_OUT, eez::Value(pressureBuffer));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM2_5_SENSOR_OUT, eez::Value(pm25Buffer));

  snprintf(pm10Buffer, sizeof(pm10Buffer), "PM10 %u", ow.pm10UgM3);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PM10_SENSOR_OUT, eez::Value(pm10Buffer));

  snprintf(co2Buffer, sizeof(co2Buffer), "%u ppm", ow.co2Ppm);
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_CO2_SENSOR_OUT, eez::Value(co2Buffer));

  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_AIR_QUALITY_TEXT_OUT, eez::Value(airQualityTextBuffer));
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

  snprintf(textWeatherBuf, sizeof(textWeatherBuf), "%s", wmoCodeToText(ow.weatherCode));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TEXT_WEATHER, eez::Value(textWeatherBuf));
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

  if (objects.menu_kfe_zasoby_4) {
    lv_obj_remove_state(objects.menu_kfe_zasoby_4, LV_STATE_DISABLED);
  }
  if (objects.menu_kfe_statystyk_1) {
    lv_obj_remove_state(objects.menu_kfe_statystyk_1, LV_STATE_DISABLED);
  }
}

void applySettingsToUi(const DeviceSettings &ds) {
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_BUZZER_STATE, eez::Value(ds.buzzerEnabled ? 1 : 0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MQTT_STATE, eez::Value(ds.mqttEnabled ? 1 : 0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TOUCH_STATE, eez::Value(ds.touchTestEnabled ? 1 : 0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_MUSIC_STATE, eez::Value(ds.backgroundMusicEnabled ? 1 : 0));
  eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_PMS_STATE, eez::Value(ds.pmsEnabled ? 1 : 0));

  if (objects.settings_melody_dropdown) {
    lv_dropdown_set_selected(objects.settings_melody_dropdown, ds.alarmMelodyIndex);
  }
}

} // namespace CommunicationProtocol
