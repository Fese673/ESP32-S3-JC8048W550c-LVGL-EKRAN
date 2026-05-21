#include "flappy_bird.h"
#include "screens.h"
#include "fonts.h"
#include "eez-flow.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

#define TAG "FlappyBird"

// Physics constants
static const float DT = 0.030f;                 // 30ms step (approx. 33 FPS)
static const float GRAVITY = 850.0f;            // gravity acceleration
static const float JUMP_VELOCITY = -280.0f;      // flap impulse (velocity upwards)
static const float TERMINAL_VELOCITY = 550.0f;  // max fall speed
static const float PIPE_SPEED = -150.0f;         // pipe scrolling speed (pixels/sec)

static const int SCREEN_WIDTH = 800;
static const int SCREEN_HEIGHT = 480;
static const int BIRD_X = 180;
static const int BIRD_RADIUS = 15;
static const int BIRD_SIZE = 30;                 // 2 * BIRD_RADIUS
static const int GAP_HEIGHT = 145;              // vertical space between pipes
static const int PIPE_WIDTH = 75;
static const int PIPE_COUNT = 3;
static const int GROUND_Y = 440;

enum GameState {
    STATE_READY,
    STATE_PLAYING,
    STATE_GAMEOVER
};

struct Pipe {
    float x;
    float gap_y;
    lv_obj_t *top_pipe;
    lv_obj_t *bottom_pipe;
    bool passed;
};

// Game state variables
static bool s_game_initialized = false;
static GameState s_state = STATE_READY;
static float s_bird_y = 200.0f;
static float s_bird_vy = 0.0f;
static int s_score = 0;
static int s_high_score = 0;

static Pipe s_pipes[PIPE_COUNT];

// Dynamic difficulty helpers
static float get_pipe_speed() {
    float speed = -150.0f - (s_score / 5) * 12.0f;
    if (speed < -260.0f) speed = -260.0f;
    return speed;
}

static int get_gap_height() {
    int gap = 145 - (s_score / 5) * 6;
    if (gap < 110) gap = 110;
    return gap;
}

// Timing
static int64_t s_last_time_us = 0;

// LVGL objects
static lv_obj_t *s_game_container = NULL;
static lv_obj_t *s_bird = NULL;
static lv_obj_t *s_bird_eye = NULL;
static lv_obj_t *s_bird_pupil = NULL;
static lv_obj_t *s_bird_beak = NULL;
static lv_obj_t *s_ground = NULL;
static lv_obj_t *s_score_label = NULL;

static lv_obj_t *s_start_overlay = NULL;
static lv_obj_t *s_start_title = NULL;
static lv_obj_t *s_start_instructions = NULL;
static lv_obj_t *s_start_record = NULL;

static lv_obj_t *s_gameover_overlay = NULL;
static lv_obj_t *s_gameover_title = NULL;
static lv_obj_t *s_gameover_score = NULL;
static lv_obj_t *s_gameover_best = NULL;
static lv_obj_t *s_btn_replay = NULL;
static lv_obj_t *s_btn_menu = NULL;

static lv_timer_t *s_timer = NULL;

// High Score Persistence
static void load_high_score() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("flappy_game", NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        int32_t val = 0;
        err = nvs_get_i32(nvs, "high_score", &val);
        if (err == ESP_OK) {
            s_high_score = (int)val;
            ESP_LOGI(TAG, "High score loaded: %d", s_high_score);
        }
        nvs_close(nvs);
    } else {
        s_high_score = 0;
        ESP_LOGI(TAG, "No high score found, default to 0");
    }
}

static void save_high_score() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("flappy_game", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_set_i32(nvs, "high_score", (int32_t)s_high_score);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "High score saved: %d", s_high_score);
    } else {
        ESP_LOGE(TAG, "Failed to save high score to NVS");
    }
}

