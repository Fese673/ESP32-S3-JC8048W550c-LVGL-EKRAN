#include "zegar_mode_ui.h"
#include "screens.h"
#include "eez-flow.h"
#include "lvgl.h"
#include "communication/time/timer_synchro.h"
#include "esp_log.h"
#include "esp_timer.h"

/*
 * Dwa okrągłe przyciski trybu 7-seg na ekranie ZEGAR (V3.0).
 *
 * Pozycje: prawy górny róg kart obrazkowych (obrazki są rodzeństwem, nie
 * rodzicami — kołka wiszą bezpośrednio na ekranie objects.zegar):
 *   - stoper:   karta abs x=86..287, y=122..458  -> kołko (253,132) 26x26
 *   - minutnik: karta abs x=315..515, y=122..461 -> kołko (481,132) 26x26
 * Odsunięcie 8 px od prawej i 10 px od góry karty; powiększona strefa
 * dotyku (+10 px) — małe kółko musi być wygodne dla palca.
 *
 * Stan: ZEGAR jest właścicielem (echo 0x18). My trzymamy tylko:
 *   s_uiMode      — co pokazują kołka,
 *   s_confirmed   — ostatni potwierdzony tryb,
 *   s_pending     — trwa okno potwierdzenia kliku (2 s, potem rollback).
 * Ekran jest persistentny -> kołka tworzymy raz i tylko restylujemy.
 */

using CommunicationProtocol::TimerSync::kSegModeClock;
using CommunicationProtocol::TimerSync::kSegModeStopwatch;
using CommunicationProtocol::TimerSync::kSegModeTimer;

static constexpr char kTag[] = "ZEGAR_MODE";

static constexpr int32_t kBtnSize = 26;
static constexpr int32_t kBtnStoperX = 253;
static constexpr int32_t kBtnStoperY = 132;
static constexpr int32_t kBtnTimerX = 481;
static constexpr int32_t kBtnTimerY = 132;
static constexpr int32_t kExtClickPx = 10;
static constexpr int64_t kPendingTimeoutUs = 2000000LL; // 2 s

static lv_obj_t *s_btn_stoper = nullptr;
static lv_obj_t *s_btn_timer = nullptr;

static uint8_t s_uiMode = kSegModeClock;
static uint8_t s_confirmedMode = kSegModeClock;
static uint8_t s_pendingMode = 0xFF; // 0xFF = brak okna potwierdzenia
static int64_t s_pendingStartUs = 0;

static void apply_styles(void) {
    const lv_opa_t btn_opa = (s_pendingMode != 0xFF) ? (lv_opa_t)160 : LV_OPA_COVER;

    const bool stoper_on = (s_uiMode == kSegModeStopwatch);
    const bool timer_on = (s_uiMode == kSegModeTimer);

    if (s_btn_stoper) {
        lv_obj_set_style_bg_opa(s_btn_stoper, stoper_on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_btn_stoper, stoper_on ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_opa(s_btn_stoper, btn_opa, LV_PART_MAIN);
    }
    if (s_btn_timer) {
        lv_obj_set_style_bg_opa(s_btn_timer, timer_on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_btn_timer, timer_on ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_opa(s_btn_timer, btn_opa, LV_PART_MAIN);
    }
}

static void mode_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    const uint8_t myMode = static_cast<uint8_t>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (myMode != kSegModeStopwatch && myMode != kSegModeTimer) {
        return;
    }

    // Radio z opcją wyłączenia: klik zapalonego = oba zgaszone (zegar).
    const uint8_t newMode = (s_uiMode == myMode) ? kSegModeClock : myMode;

    s_pendingMode = newMode;
    s_pendingStartUs = esp_timer_get_time();
    s_uiMode = newMode; // optymistycznie — rollback po 2 s bez echa
    apply_styles();

    CommunicationProtocol::TimerSync::sendSegModeCmd(newMode);
    ESP_LOGI(kTag, "seg mode click -> %u (pending)", (unsigned)newMode);
}

static lv_obj_t *create_circle(int32_t x, int32_t y, uint8_t mode) {
    lv_obj_t *btn = lv_obj_create(objects.zegar);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, kBtnSize, kBtnSize);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_ext_click_area(btn, kExtClickPx);
    lv_obj_add_event_cb(btn, mode_btn_event_cb, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(mode)));
    return btn;
}

extern "C" void zegar_mode_ui_init(void) {
    if (!objects.zegar) {
        return;
    }

    // Ekran persistentny — kolka tworzymy raz. Guard na wypadek przyszlego
    // kasowania ekranu (EEZ deleteScreen): nie trzymamy wiszacych wskaznikow.
    if (s_btn_stoper != nullptr && !lv_obj_is_valid(s_btn_stoper)) {
        s_btn_stoper = nullptr;
        s_btn_timer = nullptr;
    }

    if (s_btn_stoper == nullptr) {
        s_btn_stoper = create_circle(kBtnStoperX, kBtnStoperY, kSegModeStopwatch);
        s_btn_timer = create_circle(kBtnTimerX, kBtnTimerY, kSegModeTimer);
        ESP_LOGI(kTag, "seg mode buttons created");
    }

    // Stan startowy z ostatniego echa; potem zawsze swiezy request.
    uint8_t lastMode = kSegModeClock;
    if (CommunicationProtocol::TimerSync::getLastSegMode(lastMode)) {
        s_uiMode = lastMode;
        s_confirmedMode = lastMode;
    }
    s_pendingMode = 0xFF;
    apply_styles();

    CommunicationProtocol::TimerSync::requestSegMode();
}

extern "C" void zegar_mode_ui_tick(void) {
    if (s_btn_stoper == nullptr) {
        return; // ekran ZEGAR jeszcze nie był otwarty
    }

    // Echo z ZEGARA = prawda.
    uint8_t remote = 0;
    if (CommunicationProtocol::TimerSync::consumeSegMode(remote)) {
        if (s_pendingMode != 0xFF) {
            if (remote == s_pendingMode) {
                s_confirmedMode = remote;
                s_uiMode = remote;
                s_pendingMode = 0xFF;
                apply_styles();
                ESP_LOGI(kTag, "seg mode confirmed -> %u", (unsigned)remote);
            }
            // remote != pending: to może być stare echo przed komendą — czekamy
        } else {
            s_confirmedMode = remote;
            if (s_uiMode != remote) {
                s_uiMode = remote; // resync (np. po restarcie ZEGARA: zegar)
                apply_styles();
                ESP_LOGI(kTag, "seg mode resync -> %u", (unsigned)remote);
            }
        }
    }

    // Okno potwierdzenia minęło bez echa -> rollback do prawdy + self-heal:
    // prosimy ZEGAR o swiezy stan, zeby przyciski nie zostaly rozjechane
    // az do bezpiecznego odswiezenia (do 300 s) w razie zgubionego echa.
    if (s_pendingMode != 0xFF &&
        (esp_timer_get_time() - s_pendingStartUs) > kPendingTimeoutUs) {
        s_pendingMode = 0xFF;
        s_uiMode = s_confirmedMode;
        apply_styles();
        CommunicationProtocol::TimerSync::requestSegMode();
        ESP_LOGW(kTag, "seg mode rollback -> %u (brak echa, pytam o stan)",
                 (unsigned)s_confirmedMode);
    }
}
