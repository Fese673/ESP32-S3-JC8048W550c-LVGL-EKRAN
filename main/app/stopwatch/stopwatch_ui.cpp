#include "stopwatch_ui.h"
#include "stopwatch_logic.h"
#include "screens.h"
#include "fonts.h"
#include "eez-flow.h"
#include "lvgl.h"
#include "communication/time/timer_synchro.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static lv_timer_t *s_timer = NULL;

static lv_obj_t *s_min_label = NULL;
static lv_obj_t *s_sec_label = NULL;
static lv_obj_t *s_cs_label = NULL;
static lv_obj_t *s_unit_label = NULL;
static lv_obj_t *s_start_stop_label = NULL;

// Pending command tracking
static uint8_t s_pendingCmd = 0xFF;     // 0=start, 1=stop, 2=reset, 0xFF=none
static int64_t s_pendingCmdTimeUs = 0;

// Catch-up state: remote elapsed at the moment local START was sent
static int64_t s_catchupStartUs = 0;
static int64_t s_catchupRemoteElapsedUs = 0;

// --- Wyświetlanie: < 60 min -> MM:SS:CS + indeks "ms"; >= 60 min -> HH:MM:SS + indeks "s" ---
// Wiersz cyfr jest przeliczany tylko przy zmianie szerokości (flaga dirty), nie co tick.
static const int64_t kHoursModeThresholdMs = 3600000LL;
static bool s_layout_dirty = true;

// Ustawia tekst etykiety tylko gdy naprawdę się zmienił (zero churnu co 20 ms).
static void stoper_label_set(lv_obj_t *label, const char *text) {
    if (!label) return;
    const char *cur = lv_label_get_text(label);
    if (cur != NULL && strcmp(cur, text) == 0) return;
    lv_label_set_text(label, text);
    s_layout_dirty = true;
}

static void on_start_stop_stoper(lv_event_t *e) {
    (void)e;
    if (stopwatch_get_state() == STOPWATCH_RUNNING) {
        s_pendingCmd = 1;
        s_pendingCmdTimeUs = esp_timer_get_time();
        CommunicationProtocol::TimerSync::sendStopwatchCmd(1); // STOP
        stopwatch_stop();
    } else {
        s_pendingCmd = 0;
        s_pendingCmdTimeUs = esp_timer_get_time();
        CommunicationProtocol::TimerSync::sendStopwatchCmd(0); // START
        stopwatch_start();
    }
}

static void on_reset_stoper(lv_event_t *e) {
    (void)e;
    if (stopwatch_get_state() == STOPWATCH_RUNNING) {
        CommunicationProtocol::TimerSync::sendStopwatchCmd(1); // STOP najpierw
        stopwatch_stop();
    }
    s_pendingCmd = 2;
    s_pendingCmdTimeUs = esp_timer_get_time();
    CommunicationProtocol::TimerSync::sendStopwatchCmd(2); // potem RESET
    stopwatch_reset();
    stopwatch_ui_sync_start_stop_label();
}

void stopwatch_ui_sync_start_stop_label(void) {
    if (eez_flow_get_current_screen() != SCREEN_ID_ZEGAR) {
        return;
    }
    if (!s_start_stop_label) {
        s_start_stop_label = objects.start_stop_label_stoper;
    }
    if (!s_start_stop_label) {
        return;
    }

    const char *expected_text = stopwatch_get_state() == STOPWATCH_RUNNING ? "STOP" : "START";
    const char *current_text = lv_label_get_text(s_start_stop_label);
    if (strcmp(current_text, expected_text) != 0) {
        lv_label_set_text(s_start_stop_label, expected_text);
    }
}

