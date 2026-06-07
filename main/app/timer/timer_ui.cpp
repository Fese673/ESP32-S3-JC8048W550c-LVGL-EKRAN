#include "timer_ui.h"
#include "timer_logic.h"
#include "screens.h"
#include "fonts.h"
#include "eez-flow.h"
#include "lvgl.h"
#include "communication/time/timer_synchro.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static lv_timer_t *s_timer = NULL;

static lv_obj_t *s_roller_h = NULL;
static lv_obj_t *s_roller_m = NULL;
static lv_obj_t *s_roller_s = NULL;
static lv_obj_t *s_colon1 = NULL;
static lv_obj_t *s_colon2 = NULL;
static lv_obj_t *s_countdown_label = NULL;
static lv_obj_t *s_start_stop_label = NULL;
static volatile bool s_user_interacting = false;

// Sync state — śledzenie komend i synchronizacji z Zegarem
static uint8_t s_pendingCmd = 0xFF;     // 0=start, 1=stop, 0xFF=none
static int64_t s_pendingCmdTimeUs = 0;
static int64_t g_lastRemainingUs = 0;
static int64_t g_syncTimeUs = 0;
static int g_userDurationSec = 0;

static const char *s_roller_options_60 =
    "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
    "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
    "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
    "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"
    "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"
    "50\n51\n52\n53\n54\n55\n56\n57\n58\n59";

static const char *s_roller_options_24 =
    "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
    "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
    "20\n21\n22\n23";

// ─── Helpers ──────────────────────────────────────────────────────────

static bool is_remote_running(const CommunicationProtocol::TimerSync::TimerState& r) {
    return r.valid && (r.state == 1 || r.state == 3);
}

static bool is_local_running(void) {
    return timer_get_state() == TIMER_RUNNING;
}

static void start_timer_from_ui(int durationSec) {
    g_userDurationSec = durationSec;
    g_lastRemainingUs = (int64_t)durationSec * 1000000LL;
    g_syncTimeUs = esp_timer_get_time();
    timer_set_target_seconds(durationSec);
    timer_start();
    s_pendingCmd = 0;
    s_pendingCmdTimeUs = esp_timer_get_time();
    CommunicationProtocol::TimerSync::sendTimerCmd(0, durationSec);
}

static void stop_timer_from_ui(void) {
    s_pendingCmd = 1;
    s_pendingCmdTimeUs = esp_timer_get_time();
    CommunicationProtocol::TimerSync::sendTimerCmd(1);
    timer_reset();
}

// ─── Label sync ───────────────────────────────────────────────────────

void timer_ui_sync_start_stop_label(void) {
    if (eez_flow_get_current_screen() != SCREEN_ID_ZEGAR) return;
    if (!s_start_stop_label) s_start_stop_label = objects.start_stop_label_minutnik;
    if (!s_start_stop_label) return;

    CommunicationProtocol::TimerSync::TimerState remote = CommunicationProtocol::TimerSync::getLastTimerState();
    bool running = is_remote_running(remote) || is_local_running();
    const char *expected = running ? "ZERUJ" : "START";
    const char *current = lv_label_get_text(s_start_stop_label);
    if (strcmp(current, expected) != 0) {
        lv_label_set_text(s_start_stop_label, expected);
    }
}

// ─── Main timer UI callback (100ms) ──────────────────────────────────

