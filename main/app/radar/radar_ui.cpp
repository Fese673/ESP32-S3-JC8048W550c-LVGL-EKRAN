// Ekran radaru LD2410 — radar-sweep. Geometria co do piksela:
// tresc x27..774, pasek statusu y55..99, sweep C=(400,370) R=264
// (9 okregow co 33 px = bramki). Po prawej ramka CZULOSC (3 presety OUT).
// Paleta 1:1 z music_ui_handler.c + zielony/amber z geometrii.
#include "app/radar/radar.h"
#include "app/radar/radar_internal.h"
#include "ui/ui/fonts.h"
#include "utils/gesture/gesture.h"
#include <math.h>
#include <stdio.h>

namespace {

constexpr uint32_t C_BORDER = 0x7a7580;
constexpr uint32_t C_TXT   = 0xffffff;
constexpr uint32_t C_DIM   = 0xb0acb5;
constexpr uint32_t C_GREEN = 0x22c55e;
constexpr uint32_t C_AMBER = 0xf59e0b;
constexpr uint32_t C_GRAY  = 0x5a5760;
// Presety czulosci OUT (spojne z presetami geometrii).
constexpr uint32_t C_BG_BTN      = 0x1e1b22;
constexpr uint32_t C_BG_ACTIVE   = 0x2d2834;
constexpr uint32_t C_BORDER_DIM  = 0x4a4750;

constexpr int kCx = 400;
constexpr int kCy = 370;
constexpr int kRout = 264;
constexpr int kRingStep = 33;   // 264 / 8 bramek

lv_obj_t *s_status = nullptr;
lv_obj_t *s_mov_val = nullptr;
lv_obj_t *s_stat_val = nullptr;
lv_obj_t *s_det_val = nullptr;
lv_obj_t *s_beam = nullptr;
lv_obj_t *s_mov_blip = nullptr;
lv_obj_t *s_stat_blip = nullptr;
lv_point_precise_t s_beam_pts[2] = {{kCx, kCy}, {kCx, kCy - kRout}};
int s_beam_deg = -80;  // kat od pionu, ping-pong +-80
int s_beam_dir = 1;

// Presety czulosci OUT: 0=SREDNIA, 1=WYSOKA, 2=MALA (kolejnosc UI:
// WYSOKA/SREDNIA/MALA od gory). Stan idzie do STM32 (flash truth).
lv_obj_t *s_sens_btn[3] = {nullptr};
static const uint8_t kSensOrder[3] = {1U, 0U, 2U};  // etykieta -> kod
static const char *const kSensTxt[3] = {"WYSOKA", "ŚREDNIA", "MAŁA"};
int s_last_sens = -1;

lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w,
                     const lv_font_t *font, uint32_t color, const char *txt) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    if (w > 0) lv_obj_set_width(l, w);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_label_set_text_static(l, txt);
    return l;
}

lv_obj_t *make_dot(lv_obj_t *parent, int d, uint32_t color) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, d, d);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, d / 2, LV_PART_MAIN);
    return o;
}

lv_obj_t *make_ring(lv_obj_t *parent, int r) {
    lv_obj_t *o = lv_obj_create(parent);
    const int d = 2 * r + 2;
    lv_obj_set_pos(o, kCx - d / 2, kCy - d / 2);
    lv_obj_set_size(o, d, d);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, lv_color_hex(C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(o, d / 2, LV_PART_MAIN);
    return o;
}

lv_obj_t *make_sens_button(lv_obj_t *parent, int x, int y, int w, int h,
                           const char *txt) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_BG_BTN), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_hex(C_BORDER_DIM), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(l, &ui_font_montserrat_regular_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(C_DIM), LV_PART_MAIN);
    lv_label_set_text_static(l, txt);
    return b;
}

void sens_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint8_t code = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    radar_logic_set_sens(code);
}

// dystans cm -> promien px (skala 600 cm = R_out)
int dist_to_r(uint16_t dist_cm) {
    if (dist_cm >= 600) return kRout;
    return (int)dist_cm * 11 / 25;
}

}  // namespace

