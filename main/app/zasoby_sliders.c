#include "zasoby_sliders.h"
#include "fonts.h"

static lv_obj_t *s_free_flash_slider = NULL;
static lv_obj_t *s_used_flash_slider = NULL;
static lv_obj_t *s_cpu_core0_slider = NULL;
static lv_obj_t *s_cpu_core1_slider = NULL;

static void style_system_slider(lv_obj_t *slider) {
    if (!slider) return;

    lv_obj_set_style_bg_color(slider, lv_color_hex(0x28252c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(slider, 0, LV_PART_MAIN);

    lv_obj_set_style_bg_color(slider, lv_color_hex(0x85808a), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(slider, 0, LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
}

static lv_obj_t *create_slider(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, w, h);
    lv_slider_set_value(slider, 0, LV_ANIM_OFF);
    style_system_slider(slider);
    return slider;
}

void zasoby_sliders_create(lv_obj_t *screen) {
    if (!screen) return;

    if (s_free_flash_slider) return;

    s_free_flash_slider = create_slider(screen, 344, 208, 101, 10);
    s_used_flash_slider = create_slider(screen, 344, 330, 101, 10);
    s_cpu_core0_slider  = create_slider(screen, 203, 208, 101, 10);
    s_cpu_core1_slider  = create_slider(screen, 203, 330, 101, 10);
}

void zasoby_sliders_update(int core0_cpu, int core1_cpu, uint32_t free_flash, uint32_t used_flash) {
    if (s_cpu_core0_slider) {
        lv_slider_set_value(s_cpu_core0_slider, core0_cpu, LV_ANIM_OFF);
    }
    if (s_cpu_core1_slider) {
        lv_slider_set_value(s_cpu_core1_slider, core1_cpu, LV_ANIM_OFF);
    }
    uint32_t total = free_flash + used_flash;
    if (total > 0) {
        if (s_free_flash_slider) {
            lv_slider_set_value(s_free_flash_slider, (free_flash * 100) / total, LV_ANIM_OFF);
        }
        if (s_used_flash_slider) {
            lv_slider_set_value(s_used_flash_slider, (used_flash * 100) / total, LV_ANIM_OFF);
        }
    }
}