// Reset Game State (does not recreate objects)
static void reset_game_state() {
    s_bird_y = 200.0f;
    s_bird_vy = 0.0f;
    s_score = 0;
    
    // Position bird
    lv_obj_set_pos(s_bird, BIRD_X, (int)s_bird_y - BIRD_RADIUS);
    lv_obj_set_pos(s_bird_beak, 24, 13);
    lv_obj_set_pos(s_bird_eye, 18, 9);

    // Initial pipe positions (spread across screen width)
    float start_x = 650.0f;
    int current_gap = get_gap_height();
    for (int i = 0; i < PIPE_COUNT; i++) {
        s_pipes[i].x = start_x + i * 290.0f;
        s_pipes[i].gap_y = (float)(140 + rand() % 160); // middle gap between 140 and 300
        s_pipes[i].passed = false;

        // Top pipe height
        int top_h = (int)(s_pipes[i].gap_y - current_gap / 2);
        lv_obj_set_size(s_pipes[i].top_pipe, PIPE_WIDTH, top_h);
        lv_obj_set_pos(s_pipes[i].top_pipe, (int)s_pipes[i].x, 0);

        // Bottom pipe height & position
        int bottom_y = (int)(s_pipes[i].gap_y + current_gap / 2);
        int bottom_h = GROUND_Y - bottom_y;
        lv_obj_set_size(s_pipes[i].bottom_pipe, PIPE_WIDTH, bottom_h);
        lv_obj_set_pos(s_pipes[i].bottom_pipe, (int)s_pipes[i].x, bottom_y);
        
        lv_obj_remove_flag(s_pipes[i].top_pipe, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_pipes[i].bottom_pipe, LV_OBJ_FLAG_HIDDEN);
    }

    // Reset timing
    s_last_time_us = esp_timer_get_time();

    // Refresh score display
    lv_label_set_text_fmt(s_score_label, "%d", s_score);
}

// Trigger Game Over screen
static void trigger_game_over() {
    s_state = STATE_GAMEOVER;
    ESP_LOGI(TAG, "Game Over! Score: %d", s_score);

    // Save score if it is a new high score
    bool is_new_record = false;
    if (s_score > s_high_score) {
        s_high_score = s_score;
        is_new_record = true;
        save_high_score();
    }

    // Show game over screen
    lv_label_set_text_fmt(s_gameover_score, "WYNIK: %d", s_score);
    if (is_new_record) {
        lv_label_set_text_fmt(s_gameover_best, "NOWY REKORD: %d !", s_high_score);
        lv_obj_set_style_text_color(s_gameover_best, lv_color_hex(0x00FF66), 0); // Green record label
    } else {
        lv_label_set_text_fmt(s_gameover_best, "REKORD: %d", s_high_score);
        lv_obj_set_style_text_color(s_gameover_best, lv_color_hex(0xB0B0B0), 0);
    }

    // Update overlays visibility
    lv_obj_add_flag(s_score_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_gameover_overlay, LV_OBJ_FLAG_HIDDEN);
}

// Public API to make bird flap
void flappy_bird_jump() {
    if (s_state == STATE_READY) {
        // Tap to start playing
        s_state = STATE_PLAYING;
        s_last_time_us = esp_timer_get_time(); // Reset delta timer
        lv_obj_add_flag(s_start_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_score_label, LV_OBJ_FLAG_HIDDEN);
        s_bird_vy = JUMP_VELOCITY;
    } else if (s_state == STATE_PLAYING) {
        s_bird_vy = JUMP_VELOCITY;
    }
}

