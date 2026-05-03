#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations

// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_LOL = 0,
    FLOW_GLOBAL_VARIABLE_HOUR_TIME = 1,
    FLOW_GLOBAL_VARIABLE_MINUTES_TIME = 2,
    FLOW_GLOBAL_VARIABLE_SECONDS_TIME = 3,
    FLOW_GLOBAL_VARIABLE_NAME_DAY_DATE = 4,
    FLOW_GLOBAL_VARIABLE_NUMBER_DAY_DATE = 5,
    FLOW_GLOBAL_VARIABLE_NAME_MONTH_DATE = 6
};

// Native global variables

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/