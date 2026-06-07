#include "music_ui_handler.h"
#include "screens.h"
#include "vars.h"
#include "fonts.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "eez-flow.h"
#include "esp_random.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// --- C-linkage wrappers (declared in CommunicationProtocol.h extern "C" block) ---
void action_send_music_command(uint8_t cmd);
void action_send_music_volume(uint8_t vol);
void action_send_music_eq(uint8_t bass, uint8_t mid, uint8_t treble);
void action_request_music_state();
int action_get_flow_variable_int(uint32_t var_id);
void action_get_flow_variable_text(uint32_t var_id, char *buf, size_t buf_size);

// Static state variables
static lv_timer_t *s_timer = NULL;

// UI Panels
static lv_obj_t *s_player_panel = NULL;
static lv_obj_t *s_equalizer_panel = NULL;

// Dynamic Player Labels & Controls
static lv_obj_t *s_song_title_label = NULL;
static lv_obj_t *s_song_artist_label = NULL;
static lv_obj_t *s_time_current_label = NULL;
static lv_obj_t *s_time_total_label = NULL;
static lv_obj_t *s_progress_slider = NULL;
static lv_obj_t *s_play_btn_label = NULL;
static lv_obj_t *s_vis_bars[4] = {NULL};

// Equalizer Sliders & Labels
static lv_obj_t *s_bass_slider = NULL;
static lv_obj_t *s_mid_slider = NULL;
static lv_obj_t *s_treble_slider = NULL;
static lv_obj_t *s_volume_slider = NULL;
static lv_obj_t *s_bass_val_label = NULL;
static lv_obj_t *s_mid_val_label = NULL;
static lv_obj_t *s_treble_val_label = NULL;
static lv_obj_t *s_volume_val_label = NULL;

// Preset Buttons
static lv_obj_t *s_flat_btn = NULL;
static lv_obj_t *s_bass_btn = NULL;
static lv_obj_t *s_vocal_btn = NULL;

// Persistent buffers for music labels (bypasses safe_label.h 64B limit)
static char s_music_title_buf[256] = "";
static char s_music_artist_buf[256] = "";

// Playback state (local mirror of EEZ flow variables)
static lv_obj_t *s_status_label = NULL;

static void style_premium_slider(lv_obj_t *slider) {
    // Background track (main part)
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x28252c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    
    // Active filled part (indicator)
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x85808a), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    
    // Drag handle (knob)
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, lv_color_hex(0x7a7580), LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 1, LV_PART_KNOB);
    
    // Make the knob slightly tighter/smaller for a high-end look
    lv_obj_set_style_pad_all(slider, -1, LV_PART_KNOB);
}

// Updates the EQ labels to match the slider values
static void update_eq_labels(void) {
    if (s_bass_slider && s_bass_val_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(s_bass_slider));
        lv_label_set_text(s_bass_val_label, buf);
    }
    if (s_mid_slider && s_mid_val_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(s_mid_slider));
        lv_label_set_text(s_mid_val_label, buf);
    }
    if (s_treble_slider && s_treble_val_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(s_treble_slider));
        lv_label_set_text(s_treble_val_label, buf);
    }
    if (s_volume_slider && s_volume_val_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (int)lv_slider_get_value(s_volume_slider));
        lv_label_set_text(s_volume_val_label, buf);
    }
}

// Highlights the selected preset button
static void highlight_preset(int active_idx) {
    lv_obj_t *presets[] = { s_flat_btn, s_bass_btn, s_vocal_btn };
    for (int i = 0; i < 3; i++) {
        if (!presets[i]) continue;
        if (i == active_idx) {
            lv_obj_set_style_bg_color(presets[i], lv_color_hex(0x2d2834), LV_PART_MAIN);
            lv_obj_set_style_border_color(presets[i], lv_color_hex(0xffffff), LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(presets[i], lv_color_hex(0x1e1b22), LV_PART_MAIN);
            lv_obj_set_style_border_color(presets[i], lv_color_hex(0x4a4750), LV_PART_MAIN);
        }
    }
}

// Sliders callback
static void slider_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        update_eq_labels();
        lv_obj_t *target = lv_event_get_target(e);
        if (target == s_volume_slider) {
            action_send_music_volume((uint8_t)lv_slider_get_value(target));
        } else if (target == s_bass_slider || target == s_mid_slider || target == s_treble_slider) {
            action_send_music_eq(
                (uint8_t)lv_slider_get_value(s_bass_slider),
                (uint8_t)lv_slider_get_value(s_mid_slider),
                (uint8_t)lv_slider_get_value(s_treble_slider));
        }
        if (target != s_volume_slider) {
            highlight_preset(-1);
        }
    }
}