void radar_ui_build(lv_obj_t *screen) {
    if (screen == nullptr || s_status != nullptr) return;  // raz (persistent)

    // --- pasek statusu y55..99 ---
    s_status = make_label(screen, 27, 56, 200, &ui_font_bebas_neue35, C_GRAY, "OFFLINE");
    make_label(screen, 250, 58, 170, &ui_font_montserrat_regular_14, C_DIM, "RUCH");
    s_mov_val = make_label(screen, 250, 76, 170, &ui_font_montserrat_regular_16, C_TXT, "---");
    make_label(screen, 430, 58, 170, &ui_font_montserrat_regular_14, C_DIM, "POSTOJ");
    s_stat_val = make_label(screen, 430, 76, 170, &ui_font_montserrat_regular_16, C_TXT, "---");
    make_label(screen, 620, 58, 150, &ui_font_montserrat_regular_14, C_DIM, "ZASIEG");
    s_det_val = make_label(screen, 620, 76, 150, &ui_font_montserrat_regular_16, C_TXT, "---");

    // --- sweep: okregi bramek 1..8 + kropka srodka ---
    for (int i = 1; i <= 8; i++) make_ring(screen, i * kRingStep);
    lv_obj_t *c = make_dot(screen, 6, C_BORDER);
    lv_obj_set_pos(c, kCx - 3, kCy - 3);
    make_label(screen, 668, 360, 60, &ui_font_montserrat_regular_14, C_DIM, "6m");

    // --- wiazka + blipy ---
    s_beam = lv_line_create(screen);
    lv_obj_set_style_line_color(s_beam, lv_color_hex(C_GREEN), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_beam, 2, LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_beam, LV_OPA_50, LV_PART_MAIN);
    lv_line_set_points(s_beam, s_beam_pts, 2);
    s_mov_blip = make_dot(screen, 12, C_AMBER);
    s_stat_blip = make_dot(screen, 12, C_DIM);
    lv_obj_add_flag(s_mov_blip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_stat_blip, LV_OBJ_FLAG_HIDDEN);

    // --- slupki bramek usuniete (dane bramek niedostepne) — zostaje sweep.

    // --- ramka CZULOSC: 3 presety po prawej (strefa x666..796, y96..336;
    // nie koliduje z sweepem max x=665, "6m" y360 ani swipe y>=390).
    {
        lv_obj_t *fr = lv_obj_create(screen);
        lv_obj_set_pos(fr, 666, 96);
        lv_obj_set_size(fr, 130, 240);
        lv_obj_remove_flag(fr, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(fr, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(fr, lv_color_hex(C_BG_BTN), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(fr, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(fr, lv_color_hex(C_BORDER_DIM), LV_PART_MAIN);
        lv_obj_set_style_border_width(fr, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(fr, 10, LV_PART_MAIN);

        lv_obj_t *t = lv_label_create(fr);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 6);
        lv_obj_set_style_text_font(t, &ui_font_montserrat_regular_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(t, lv_color_hex(C_DIM), LV_PART_MAIN);
        lv_label_set_text_static(t, "CZUŁOŚĆ");

        for (int i = 0; i < 3; i++) {
            lv_obj_t *b = make_sens_button(fr, 6, 34 + i * 66, 118, 60,
                                           kSensTxt[i]);
            lv_obj_add_event_cb(b, sens_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)kSensOrder[i]);
            s_sens_btn[i] = b;
        }
    }

    gesture_register_recursive(screen);
}

void radar_ui_sync(uint8_t presence, uint16_t mov_dist, uint8_t mov_energy,
                   uint16_t stat_dist, uint8_t stat_energy, uint16_t detect_dist,
                   const uint8_t *mov_gates, const uint8_t *stat_gates,
                   uint8_t sens) {
    if (s_status == nullptr) return;

    (void)mov_gates;   // bramki nie sa juz rysowane (dane niedostepne)
    (void)stat_gates;

    char buf[32];
    const bool offline = (presence & 0x80U) != 0;
    const bool somebody = (presence & 0x01U) != 0;
    const bool moving = (presence & 0x02U) != 0;
    const bool stationary = (presence & 0x04U) != 0;

    lv_label_set_text(s_status, offline ? "OFFLINE" : (somebody ? "OBECNY" : "PUSTO"));
    lv_obj_set_style_text_color(s_status,
                                lv_color_hex(offline ? C_GRAY : (somebody ? C_GREEN : C_GRAY)),
                                LV_PART_MAIN);

    if (moving) {
        snprintf(buf, sizeof(buf), "%uCM E%u", mov_dist, mov_energy);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_mov_val, buf);

    if (stationary) {
        snprintf(buf, sizeof(buf), "%uCM E%u", stat_dist, stat_energy);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_stat_val, buf);

    snprintf(buf, sizeof(buf), "%uCM", detect_dist);
    lv_label_set_text(s_det_val, buf);

    // --- wiazka ping-pong +-80 stopni (caly czas, tez przy OFFLINE) ---
    s_beam_deg += 6 * s_beam_dir;
    if (s_beam_deg >= 80) {
        s_beam_deg = 80;
        s_beam_dir = -1;
    } else if (s_beam_deg <= -80) {
        s_beam_deg = -80;
        s_beam_dir = 1;
    }
    const float a = (float)s_beam_deg * 3.14159265f / 180.0f;
    s_beam_pts[0].x = kCx;
    s_beam_pts[0].y = kCy;
    s_beam_pts[1].x = (float)kCx + (float)kRout * sinf(a);
    s_beam_pts[1].y = (float)kCy - (float)kRout * cosf(a);
    lv_line_set_points(s_beam, s_beam_pts, 2);

    // --- blipy: ruch -25 stopni, postoj +25 (brak azymutu z LD2410) ---
    if (!offline && moving) {
        const int r = dist_to_r(mov_dist);
        const int d = 12 + (int)mov_energy * 20 / 100;  // 12..32 px
        lv_obj_set_size(s_mov_blip, d, d);
        lv_obj_set_style_radius(s_mov_blip, d / 2, LV_PART_MAIN);
        lv_obj_set_pos(s_mov_blip, kCx - r * 423 / 1000 - d / 2, kCy - r * 906 / 1000 - d / 2);
        lv_obj_clear_flag(s_mov_blip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_mov_blip, LV_OBJ_FLAG_HIDDEN);
    }
    if (!offline && stationary) {
        const int r = dist_to_r(stat_dist);
        const int d = 12 + (int)stat_energy * 20 / 100;
        lv_obj_set_size(s_stat_blip, d, d);
        lv_obj_set_style_radius(s_stat_blip, d / 2, LV_PART_MAIN);
        lv_obj_set_pos(s_stat_blip, kCx + r * 423 / 1000 - d / 2, kCy - r * 906 / 1000 - d / 2);
        lv_obj_clear_flag(s_stat_blip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_stat_blip, LV_OBJ_FLAG_HIDDEN);
    }

    // --- czulosc OUT: podswietlenie aktywnego presetu (jak presety geometrii) ---
    if ((int)sens != s_last_sens) {
        s_last_sens = (int)sens;
        for (int i = 0; i < 3; i++) {
            if (s_sens_btn[i] == nullptr) continue;
            const bool act = (kSensOrder[i] == sens);
            lv_obj_set_style_bg_color(s_sens_btn[i],
                                      lv_color_hex(act ? C_BG_ACTIVE : C_BG_BTN),
                                      LV_PART_MAIN);
            lv_obj_set_style_border_color(s_sens_btn[i],
                                          lv_color_hex(act ? C_TXT : C_BORDER_DIM),
                                          LV_PART_MAIN);
            lv_obj_t *l = lv_obj_get_child(s_sens_btn[i], 0);
            if (l != nullptr) {
                lv_obj_set_style_text_color(l,
                                            lv_color_hex(act ? C_TXT : C_DIM),
                                            LV_PART_MAIN);
            }
        }
    }
}