static void timer_ui_cb(lv_timer_t *timer) {
    (void)timer;
    if (eez_flow_get_current_screen() != SCREEN_ID_ZEGAR) return;
    if (!s_roller_h || !s_roller_m || !s_roller_s || !s_countdown_label) return;

    int64_t now = esp_timer_get_time();
    CommunicationProtocol::TimerSync::TimerState remote = CommunicationProtocol::TimerSync::getLastTimerState();

    // Lokalny tick — wykryj wygaśnięcie niezależnie od Zegara
    timer_tick();

    // ── Phase 1: Synchronizacja stanu z Zegarem ──
    // ZASADA: Zegar jest source of truth. Gution reaguje na zmiany stanu.
    bool allow_sync = false;
    bool force_idle = false;

    if (remote.valid) {
        if (remote.state == 0) {
            // IDLE — bezwarunkowy reset (stop z enkodera / expire)
            force_idle = true;
            allow_sync = true;
            s_pendingCmd = 0xFF;
        } else if (s_pendingCmd == 0xFF) {
            // Brak pending cmd — normalny sync
            allow_sync = true;
        } else {
            // Pending cmd — sync tylko po potwierdzeniu lub timeout 2s
            bool confirmed = (s_pendingCmd == 0 && remote.state == 1) ||
                             (s_pendingCmd == 1 && remote.state == 0);
            if (confirmed || now > s_pendingCmdTimeUs + 2000000LL) {
                allow_sync = true;
                s_pendingCmd = 0xFF;
            }
        }
    }

    if (allow_sync) {
        g_lastRemainingUs = (int64_t)remote.remainingSec * 1000000LL;
        g_syncTimeUs = now;

        // Zachowaj oryginalny czas użytkownika z durationSec
        if (remote.durationSec > 0) {
            g_userDurationSec = remote.durationSec;
        }

        if (force_idle || remote.state == 0 || remote.state == 2) {
            // IDLE lub PAUSED → pokaż rollery
            timer_reset();
        } else if (remote.state == 1) {
            // RUNNING → sync z Zegarem
            int dur = remote.durationSec > 0 ? remote.durationSec : remote.remainingSec;
            timer_sync_start(dur, remote.remainingSec);
            g_userDurationSec = dur;
        } else if (remote.state == 3) {
            // RINGING → potraktuj jak IDLE (rollers)
            timer_reset();
        }
    }

    // ── Phase 2: Display ──
    timer_state_t state = timer_get_state();
    int total_sec;

    if (state == TIMER_RUNNING && g_syncTimeUs > 0) {
        // Interpolacja: ostatnia znana wartość - elapsed od sync
        int64_t elapsed_us = now - g_syncTimeUs;
        int64_t remaining_us = g_lastRemainingUs - elapsed_us;
        if (remaining_us < 0) remaining_us = 0;
        total_sec = (int)((remaining_us + 999999LL) / 1000000LL);
    } else if (state == TIMER_RUNNING) {
        total_sec = timer_get_remaining_seconds();
    } else {
        // IDLE: wyświetl oryginalny czas użytkownika
        total_sec = g_userDurationSec > 0 ? g_userDurationSec : timer_get_target_seconds();
    }

    // ── Phase 3: Render ──
    if (state == TIMER_IDLE) {
        // Rollery — ustawienia czasu
        lv_obj_clear_flag(s_roller_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_roller_m, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_roller_s, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_colon1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_colon2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_countdown_label, LV_OBJ_FLAG_HIDDEN);

        if (!s_user_interacting) {
            int dh = total_sec / 3600;
            int dm = (total_sec % 3600) / 60;
            int ds = total_sec % 60;
            if ((int)lv_roller_get_selected(s_roller_h) != dh)
                lv_roller_set_selected(s_roller_h, dh, LV_ANIM_OFF);
            if ((int)lv_roller_get_selected(s_roller_m) != dm)
                lv_roller_set_selected(s_roller_m, dm, LV_ANIM_OFF);
            if ((int)lv_roller_get_selected(s_roller_s) != ds)
                lv_roller_set_selected(s_roller_s, ds, LV_ANIM_OFF);
        }
    } else {
        // Countdown — odliczanie
        lv_obj_add_flag(s_roller_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_roller_m, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_roller_s, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_colon1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_colon2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_countdown_label, LV_OBJ_FLAG_HIDDEN);

        int h = total_sec / 3600;
        int m = (total_sec % 3600) / 60;
        int s = total_sec % 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
        lv_label_set_text(s_countdown_label, buf);
    }

    timer_ui_sync_start_stop_label();
}

// ─── Button handlers ──────────────────────────────────────────────────

static void on_start_stop_minutnik(lv_event_t *e) {
    (void)e;
    CommunicationProtocol::TimerSync::TimerState remote = CommunicationProtocol::TimerSync::getLastTimerState();
    bool running = is_remote_running(remote) || is_local_running();

    if (!running) {
        int target = timer_get_target_seconds();
        if (target == 0) target = 60;
        start_timer_from_ui(target);
    } else {
        stop_timer_from_ui();
    }
    timer_ui_sync_start_stop_label();
    timer_ui_cb(NULL);
}

static void on_roller_pressed(lv_event_t *e) {
    (void)e;
    s_user_interacting = true;
}

static void on_roller_released(lv_event_t *e) {
    (void)e;
    s_user_interacting = false;
}

static void on_roller_value_changed(lv_event_t *e) {
    (void)e;
    if (timer_get_state() != TIMER_IDLE) return;
    if (!s_roller_h || !s_roller_m || !s_roller_s) return;

    int h = lv_roller_get_selected(s_roller_h);
    int m = lv_roller_get_selected(s_roller_m);
    int s = lv_roller_get_selected(s_roller_s);
    timer_set_time(h, m, s);
    g_userDurationSec = h * 3600 + m * 60 + s;
}

// ─── Init ─────────────────────────────────────────────────────────────

