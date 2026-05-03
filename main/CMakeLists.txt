# idf_component_register(SRCS main.c)

set(SOURCES_C
    sunton_esp32_8048s050c.c
)

file(GLOB_RECURSE SOURCES_UI_C
    ./ui/ui/*.c
    ./ui/ui/**/*.c
)

file(GLOB_RECURSE SOURCES_LOGS_CXX
    ./logs/*.cpp
)

file(GLOB_RECURSE SOURCES_CPP
    main.cpp
    ./ui/ui/*.cpp
)


set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DEEZ_PLATFORM_ESP32 -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEEZ_PLATFORM_ESP32 -DEEZ_FOR_LVGL -DLV_LVGL_H_INCLUDE_SIMPLE")


idf_component_register(
    SRCS ${SOURCES_C} ${SOURCES_UI_C} ${SOURCES_LOGS_CXX} ${SOURCES_CPP}
    INCLUDE_DIRS    "."
    ./ui/ui
    ./logs
    REQUIRES        lvgl esp_lcd_touch_gt911 esp_timer esp_lcd esp_driver_ledc esp_driver_gpio
    )

# Add LVGL include path for EEZ framework compatibility
target_include_directories(${COMPONENT_LIB} PUBLIC
    "${COMPONENT_DIR}/../managed_components/lvgl__lvgl"
    "${COMPONENT_DIR}/../managed_components/lvgl__lvgl/src"
)