// Game Physics Loop
static void flappy_bird_timer_cb(lv_timer_t *timer) {
    (void)timer;

    if (eez_flow_get_current_screen() != SCREEN_ID_GRY_TEST) {
        return;
    }

    // Calculate real elapsed time (Delta Time) to keep physics perfectly smooth
    int64_t now_us = esp_timer_get_time();
    float dt = 0.030f;
    if (s_last_time_us > 0) {
        dt = (float)(now_us - s_last_time_us) / 1000000.0f;
    }
    s_last_time_us = now_us;

    // Bound dt to prevent huge skips during hiccups (like flash writes)
    if (dt > 0.050f) {
        dt = 0.050f;
    }

    if (s_state == STATE_READY) {
        // High polish: sine floating animation on start screen
        static float s_ready_time = 0;
        s_ready_time += dt * 4.5f;
        s_bird_y = 200.0f + sinf(s_ready_time) * 15.0f;
        lv_obj_set_pos(s_bird, BIRD_X, (int)s_bird_y - BIRD_RADIUS);
        
        // Wiggle beak slightly
        int beak_y = 13 + (int)(sinf(s_ready_time * 2.0f) * 2.0f);
        lv_obj_set_pos(s_bird_beak, 24, beak_y);
        
        // Neon pulse start message opacity
        static float s_blink_time = 0;
        s_blink_time += dt * 3.5f;
        int blink_opa = 80 + (int)(175.0f * (0.5f + 0.5f * sinf(s_blink_time)));
        lv_obj_set_style_text_opa(s_start_instructions, blink_opa, 0);
        return;
    }

    if (s_state != STATE_PLAYING) {
        return;
    }

    // 1. Physics update: Bird
    s_bird_vy += GRAVITY * dt;
    if (s_bird_vy > TERMINAL_VELOCITY) {
        s_bird_vy = TERMINAL_VELOCITY;
    }
    s_bird_y += s_bird_vy * dt;

    // Apply positions
    lv_obj_set_pos(s_bird, BIRD_X, (int)s_bird_y - BIRD_RADIUS);

    // High polish: smooth interpolated pitch animation (based on vertical velocity)
    float pitch_factor = (s_bird_vy - JUMP_VELOCITY) / (TERMINAL_VELOCITY - JUMP_VELOCITY);
    if (pitch_factor < 0.0f) pitch_factor = 0.0f;
    if (pitch_factor > 1.0f) pitch_factor = 1.0f;

    int beak_y = 7 + (int)(pitch_factor * 11.0f); // 7 (up) to 18 (down)
    int eye_y = 5 + (int)(pitch_factor * 7.0f);   // 5 (up) to 12 (down)
    lv_obj_set_pos(s_bird_beak, 24, beak_y);
    lv_obj_set_pos(s_bird_eye, 18, eye_y);

    // 2. Ceil & Floor collision check
    if (s_bird_y + BIRD_RADIUS >= GROUND_Y) {
        s_bird_y = (float)(GROUND_Y - BIRD_RADIUS);
        trigger_game_over();
        return;
    }
    if (s_bird_y - BIRD_RADIUS <= 0) {
        s_bird_y = (float)BIRD_RADIUS;
        s_bird_vy = 50.0f; // bounce off ceiling slightly
    }

    // 3. Move pipes & check collision
    for (int i = 0; i < PIPE_COUNT; i++) {
        s_pipes[i].x += get_pipe_speed() * dt;

        // Reset pipe if off screen
        if (s_pipes[i].x < -PIPE_WIDTH) {
            float max_x = 0;
            for (int j = 0; j < PIPE_COUNT; j++) {
                if (s_pipes[j].x > max_x) {
                    max_x = s_pipes[j].x;
                }
            }
            s_pipes[i].x = max_x + 290.0f;
            s_pipes[i].gap_y = (float)(140 + rand() % 160);
            s_pipes[i].passed = false;

            int current_gap = get_gap_height();
            // Update top pipe height & bottom pipe height
            int top_h = (int)(s_pipes[i].gap_y - current_gap / 2);
            lv_obj_set_size(s_pipes[i].top_pipe, PIPE_WIDTH, top_h);

            int bottom_y = (int)(s_pipes[i].gap_y + current_gap / 2);
            int bottom_h = GROUND_Y - bottom_y;
            lv_obj_set_size(s_pipes[i].bottom_pipe, PIPE_WIDTH, bottom_h);
            lv_obj_set_pos(s_pipes[i].bottom_pipe, (int)s_pipes[i].x, bottom_y);
        }

        // Apply updated pipe position
        lv_obj_set_pos(s_pipes[i].top_pipe, (int)s_pipes[i].x, 0);
        
        int current_gap = get_gap_height();
        int bottom_y = (int)(s_pipes[i].gap_y + current_gap / 2);
        lv_obj_set_pos(s_pipes[i].bottom_pipe, (int)s_pipes[i].x, bottom_y);

        // Score update check
        if (!s_pipes[i].passed && s_pipes[i].x + PIPE_WIDTH < BIRD_X) {
            s_score++;
            lv_label_set_text_fmt(s_score_label, "%d", s_score);
            s_pipes[i].passed = true;
        }

        // Bounding Box Collision check
        if (s_pipes[i].x < BIRD_X + BIRD_RADIUS && s_pipes[i].x + PIPE_WIDTH > BIRD_X - BIRD_RADIUS) {
            int current_gap = get_gap_height();
            float top_edge = s_pipes[i].gap_y - current_gap / 2.0f;
            float bottom_edge = s_pipes[i].gap_y + current_gap / 2.0f;

            if (s_bird_y - BIRD_RADIUS < top_edge || s_bird_y + BIRD_RADIUS > bottom_edge) {
                trigger_game_over();
                return;
            }
        }
    }
}

