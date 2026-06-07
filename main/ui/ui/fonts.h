#ifndef EEZ_LVGL_UI_FONTS_H
#define EEZ_LVGL_UI_FONTS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t ui_font_calibri_30;
extern const lv_font_t ui_font_calibri;
extern const lv_font_t ui_font_consola_18;
extern const lv_font_t ui_font_consolai_16;
extern const lv_font_t ui_font_bebas_neue_regular;
extern const lv_font_t ui_font_bebas_neue58;
extern const lv_font_t ui_font_bebas_neue30;
extern const lv_font_t ui_font_bebas_neue35;
extern const lv_font_t ui_font_consolai_13;
extern const lv_font_t ui_font_montserrat_regular_16;
extern const lv_font_t ui_font_montserrat_regular_14;

#ifndef EXT_FONT_DESC_T
#define EXT_FONT_DESC_T
typedef struct _ext_font_desc_t {
    const char *name;
    const void *font_ptr;
} ext_font_desc_t;
#endif

extern ext_font_desc_t fonts[];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_FONTS_H*/