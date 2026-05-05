# idf_component_register(SRCS main.c)

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
    ./app/main.cpp
)

set(SOURCES_COMMUNICATION_CPP
    ./communication/CommunicationProtocol.cpp
    ./communication/CobsFraming.cpp
    ./communication/CommunicationState.cpp
    ./communication/UartTransport.cpp
)

file(GLOB_RECURSE SOURCES_UI_CPP
    ./ui/ui/*.cpp
)


set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DEEZ_PLATFORM_ESP32 -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEEZ_PLATFORM_ESP32 -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE")


idf_component_register(
    SRCS ${SOURCES_C} ${SOURCES_UI_C} ${SOURCES_UTILS_CXX} ${SOURCES_APP_CPP} ${SOURCES_COMMUNICATION_CPP} ${SOURCES_UI_CPP}
    INCLUDE_DIRS    "."
                    "./hal/display"
                    "./ui/ui"
                    "./utils"
                    "./communication"
                    "./services/time"
                    "./services/wifi"
                    "./services/sntp"
                    "./utils/log"
                    "./app"
    REQUIRES        lvgl esp_lcd_touch_gt911 esp_timer esp_lcd esp_driver_ledc esp_driver_gpio esp_driver_uart esp_wifi esp_event nvs_flash esp_netif lwip
    )

# Add LVGL include path for EEZ framework compatibility
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${COMPONENT_DIR}/../managed_components/lvgl__lvgl"
    "${COMPONENT_DIR}/../managed_components/lvgl__lvgl/src"
)