static void send_current_eq() {
    action_send_music_eq(
        (uint8_t)lv_slider_get_value(s_bass_slider),
        (uint8_t)lv_slider_get_value(s_mid_slider),
        (uint8_t)lv_slider_get_value(s_treble_slider));
}

// Preset FLAT callback
static void preset_flat_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_slider_set_value(s_bass_slider, 50, LV_ANIM_ON);
        lv_slider_set_value(s_mid_slider, 50, LV_ANIM_ON);
        lv_slider_set_value(s_treble_slider, 50, LV_ANIM_ON);
        update_eq_labels();
        highlight_preset(0);
        send_current_eq();
    }
}

// Preset BASS callback
static void preset_bass_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_slider_set_value(s_bass_slider, 90, LV_ANIM_ON);
        lv_slider_set_value(s_mid_slider, 60, LV_ANIM_ON);
        lv_slider_set_value(s_treble_slider, 40, LV_ANIM_ON);
        update_eq_labels();
        highlight_preset(1);
        send_current_eq();
    }
}

// Preset VOCAL callback
static void preset_vocal_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_slider_set_value(s_bass_slider, 40, LV_ANIM_ON);
        lv_slider_set_value(s_mid_slider, 80, LV_ANIM_ON);
        lv_slider_set_value(s_treble_slider, 60, LV_ANIM_ON);
        update_eq_labels();
        highlight_preset(2);
        send_current_eq();
    }
}

// Play/Pause button callback
static void play_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        int playing = action_get_flow_variable_int(FLOW_GLOBAL_VARIABLE_MUSIC_PLAYING);
        action_send_music_command(playing ? 1 : 0);
    }
}

// Prev song callback
static void prev_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        action_send_music_command(3);
    }
}

// Next song callback
static void next_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        action_send_music_command(2);
    }
}