void timer_ui_init(void) {
    // SRAM leak guard — nie twórz obiektów ponownie
    if (s_roller_h != NULL) {
        timer_ui_sync_start_stop_label();
        return;
    }

    lv_obj_t *parent = objects.minutnik_container;
    if (!parent) return;

    // Rollery
    s_roller_h = lv_roller_create(parent);
    lv_roller_set_options(s_roller_h, s_roller_options_24, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_h, 3);
    lv_obj_set_size(s_roller_h, 44, 130);
    lv_obj_set_pos(s_roller_h, 4, 21);

    s_colon1 = lv_label_create(parent);
    lv_label_set_text(s_colon1, ":");
    lv_obj_set_style_text_font(s_colon1, &ui_font_bebas_neue30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_colon1, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_colon1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(s_colon1, 14, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_colon1, 51, 70);

    s_roller_m = lv_roller_create(parent);
    lv_roller_set_options(s_roller_m, s_roller_options_60, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_m, 3);
    lv_obj_set_size(s_roller_m, 44, 130);
    lv_obj_set_pos(s_roller_m, 68, 21);

    s_colon2 = lv_label_create(parent);
    lv_label_set_text(s_colon2, ":");
    lv_obj_set_style_text_font(s_colon2, &ui_font_bebas_neue30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_colon2, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_colon2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(s_colon2, 14, LV_SIZE_CONTENT);
    lv_obj_set_pos(s_colon2, 115, 70);

    s_roller_s = lv_roller_create(parent);
    lv_roller_set_options(s_roller_s, s_roller_options_60, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_s, 3);
    lv_obj_set_size(s_roller_s, 44, 130);
    lv_obj_set_pos(s_roller_s, 132, 21);

    lv_obj_t *rollers[] = { s_roller_h, s_roller_m, s_roller_s };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *r = rollers[i];
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
        lv_obj_set_style_text_font(r, &ui_font_montserrat_regular_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(r, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_pad_left(r, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_right(r, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_top(r, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(r, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, LV_PART_SELECTED);
        lv_obj_set_style_border_width(r, 0, LV_PART_SELECTED);
        lv_obj_set_style_text_font(r, &ui_font_bebas_neue30, LV_PART_SELECTED);
        lv_obj_set_style_text_color(r, lv_color_hex(0xffffff), LV_PART_SELECTED);
        lv_obj_set_style_pad_left(r, 0, LV_PART_SELECTED);
        lv_obj_set_style_pad_right(r, 0, LV_PART_SELECTED);
        lv_obj_set_style_pad_top(r, 0, LV_PART_SELECTED);
        lv_obj_set_style_pad_bottom(r, 0, LV_PART_SELECTED);
        lv_obj_add_event_cb(r, on_roller_value_changed, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(r, on_roller_pressed, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(r, on_roller_released, LV_EVENT_RELEASED, NULL);
    }

    s_countdown_label = lv_label_create(parent);
    lv_obj_set_size(s_countdown_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_countdown_label, &ui_font_bebas_neue35, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_countdown_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_countdown_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_countdown_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_countdown_label, LV_OBJ_FLAG_HIDDEN);

    s_start_stop_label = objects.start_stop_label_minutnik;
    timer_ui_sync_start_stop_label();

    if (objects.start_stop_minutnik_button) {
        uint32_t event_cnt = lv_obj_get_event_count(objects.start_stop_minutnik_button);
        for (uint32_t i = event_cnt; i > 0; i--) {
            lv_obj_remove_event(objects.start_stop_minutnik_button, i - 1);
        }
        lv_obj_add_event_cb(objects.start_stop_minutnik_button, on_start_stop_minutnik, LV_EVENT_PRESSED, NULL);
    }

    s_timer = lv_timer_create(timer_ui_cb, 100, NULL);
}

// ─── EEZ Flow Actions overrides ───────────────────────────────────────

extern "C" {

void action_action_plus_hour_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_hours(1);
    g_userDurationSec = timer_get_target_seconds();
    timer_ui_cb(NULL);
}

void action_action_plus_minutes_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_minutes(1);
    g_userDurationSec = timer_get_target_seconds();
    timer_ui_cb(NULL);
}

void action_action_plus_seconds_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_seconds(1);
    g_userDurationSec = timer_get_target_seconds();
    timer_ui_cb(NULL);
}

void action_action_minus_hour_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_hours(-1);
    g_userDurationSec = timer_get_target_seconds();
    timer_ui_cb(NULL);
}

void action_action_minus_minutes_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_minutes(-1);
    g_userDurationSec = timer_get_target_seconds();
    timer_ui_cb(NULL);
}

void action_action_minus_seconds_minutnik(lv_event_t * e) {
    (void)e;
    timer_add_seconds(-1);
    g_userDurationSec = timer_get_target_seconds();
    timer_ui_cb(NULL);
}

void action_action_toggle_start_stop_minutnik(lv_event_t * e) {
    (void)e;
    CommunicationProtocol::TimerSync::TimerState remote = CommunicationProtocol::TimerSync::getLastTimerState();
    bool running = is_remote_running(remote) || is_local_running();

    if (!running) {
        int target = timer_get_target_seconds();
        if (target == 0) target = 60;
        start_timer_from_ui(target);
    } else {
        stop_timer_from_ui();
    }
    timer_ui_sync_start_stop_label();
    timer_ui_cb(NULL);
}

}
