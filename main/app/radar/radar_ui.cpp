// Ekran RADAR — uklad dwudzielny 1:1 z siatka ekranu Muzyka/Geometrii.
// Lewy panel (272x395): opcje radaru — czulosc OUT / czas auto-zjazdu [min] /
// automat WL-WYL / "PLYNNE WYLACZENIE" (bramka dimu 7-seg w STM32).
// Prawy panel (460x395): radar-sweep pomniejszony do R=208 px (8 bramek x
// 26 px = 6 m) ze statusem i kolumnami danych.
// Zero logiki: eventy wola radar_logic_set_sens i screen_geometry_*.
// Wszystko tworzone RAZ (persistent, por. ERR_017), tick 100 ms odswieza teksty.
#include "app/radar/radar.h"
#include "app/radar/radar_internal.h"
#include "app/screen_geometry/screen_geometry.h"
#include "ui/ui/fonts.h"
#include "utils/gesture/gesture.h"
#include <math.h>
#include <stdio.h>

namespace {

// Paleta 1:1 z music_ui_handler.c / screen_geometry_ui.cpp.
constexpr uint32_t C_BG_PANEL   = 0x18161b;
constexpr uint32_t C_BG_BTN     = 0x1e1b22;
constexpr uint32_t C_BG_ACTIVE  = 0x2d2834;
constexpr uint32_t C_BORDER     = 0x7a7580;
constexpr uint32_t C_BORDER_DIM = 0x4a4750;
constexpr uint32_t C_TXT        = 0xffffff;
constexpr uint32_t C_DIM        = 0xb0acb5;
constexpr uint32_t C_FAINT      = 0x85808a;
constexpr uint32_t C_GREEN      = 0x22c55e;
constexpr uint32_t C_RED        = 0xef4444;
constexpr uint32_t C_AMBER      = 0xf59e0b;
constexpr uint32_t C_GRAY       = 0x5a5760;

// Siatka paneli 1:1 z ekranem Muzyka (margines 27, odstep 15, prawy 26).
constexpr int kOptsX = 27, kOptsY = 55, kOptsW = 272, kOptsH = 395;
constexpr int kRadX = 314, kRadY = 55, kRadW = 460, kRadH = 395;

// Sweep w prawym panelu (wspolrzedne lokalne): srodek przy dolnej krawedzi,
// R = 8 x 26 px. Skala: R_out = 600 cm; "+-80 st." wiazka ping-pong.
constexpr int kCx = 220;
constexpr int kCy = 310;
constexpr int kRout = 208;
constexpr int kRingStep = 26;

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
const uint8_t kSensOrder[3] = {1U, 0U, 2U};  // etykieta -> kod
const char *const kSensTxt[3] = {"WYSOKA", "ŚREDNIA", "MAŁA"};
int s_last_sens = -1;

// Presety czasu auto-zjazdu: 30/180/600/2700 s (truth STM32, ac_sanitize).
// UI w minutach: 0,5 / 3 / 10 / 45.
constexpr int kAcloseCount = 4;
const uint16_t kAcloseSec[kAcloseCount] = {30U, 180U, 600U, 2700U};
const char *const kAcloseTxt[kAcloseCount] = {"0,5", "3", "10", "45"};
lv_obj_t *s_aclose_btn[kAcloseCount] = {nullptr};
lv_obj_t *s_aclose_info = nullptr;
int s_last_aclose = -1;

// Automat LD2410C (WL/WYL) — ten sam stan co kafelek na Geometrii.
lv_obj_t *s_radar_btn = nullptr;
lv_obj_t *s_radar_btn_label = nullptr;
int s_last_radar_en = -1;

// "Plynne wylaczenie" — bramka premium dimu 7-seg (dim 2..user% przy
// zjezdzie; truth + echo w STM32, tu tylko HMI).
lv_obj_t *s_onoff_btn = nullptr;
lv_obj_t *s_onoff_btn_label = nullptr;
int s_last_onoff = -1;

lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h) {
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

// Cienka kreska rozdzielajaca sekcje panelu opcji (wzorzec Geometrii).
lv_obj_t *make_divider(lv_obj_t *parent, int x, int y, int w) {
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

// Preset (czulosc / czas / automat): przycisk + wysrodkowany napis.
lv_obj_t *make_preset_button(lv_obj_t *parent, int x, int y, int w, int h,
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
    lv_obj_set_style_text_font(l, &ui_font_montserrat_regular_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(C_DIM), LV_PART_MAIN);
    lv_label_set_text_static(l, txt);
    return b;
}

// Podswietlenie aktywnego presetu (wzorzec presetow Muzyka/Geometrii).
void style_preset(lv_obj_t *b, bool active) {
    if (b == nullptr) return;
    lv_obj_set_style_bg_color(b, lv_color_hex(active ? C_BG_ACTIVE : C_BG_BTN),
                              LV_PART_MAIN);
    lv_obj_set_style_border_color(b, lv_color_hex(active ? C_TXT : C_BORDER_DIM),
                                  LV_PART_MAIN);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (l != nullptr) {
        lv_obj_set_style_text_color(l, lv_color_hex(active ? C_TXT : C_DIM),
                                    LV_PART_MAIN);
    }
}

void sens_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint8_t code = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    radar_logic_set_sens(code);
}

void aclose_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint16_t sec = (uint16_t)(intptr_t)lv_event_get_user_data(e);
    screen_geometry_auto_close_set(sec);
}

