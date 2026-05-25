#include "music_ui_handler.h"
#include "screens.h"
#include "fonts.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "eez-flow.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// --- MACRO FLAG TO ENABLE/DISABLE SIMULATOR ---
#define MUSIC_SIMULATOR_ENABLE 1

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

// Playback Simulation Variables
static bool s_is_playing = false;
static int s_current_time_s = 84;
static int s_total_time_s = 222;

static const char *s_mock_songs[][2] = {
    {"Adventure Of A Lifetime", "Coldplay"},
    {"Get Lucky", "Daft Punk"},
    {"Billie Jean", "Michael Jackson"}
};
static int s_current_song_idx = 0;

// Typewriter revealing animation state variables
static char s_target_title[128] = "";
static char s_target_artist[128] = "";
static int s_current_title_len = 0;
static int s_current_artist_len = 0;
static bool s_title_anim_active = false;
static bool s_artist_anim_active = false;
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
#if MUSIC_SIMULATOR_ENABLE
        lv_obj_t *target = lv_event_get_target(e);
        if (target != s_volume_slider) {
            highlight_preset(-1);
        }
#endif
    }
}

// Preset FLAT callback
static void preset_flat_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
#if MUSIC_SIMULATOR_ENABLE
        lv_slider_set_value(s_bass_slider, 50, LV_ANIM_ON);
        lv_slider_set_value(s_mid_slider, 50, LV_ANIM_ON);
        lv_slider_set_value(s_treble_slider, 50, LV_ANIM_ON);
        update_eq_labels();
        highlight_preset(0);
#endif
    }
}

// Preset BASS callback
static void preset_bass_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
#if MUSIC_SIMULATOR_ENABLE
        lv_slider_set_value(s_bass_slider, 90, LV_ANIM_ON);
        lv_slider_set_value(s_mid_slider, 60, LV_ANIM_ON);
        lv_slider_set_value(s_treble_slider, 40, LV_ANIM_ON);
        update_eq_labels();
        highlight_preset(1);
#endif
    }
}

// Preset VOCAL callback
static void preset_vocal_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
#if MUSIC_SIMULATOR_ENABLE
        lv_slider_set_value(s_bass_slider, 40, LV_ANIM_ON);
        lv_slider_set_value(s_mid_slider, 80, LV_ANIM_ON);
        lv_slider_set_value(s_treble_slider, 60, LV_ANIM_ON);
        update_eq_labels();
        highlight_preset(2);
#endif
    }
}

// Play/Pause button callback
static void play_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
#if MUSIC_SIMULATOR_ENABLE
        s_is_playing = !s_is_playing;
        if (s_is_playing) {
            lv_label_set_text_static(s_play_btn_label, LV_SYMBOL_PAUSE);
        } else {
            lv_label_set_text_static(s_play_btn_label, LV_SYMBOL_PLAY);
        }
#endif
    }
}

static int get_next_utf8_boundary(const char *str, int current_pos) {
    if (str[current_pos] == '\0') {
        return current_pos;
    }
    int pos = current_pos + 1;
    while (str[pos] != '\0' && ((str[pos] & 0xC0) == 0x80)) {
        pos++;
    }
    return pos;
}

// Updates title and artist label texts (starts the typewriter reveal animation)
static void update_song_info(void) {
    snprintf(s_target_title, sizeof(s_target_title), "%s", s_mock_songs[s_current_song_idx][0]);
    snprintf(s_target_artist, sizeof(s_target_artist), "%s", s_mock_songs[s_current_song_idx][1]);
    
    s_current_title_len = 0;
    s_current_artist_len = 0;
    s_title_anim_active = true;
    s_artist_anim_active = true;

    if (s_song_title_label) {
        lv_label_set_text(s_song_title_label, "Tytuł: ");
    }
    if (s_song_artist_label) {
        lv_label_set_text(s_song_artist_label, "Artysta: ");
    }
}

// Prev song callback
static void prev_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
#if MUSIC_SIMULATOR_ENABLE
        s_current_song_idx--;
        if (s_current_song_idx < 0) s_current_song_idx = 2;
        s_current_time_s = 0;
        update_song_info();
#endif
    }
}

// Next song callback
static void next_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
#if MUSIC_SIMULATOR_ENABLE
        s_current_song_idx++;
        if (s_current_song_idx > 2) s_current_song_idx = 0;
        s_current_time_s = 0;
        update_song_info();
#endif
    }
}

