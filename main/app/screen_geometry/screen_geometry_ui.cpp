// Panel GEOMETRIA EKRANU — UI (widgety LVGL + style premium jak Muzyka).
// Zero logiki ruchu: eventy wola geom_logic_* z screen_geometry_logic.cpp.
// Wszystko tworzone RAZ (persistent, por. ERR_017), tick tylko odswieza teksty.

#include "screen_geometry_internal.h"
#include "screens.h"
#include "fonts.h"
#include "gesture.h"
#include <stdio.h>

// Paleta 1:1 z music_ui_handler.c
#define C_BG_PANEL   0x18161b
#define C_BG_BTN     0x1e1b22
#define C_BG_ACTIVE  0x2d2834
#define C_BORDER     0x7a7580
#define C_BORDER_DIM 0x4a4750
#define C_TXT        0xffffff
#define C_TXT_DIM    0xb0acb5
#define C_TXT_FAINT  0x85808a
#define C_AMBER      0xf59e0b
#define C_GREEN      0x22c55e
#define C_RED        0xef4444 // tylko napis LD2410C-OFF (inne ekrany nie uzywaja)

// Strefa ekranu: top-bar EEZ (zegar + ikony) zajmuje y<50, nasze panele od y=55.
#define P1_X 27
#define P1_Y 55
#define P1_W 460
#define P1_H 385
#define P2_X 502
#define P2_Y 55
#define P2_W 271
#define P2_H 385

static lv_obj_t *s_manual_panel = NULL;
static lv_obj_t *s_preset_panel = NULL;

static lv_obj_t *s_pos_label = NULL;
static lv_obj_t *s_sub_label = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_speed_val_label = NULL;
static lv_obj_t *s_speed_slider = NULL;

static lv_obj_t *s_left_btn = NULL;
static lv_obj_t *s_stop_btn = NULL;
static lv_obj_t *s_right_btn = NULL;

static lv_obj_t *s_cal_btn = NULL;
static lv_obj_t *s_cal_btn_label = NULL;
static lv_obj_t *s_cal_hint = NULL;
static lv_obj_t *s_exec_btn_label = NULL;
static lv_obj_t *s_radar_btn = NULL;
static lv_obj_t *s_radar_btn_label = NULL;

#define GEOM_MAX_PRESETS 6
static lv_obj_t *s_preset_btns[GEOM_MAX_PRESETS] = { NULL };
static lv_obj_t *s_preset_labels[GEOM_MAX_PRESETS] = { NULL };
static int s_preset_built = 0;

static int s_last_hold = 99;
static int s_last_status = -1;
static int s_last_sel = -1;
static int s_last_home = -1;
static int s_last_pulse = -1;
static int s_last_radar = -1;
static int s_last_radar_out = -1;

static void style_premium_slider(lv_obj_t *slider)
{
    if (!slider) return;
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x28252c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x85808a), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(0x7a7580), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 1, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, -1, LV_PART_KNOB);
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(p, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(p, lv_color_hex(C_BG_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(p, lv_color_hex(C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(p, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(p, 12, LV_PART_MAIN);
    return p;
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w,
                            const lv_font_t *font, uint32_t color, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    if (w > 0) lv_obj_set_width(l, w);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_label_set_text_static(l, txt);
    return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h, int radius)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_BG_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_hex(C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(b, radius, LV_PART_MAIN);
    return b;
}

static lv_obj_t *make_small_button(lv_obj_t *parent, int x, int y, int w, int h,
                                   const char *txt)
{
    lv_obj_t *b = make_button(parent, x, y, w, h, 6);
    lv_obj_set_style_border_color(b, lv_color_hex(C_BORDER_DIM), LV_PART_MAIN);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(l, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(C_TXT_DIM), LV_PART_MAIN);
    lv_label_set_text_static(l, txt);
    return b;
}

// Cienka kreska rozdzielajaca sekcje (jazda / kalibracja).
static lv_obj_t *make_divider(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *d = lv_obj_create(parent);
    lv_obj_set_pos(d, x, y);
    lv_obj_set_size(d, w, 2);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(d, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(d, lv_color_hex(C_BORDER_DIM), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(d, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(d, 0, LV_PART_MAIN);
    return d;
}

// --- eventy (LVGL task, nieblokuja) ---

static void left_hold_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        geom_logic_hold_start(-1);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        geom_logic_hold_stop();
    }
}

static void right_hold_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        geom_logic_hold_start(1);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        geom_logic_hold_stop();
    }
}

static void stop_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        geom_logic_stop_press();
    }
}

static void speed_slider_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int v = (int)lv_slider_get_value(slider);
    geom_logic_set_speed(v);
    if (s_speed_val_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d KR/S", geom_logic_speed());
        lv_label_set_text(s_speed_val_label, buf);
    }
}

static void speed_preset_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    // FIX: handler jest autorytatywny — LV_ANIM_ON nie dawal synchronicznego
    // VALUE_CHANGED, przez co label zostawal ze stara wartoscia. Ustawiamy
    // slider/logike/label jawnie i deterministycznie (ANIM_OFF).
    geom_logic_set_speed(v);
    geom_logic_speed_commit(); // preset = commit SET_SPEED od razu
    v = geom_logic_speed();
    if (s_speed_slider) {
        lv_slider_set_value(s_speed_slider, v, LV_ANIM_OFF);
    }
    if (s_speed_val_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d KR/S", v);
        lv_label_set_text(s_speed_val_label, buf);
    }
}