void radar_toggle_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    screen_geometry_radar_toggle();
}

void onoff_toggle_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    screen_geometry_onoff_toggle();
}

// dystans cm -> promien px (skala 600 cm = R_out)
int dist_to_r(uint16_t dist_cm) {
    if (dist_cm >= 600) return kRout;
    return (int)dist_cm * kRout / 600;
}

}  // namespace

void radar_ui_build(lv_obj_t *screen) {
    if (screen == nullptr || s_status != nullptr) return;  // raz (persistent)

    // --- prawy panel: radar (sweep + status + kolumny danych) ---
    lv_obj_t *rp = make_panel(screen, kRadX, kRadY, kRadW, kRadH);

    // Pasek statusu (naglowek panelu): stan obecnosci + 3 kolumny danych.
    s_status = make_label(rp, 16, 2, 140, &ui_font_bebas_neue35, C_GRAY, "OFFLINE");
    make_label(rp, 165, 6, 95, &ui_font_montserrat_regular_14, C_DIM, "RUCH");
    s_mov_val = make_label(rp, 165, 24, 95, &ui_font_montserrat_regular_16, C_TXT, "---");
    make_label(rp, 262, 6, 95, &ui_font_montserrat_regular_14, C_DIM, "POSTOJ");
    s_stat_val = make_label(rp, 262, 24, 95, &ui_font_montserrat_regular_16, C_TXT, "---");
    make_label(rp, 358, 6, 95, &ui_font_montserrat_regular_14, C_DIM, "ZASIEG");
    s_det_val = make_label(rp, 358, 24, 95, &ui_font_montserrat_regular_16, C_TXT, "---");

    // Sweep: okregi bramek 1..8 + kropka srodka + skala 6 m.
    for (int i = 1; i <= 8; i++) make_ring(rp, i * kRingStep);
    lv_obj_t *c = make_dot(rp, 6, C_BORDER);
    lv_obj_set_pos(c, kCx - 3, kCy - 3);
    make_label(rp, 432, 300, 26, &ui_font_montserrat_regular_14, C_FAINT, "6m");

    // Wiazka + blipy (barwy z geometrii: amber = ruch, dim = postoj).
    s_beam = lv_line_create(rp);
    lv_obj_set_style_line_color(s_beam, lv_color_hex(C_GREEN), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_beam, 2, LV_PART_MAIN);
    lv_obj_set_style_line_opa(s_beam, LV_OPA_50, LV_PART_MAIN);
    lv_line_set_points(s_beam, s_beam_pts, 2);
    s_mov_blip = make_dot(rp, 12, C_AMBER);
    s_stat_blip = make_dot(rp, 12, C_DIM);
    lv_obj_add_flag(s_mov_blip, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_stat_blip, LV_OBJ_FLAG_HIDDEN);

    // --- lewy panel: opcje radaru (styl premium jak Muzyka) ---
    lv_obj_t *op = make_panel(screen, kOptsX, kOptsY, kOptsW, kOptsH);

    make_label(op, 20, 15, 0, &ui_font_montserrat_regular_16, C_DIM, "OPCJE RADARU");

    // Czulosc OUT: 3 presety w rzedzie (1:1 z ramka CZULOSC radaru).
    make_label(op, 20, 44, 0, &ui_font_montserrat_regular_14, C_DIM, "CZUŁOŚĆ");
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = make_preset_button(op, 20 + i * 80, 62, 72, 40, kSensTxt[i]);
        lv_obj_add_event_cb(b, sens_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)kSensOrder[i]);
        s_sens_btn[i] = b;
    }

    make_divider(op, 20, 112, 232);

    // Czas auto-zjazdu radaru: presety w minutach (0,5 / 3 / 10 / 45).
    make_label(op, 20, 124, 0, &ui_font_montserrat_regular_14, C_DIM,
               "CZAS AUTO-ZJAZDU [MIN]");
    for (int i = 0; i < kAcloseCount; i++) {
        lv_obj_t *b = make_preset_button(op, 20 + i * 59, 142, 53, 40,
                                         kAcloseTxt[i]);
        lv_obj_add_event_cb(b, aclose_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)kAcloseSec[i]);
        s_aclose_btn[i] = b;
    }

    make_divider(op, 20, 192, 232);

    // Automat LD2410C: WL/WYL (persist + echo 0x3B, flaga RADAR_EN).
    s_radar_btn = make_preset_button(op, 20, 204, 232, 48, "RADAR: WYŁĄCZONY");
    lv_obj_set_style_radius(s_radar_btn, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_radar_btn, 2, LV_PART_MAIN);
    s_radar_btn_label = lv_obj_get_child(s_radar_btn, 0);
    lv_obj_set_style_text_font(s_radar_btn_label, &ui_font_montserrat_regular_16,
                               LV_PART_MAIN);
    lv_obj_add_event_cb(s_radar_btn, radar_toggle_cb, LV_EVENT_CLICKED, nullptr);

    // "Plynne wylaczenie": bramka dimu 7-seg (persist + echo 0x3B, bit4).
    s_onoff_btn = make_preset_button(op, 20, 260, 232, 48,
                                     "PŁYNNE WYŁĄCZENIE: WYŁ.");
    lv_obj_set_style_radius(s_onoff_btn, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_onoff_btn, 2, LV_PART_MAIN);
    s_onoff_btn_label = lv_obj_get_child(s_onoff_btn, 0);
    lv_obj_add_event_cb(s_onoff_btn, onoff_toggle_cb, LV_EVENT_CLICKED, nullptr);

    // Odczyt aktywnego czasu (spojny z podswietlonym presetem).
    s_aclose_info = make_label(op, 20, 318, 232, &ui_font_montserrat_regular_14,
                               C_FAINT, "AUTO-ZJAZD: 0,5 MIN");

    gesture_register_recursive(screen);
}