// Standard 100ms timer callback managing UI updates and simulator loops
static void music_timer_cb(lv_timer_t *timer) {
    (void)timer;

    if (eez_flow_get_current_screen() != SCREEN_ID_MUZYKA || !objects.muzyka) {
        s_player_panel = NULL;
        s_equalizer_panel = NULL;
        s_song_title_label = NULL;
        s_song_artist_label = NULL;
        s_time_current_label = NULL;
        s_time_total_label = NULL;
        s_progress_slider = NULL;
        s_play_btn_label = NULL;
        s_bass_slider = NULL;
        s_mid_slider = NULL;
        s_treble_slider = NULL;
        s_volume_slider = NULL;
        s_bass_val_label = NULL;
        s_mid_val_label = NULL;
        s_treble_val_label = NULL;
        s_volume_val_label = NULL;
        s_flat_btn = NULL;
        s_bass_btn = NULL;
        s_vocal_btn = NULL;
        s_status_label = NULL;
        for (int i = 0; i < 4; i++) {
            s_vis_bars[i] = NULL;
        }
        s_title_anim_active = false;
        s_artist_anim_active = false;
        return;
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
        lv_obj_set_style_text_color(s_song_title_label, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_song_title_label, &ui_font_montserrat_regular_20, LV_PART_MAIN);
        lv_label_set_text_static(s_song_title_label, "Tytuł: Adventure Of A Lifetime");

        // Artist Info
        s_song_artist_label = lv_label_create(s_player_panel);
        lv_obj_set_pos(s_song_artist_label, 20, 75);
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
        lv_slider_set_range(s_progress_slider, 0, s_total_time_s);
        style_premium_slider(s_progress_slider);

        // Current time label
        s_time_current_label = lv_label_create(s_player_panel);
        lv_obj_set_pos(s_time_current_label, 160, 155);
        lv_obj_set_style_text_color(s_time_current_label, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_time_current_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_time_current_label, "1:24");

        // Total time label
        s_time_total_label = lv_label_create(s_player_panel);
        lv_obj_set_pos(s_time_total_label, 405, 155);
        lv_obj_set_style_text_color(s_time_total_label, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_time_total_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_time_total_label, "3:42");

        // Minimalist Connected Audio Status Spec
        s_status_label = lv_label_create(s_player_panel);
        lv_obj_set_pos(s_status_label, 160, 185);
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x85808a), LV_PART_MAIN);
        lv_obj_set_style_text_font(s_status_label, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_label_set_text_static(s_status_label, "POŁĄCZONO");

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

        // Set FLAT preset active initially
        highlight_preset(0);
        update_eq_labels();

        // Start initial typewriter animation for the first song
        update_song_info();
    }

    // --- ANIMATE TYPEWRITER EFFECT ---
    if (s_title_anim_active && s_song_title_label) {
        s_current_title_len = get_next_utf8_boundary(s_target_title, s_current_title_len);
        
        char temp_buf[128];
        snprintf(temp_buf, sizeof(temp_buf), "Tytuł: %.*s", s_current_title_len, s_target_title);
        
        lv_label_set_text(s_song_title_label, temp_buf);
        
        if (s_target_title[s_current_title_len] == '\0') {
            s_title_anim_active = false;
        }
    }

    if (s_artist_anim_active && s_song_artist_label) {
        s_current_artist_len = get_next_utf8_boundary(s_target_artist, s_current_artist_len);
        
        char temp_buf[128];
        snprintf(temp_buf, sizeof(temp_buf), "Artysta: %.*s", s_current_artist_len, s_target_artist);
        
        lv_label_set_text(s_song_artist_label, temp_buf);
        
        if (s_target_artist[s_current_artist_len] == '\0') {
            s_artist_anim_active = false;
        }
    }

    // --- DYNAMIC STATUS UPDATE ---
    if (s_status_label) {
        if (s_is_playing) {
            lv_label_set_text_static(s_status_label, "POŁĄCZONO");
            lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x85808a), LV_PART_MAIN);
        } else {
            lv_label_set_text_static(s_status_label, "ROZŁĄCZONO");
            lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x5a5760), LV_PART_MAIN);
        }
    }

#if MUSIC_SIMULATOR_ENABLE
    // --- SIMULATED PLAYBACK LOOPS ---
    if (s_is_playing) {
        s_current_time_s += 1; // Simulated advance (since cb is called 1s in logic or 100ms?)
        // Wait, timer is called every 100ms, so every 10 ticks is 1 second!
        static int sub_sec = 0;
        sub_sec++;
        if (sub_sec >= 10) {
            sub_sec = 0;
            s_current_time_s++;
            if (s_current_time_s > s_total_time_s) {
                // Loop song
                s_current_time_s = 0;
            }
            
            // Update Progress Slider
            lv_slider_set_value(s_progress_slider, s_current_time_s, LV_ANIM_ON);

            // Update Current Time Label
            char cur_buf[16];
            snprintf(cur_buf, sizeof(cur_buf), "%d:%02d", s_current_time_s / 60, s_current_time_s % 60);
            lv_label_set_text(s_time_current_label, cur_buf);
        }

        // Bouncing Spectrum Visualizer animation (every 100ms)
        for (int i = 0; i < 4; i++) {
            int h = rand() % 20 + 4;
            lv_obj_set_height(s_vis_bars[i], h);
            // Move y slightly to keep bars bottom-aligned or just keep standard top-align
        }
    } else {
        // Music is paused: drop visualizer bars to minimum height
        for (int i = 0; i < 4; i++) {
            lv_obj_set_height(s_vis_bars[i], 4);
        }
    }
#endif
}

// Public API Initialization
void music_ui_handler_init(void) {
    if (s_timer) {
        return;
    }
    s_timer = lv_timer_create(music_timer_cb, 100, NULL);
}
