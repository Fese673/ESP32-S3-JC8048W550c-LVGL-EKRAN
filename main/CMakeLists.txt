# idf_component_register(SRCS main.c)
# Force re-scan for new UI images

file(GLOB_RECURSE SOURCES_C
    ./hal/**/*.c
    ./services/**/*.c
)

file(GLOB_RECURSE SOURCES_UI_C
    ./ui/ui/*.c
    ./ui/ui/**/*.c
)

file(GLOB_RECURSE SOURCES_UTILS_CXX
    ./utils/**/*.cpp
)

set(SOURCES_APP_CPP
    ./app/clock_logic.cpp
    ./app/notification_logic.cpp
    ./app/stopwatch_logic.cpp
    ./app/stopwatch_ui.cpp
    ./app/timer_logic.cpp
    ./app/timer_ui.cpp
    ./app/game/flappy_bird.cpp
    ./app/main.cpp
)

set(SOURCES_COMMUNICATION_CPP
    ./communication/CommunicationProtocol.cpp
    ./communication/CommHandshake.cpp
    ./communication/CommUiMapper.cpp
    ./communication/CobsFraming.cpp
    ./communication/CommunicationState.cpp
    ./communication/UartTransport.cpp
)

file(GLOB_RECURSE SOURCES_UI_CPP
    ./ui/ui/*.cpp
)


set(SOURCES_WIFI_SET_C
    ./setting_wifi/set_wifi.c
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DEEZ_PLATFORM_ESP32 -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEEZ_PLATFORM_ESP32 -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE")


idf_component_register(
    SRCS ${SOURCES_C} ${SOURCES_UI_C} ${SOURCES_UTILS_CXX} ${SOURCES_APP_CPP} ${SOURCES_COMMUNICATION_CPP} ${SOURCES_UI_CPP} ${SOURCES_WIFI_SET_C}
    INCLUDE_DIRS    "."
                    "./hal/display"
                    "./ui/ui"
                    "./utils"
                    "./utils/comm_log"
                    "./communication"
                    "./services/time"
                    "./services/wifi"
                    "./services/sntp"
                    "./services/bpm"
                    "./utils/log"
                    "./app"
                    "./app/game"
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