// Global Touch Event Callback
static void parent_screen_touch_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG, "Screen pressed! State: %d", (int)s_state);
        // Taps trigger jump unless Game Over is active
        if (s_state != STATE_GAMEOVER) {
            flappy_bird_jump();
        }
    }
}

// Button Events
static void replay_btn_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Replay clicked!");
        lv_obj_add_flag(s_gameover_overlay, LV_OBJ_FLAG_HIDDEN);
        reset_game_state();
        s_state = STATE_PLAYING;
        lv_obj_remove_flag(s_score_label, LV_OBJ_FLAG_HIDDEN);
        s_bird_vy = JUMP_VELOCITY;
    }
}

static void menu_btn_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Menu clicked!");
        flappy_bird_cleanup();
        // Go back to the main menu screen
        eez_flow_set_screen(SCREEN_ID_MENU, (lv_scr_load_anim_t)0, 200, 0);
    }
}

// Game initialization (once per screen create or reuse)
void flappy_bird_init(lv_obj_t *parent_screen) {
    if (parent_screen == NULL) {
        ESP_LOGE(TAG, "Parent screen is NULL!");
        return;
    }

    ESP_LOGI(TAG, "Initializing Flappy Bird on screen GRY TEST");
    load_high_score();

    // Ensure parent_screen is not scrollable
    lv_obj_remove_flag(parent_screen, LV_OBJ_FLAG_SCROLLABLE);

    if (!s_game_initialized) {
        // 1. Create a transparent game container so we can easily show/hide everything
        s_game_container = lv_obj_create(parent_screen);
        lv_obj_set_pos(s_game_container, 0, 0);
        lv_obj_set_size(s_game_container, SCREEN_WIDTH, SCREEN_HEIGHT);
        lv_obj_set_style_pad_all(s_game_container, 0, 0);
        lv_obj_set_style_border_width(s_game_container, 0, 0);
        lv_obj_set_style_radius(s_game_container, 0, 0);
        // Stylize container background to dark cosmic blue
        lv_obj_set_style_bg_color(s_game_container, lv_color_hex(0x110b1a), 0);
        lv_obj_set_style_bg_opa(s_game_container, LV_OPA_COVER, 0);
        lv_obj_remove_flag(s_game_container, LV_OBJ_FLAG_SCROLLABLE);
        
        // Add event callback directly on the game container
        lv_obj_add_flag(s_game_container, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_game_container, parent_screen_touch_cb, LV_EVENT_PRESSED, NULL);

        // 2. Ground
        s_ground = lv_obj_create(s_game_container);
        lv_obj_set_pos(s_ground, 0, GROUND_Y);
        lv_obj_set_size(s_ground, SCREEN_WIDTH, SCREEN_HEIGHT - GROUND_Y);
        lv_obj_set_style_pad_all(s_ground, 0, 0);
        lv_obj_set_style_radius(s_ground, 0, 0);
        lv_obj_set_style_bg_color(s_ground, lv_color_hex(0x18122b), 0);
        lv_obj_set_style_border_width(s_ground, 0, 0);
        lv_obj_set_style_border_side(s_ground, LV_BORDER_SIDE_TOP, 0);
        // Neon purple top border on the ground
        lv_obj_set_style_border_width(s_ground, 3, 0);
        lv_obj_set_style_border_color(s_ground, lv_color_hex(0xa020f0), 0);
        lv_obj_remove_flag(s_ground, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_ground, LV_OBJ_FLAG_CLICKABLE);

        // 3. Score label (sitting in the background)
        s_score_label = lv_label_create(s_game_container);
        lv_obj_set_style_text_font(s_score_label, &ui_font_bebas_neue_regular52, 0);
        lv_obj_set_style_text_color(s_score_label, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_text_opa(s_score_label, 120, 0); // semi-transparent
        lv_obj_align(s_score_label, LV_ALIGN_TOP_MID, 0, 60);
        lv_label_set_text(s_score_label, "0");
        lv_obj_remove_flag(s_score_label, LV_OBJ_FLAG_CLICKABLE);

        // 4. Pipes Pooling
        for (int i = 0; i < PIPE_COUNT; i++) {
            // Top pipe
            s_pipes[i].top_pipe = lv_obj_create(s_game_container);
            lv_obj_set_style_pad_all(s_pipes[i].top_pipe, 0, 0);
            lv_obj_set_style_radius(s_pipes[i].top_pipe, 6, 0);
            lv_obj_set_style_bg_color(s_pipes[i].top_pipe, lv_color_hex(0x0c1a11), 0); // dark forest green
            lv_obj_set_style_border_width(s_pipes[i].top_pipe, 3, 0);
            lv_obj_set_style_border_color(s_pipes[i].top_pipe, lv_color_hex(0x00FF66), 0); // Neon green
            // Glow effect
            lv_obj_set_style_shadow_width(s_pipes[i].top_pipe, 8, 0);
            lv_obj_set_style_shadow_color(s_pipes[i].top_pipe, lv_color_hex(0x00FF66), 0);
            lv_obj_set_style_shadow_opa(s_pipes[i].top_pipe, 130, 0);
            lv_obj_remove_flag(s_pipes[i].top_pipe, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(s_pipes[i].top_pipe, LV_OBJ_FLAG_CLICKABLE);

            // Bottom pipe
            s_pipes[i].bottom_pipe = lv_obj_create(s_game_container);
            lv_obj_set_style_pad_all(s_pipes[i].bottom_pipe, 0, 0);
            lv_obj_set_style_radius(s_pipes[i].bottom_pipe, 6, 0);
            lv_obj_set_style_bg_color(s_pipes[i].bottom_pipe, lv_color_hex(0x0c1a11), 0);
            lv_obj_set_style_border_width(s_pipes[i].bottom_pipe, 3, 0);
            lv_obj_set_style_border_color(s_pipes[i].bottom_pipe, lv_color_hex(0x00FF66), 0);
            // Glow effect
            lv_obj_set_style_shadow_width(s_pipes[i].bottom_pipe, 8, 0);
            lv_obj_set_style_shadow_color(s_pipes[i].bottom_pipe, lv_color_hex(0x00FF66), 0);
            lv_obj_set_style_shadow_opa(s_pipes[i].bottom_pipe, 130, 0);
            lv_obj_remove_flag(s_pipes[i].bottom_pipe, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(s_pipes[i].bottom_pipe, LV_OBJ_FLAG_CLICKABLE);
        }

        // 5. Bird Setup
        s_bird = lv_obj_create(s_game_container);
        lv_obj_set_size(s_bird, BIRD_SIZE, BIRD_SIZE);
        lv_obj_set_style_pad_all(s_bird, 0, 0);
        lv_obj_set_style_radius(s_bird, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_bird, lv_color_hex(0xff6f00), 0); // Neon orange body
        lv_obj_set_style_border_width(s_bird, 2, 0);
        lv_obj_set_style_border_color(s_bird, lv_color_hex(0xffffff), 0); // white neon rim
        // Glow effect
        lv_obj_set_style_shadow_width(s_bird, 10, 0);
        lv_obj_set_style_shadow_color(s_bird, lv_color_hex(0xff6f00), 0);
        lv_obj_set_style_shadow_opa(s_bird, 160, 0);
        lv_obj_remove_flag(s_bird, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_bird, LV_OBJ_FLAG_CLICKABLE);

        // Eye
        s_bird_eye = lv_obj_create(s_bird);
        lv_obj_set_size(s_bird_eye, 8, 8);
        lv_obj_set_style_pad_all(s_bird_eye, 0, 0);
        lv_obj_set_style_radius(s_bird_eye, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_bird_eye, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_width(s_bird_eye, 0, 0);
        lv_obj_set_pos(s_bird_eye, 18, 9);
        lv_obj_remove_flag(s_bird_eye, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_bird_eye, LV_OBJ_FLAG_CLICKABLE);

        // Pupil
        s_bird_pupil = lv_obj_create(s_bird_eye);
        lv_obj_set_size(s_bird_pupil, 3, 3);
        lv_obj_set_style_pad_all(s_bird_pupil, 0, 0);
        lv_obj_set_style_radius(s_bird_pupil, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_bird_pupil, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(s_bird_pupil, 0, 0);
        lv_obj_align(s_bird_pupil, LV_ALIGN_CENTER, 1, 0);
        lv_obj_remove_flag(s_bird_pupil, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_bird_pupil, LV_OBJ_FLAG_CLICKABLE);

        // Beak
        s_bird_beak = lv_obj_create(s_bird);
        lv_obj_set_size(s_bird_beak, 11, 7);
        lv_obj_set_style_pad_all(s_bird_beak, 0, 0);
        lv_obj_set_style_radius(s_bird_beak, 3, 0);
        lv_obj_set_style_bg_color(s_bird_beak, lv_color_hex(0xffd700), 0); // gold/yellow
        lv_obj_set_style_border_width(s_bird_beak, 0, 0);
        lv_obj_set_pos(s_bird_beak, 24, 13);
        lv_obj_remove_flag(s_bird_beak, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_bird_beak, LV_OBJ_FLAG_CLICKABLE);

        // 6. Start Overlay
        s_start_overlay = lv_obj_create(s_game_container);
        lv_obj_set_size(s_start_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
        lv_obj_set_pos(s_start_overlay, 0, 0);
        lv_obj_set_style_pad_all(s_start_overlay, 0, 0);
        lv_obj_set_style_border_width(s_start_overlay, 0, 0);
        lv_obj_set_style_bg_opa(s_start_overlay, LV_OPA_TRANSP, 0); // transparent background
        lv_obj_remove_flag(s_start_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_start_overlay, LV_OBJ_FLAG_CLICKABLE);

        s_start_title = lv_label_create(s_start_overlay);
        lv_obj_set_style_text_font(s_start_title, &ui_font_bebas_neue_regular52, 0);
        lv_obj_set_style_text_color(s_start_title, lv_color_hex(0xff6f00), 0);
        lv_obj_align(s_start_title, LV_ALIGN_CENTER, 0, -80);
        lv_label_set_text(s_start_title, "NEON FLAPPY");
        lv_obj_remove_flag(s_start_title, LV_OBJ_FLAG_CLICKABLE);

        s_start_instructions = lv_label_create(s_start_overlay);
        lv_obj_set_style_text_font(s_start_instructions, &ui_font_consola_18, 0);
        lv_obj_set_style_text_color(s_start_instructions, lv_color_hex(0xffffff), 0);
        lv_obj_align(s_start_instructions, LV_ALIGN_CENTER, 0, 10);
        lv_label_set_text(s_start_instructions, "TAPNIJ EKRAN ABY ROZPOCZĄĆ GRĘ");
        lv_obj_remove_flag(s_start_instructions, LV_OBJ_FLAG_CLICKABLE);

        s_start_record = lv_label_create(s_start_overlay);
        lv_obj_set_style_text_font(s_start_record, &ui_font_consola_18, 0);
        lv_obj_set_style_text_color(s_start_record, lv_color_hex(0x00FF66), 0);
        lv_obj_align(s_start_record, LV_ALIGN_CENTER, 0, 60);
        lv_label_set_text_fmt(s_start_record, "REKORD: %d", s_high_score);
        lv_obj_remove_flag(s_start_record, LV_OBJ_FLAG_CLICKABLE);

        // 7. Game Over Overlay
        s_gameover_overlay = lv_obj_create(s_game_container);
        lv_obj_set_size(s_gameover_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
        lv_obj_set_pos(s_gameover_overlay, 0, 0);
        lv_obj_set_style_pad_all(s_gameover_overlay, 0, 0);
        lv_obj_set_style_border_width(s_gameover_overlay, 0, 0);
        lv_obj_set_style_radius(s_gameover_overlay, 0, 0);
        lv_obj_set_style_bg_color(s_gameover_overlay, lv_color_hex(0x0b0713), 0); // dark tint
        lv_obj_set_style_bg_opa(s_gameover_overlay, 195, 0); // glassmorphic transparent overlay
        lv_obj_remove_flag(s_gameover_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_gameover_overlay, LV_OBJ_FLAG_HIDDEN); // Hidden by default

        s_gameover_title = lv_label_create(s_gameover_overlay);
        lv_obj_set_style_text_font(s_gameover_title, &ui_font_bebas_neue_regular52, 0);
        lv_obj_set_style_text_color(s_gameover_title, lv_color_hex(0xff2a2a), 0); // Neon red
        lv_obj_align(s_gameover_title, LV_ALIGN_CENTER, 0, -110);
        lv_label_set_text(s_gameover_title, "KONIEC GRY");

        s_gameover_score = lv_label_create(s_gameover_overlay);
        lv_obj_set_style_text_font(s_gameover_score, &ui_font_bebas_neue_regular52, 0);
        lv_obj_set_style_text_color(s_gameover_score, lv_color_hex(0xffffff), 0);
        lv_obj_align(s_gameover_score, LV_ALIGN_CENTER, 0, -45);
        lv_label_set_text(s_gameover_score, "WYNIK: 0");

        s_gameover_best = lv_label_create(s_gameover_overlay);
        lv_obj_set_style_text_font(s_gameover_best, &ui_font_consola_18, 0);
        lv_obj_set_style_text_color(s_gameover_best, lv_color_hex(0xB0B0B0), 0);
        lv_obj_align(s_gameover_best, LV_ALIGN_CENTER, 0, 10);
        lv_label_set_text(s_gameover_best, "REKORD: 0");

        // Buttons Container
        lv_obj_t *btn_container = lv_obj_create(s_gameover_overlay);
        lv_obj_set_size(btn_container, 450, 80);
        lv_obj_align(btn_container, LV_ALIGN_CENTER, 0, 95);
        lv_obj_set_style_pad_all(btn_container, 0, 0);
        lv_obj_set_style_border_width(btn_container, 0, 0);
        lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);

        // Replay Button
        s_btn_replay = lv_button_create(btn_container);
        lv_obj_set_size(s_btn_replay, 190, 48);
        lv_obj_set_pos(s_btn_replay, 20, 16);
        lv_obj_set_style_radius(s_btn_replay, 10, 0);
        lv_obj_set_style_bg_color(s_btn_replay, lv_color_hex(0x8a2be2), 0); // Neon purple
        lv_obj_set_style_border_width(s_btn_replay, 2, 0);
        lv_obj_set_style_border_color(s_btn_replay, lv_color_hex(0xffffff), 0);
        lv_obj_add_event_cb(s_btn_replay, replay_btn_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *replay_label = lv_label_create(s_btn_replay);
        lv_obj_set_style_text_font(replay_label, &ui_font_consola_18, 0);
        lv_obj_set_style_text_color(replay_label, lv_color_hex(0xffffff), 0);
        lv_obj_align(replay_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(replay_label, "ZAGRAJ PONOWNIE");

        // Menu Button
        s_btn_menu = lv_button_create(btn_container);
        lv_obj_set_size(s_btn_menu, 190, 48);
        lv_obj_set_pos(s_btn_menu, 240, 16);
        lv_obj_set_style_radius(s_btn_menu, 10, 0);
        lv_obj_set_style_bg_color(s_btn_menu, lv_color_hex(0x44414d), 0); // dark gray-purple
        lv_obj_set_style_border_width(s_btn_menu, 2, 0);
        lv_obj_set_style_border_color(s_btn_menu, lv_color_hex(0xB0B0B0), 0);
        lv_obj_add_event_cb(s_btn_menu, menu_btn_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *menu_label = lv_label_create(s_btn_menu);
        lv_obj_set_style_text_font(menu_label, &ui_font_consola_18, 0);
        lv_obj_set_style_text_color(menu_label, lv_color_hex(0xffffff), 0);
        lv_obj_align(menu_label, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(menu_label, "MENU GŁÓWNE");

        s_game_initialized = true;
    }

    // Always reset visual elements to READY state
    s_state = STATE_READY;
    lv_label_set_text_fmt(s_start_record, "REKORD: %d", s_high_score);

    lv_obj_remove_flag(s_game_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_start_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_gameover_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_score_label, LV_OBJ_FLAG_HIDDEN);

    reset_game_state();

    // Start timer (30ms rate for smooth rendering)
    if (s_timer == NULL) {
        s_timer = lv_timer_create(flappy_bird_timer_cb, 30, NULL);
    } else {
        lv_timer_resume(s_timer);
    }
}

// Clean up timer, delete containers
void flappy_bird_cleanup() {
    ESP_LOGI(TAG, "Cleaning up Flappy Bird");

    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    if (s_game_container != NULL) {
        lv_obj_delete(s_game_container);
        s_game_container = NULL;
    }

    s_bird = NULL;
    s_bird_eye = NULL;
    s_bird_pupil = NULL;
    s_bird_beak = NULL;
    s_ground = NULL;
    s_score_label = NULL;
    s_start_overlay = NULL;
    s_start_title = NULL;
    s_start_instructions = NULL;
    s_start_record = NULL;
    s_gameover_overlay = NULL;
    s_gameover_title = NULL;
    s_gameover_score = NULL;
    s_gameover_best = NULL;
    s_btn_replay = NULL;
    s_btn_menu = NULL;
    for (int i = 0; i < PIPE_COUNT; i++) {
        s_pipes[i].top_pipe = NULL;
        s_pipes[i].bottom_pipe = NULL;
    }

    s_game_initialized = false;
}
