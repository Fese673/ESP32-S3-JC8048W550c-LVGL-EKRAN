/*
 * font_stubs.c
 *
 * Lightweight replacements for font files that are referenced in the EEZ
 * font table but never actually applied to any widget in the UI.
 *
 * These stubs remove ~1.1 MB of font bitmap data from the binary
 * (calibri_30: 215KB + consolai_13: 45KB + bebas_neue_regular52: 854KB)
 * while keeping the font symbols available for linking.
 *
 * The callbacks delegate to the fallback font's get_glyph_dsc, which
 * stores a pointer to the fallback's font_dsc inside glyph_dsc_out.
 * The subsequent get_glyph_bitmap call reads that pointer and renders
 * using the fallback's bitmap data — transparent to LVGL.
 */

#include "lvgl.h"

/* ------------------------------------------------------------------ */
/*  Delegate callbacks — forward to the fallback font                  */
/* ------------------------------------------------------------------ */

static bool stub_get_glyph_dsc(const lv_font_t *font,
                                lv_font_glyph_dsc_t *dsc_out,
                                uint32_t unicode_letter,
                                uint32_t unicode_letter_next)
{
    if (!font->fallback || !font->fallback->get_glyph_dsc) return false;
    return font->fallback->get_glyph_dsc(
        font->fallback, dsc_out,
        unicode_letter, unicode_letter_next);
}

static const void *stub_get_glyph_bitmap(
    lv_font_glyph_dsc_t *g_dsc, lv_draw_buf_t *draw_buf)
{
    /* g_dsc was filled by fallback's get_glyph_dsc and already
     * contains a pointer to the fallback's lv_font_fmt_txt_dsc_t.
     * The standard fmt_txt bitmap function uses that pointer. */
    return lv_font_get_bitmap_fmt_txt(g_dsc, draw_buf);
}

/* ------------------------------------------------------------------ */
/*  Stub font descriptors                                             */
/* ------------------------------------------------------------------ */

#define FONT_STUB(name, fallback_font)                                        \
    const lv_font_t name = {                                                  \
        .get_glyph_dsc       = stub_get_glyph_dsc,                            \
        .get_glyph_bitmap    = stub_get_glyph_bitmap,                         \
        .line_height         = 16,                                            \
        .base_line           = 3,                                             \
        .subpx               = LV_FONT_SUBPX_NONE,                            \
        .underline_position  = -2,                                            \
        .underline_thickness = 1,                                             \
        .dsc                 = NULL,                                           \
        .fallback            = &fallback_font,                                \
        .user_data           = NULL,                                          \
    }

/*
 * calibri_30             (215 KB)  — only in EEZ font table
 * consolai_13             (45 KB)  — only in EEZ font table
 * bebas_neue_regular52   (854 KB)  — was used by Flappy Bird, now uses bebas_neue58
 */
FONT_STUB(ui_font_calibri_30,           lv_font_montserrat_14);
FONT_STUB(ui_font_consolai_13,          lv_font_montserrat_14);
FONT_STUB(ui_font_bebas_neue_regular52, lv_font_montserrat_14);
