# idf_component_register(SRCS main.c)
# Force re-scan for new UI images and services

file(GLOB_RECURSE SOURCES_C
    ./hal/**/*.c
    ./services/**/*.c
)

file(GLOB_RECURSE SOURCES_CUSTOM_ICONS
    ./custom_icons/*.c
)

file(GLOB_RECURSE SOURCES_UI_C
    ./ui/ui/*.c
    ./ui/ui/**/*.c
)

# Odrzucamy nieużywane fonty — zastąpione lekkimi stubami (oszczędność ~1.1 MB)
list(REMOVE_ITEM SOURCES_UI_C
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ui/ui_font_calibri_30.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ui/ui_font_consolai_13.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ui/ui_font_bebas_neue_regular52.c
)

file(GLOB_RECURSE SOURCES_UTILS_CXX
    ./utils/**/*.cpp
)

set(SOURCES_APP_CPP
    ./app/clock/clock_logic.cpp
    ./app/clock/clock_timezone_tiles.c
    ./app/notification/notification_logic.cpp
    ./app/stopwatch/stopwatch_logic.cpp
    ./app/stopwatch/stopwatch_ui.cpp
    ./app/timer/timer_logic.cpp
    ./app/timer/timer_ui.cpp
    ./app/alarm_logic.cpp
    ./app/alarm_ui.cpp
    ./app/alarm/alarm_popup.cpp
    ./app/game/flappy_bird.cpp
    ./app/screen_geometry/screen_geometry_ui.cpp
    ./app/screen_geometry/screen_geometry_logic.cpp
    ./app/radar/radar_logic.cpp
    ./app/radar/radar_ui.cpp
    ./app/main_handler.cpp
    ./app/main.cpp
    ./app/weather/weather_icon_handler.c
    ./app/zasoby_sliders.c
    ./services/sensor/sensors_wiki.cpp
    ./diagnostics/lv_perf_diag.cpp
    ./app/font_stubs.c
)

set(SOURCES_COMMUNICATION_CPP
    ./communication/CommunicationProtocol.cpp
    ./communication/CommHandshake.cpp
    ./communication/CommUiMapper.cpp
    ./communication/CobsFraming.cpp
    ./communication/CommunicationState.cpp
    ./communication/UartTransport.cpp
    ./communication/time/timer_synchro.cpp
)

file(GLOB_RECURSE SOURCES_UI_CPP
    ./ui/ui/*.cpp
)


set(SOURCES_WIFI_SET_C
    ./setting_wifi/set_wifi.cpp
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DEEZ_PLATFORM_ESP32 -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEEZ_PLATFORM_ESP32 -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE")


idf_component_register(
    SRCS ${SOURCES_C} ${SOURCES_CUSTOM_ICONS} ${SOURCES_UI_C} ${SOURCES_UTILS_CXX} ${SOURCES_APP_CPP} ${SOURCES_COMMUNICATION_CPP} ${SOURCES_UI_CPP} ${SOURCES_WIFI_SET_C}
    INCLUDE_DIRS    "."
                    "./custom_icons"
                    "./hal/display"
                    "./ui/ui"
                    "./utils"
                    "./utils/comm_log"
                    "./utils/gesture"
                    "./communication"
                    "./communication/time"
                    "./services/time"
                    "./services/wifi"
                    "./services/sntp"
                    "./services/bpm"
                    "./services/sensor"
                    "./utils/log"
                    "./app"
                    "./app/clock"
                    "./app/alarm"
                    "./app/timer"
                    "./app/stopwatch"
                    "./app/notification"
                    "./app/weather"
                    "./app/game"
                    "./app/screen_geometry"
                    "./app/radar"
                    "./setting_wifi"
    REQUIRES        lvgl esp_lcd_touch_gt911 esp_timer esp_lcd esp_driver_ledc esp_driver_gpio esp_driver_uart esp_wifi esp_event nvs_flash esp_netif lwip
    )

# Add LVGL include path for EEZ framework compatibility
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${COMPONENT_DIR}/../managed_components/lvgl__lvgl"
    "${COMPONENT_DIR}/../managed_components/lvgl__lvgl/src"
)

# SRAM fragmentation fix: intercept lv_label_set_text in all UI files
target_compile_options(${COMPONENT_LIB} PRIVATE "-include" "safe_label.h")