// Ustawia wiersz [min][:][sek][:][setne] w kontenerze: dobiera font (bebas35->bebas30)
// i letter_space (2->0), centruje i pozycjonuje separatory obj41/obj43. Przelicza
// tylko gdy dirty (zmiana liczby cyfr/szerokości) — nie w każdym ticku 20 ms.
//
// UWAGA: nazwy objNN nadaje EEZ i zmieniają się przy każdym eksporcie.
// Dlatego przed ruszeniem separatorów weryfikujemy, że naprawdę leżą w wierszu
// stopera (ten sam parent co cyfry) — inaczej stoper_apply_layout() potrafił
// wyrwać dwukropek z pigułki zegara w nagłówku (obj40) i go popsuć.
static void stoper_apply_layout(void) {
    if (!s_layout_dirty) return;
    if (!s_min_label || !s_sec_label || !s_cs_label) return;
    s_layout_dirty = false;

    lv_obj_t *parent = lv_obj_get_parent(s_min_label);
    if (!parent) return;
    const int32_t container_w = lv_obj_get_width(parent);
    const int32_t avail_w = container_w - 12; // margines 6 px z każdej strony
    if (avail_w <= 0) return;

    lv_obj_t *digits[3] = { s_min_label, s_sec_label, s_cs_label };
    lv_obj_t *colon1 = objects.obj41;
    lv_obj_t *colon2 = objects.obj43;

    // Guard przed rozjazdem numeracji EEZ: separatory muszą być labelami
    // w tym samym kontenerze co cyfry. W przeciwnym razie nie ruszamy niczego.
    if (colon1 && (!lv_obj_check_type(colon1, &lv_label_class) ||
                   lv_obj_get_parent(colon1) != parent)) {
        colon1 = NULL;
    }
    if (colon2 && (!lv_obj_check_type(colon2, &lv_label_class) ||
                   lv_obj_get_parent(colon2) != parent)) {
        colon2 = NULL;
    }

    static const uint32_t kFontSize[3] = { 35, 35, 30 };
    static const int32_t kLetterSpace[3] = { 2, 0, 0 };
    static const int32_t kGap = 2;
    static const int32_t kColonFallbackW = 7;

    int32_t w_digit[3] = { 0, 0, 0 };
    int32_t w_colon1 = kColonFallbackW;
    int32_t w_colon2 = kColonFallbackW;
    int32_t total_w = 0;

    for (uint8_t c = 0; c < 3; c++) {
        const lv_font_t *font = (kFontSize[c] >= 33) ? &ui_font_bebas_neue35 : &ui_font_bebas_neue30;
        for (int i = 0; i < 3; i++) {
            lv_obj_set_style_text_font(digits[i], font, 0);
            lv_obj_set_style_text_letter_space(digits[i], kLetterSpace[c], 0);
        }
        if (colon1) lv_obj_set_style_text_font(colon1, font, 0);
        if (colon2) lv_obj_set_style_text_font(colon2, font, 0);
        for (int i = 0; i < 3; i++) {
            lv_obj_update_layout(digits[i]);
            w_digit[i] = lv_obj_get_width(digits[i]);
        }
        if (colon1) { lv_obj_update_layout(colon1); w_colon1 = lv_obj_get_width(colon1); }
        if (colon2) { lv_obj_update_layout(colon2); w_colon2 = lv_obj_get_width(colon2); }

        total_w = w_digit[0] + w_digit[1] + w_digit[2] + w_colon1 + w_colon2 + 4 * kGap;
        if (total_w <= avail_w) break;
        // ostatnia (najmniejsza) kandydatura zostaje nawet gdy nadal się nie mieści
    }

    int32_t x = (container_w - total_w) / 2;
    if (x < 6) x = 6; // nie wychodź poza lewy margines przy skrajnych długościach

    lv_obj_set_x(s_min_label, x); x += w_digit[0] + kGap;
    if (colon1) lv_obj_set_x(colon1, x);
    x += w_colon1 + kGap;
    lv_obj_set_x(s_sec_label, x); x += w_digit[1] + kGap;
    if (colon2) lv_obj_set_x(colon2, x);
    x += w_colon2 + kGap;
    lv_obj_set_x(s_cs_label, x);
}