static void angle_preset_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int deg = (int)(intptr_t)lv_event_get_user_data(e);
    geom_logic_preset_tap(deg); // single = cel, double = dom po resecie
}

static void cal_next_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    geom_logic_cal_next();
}

static void execute_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    geom_logic_execute_selected();
}

static void radar_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    geom_logic_radar_toggle();
}

// --- budowa ---

static void build_manual_panel(void)
{
    lv_obj_t *p = make_panel(objects.geometria_ekranu, P1_X, P1_Y, P1_W, P1_H);
    s_manual_panel = p;

    make_label(p, 20, 15, 0, &ui_font_montserrat_regular_16, C_TXT_DIM,
               "STEROWANIE RĘCZNE");

    // LD2410C: automat obecnosci (flaga we flash STM32, zielony/czerwony).
    // Gorny prawy rog lewego panelu — pomniejszony, z odstepem od napisow
    // ("STEROWANIE RECZNE" po lewej, "POS" ponizej).
    // Napis = stan funkcji; RAMKA = sygnal OUT na zywo (zielona = widzi kogos).
    s_radar_btn = make_button(p, 240, 8, 200, 32, 8);
    lv_obj_set_style_border_width(s_radar_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_radar_btn, lv_color_hex(C_RED), LV_PART_MAIN);
    lv_obj_add_event_cb(s_radar_btn, radar_cb, LV_EVENT_CLICKED, NULL);
    s_radar_btn_label = lv_label_create(s_radar_btn);
    lv_obj_align(s_radar_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(s_radar_btn_label, &ui_font_montserrat_regular_14,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_radar_btn_label, lv_color_hex(C_RED), LV_PART_MAIN);
    lv_label_set_text_static(s_radar_btn_label, "RADAR MIKROFALOWY");

    // Presety czasu auto-zjazdu przeniesione na ekran RADAR (panel opcji);
    // tu zostaje sam kafelek RADAR MIKROFALOWY (WL/WYL automatu).
    s_pos_label = make_label(p, 20, 44, 320, &ui_font_bebas_neue30, C_TXT, "POS 0");
    // Label nie moze konsumowac dotyku.
    lv_obj_remove_flag(s_pos_label, LV_OBJ_FLAG_CLICKABLE);
    s_sub_label = make_label(p, 20, 82, 400, &ui_font_montserrat_regular_14,
                             C_TXT_FAINT, "0.00 OBR | 400 KR/S");
    s_status_label = make_label(p, 20, 106, 400, &ui_font_montserrat_regular_16,
                                C_TXT_FAINT, "STATUS: GOTOWY");

    // Rząd DÓŁ / STOP / GÓRA (y=140, h=76 — powyzej strefy swipe 390)
    s_left_btn = make_button(p, 30, 140, 110, 76, 16);
    lv_obj_add_event_cb(s_left_btn, left_hold_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_left_btn, left_hold_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_left_btn, left_hold_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *ll = lv_label_create(s_left_btn);
    lv_obj_align(ll, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_text_font(ll, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(ll, lv_color_hex(C_TXT), LV_PART_MAIN);
    lv_label_set_text_static(ll, LV_SYMBOL_LEFT);
    lv_obj_t *llc = lv_label_create(s_left_btn);
    lv_obj_align(llc, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_font(llc, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(llc, lv_color_hex(C_TXT_DIM), LV_PART_MAIN);
    lv_label_set_text_static(llc, "DÓŁ");

    s_stop_btn = make_button(p, 170, 140, 120, 76, 16);
    lv_obj_add_event_cb(s_stop_btn, stop_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_t *sl = lv_label_create(s_stop_btn);
    lv_obj_align(sl, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_text_font(sl, &ui_font_montserrat_regular_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(sl, lv_color_hex(C_TXT), LV_PART_MAIN);
    lv_label_set_text_static(sl, "STOP");
    lv_obj_t *slc = lv_label_create(s_stop_btn);
    lv_obj_align(slc, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_font(slc, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(slc, lv_color_hex(C_TXT_DIM), LV_PART_MAIN);
    lv_label_set_text_static(slc, "NATYCHMIAST");

    s_right_btn = make_button(p, 320, 140, 110, 76, 16);
    lv_obj_add_event_cb(s_right_btn, right_hold_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_right_btn, right_hold_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_right_btn, right_hold_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *rl = lv_label_create(s_right_btn);
    lv_obj_align(rl, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(rl, lv_color_hex(C_TXT), LV_PART_MAIN);
    lv_label_set_text_static(rl, LV_SYMBOL_RIGHT);
    lv_obj_t *rlc = lv_label_create(s_right_btn);
    lv_obj_align(rlc, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_text_font(rlc, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(rlc, lv_color_hex(C_TXT_DIM), LV_PART_MAIN);
    lv_label_set_text_static(rlc, "GÓRA");

    make_label(p, 20, 250, 0, &ui_font_montserrat_regular_16, C_TXT_DIM, "PRĘDKOŚĆ");
    {
        char spd_init[16];
        snprintf(spd_init, sizeof(spd_init), "%d KR/S", geom_logic_speed());
        s_speed_val_label = make_label(p, 330, 250, 110, &ui_font_montserrat_regular_16,
                                       C_TXT, "400 KR/S");
        lv_label_set_text(s_speed_val_label, spd_init); // wartosc z NVS, nie default
    }
    lv_obj_set_style_text_align(s_speed_val_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);

    s_speed_slider = lv_slider_create(p);
    lv_obj_set_pos(s_speed_slider, 20, 278);
    lv_obj_set_size(s_speed_slider, 420, 12);
    lv_slider_set_range(s_speed_slider, 100, 1000);
    lv_slider_set_value(s_speed_slider, geom_logic_speed(), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_speed_slider, speed_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    // Po puszczeniu palca: SET_SPEED do STM32 (auto-powrot jedzie ta
    // predkoscia). Drag tylko aktualizuje RAM/label.
    lv_obj_add_event_cb(s_speed_slider,
                        [](lv_event_t *e) {
                            if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
                                geom_logic_speed_commit();
                            }
                        },
                        LV_EVENT_RELEASED, NULL);
    // Znacznik: main.cpp NIE nadpisuje tego slidera globalnym stylem
    // (inaczej headline premium styl znika przy wejsciu na ekran).
    lv_obj_add_flag(s_speed_slider, LV_OBJ_FLAG_USER_1);
    style_premium_slider(s_speed_slider);

    static const char *spd_txt[] = { "100", "400", "700", "1000" };
    static const int spd_val[] = { 100, 400, 700, 1000 };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = make_small_button(p, 20 + i * 107, 302, 99, 30, spd_txt[i]);
        lv_obj_add_event_cb(b, speed_preset_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)spd_val[i]);
    }
}

static void build_preset_panel(void)
{
    lv_obj_t *p = make_panel(objects.geometria_ekranu, P2_X, P2_Y, P2_W, P2_H);
    s_preset_panel = p;

    make_label(p, 20, 15, 0, &ui_font_montserrat_regular_16, C_TXT_DIM, "PRESETY KĄTÓW");

    // Siatka z listy logiki (strojenie w kodzie: kAnglePresets). 2 kolumny.
    // Klik = wybor celu (highlight), DWUKLIK = pozycja bazowa po resecie (zielony).
    int n = geom_logic_preset_count();
    if (n > GEOM_MAX_PRESETS) n = GEOM_MAX_PRESETS;
    s_preset_built = n;
    for (int i = 0; i < n; i++) {
        int col = i % 2;
        int row = i / 2;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d\xC2\xB0", geom_logic_preset_deg_at(i));
        lv_obj_t *b = make_small_button(p, 20 + col * 120, 45 + row * 54, 110, 44, "?");
        lv_obj_t *lbl = lv_obj_get_child(b, 0);
        lv_label_set_text(lbl, buf); // kopia do bufora safe_label (<64 B)
        lv_obj_add_event_cb(b, angle_preset_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)geom_logic_preset_deg_at(i));
        s_preset_btns[i] = b;
        s_preset_labels[i] = lbl;
    }

    // --- WYKONAJ: wysyla wybrany preset do ZEGAR ESP32 (zapis + wykonanie).
    // Etykieta pokazuje cel — osobny label CEL zbedny.
    lv_obj_t *rb = make_button(p, 20, 210, 231, 34, 8);
    lv_obj_add_event_cb(rb, execute_cb, LV_EVENT_CLICKED, NULL);
    s_exec_btn_label = lv_label_create(rb);
    lv_obj_align(s_exec_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(s_exec_btn_label, &ui_font_montserrat_regular_16,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_exec_btn_label, lv_color_hex(C_TXT), LV_PART_MAIN);
    lv_label_set_text_static(s_exec_btn_label, "JEDŹ DO 45°");

    // Kreska: jazda/presety powyzej, kalibracja poniżej.
    make_divider(p, 20, 256, 231);

    // --- Kalibracja reczna: jeden przycisk, dwa potwierdzenia, powrot do 0.
    make_label(p, 20, 270, 0, &ui_font_montserrat_regular_16, C_TXT_DIM, "KALIBRACJA");

    lv_obj_t *cb = make_button(p, 20, 294, 231, 36, 8);
    lv_obj_add_event_cb(cb, cal_next_cb, LV_EVENT_CLICKED, NULL);
    s_cal_btn = cb;
    s_cal_btn_label = lv_label_create(cb);
    lv_obj_align(s_cal_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(s_cal_btn_label, &ui_font_montserrat_regular_16,
                               LV_PART_MAIN);
    lv_obj_set_style_text_color(s_cal_btn_label, lv_color_hex(C_TXT), LV_PART_MAIN);
    lv_label_set_text_static(s_cal_btn_label, "KALIBRUJ");

    s_cal_hint = lv_label_create(p);
    lv_obj_set_pos(s_cal_hint, 20, 338);
    lv_obj_set_width(s_cal_hint, 231);
    lv_label_set_long_mode(s_cal_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_cal_hint, lv_color_hex(C_TXT_FAINT), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_cal_hint, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_label_set_text_static(s_cal_hint, "KALIBRACJA RĘCZNA: NAJPIERW 0°, POTEM 90°.");
}

void geom_ui_build(lv_obj_t *screen)
{
    if (!screen || s_manual_panel) return; // raz (persistent)
    build_manual_panel();
    build_preset_panel();
    // Nowe widgety tez pod system gestow (idempotentne, jak sensors_wiki_init).
    gesture_register_recursive(screen);
}

void geom_ui_restyle(void)
{
    // main.cpp::style_widgets_recursive nadpisuje slidery przy wejsciu na ekran.
    style_premium_slider(s_speed_slider);
}

void geom_ui_sync(void)
{
    if (!s_manual_panel) return;

    char buf[48];
    int32_t pos = geom_logic_pos();
    int speed = geom_logic_speed();

    snprintf(buf, sizeof(buf), "POS %ld", (long)pos);
    lv_label_set_text(s_pos_label, buf);

    double obr = (double)pos / 2048.0;
    snprintf(buf, sizeof(buf), "%.2f OBR | %d KR/S", obr, speed);
    lv_label_set_text(s_sub_label, buf);

    // Sync slidera/labelu z logika (adopcja predkosci z NVS ZEGAR-a,
    // np. po reboocie wraca 1000). Palec na sliderze ma pierwszenstwo —
    // w trakcie drag nie szarpiemy (eventy i tak wola set_speed).
    if (s_speed_val_label) {
        snprintf(buf, sizeof(buf), "%d KR/S", speed);
        lv_label_set_text(s_speed_val_label, buf);
    }
    if (s_speed_slider && !lv_obj_has_state(s_speed_slider, LV_STATE_PRESSED)) {
        if ((int)lv_slider_get_value(s_speed_slider) != speed) {
            lv_slider_set_value(s_speed_slider, speed, LV_ANIM_OFF);
        }
    }

    lv_label_set_text(s_status_label, geom_logic_status_text());

    // Puls potwierdzenia zapisu kalibracji: przycisk swieci na zielono 1,5 s.
    {
        int pulse = geom_logic_cal_saved_pulse();
        if (pulse != s_last_pulse) {
            s_last_pulse = pulse;
            if (s_cal_btn) {
                lv_obj_set_style_bg_color(s_cal_btn,
                                          lv_color_hex(pulse ? 0x1e3a28 : C_BG_BTN),
                                          LV_PART_MAIN);
                lv_obj_set_style_border_color(s_cal_btn,
                                              lv_color_hex(pulse ? 0x22c55e : C_BORDER),
                                              LV_PART_MAIN);
            }
        }
    }

    if (s_cal_btn_label) {
        lv_label_set_text(s_cal_btn_label, geom_logic_cal_btn_text());
    }
    if (s_cal_hint) {
        lv_label_set_text(s_cal_hint, geom_logic_cal_hint());
    }

    // Wybor celu: highlight presetu + etykieta JEDŹ DO.
    int sel = geom_logic_selected_preset();
    if (s_exec_btn_label) {
        snprintf(buf, sizeof(buf), "JEDŹ DO %d\xC2\xB0", sel);
        lv_label_set_text(s_exec_btn_label, buf);
    }
    if (sel != s_last_sel) {
        s_last_sel = sel;
        for (int i = 0; i < s_preset_built; i++) {
            if (!s_preset_btns[i]) continue;
            bool act = (geom_logic_preset_deg_at(i) == sel);
            lv_obj_set_style_bg_color(s_preset_btns[i],
                                      lv_color_hex(act ? C_BG_ACTIVE : C_BG_BTN),
                                      LV_PART_MAIN);
            lv_obj_set_style_border_color(s_preset_btns[i],
                                          lv_color_hex(act ? C_TXT : C_BORDER_DIM),
                                          LV_PART_MAIN);
        }
    }

    // Dom po resecie (double-click presetu): cyfry+stopnie na zielono.
    int home = geom_logic_home_preset();
    if (home != s_last_home) {
        s_last_home = home;
        for (int i = 0; i < s_preset_built; i++) {
            if (!s_preset_labels[i]) continue;
            bool is_home = (geom_logic_preset_deg_at(i) == home);
            lv_obj_set_style_text_color(s_preset_labels[i],
                                        lv_color_hex(is_home ? 0x22c55e : C_TXT_DIM),
                                        LV_PART_MAIN);
        }
    }

    int st = (int)geom_logic_status();
    if (st != s_last_status) {
        s_last_status = st;
        uint32_t c = C_TXT_FAINT;
        if (st == GEOM_ST_DRIVE_L || st == GEOM_ST_DRIVE_R) c = C_TXT;
        else if (st == GEOM_ST_FLASH) c = C_AMBER;
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(c), LV_PART_MAIN);
    }

    // LD2410C: napis = stan funkcji (zielony ON / czerwony OFF).
    int ren = geom_logic_radar_en() ? 1 : 0;
    if (ren != s_last_radar) {
        s_last_radar = ren;
        if (s_radar_btn_label) {
            lv_obj_set_style_text_color(s_radar_btn_label,
                                        lv_color_hex(ren ? C_GREEN : C_RED),
                                        LV_PART_MAIN);
        }
    }
    // Ramka = sygnal OUT na zywo: zielona gdy radar widzi kogos, czerwona pusto.
    int rout = geom_logic_radar_out() ? 1 : 0;
    if (rout != s_last_radar_out) {
        s_last_radar_out = rout;
        if (s_radar_btn) {
            lv_obj_set_style_border_color(s_radar_btn,
                                          lv_color_hex(rout ? C_GREEN : C_RED),
                                          LV_PART_MAIN);
        }
    }

    int hold = geom_logic_hold_dir();
    if (hold != s_last_hold) {
        s_last_hold = hold;
        if (s_left_btn) {
            bool act = (hold < 0);
            lv_obj_set_style_bg_color(s_left_btn, lv_color_hex(act ? C_BG_ACTIVE : C_BG_BTN),
                                      LV_PART_MAIN);
            lv_obj_set_style_border_color(s_left_btn,
                                          lv_color_hex(act ? C_TXT : C_BORDER), LV_PART_MAIN);
        }
        if (s_right_btn) {
            bool act = (hold > 0);
            lv_obj_set_style_bg_color(s_right_btn, lv_color_hex(act ? C_BG_ACTIVE : C_BG_BTN),
                                      LV_PART_MAIN);
            lv_obj_set_style_border_color(s_right_btn,
                                          lv_color_hex(act ? C_TXT : C_BORDER), LV_PART_MAIN);
        }
    }
}