// Standard 100ms timer callback managing UI updates and simulator loops
static void music_timer_cb(lv_timer_t *timer) {
    (void)timer;
    static bool s_was_on_screen = false;
    static uint16_t s_requestCount = 0;

    if (eez_flow_get_current_screen() != SCREEN_ID_MUZYKA || !objects.muzyka) {
        s_was_on_screen = false;
        s_requestCount = 0;
        // Panels są persistentne (jak wszystkie inne ekrany).
        // ERR_017: delete/recreate powodował fragmentację SRAM i widoczne zacięcie UI.
        return;
    }

    // Screen enter detection — request full music state from ZEGAR
    if (!s_was_on_screen) {
        s_was_on_screen = true;
        s_requestCount = 0;
        action_request_music_state();
    }

    // Periodic re-request every 30s (300 ticks × 100ms) — Guition is always the active
    // sync initiator regardless of ZEGAR WiFi/BT mode.
    s_requestCount++;
    if (s_requestCount >= 300) {
        s_requestCount = 0;
        action_request_music_state();
    }

    if (!s_player_panel) {
        // --- 1. CREATE PLAYER PANEL (Left Side, 460x395px) ---
        s_player_panel = lv_obj_create(objects.muzyka);
        lv_obj_set_pos(s_player_panel, 27, 55);
        lv_obj_set_size(s_player_panel, 460, 395);
        lv_obj_remove_flag(s_player_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_player_panel, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_player_panel, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_player_panel, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_player_panel, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_player_panel, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_player_panel, 12, LV_PART_MAIN);

        // Section Title
        lv_obj_t *subtitle = lv_label_create(s_player_panel);
        lv_obj_set_pos(subtitle, 20, 15);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0xb0acb5), LV_PART_MAIN);
        lv_obj_set_style_text_font(subtitle, &ui_font_montserrat_regular_16, LV_PART_MAIN);
        lv_label_set_text_static(subtitle, "TERAZ ODTWARZANE");

        // Song Title
        s_song_title_label = lv_label_create(s_player_panel);
        lv_obj_set_pos(s_song_title_label, 20, 45);
        lv_obj_set_width(s_song_title_label, 370);
        lv_label_set_long_mode(s_song_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(s_song_title_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_song_title_label, &ui_font_montserrat_regular_16, LV_PART_MAIN);
        lv_label_set_text_static(s_song_title_label, "Tytuł: Adventure Of A Lifetime");

        // Artist Info
        s_song_artist_label = lv_label_create(s_player_panel);
        lv_obj_set_pos(s_song_artist_label, 20, 75);
        lv_obj_set_width(s_song_artist_label, 370);
        lv_label_set_long_mode(s_song_artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(s_song_artist_label, lv_color_hex(0xb0acb5), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_song_artist_label, &ui_font_montserrat_regular_16, LV_PART_MAIN);
        lv_label_set_text_static(s_song_artist_label, "Artysta: Coldplay");

        // Micro-Spectrum Visualizer Bars
        int bar_x_start = 400;
        for (int i = 0; i < 4; i++) {
            s_vis_bars[i] = lv_obj_create(s_player_panel);
            lv_obj_set_pos(s_vis_bars[i], bar_x_start + (i * 8), 48);
            lv_obj_set_size(s_vis_bars[i], 4, 16);
            lv_obj_remove_flag(s_vis_bars[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(s_vis_bars[i], 0, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_vis_bars[i], lv_color_hex(0x7a7580), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_vis_bars[i], LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(s_vis_bars[i], 0, LV_PART_MAIN);
            lv_obj_set_style_radius(s_vis_bars[i], 1, LV_PART_MAIN);
        }

        // --- 2. VECTOR VINYL ASSEMBLY ---
        // Base outer disc
        lv_obj_t *vinyl_disc = lv_obj_create(s_player_panel);
        lv_obj_set_pos(vinyl_disc, 20, 115);
        lv_obj_set_size(vinyl_disc, 120, 120);
        lv_obj_remove_flag(vinyl_disc, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(vinyl_disc, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(vinyl_disc, lv_color_hex(0x0f0d11), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vinyl_disc, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(vinyl_disc, lv_color_hex(0x3a373f), LV_PART_MAIN);
        lv_obj_set_style_border_width(vinyl_disc, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(vinyl_disc, 60, LV_PART_MAIN);

        // Groove tracks 1 (100x100)
        lv_obj_t *groove1 = lv_obj_create(vinyl_disc);
        lv_obj_set_size(groove1, 100, 100);
        lv_obj_align(groove1, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(groove1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(groove1, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_color(groove1, lv_color_hex(0x242228), LV_PART_MAIN);
        lv_obj_set_style_border_width(groove1, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(groove1, 50, LV_PART_MAIN);

        // Groove tracks 2 (80x80)
        lv_obj_t *groove2 = lv_obj_create(vinyl_disc);
        lv_obj_set_size(groove2, 80, 80);
        lv_obj_align(groove2, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(groove2, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(groove2, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_color(groove2, lv_color_hex(0x242228), LV_PART_MAIN);
        lv_obj_set_style_border_width(groove2, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(groove2, 40, LV_PART_MAIN);

        // Center paper label (42x42)
        lv_obj_t *paper_label = lv_obj_create(vinyl_disc);
        lv_obj_set_size(paper_label, 42, 42);
        lv_obj_align(paper_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(paper_label, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(paper_label, lv_color_hex(0x34293a), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(paper_label, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(paper_label, lv_color_hex(0x5e5466), LV_PART_MAIN);
        lv_obj_set_style_border_width(paper_label, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(paper_label, 21, LV_PART_MAIN);

        // Spindle center hole (10x10)
        lv_obj_t *spindle_hole = lv_obj_create(vinyl_disc);
        lv_obj_set_size(spindle_hole, 10, 10);
        lv_obj_align(spindle_hole, LV_ALIGN_CENTER, 0, 0);
        lv_obj_remove_flag(spindle_hole, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(spindle_hole, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(spindle_hole, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(spindle_hole, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(spindle_hole, 5, LV_PART_MAIN);

        // --- 3. MINIMALIST TONEARM ---
        // Pivot base
        lv_obj_t *pivot = lv_obj_create(s_player_panel);
        lv_obj_set_pos(pivot, 138, 105);
        lv_obj_set_size(pivot, 14, 14);
        lv_obj_remove_flag(pivot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(pivot, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(pivot, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pivot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(pivot, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_border_width(pivot, 1.5, LV_PART_MAIN);
        lv_obj_set_style_radius(pivot, 7, LV_PART_MAIN);

        // Tonearm needle line
        lv_obj_t *line = lv_line_create(s_player_panel);
        static lv_point_precise_t line_points[] = { {145, 112}, {100, 150}, {80, 156} };
        lv_line_set_points(line, line_points, 3);
        lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
        lv_obj_set_style_line_color(line, lv_color_hex(0x7a7580), LV_PART_MAIN);

        // Cartridge headshell
        lv_obj_t *head = lv_obj_create(s_player_panel);
        lv_obj_set_pos(head, 77, 152);
        lv_obj_set_size(head, 6, 8);
        lv_obj_remove_flag(head, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(head, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(head, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(head, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(head, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(head, 1, LV_PART_MAIN);

        // --- 4. PLAYBACK PROGRESS ROW (Right of Vinyl) ---
        // Progress Slider
        s_progress_slider = lv_slider_create(s_player_panel);
        lv_obj_set_pos(s_progress_slider, 160, 140);
        lv_obj_set_size(s_progress_slider, 280, 6);
        lv_slider_set_range(s_progress_slider, 0, 100);
        style_premium_slider(s_progress_slider);

        // Current time label
        s_time_current_label = lv_label_create(s_player_panel);
        lv_obj_set_pos(s_time_current_label, 160, 155);
        lv_obj_set_style_text_color(s_time_current_label, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_time_current_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_time_current_label, "-:--");

        // Total time label
        s_time_total_label = lv_label_create(s_player_panel);
        lv_obj_set_pos(s_time_total_label, 405, 155);
        lv_obj_set_style_text_color(s_time_total_label, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_time_total_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_time_total_label, "-:--");

        // Minimalist Connected Audio Status Spec
        s_status_label = lv_label_create(s_player_panel);
        lv_obj_set_pos(s_status_label, 160, 185);
        lv_obj_set_width(s_status_label, 280); // Sztywna szerokość dla przewijanego marquee
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_status_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_status_label, "PO\u0141\u0104CZONO");

        // --- 5. CONTROL BUTTONS (y = 265 - 345) ---
        // Prev button
        lv_obj_t *prev_btn = lv_button_create(s_player_panel);
        lv_obj_set_pos(prev_btn, 100, 275);
        lv_obj_set_size(prev_btn, 60, 60);
        lv_obj_set_style_pad_all(prev_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(prev_btn, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        lv_obj_set_style_border_color(prev_btn, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_border_width(prev_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(prev_btn, 30, LV_PART_MAIN);
        lv_obj_add_event_cb(prev_btn, prev_btn_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *prev_sym = lv_label_create(prev_btn);
        lv_obj_align(prev_sym, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(prev_sym, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_label_set_text_static(prev_sym, LV_SYMBOL_PREV);

        // Play/Pause button
        lv_obj_t *play_btn = lv_button_create(s_player_panel);
        lv_obj_set_pos(play_btn, 190, 265);
        lv_obj_set_size(play_btn, 80, 80);
        lv_obj_set_style_pad_all(play_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(play_btn, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        lv_obj_set_style_border_color(play_btn, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_border_width(play_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(play_btn, 40, LV_PART_MAIN);
        lv_obj_add_event_cb(play_btn, play_btn_cb, LV_EVENT_CLICKED, NULL);

        s_play_btn_label = lv_label_create(play_btn);
        lv_obj_align(s_play_btn_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(s_play_btn_label, &lv_font_montserrat_30, LV_PART_MAIN);
        lv_label_set_text_static(s_play_btn_label, LV_SYMBOL_PLAY);

        // Next button
        lv_obj_t *next_btn = lv_button_create(s_player_panel);
        lv_obj_set_pos(next_btn, 300, 275);
        lv_obj_set_size(next_btn, 60, 60);
        lv_obj_set_style_pad_all(next_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(next_btn, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        lv_obj_set_style_border_color(next_btn, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_border_width(next_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(next_btn, 30, LV_PART_MAIN);
        lv_obj_add_event_cb(next_btn, next_btn_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *next_sym = lv_label_create(next_btn);
        lv_obj_align(next_sym, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(next_sym, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_label_set_text_static(next_sym, LV_SYMBOL_NEXT);

        // --- 6. CREATE EQUALIZER PANEL (Right Side, 272x395px) ---
        s_equalizer_panel = lv_obj_create(objects.muzyka);
        lv_obj_set_pos(s_equalizer_panel, 502, 55);
        lv_obj_set_size(s_equalizer_panel, 272, 395);
        lv_obj_remove_flag(s_equalizer_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(s_equalizer_panel, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_equalizer_panel, lv_color_hex(0x18161b), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_equalizer_panel, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_equalizer_panel, lv_color_hex(0x7a7580), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_equalizer_panel, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_equalizer_panel, 12, LV_PART_MAIN);

        // Equalizer Section Title
        lv_obj_t *eq_title = lv_label_create(s_equalizer_panel);
        lv_obj_set_pos(eq_title, 20, 15);
        lv_obj_set_style_text_color(eq_title, lv_color_hex(0xb0acb5), LV_PART_MAIN);
        lv_obj_set_style_text_font(eq_title, &ui_font_montserrat_regular_16, LV_PART_MAIN);
        lv_label_set_text_static(eq_title, "KOREKTOR D\u0179WI\u0118KU");

        // --- 7. FOUR VERTICAL PASM SLIDERS (BASS, MID, TRE, VOL) ---
        // pasmo BASS
        s_bass_val_label = lv_label_create(s_equalizer_panel);
        lv_obj_set_pos(s_bass_val_label, 20, 42);
        lv_obj_set_style_text_color(s_bass_val_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_bass_val_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_bass_val_label, "50%");

        s_bass_slider = lv_slider_create(s_equalizer_panel);
        lv_obj_set_pos(s_bass_slider, 28, 65);
        lv_obj_set_size(s_bass_slider, 12, 180);
        lv_slider_set_value(s_bass_slider, 50, LV_ANIM_OFF);
        lv_obj_add_event_cb(s_bass_slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
        style_premium_slider(s_bass_slider);

        lv_obj_t *bass_lbl = lv_label_create(s_equalizer_panel);
        lv_obj_set_pos(bass_lbl, 15, 255);
        lv_obj_set_style_text_color(bass_lbl, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(bass_lbl, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(bass_lbl, "BASS");

        // pasmo MID
        s_mid_val_label = lv_label_create(s_equalizer_panel);
        lv_obj_set_pos(s_mid_val_label, 84, 42);
        lv_obj_set_style_text_color(s_mid_val_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_mid_val_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_mid_val_label, "50%");

        s_mid_slider = lv_slider_create(s_equalizer_panel);
        lv_obj_set_pos(s_mid_slider, 92, 65);
        lv_obj_set_size(s_mid_slider, 12, 180);
        lv_slider_set_value(s_mid_slider, 50, LV_ANIM_OFF);
        lv_obj_add_event_cb(s_mid_slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
        style_premium_slider(s_mid_slider);

        lv_obj_t *mid_lbl = lv_label_create(s_equalizer_panel);
        lv_obj_set_pos(mid_lbl, 82, 255);
        lv_obj_set_style_text_color(mid_lbl, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(mid_lbl, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(mid_lbl, "MID");

        // pasmo TREBLE
        s_treble_val_label = lv_label_create(s_equalizer_panel);
        lv_obj_set_pos(s_treble_val_label, 148, 42);
        lv_obj_set_style_text_color(s_treble_val_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_treble_val_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_treble_val_label, "50%");

        s_treble_slider = lv_slider_create(s_equalizer_panel);
        lv_obj_set_pos(s_treble_slider, 156, 65);
        lv_obj_set_size(s_treble_slider, 12, 180);
        lv_slider_set_value(s_treble_slider, 50, LV_ANIM_OFF);
        lv_obj_add_event_cb(s_treble_slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
        style_premium_slider(s_treble_slider);

        lv_obj_t *tre_lbl = lv_label_create(s_equalizer_panel);
        lv_obj_set_pos(tre_lbl, 146, 255);
        lv_obj_set_style_text_color(tre_lbl, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(tre_lbl, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(tre_lbl, "TRE");

        // pasmo VOLUME (VOL)
        s_volume_val_label = lv_label_create(s_equalizer_panel);
        lv_obj_set_pos(s_volume_val_label, 212, 42);
        lv_obj_set_style_text_color(s_volume_val_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_volume_val_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_volume_val_label, "50%");

        s_volume_slider = lv_slider_create(s_equalizer_panel);
        lv_obj_set_pos(s_volume_slider, 220, 65);
        lv_obj_set_size(s_volume_slider, 12, 180);
        lv_slider_set_value(s_volume_slider, 50, LV_ANIM_OFF);
        lv_obj_add_event_cb(s_volume_slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
        style_premium_slider(s_volume_slider);

        lv_obj_t *vol_lbl = lv_label_create(s_equalizer_panel);
        lv_obj_set_pos(vol_lbl, 210, 255);
        lv_obj_set_style_text_color(vol_lbl, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(vol_lbl, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(vol_lbl, "VOL");

        // --- 8. PRESET BUTTONS (y = 295, w=64, h=30) ---
        // FLAT Button
        s_flat_btn = lv_button_create(s_equalizer_panel);
        lv_obj_set_pos(s_flat_btn, 24, 295);
        lv_obj_set_size(s_flat_btn, 64, 30);
        lv_obj_set_style_pad_all(s_flat_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_flat_btn, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_flat_btn, lv_color_hex(0x4a4750), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_flat_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_flat_btn, 6, LV_PART_MAIN);
        lv_obj_add_event_cb(s_flat_btn, preset_flat_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *flat_lbl = lv_label_create(s_flat_btn);
        lv_obj_align(flat_lbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(flat_lbl, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(flat_lbl, lv_color_hex(0xb0acb5), LV_PART_MAIN);
        lv_label_set_text_static(flat_lbl, "FLAT");

        // BASS Button
        s_bass_btn = lv_button_create(s_equalizer_panel);
        lv_obj_set_pos(s_bass_btn, 104, 295);
        lv_obj_set_size(s_bass_btn, 64, 30);
        lv_obj_set_style_pad_all(s_bass_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_bass_btn, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_bass_btn, lv_color_hex(0x4a4750), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_bass_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_bass_btn, 6, LV_PART_MAIN);
        lv_obj_add_event_cb(s_bass_btn, preset_bass_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *bass_preset_lbl = lv_label_create(s_bass_btn);
        lv_obj_align(bass_preset_lbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(bass_preset_lbl, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(bass_preset_lbl, lv_color_hex(0xb0acb5), LV_PART_MAIN);
        lv_label_set_text_static(bass_preset_lbl, "BASS");

        // VOCAL Button
        s_vocal_btn = lv_button_create(s_equalizer_panel);
        lv_obj_set_pos(s_vocal_btn, 184, 295);
        lv_obj_set_size(s_vocal_btn, 64, 30);
        lv_obj_set_style_pad_all(s_vocal_btn, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_vocal_btn, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        lv_obj_set_style_border_color(s_vocal_btn, lv_color_hex(0x4a4750), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_vocal_btn, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(s_vocal_btn, 6, LV_PART_MAIN);
        lv_obj_add_event_cb(s_vocal_btn, preset_vocal_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *vocal_lbl = lv_label_create(s_vocal_btn);
        lv_obj_align(vocal_lbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(vocal_lbl, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(vocal_lbl, lv_color_hex(0xb0acb5), LV_PART_MAIN);
        lv_label_set_text_static(vocal_lbl, "VOCAL");

        // Initialize sliders from last-known EEZ flow values (avoid default-50 flash)
        if (s_bass_slider)   lv_slider_set_value(s_bass_slider,   action_get_flow_variable_int(FLOW_GLOBAL_VARIABLE_MUSIC_BASS),   LV_ANIM_OFF);
        if (s_mid_slider)    lv_slider_set_value(s_mid_slider,    action_get_flow_variable_int(FLOW_GLOBAL_VARIABLE_MUSIC_MID),    LV_ANIM_OFF);
        if (s_treble_slider) lv_slider_set_value(s_treble_slider, action_get_flow_variable_int(FLOW_GLOBAL_VARIABLE_MUSIC_TREBLE), LV_ANIM_OFF);
        if (s_volume_slider) lv_slider_set_value(s_volume_slider, action_get_flow_variable_int(FLOW_GLOBAL_VARIABLE_MUSIC_VOLUME), LV_ANIM_OFF);
        update_eq_labels();
        // Preset highlight from current EQ (no preset shown if custom values)
        {
            int b = (int)lv_slider_get_value(s_bass_slider);
            int m = (int)lv_slider_get_value(s_mid_slider);
            int t = (int)lv_slider_get_value(s_treble_slider);
            int p = -1;
            if (b == 50 && m == 50 && t == 50) p = 0;
            else if (b == 90 && m == 60 && t == 40) p = 1;
            else if (b == 40 && m == 80 && t == 60) p = 2;
            if (p >= 0) highlight_preset(p);
        }
    }

    // --- UPDATE TITLE & ARTIST FROM EEZ FLOW ---
    if (s_song_title_label && s_song_artist_label) {
        char title[128], artist[128];
        action_get_flow_variable_text(FLOW_GLOBAL_VARIABLE_MUSIC_TITLE, title, sizeof(title));
        action_get_flow_variable_text(FLOW_GLOBAL_VARIABLE_MUSIC_ARTIST, artist, sizeof(artist));

        snprintf(s_music_title_buf, sizeof(s_music_title_buf), "Tytu\u0142: %s", title);
        snprintf(s_music_artist_buf, sizeof(s_music_artist_buf), "Artysta: %s", artist);
        lv_label_set_text_static(s_song_title_label, s_music_title_buf);
        lv_label_set_text_static(s_song_artist_label, s_music_artist_buf);
    }

    // --- STATUS FROM EEZ FLOW ---
    if (s_status_label) {
        int connected = action_get_flow_variable_int(FLOW_GLOBAL_VARIABLE_MUSIC_CONNECTED);
        static int s_prev_connected = -1;
        if (connected != s_prev_connected) {
            s_prev_connected = connected;
            if (connected) {
                lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_CLIP);
                lv_label_set_text_static(s_status_label, "PO\u0141\u0104CZONO");
                lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x85808a), LV_PART_MAIN);
            } else {
                lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
                lv_label_set_text_static(s_status_label, "ROZ\u0141\u0104CZONO Aby po\u0142\u0105czy\u0107 si\u0119, w\u0142\u0105cz tryb bluetooth za pomoc\u0105 przycisku BLT na ekranie g\u0142\u00f3wnym.       ");
                lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x5a5760), LV_PART_MAIN);
            }
        }
    }

    // --- PLAY/PAUSE ICON FROM EEZ FLOW ---
    if (s_play_btn_label) {
        int playing = action_get_flow_variable_int(FLOW_GLOBAL_VARIABLE_MUSIC_PLAYING);
        lv_label_set_text_static(s_play_btn_label, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }

    // Bouncing Spectrum Visualizer animation (every 100ms) — only when playing
    int playing = action_get_flow_variable_int(FLOW_GLOBAL_VARIABLE_MUSIC_PLAYING);
    for (int i = 0; i < 4; i++) {
        if (s_vis_bars[i]) {
            int h = playing ? (rand() % 20 + 4) : 4;
            lv_obj_set_height(s_vis_bars[i], h);
        }
    }
}

// ─── Public API ────────────────────────────────────────────────────────────

void music_ui_set_volume(uint8_t vol) {
    if (vol > 100) vol = 100;
    if (s_volume_slider) {
        lv_slider_set_value(s_volume_slider, vol, LV_ANIM_OFF);
    }
    update_eq_labels();
}

void music_ui_set_eq(uint8_t bass, uint8_t mid, uint8_t treble) {
    if (s_bass_slider)   lv_slider_set_value(s_bass_slider,   bass,   LV_ANIM_OFF);
    if (s_mid_slider)    lv_slider_set_value(s_mid_slider,    mid,    LV_ANIM_OFF);
    if (s_treble_slider) lv_slider_set_value(s_treble_slider, treble, LV_ANIM_OFF);
    update_eq_labels();
}

void music_ui_reset(void) {
    if (s_volume_slider) lv_slider_set_value(s_volume_slider, 50, LV_ANIM_OFF);
    if (s_bass_slider)   lv_slider_set_value(s_bass_slider,   50, LV_ANIM_OFF);
    if (s_mid_slider)    lv_slider_set_value(s_mid_slider,    50, LV_ANIM_OFF);
    if (s_treble_slider) lv_slider_set_value(s_treble_slider, 50, LV_ANIM_OFF);
    update_eq_labels();
    highlight_preset(0);
    action_request_music_state();
}

void music_ui_handler_init(void) {
    if (s_timer) {
        return;
    }
    srand(esp_random());
    s_timer = lv_timer_create(music_timer_cb, 100, NULL);
}
