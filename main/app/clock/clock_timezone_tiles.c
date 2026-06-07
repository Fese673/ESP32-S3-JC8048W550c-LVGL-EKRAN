#include "clock_timezone_tiles.h"
#include "screens.h"
#include "eez-flow.h"
#include "lvgl.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>

#define TILE_W           660
#define TILE_H            68
#define TILE_X            83   // centered over kfe tiles (86..740) → 3px overhang each side
#define TILE_Y           45
#define COL_W           165
#define FONT_NAME       &ui_font_montserrat_regular_16
#define FONT_TIME       &ui_font_calibri_30
#define COLOR_NAME      lv_color_hex(0xb0acb5)
#define COLOR_TIME      lv_color_hex(0xffffff)

static lv_obj_t *s_tile = NULL;

static lv_obj_t *s_lbl_pl_name = NULL;
static lv_obj_t *s_lbl_pl_time = NULL;
static lv_obj_t *s_lbl_usa_name = NULL;
static lv_obj_t *s_lbl_usa_time = NULL;
static lv_obj_t *s_lbl_jp_name = NULL;
static lv_obj_t *s_lbl_jp_time = NULL;
static lv_obj_t *s_lbl_ru_name = NULL;
static lv_obj_t *s_lbl_ru_time = NULL;

static char s_buf_pl[6];
static char s_buf_usa[6];
static char s_buf_jp[6];
static char s_buf_ru[6];

static int s_last_h_pl = -1;
static int s_last_m_pl = -1;
static int s_last_h_usa = -1;
static int s_last_m_usa = -1;
static int s_last_h_jp = -1;
static int s_last_m_jp = -1;
static int s_last_h_ru = -1;
static int s_last_m_ru = -1;

static lv_obj_t *create_col_label(lv_obj_t *parent, const lv_font_t *font,
                                   lv_color_t color, int col_x, int col_w,
                                   int y) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_pos(lbl, col_x, y);
    lv_obj_set_size(lbl, col_w, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text_static(lbl, "--:--");
    return lbl;
}

static void create_separator(lv_obj_t *parent, int x) {
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_set_pos(sep, x, 10);
    lv_obj_set_size(sep, 1, TILE_H - 20);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x7a7580), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sep, 0, LV_PART_MAIN);
}

void clock_timezone_tiles_init(void) {
    if (s_tile) return;
    if (!objects.zegar) return;

    s_tile = lv_obj_create(objects.zegar);
    lv_obj_set_pos(s_tile, TILE_X, TILE_Y);
    lv_obj_set_size(s_tile, TILE_W, TILE_H);
    lv_obj_remove_flag(s_tile, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(s_tile, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_tile, lv_color_hex(0x18161b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_tile, lv_color_hex(0x7a7580), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tile, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_tile, 12, LV_PART_MAIN);

    int x_pl  = 0;
    int x_usa = COL_W;
    int x_jp  = COL_W * 2;
    int x_ru  = COL_W * 3;

    s_lbl_pl_name = create_col_label(s_tile, FONT_NAME, COLOR_NAME,
                                      x_pl, COL_W, 8);
    lv_label_set_text_static(s_lbl_pl_name, "POLSKA");

    s_lbl_pl_time = create_col_label(s_tile, FONT_TIME, COLOR_TIME,
                                      x_pl, COL_W, 30);

    s_lbl_usa_name = create_col_label(s_tile, FONT_NAME, COLOR_NAME,
                                       x_usa, COL_W, 8);
    lv_label_set_text_static(s_lbl_usa_name, "USA");

    s_lbl_usa_time = create_col_label(s_tile, FONT_TIME, COLOR_TIME,
                                       x_usa, COL_W, 30);

    s_lbl_jp_name = create_col_label(s_tile, FONT_NAME, COLOR_NAME,
                                      x_jp, COL_W, 8);
    lv_label_set_text_static(s_lbl_jp_name, "JAPONIA");

    s_lbl_jp_time = create_col_label(s_tile, FONT_TIME, COLOR_TIME,
                                      x_jp, COL_W, 30);

    s_lbl_ru_name = create_col_label(s_tile, FONT_NAME, COLOR_NAME,
                                      x_ru, COL_W, 8);
    lv_label_set_text_static(s_lbl_ru_name, "ROSJA");

    s_lbl_ru_time = create_col_label(s_tile, FONT_TIME, COLOR_TIME,
                                      x_ru, COL_W, 30);

    create_separator(s_tile, x_usa);
    create_separator(s_tile, x_jp);
    create_separator(s_tile, x_ru);
}

static void update_col(lv_obj_t *lbl, char *buf, size_t buf_sz,
                        int h, int m, int *last_h, int *last_m) {
    if (h == *last_h && m == *last_m) return;
    *last_h = h;
    *last_m = m;
    snprintf(buf, buf_sz, "%02d:%02d", h, m);
    lv_label_set_text_static(lbl, buf);
}

void clock_timezone_tiles_update(bool valid,
                                  int h_pl, int m_pl,
                                  int h_usa, int m_usa,
                                  int h_jp, int m_jp,
                                  int h_ru, int m_ru) {
    if (!s_tile) return;

    if (!valid) {
        lv_label_set_text_static(s_lbl_pl_time,  "--:--");
        lv_label_set_text_static(s_lbl_usa_time, "--:--");
        lv_label_set_text_static(s_lbl_jp_time,  "--:--");
        lv_label_set_text_static(s_lbl_ru_time,  "--:--");
        s_last_h_pl = s_last_m_pl = -1;
        s_last_h_usa = s_last_m_usa = -1;
        s_last_h_jp = s_last_m_jp = -1;
        s_last_h_ru = s_last_m_ru = -1;
        return;
    }

    update_col(s_lbl_pl_time,  s_buf_pl,  sizeof(s_buf_pl),
               h_pl, m_pl, &s_last_h_pl, &s_last_m_pl);

    update_col(s_lbl_usa_time, s_buf_usa, sizeof(s_buf_usa),
               h_usa, m_usa, &s_last_h_usa, &s_last_m_usa);

    update_col(s_lbl_jp_time,  s_buf_jp,  sizeof(s_buf_jp),
               h_jp, m_jp, &s_last_h_jp, &s_last_m_jp);

    update_col(s_lbl_ru_time,  s_buf_ru,  sizeof(s_buf_ru),
               h_ru, m_ru, &s_last_h_ru, &s_last_m_ru);
}