static void stopwatch_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (eez_flow_get_current_screen() != SCREEN_ID_ZEGAR) return;
    if (!s_min_label) return;

    int64_t now = esp_timer_get_time();
    CommunicationProtocol::TimerSync::StopwatchState remote = CommunicationProtocol::TimerSync::getLastStopwatchState();
    stopwatch_state_t local = stopwatch_get_state();

    // --- Phase 1: Synchronizacja stanu z Zegarem ---
    if (remote.valid) {
        bool pending = (s_pendingCmd != 0xFF && now < s_pendingCmdTimeUs + 2000000LL);

        if (remote.state == 0) {
            // Zegar IDLE → reset (zawsze, pending/reset jest OK)
            if (local != STOPWATCH_IDLE) stopwatch_reset();
            s_pendingCmd = 0xFF;
        } else if (remote.state == 1 && local == STOPWATCH_IDLE && !pending) {
            // IDLE→RUNNING: catch-up elapsed
            stopwatch_start();
            s_catchupStartUs = now;
            s_catchupRemoteElapsedUs = (int64_t)remote.elapsedMs * 1000;
            stopwatch_set_elapsed_us(s_catchupRemoteElapsedUs);
        } else if (remote.state == 1 && local == STOPWATCH_PAUSED && !pending) {
            // PAUSED→RUNNING: resume (elapsed OK from stop sync)
            stopwatch_start();
        } else if (remote.state == 2 && local == STOPWATCH_RUNNING && !pending) {
            // RUNNING→STOPPED: stop + sync elapsed
            stopwatch_stop();
            stopwatch_set_elapsed_us((int64_t)remote.elapsedMs * 1000);
            s_pendingCmd = 0xFF;
        } else if (remote.state == 2 && local == STOPWATCH_PAUSED) {
            // STOPPED: sync elapsed (static)
            stopwatch_set_elapsed_us((int64_t)remote.elapsedMs * 1000);
            if (pending && s_pendingCmd == 1) s_pendingCmd = 0xFF;
        }

        // Catch-up: when remote transitions to RUNNING, adjust local elapsed
        if (remote.state == 1 && local == STOPWATCH_RUNNING && s_catchupStartUs > 0) {
            int64_t local_elapsed = now - s_catchupStartUs + s_catchupRemoteElapsedUs;
            int64_t remote_elapsed = (int64_t)remote.elapsedMs * 1000;
            int64_t diff = remote_elapsed - local_elapsed;
            if (diff > 100000 || diff < -100000) {  // >100ms drift
                stopwatch_set_elapsed_us(remote_elapsed);
                s_catchupStartUs = now;
                s_catchupRemoteElapsedUs = remote_elapsed;
            }
        }

        // Clear catch-up when not running
        if (remote.state != 1 || local != STOPWATCH_RUNNING) {
            s_catchupStartUs = 0;
        }
    }

    // --- Phase 2: Display — ZAWSZE lokalny (płynne 50fps) ---
    // < 60 min: MM:SS:CS + indeks "ms"; >= 60 min: HH:MM:SS + indeks "s".
    int64_t elapsed_us = stopwatch_get_elapsed_us();
    int64_t total_ms = elapsed_us / 1000;
    bool hours_mode = total_ms >= kHoursModeThresholdMs;

    int field1, field2, field3;
    if (hours_mode) {
        field1 = (int)(total_ms / 3600000LL);
        field2 = (int)((total_ms / 60000LL) % 60);
        field3 = (int)((total_ms / 1000LL) % 60);
    } else {
        field1 = (int)(total_ms / 60000LL);
        field2 = (int)((total_ms / 1000LL) % 60);
        field3 = (int)((total_ms % 1000LL) / 10);
    }

    char buf[8];
    snprintf(buf, sizeof(buf), "%02d", field1); stoper_label_set(s_min_label, buf);
    snprintf(buf, sizeof(buf), "%02d", field2); stoper_label_set(s_sec_label, buf);
    snprintf(buf, sizeof(buf), "%02d", field3); stoper_label_set(s_cs_label, buf);

    if (s_unit_label) {
        const char *unit = hours_mode ? "s" : "ms";
        const char *cur_unit = lv_label_get_text(s_unit_label);
        if (cur_unit == NULL || strcmp(cur_unit, unit) != 0) {
            lv_label_set_text(s_unit_label, unit);
        }
    }

    stoper_apply_layout();
}

void stopwatch_ui_init(void) {
    lv_obj_add_flag(objects.minutes_stoper_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.seconds_stoper_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.ms_stoper_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *parent = lv_obj_get_parent(objects.minutes_stoper_label);

    s_min_label = lv_label_create(parent);
    lv_obj_set_pos(s_min_label, 9, 18);
    lv_obj_set_size(s_min_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(s_min_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_min_label, &ui_font_bebas_neue35, 0);
    lv_obj_set_style_text_letter_space(s_min_label, 2, 0);
    lv_label_set_text(s_min_label, "00");

    s_sec_label = lv_label_create(parent);
    lv_obj_set_pos(s_sec_label, 61, 18);
    lv_obj_set_size(s_sec_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(s_sec_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_sec_label, &ui_font_bebas_neue35, 0);
    lv_obj_set_style_text_letter_space(s_sec_label, 2, 0);
    lv_label_set_text(s_sec_label, "00");

    s_cs_label = lv_label_create(parent);
    lv_obj_set_pos(s_cs_label, 112, 17);
    lv_obj_set_size(s_cs_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(s_cs_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_cs_label, &ui_font_bebas_neue35, 0);
    lv_obj_set_style_text_letter_space(s_cs_label, 2, 0);
    lv_label_set_text(s_cs_label, "00");

    // Indeks jednostki — prawy dolny róg kontenera, wewnątrz zdjęcia kfe_stoper.
    // Montserrat 18: zwykłe małe litery "ms"/"s" (nie kapitaliki jak w Bebas),
    // ~30% mniejsze od bebas30; dosunięty do samego rogu.
    s_unit_label = lv_label_create(parent);
    lv_obj_set_size(s_unit_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(s_unit_label, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_unit_label, &lv_font_montserrat_18, 0);
    lv_label_set_text(s_unit_label, "ms");
    lv_obj_align(s_unit_label, LV_ALIGN_BOTTOM_RIGHT, -1, 0);

    if (objects.start_stop_stoper_button) {
        lv_obj_add_event_cb(objects.start_stop_stoper_button, on_start_stop_stoper, LV_EVENT_PRESSED, NULL);
    }
    if (objects.reset_stoper_button) {
        lv_obj_add_event_cb(objects.reset_stoper_button, on_reset_stoper, LV_EVENT_PRESSED, NULL);
    }

    stopwatch_ui_sync_start_stop_label();

    s_timer = lv_timer_create(stopwatch_timer_cb, 20, NULL);
}