void radar_ui_sync(uint8_t presence, uint16_t mov_dist, uint8_t mov_energy,
                   uint16_t stat_dist, uint8_t stat_energy, uint16_t detect_dist,
                   const uint8_t *mov_gates, const uint8_t *stat_gates,
                   uint8_t sens) {
    if (s_status == nullptr) return;

    (void)mov_gates;   // bramki nie sa rysowane (dane niedostepne)
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

    // Przy OFFLINE kolumny nie moga pokazywac starych dystansow (bity flag
    // presence potrafia zostac z ostatniej ramki).
    if (!offline && moving) {
        snprintf(buf, sizeof(buf), "%uCM E%u", mov_dist, mov_energy);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_mov_val, buf);

    if (!offline && stationary) {
        snprintf(buf, sizeof(buf), "%uCM E%u", stat_dist, stat_energy);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    lv_label_set_text(s_stat_val, buf);

    if (!offline) {
        snprintf(buf, sizeof(buf), "%uCM", detect_dist);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
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
        const int d = 10 + (int)mov_energy * 16 / 100;  // 10..26 px
        lv_obj_set_size(s_mov_blip, d, d);
        lv_obj_set_style_radius(s_mov_blip, d / 2, LV_PART_MAIN);
        lv_obj_set_pos(s_mov_blip, kCx - r * 423 / 1000 - d / 2,
                       kCy - r * 906 / 1000 - d / 2);
        lv_obj_clear_flag(s_mov_blip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_mov_blip, LV_OBJ_FLAG_HIDDEN);
    }
    if (!offline && stationary) {
        const int r = dist_to_r(stat_dist);
        const int d = 10 + (int)stat_energy * 16 / 100;
        lv_obj_set_size(s_stat_blip, d, d);
        lv_obj_set_style_radius(s_stat_blip, d / 2, LV_PART_MAIN);
        lv_obj_set_pos(s_stat_blip, kCx + r * 423 / 1000 - d / 2,
                       kCy - r * 906 / 1000 - d / 2);
        lv_obj_clear_flag(s_stat_blip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_stat_blip, LV_OBJ_FLAG_HIDDEN);
    }

    // --- czulosc OUT: podswietlenie aktywnego presetu ---
    if ((int)sens != s_last_sens) {
        s_last_sens = (int)sens;
        for (int i = 0; i < 3; i++) {
            style_preset(s_sens_btn[i], kSensOrder[i] == sens);
        }
    }

    // --- czas auto-zjazdu: podswietlenie presetu + odczyt [min] ---
    const int ac = screen_geometry_auto_close_sec();
    if (ac != s_last_aclose) {
        s_last_aclose = ac;
        for (int i = 0; i < kAcloseCount; i++) {
            style_preset(s_aclose_btn[i], ac == (int)kAcloseSec[i]);
        }
        if (s_aclose_info != nullptr) {
            char abuf[32];
            if (ac == 30) {
                snprintf(abuf, sizeof(abuf), "AUTO-ZJAZD: 0,5 MIN");
            } else {
                snprintf(abuf, sizeof(abuf), "AUTO-ZJAZD: %d MIN", ac / 60);
            }
            lv_label_set_text(s_aclose_info, abuf);
        }
    }

    // --- automat WL/WYL: napis + kolor stanu funkcji (mirror 0x3B) ---
    const int ren = screen_geometry_radar_enabled() ? 1 : 0;
    if (ren != s_last_radar_en) {
        s_last_radar_en = ren;
        if (s_radar_btn_label != nullptr) {
            lv_label_set_text_static(s_radar_btn_label,
                                     ren ? "RADAR: WŁĄCZONY" : "RADAR: WYŁĄCZONY");
            lv_obj_set_style_text_color(s_radar_btn_label,
                                        lv_color_hex(ren ? C_GREEN : C_RED),
                                        LV_PART_MAIN);
        }
        if (s_radar_btn != nullptr) {
            lv_obj_set_style_border_color(s_radar_btn,
                                          lv_color_hex(ren ? C_GREEN : C_RED),
                                          LV_PART_MAIN);
        }
    }

    // --- "plynne wylaczenie": napis + kolor stanu funkcji (mirror 0x3B) ---
    const int ron = screen_geometry_onoff_enabled() ? 1 : 0;
    if (ron != s_last_onoff) {
        s_last_onoff = ron;
        if (s_onoff_btn_label != nullptr) {
            lv_label_set_text_static(s_onoff_btn_label,
                                     ron ? "PŁYNNE WYŁĄCZENIE: WŁ."
                                         : "PŁYNNE WYŁĄCZENIE: WYŁ.");
            lv_obj_set_style_text_color(s_onoff_btn_label,
                                        lv_color_hex(ron ? C_GREEN : C_RED),
                                        LV_PART_MAIN);
        }
        if (s_onoff_btn != nullptr) {
            lv_obj_set_style_border_color(s_onoff_btn,
                                          lv_color_hex(ron ? C_GREEN : C_RED),
                                          LV_PART_MAIN);
        }
    }
}
