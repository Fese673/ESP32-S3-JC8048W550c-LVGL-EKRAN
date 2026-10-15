#include "set_wifi.h"
#include "ui/ui/screens.h"
#include "ui/ui/fonts.h"
#include "communication/CommunicationProtocol.h"
#include "communication/CommunicationState.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/*
 * Panel "Ustawienia WiFi" (V3.0, ujednolicone WiFi).
 *
 * Ekran jest TYLKO HMI: nie laczy sie z WiFi (WIFI_ENABLE=0 w wifi_service.c),
 * nie odpala SNTP. Wszystko robi ZEGAR ESP32 po kablu COBS (ramki 0x11):
 *  - SKANUJE swoja antena (lista = sieci widziane przez ZEGARA),
 *  - TESTOWAL haslo i ZAPISUJE dane ("zegar": wifiSsid/wifiPass),
 *  - ADOPTUJE nowe dane od razu (bez restartu) albo wraca na stare.
 * Czas na ekranie plynie z ZEGARA ramka 0x03 co 1 s (co do sekundy).
 *
 * Ten plik: wylacznie UI + maszyna stanow (IDLE/SCANNING/TESTING).
 * Zero dynamicznych alokacji: stale bufory, lista czyszczona per skan,
 * timery tworzone raz — wielokrotne wejscia nie moga wyciekac pamieci.
 */

static const char *TAG = "WIFI_SET";

static constexpr uint8_t kScanMax = 12;
static constexpr int64_t kScanTimeoutMs = 12000;
static constexpr int64_t kTestTimeoutMs = 25000;
static constexpr int kTestSeconds = 15;
static constexpr uint32_t kPollMs = 250;

static constexpr uint32_t COL_INFO = 0xFFFFFF;
static constexpr uint32_t COL_OK = 0x22C55E;
static constexpr uint32_t COL_ERR = 0xEF4444;
static constexpr uint32_t COL_WARN = 0xF59E0B;

enum panel_state_t {
    ST_IDLE = 0,
    ST_SCANNING,
    ST_TESTING,
};

static panel_state_t s_state = ST_IDLE;
static char s_selected_ssid[33] = "";
static bool s_selected_open = false;
static int64_t s_state_started_us = 0;
static int s_last_remaining_s = -1;

static CommunicationProtocol::WifiScanEntry s_scan_entries[kScanMax];
static uint8_t s_scan_count = 0;
static char s_saved_ssid[33] = "";

static char s_status_buf[192];
static lv_timer_t *s_poll_timer = NULL;
static lv_obj_t *s_wifi_connect_btn = NULL;
static lv_obj_t *s_wifi_scan_btn = NULL;

static void list_btn_event_handler(lv_event_t *e);
static void scan_btn_event_handler(lv_event_t *e);
static void connect_btn_event_handler(lv_event_t *e);

// ─── Pomocnicze ───────────────────────────────────────────────────────

static void set_status(uint32_t color, const char *fmt, ...) {
    if (!objects.wifi_status_labe) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_status_buf, sizeof(s_status_buf), fmt, args);
    va_end(args);
    lv_label_set_text_static(objects.wifi_status_labe, s_status_buf);
    lv_obj_set_style_text_color(objects.wifi_status_labe, lv_color_hex(color),
                                LV_PART_MAIN);
}

static void set_panel_busy(bool busy) {
    const lv_opa_t opa = busy ? static_cast<lv_opa_t>(130)
                              : static_cast<lv_opa_t>(LV_OPA_COVER);
    if (s_wifi_connect_btn) {
        lv_obj_set_style_opa(s_wifi_connect_btn, opa, LV_PART_MAIN);
    }
    if (s_wifi_scan_btn) {
        lv_obj_set_style_opa(s_wifi_scan_btn, opa, LV_PART_MAIN);
    }
}

static void show_no_reply(void) {
    set_status(COL_ERR,
               "ZEGAR nie odpowiada - sprawd\u017A przew\u00F3d "
               "i wci\u015Bnij RESET na ZEGARZE.");
}

// ─── Akcje panelu ─────────────────────────────────────────────────────

static void panel_start_scan(void) {
    if (s_state != ST_IDLE) return;
    s_state = ST_SCANNING;
    s_state_started_us = esp_timer_get_time();
    set_panel_busy(true);
    set_status(COL_INFO, "Sprawdzam, jakie sieci widzi ZEGAR...");
    CommunicationProtocol::sendWifiInfoRequest();
    CommunicationProtocol::sendWifiScanRequest();
    ESP_LOGI(TAG, "scan requested from ZEGAR");
}

static void panel_request_connect(void) {
    if (s_state != ST_IDLE) return;

    if (s_selected_ssid[0] == '\0') {
        set_status(COL_ERR, "Najpierw wybierz sie\u0107 z listy.");
        return;
    }

    const char *pwd = (objects.wifi_password_input)
                          ? lv_textarea_get_text(objects.wifi_password_input)
                          : "";
    if (!s_selected_open && (pwd == NULL || pwd[0] == '\0')) {
        set_status(COL_ERR, "Wpisz has\u0142o do sieci %s.", s_selected_ssid);
        return;
    }

    s_state = ST_TESTING;
    s_state_started_us = esp_timer_get_time();
    s_last_remaining_s = -1;
    set_panel_busy(true);
    set_status(COL_INFO,
               "\u0141\u0105cz\u0119 ZEGARA z sieci\u0105 \"%s\"... "
               "sprawdzam has\u0142o (do %d s)",
               s_selected_ssid, kTestSeconds);
    CommunicationProtocol::sendWifiSetCredentials(s_selected_ssid,
                                                  s_selected_open ? "" : pwd);
    ESP_LOGI(TAG, "credentials sent to ZEGAR ssid=%s open=%d", s_selected_ssid,
             (int)s_selected_open);
}

// ─── Render listy / wynikow ───────────────────────────────────────────

static void rebuild_wifi_list(uint8_t status, uint8_t count) {
    if (objects.wifi_list) {
        lv_obj_clean(objects.wifi_list);
    }
    s_scan_count = 0;
    s_selected_ssid[0] = '\0';
    s_selected_open = false;

    if (status != 0) {
        if (status == 5) { // kResWifiOff (tryb BT) — lustro WifiProvisioning.h
            set_status(COL_WARN,
                       "ZEGAR pracuje w trybie Bluetooth - prze\u0142\u0105cz go "
                       "na WiFi i naci\u015Bnij OD\u015AWIE\u017B.");
        } else if (status == 4) { // kResBusy
            set_status(COL_WARN,
                       "ZEGAR jest zaj\u0119ty inn\u0105 operacj\u0105 - "
                       "spr\u00F3buj za chwil\u0119.");
        } else {
            set_status(COL_ERR,
                       "Nie uda\u0142o si\u0119 pobra\u0107 listy sieci z ZEGARA "
                       "- spr\u00F3buj ponownie.");
        }
        return;
    }

    if (count == 0) {
        set_status(COL_WARN,
                   "ZEGAR nie znalaz\u0142 \u017Cadnej sieci. Sprawd\u017A router "
                   "i naci\u015Bnij OD\u015AWIE\u017B.");
        return;
    }

    s_scan_count = count;
    if (objects.wifi_list) {
        for (uint8_t i = 0; i < count; ++i) {
            const char *symbol = s_scan_entries[i].open ? LV_SYMBOL_WIFI
                                                        : LV_SYMBOL_SETTINGS;
            lv_obj_t *btn = lv_list_add_button(objects.wifi_list, symbol,
                                               s_scan_entries[i].ssid);
            lv_obj_add_event_cb(btn, list_btn_event_handler, LV_EVENT_CLICKED,
                                NULL);
        }
    }

    if (s_saved_ssid[0] != '\0') {
        set_status(COL_INFO,
                   "Znaleziono %u sieci. Wybierz sie\u0107 z listy i wpisz "
                   "has\u0142o. Zapisana sie\u0107 w ZEGARZE: %s.",
                   (unsigned)count, s_saved_ssid);
    } else {
        set_status(COL_INFO,
                   "Znaleziono %u sieci. Wybierz sie\u0107 z listy i wpisz "
                   "has\u0142o. ZEGAR nie ma jeszcze zapisanej sieci.",
                   (unsigned)count);
    }
}

static void show_cred_result(uint8_t code, const char *ssid) {
    const char *name = (ssid != NULL && ssid[0] != '\0') ? ssid : s_selected_ssid;
    switch (code) {
    case 0: // kResOk — zapisane i aktywne od razu, restart tylko awaryjnie
        set_status(COL_OK,
                   "Gotowe! ZEGAR po\u0142\u0105czony z sieci\u0105 \"%s\". "
                   "Ustawienie zapisane i aktywne od razu. Gdyby co\u015B nie "
                   "dzia\u0142a\u0142o: wy\u0142\u0105cz ZEGARA z gniazdka i "
                   "w\u0142\u0105cz ponownie.",
                   name);
        break;
    case 1: // kResBadAuth
        set_status(COL_ERR,
                   "Nie uda\u0142o si\u0119 po\u0142\u0105czy\u0107 z \"%s\" "
                   "- sprawd\u017A has\u0142o i spr\u00F3buj jeszcze raz. "
                   "Nic nie zapisano.",
                   name);
        break;
    case 2: // kResNoAp
        set_status(COL_ERR,
                   "ZEGAR nie widzi sieci \"%s\" - przesu\u0144 go "
                   "bli\u017Cej routera albo od\u015Bwie\u017C list\u0119.",
                   name);
        break;
    case 3: // kResTimeout
        set_status(COL_ERR,
                   "Sie\u0107 \"%s\" nie odpowiedzia\u0142a w czasie - "
                   "spr\u00F3buj ponownie.",
                   name);
        break;
    case 4: // kResBusy
        set_status(COL_WARN,
                   "ZEGAR jest zaj\u0119ty inn\u0105 operacj\u0105 - spr\u00F3buj "
                   "za chwil\u0119.");
        break;
    case 5: // kResWifiOff
        set_status(COL_WARN,
                   "ZEGAR pracuje teraz w trybie Bluetooth. Prze\u0142\u0105cz go "
                   "na WiFi i spr\u00F3buj ponownie.");
        break;
    case 6: // kResNvsFail
        set_status(COL_WARN,
                   "Po\u0142\u0105czenie udane, ale nie uda\u0142o si\u0119 "
                   "zapisa\u0107 ustawienia w ZEGARZE - spr\u00F3buj ponownie.");
        break;
    case 7: // kResError — blad wewnetrzny/drivera ZEGARA
        set_status(COL_ERR,
                   "ZEGAR zg\u0142osi\u0142 b\u0142\u0105d wewn\u0119trzny "
                   "- spr\u00F3buj ponownie.");
        break;
    case 8: // kResInvalid
        set_status(COL_ERR,
                   "Nieprawid\u0142owa nazwa sieci lub has\u0142o (za d\u0142ugie?).");
        break;
    default:
        set_status(COL_ERR,
                   "Nie uda\u0142o si\u0119 po\u0142\u0105czy\u0107 (kod %u). "
                   "Spr\u00F3buj ponownie.",
                   (unsigned)code);
        break;
    }
}

// ─── Zdarzenia UI ─────────────────────────────────────────────────────

static void list_btn_event_handler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_state != ST_IDLE) return; // test/skan w toku — nie mieszaj

    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const char *text = lv_list_get_button_text(objects.wifi_list, obj);
    if (text == NULL) return;

    strncpy(s_selected_ssid, text, sizeof(s_selected_ssid) - 1);
    s_selected_ssid[sizeof(s_selected_ssid) - 1] = '\0';

    s_selected_open = false;
    for (uint8_t i = 0; i < s_scan_count; ++i) {
        if (strcmp(s_scan_entries[i].ssid, s_selected_ssid) == 0) {
            s_selected_open = (s_scan_entries[i].open != 0);
            break;
        }
    }

    if (objects.wifi_password_input) {
        lv_textarea_set_max_length(objects.wifi_password_input, 64);
        lv_textarea_set_text(objects.wifi_password_input, "");
        lv_textarea_set_placeholder_text(
            objects.wifi_password_input,
            s_selected_open ? "Brak has\u0142a - sie\u0107 otwarta"
                            : "Has\u0142o...");
    }

    if (s_selected_open) {
        set_status(COL_INFO,
                   "Wybrano: %s (sie\u0107 otwarta - has\u0142o "
                   "niepotrzebne). Naci\u015Bnij PO\u0141\u0104CZ.",
                   s_selected_ssid);
    } else {
        set_status(COL_INFO,
                   "Wybrano: %s. Wpisz has\u0142o i naci\u015Bnij PO\u0141\u0104CZ.",
                   s_selected_ssid);
    }
    ESP_LOGI(TAG, "selected ssid=%s open=%d", s_selected_ssid,
             (int)s_selected_open);
}

static void scan_btn_event_handler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    panel_start_scan();
}

static void connect_btn_event_handler(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    panel_request_connect();
}

// ─── Puls: wyniki z ZEGARA + timeouty ─────────────────────────────────

static void poll_cb(lv_timer_t *t) {
    (void)t;

    // 1) Info o ZEGARZE (zapisany SSID — cache do komunikatow).
    uint8_t info_wifi_on = 0, info_bt = 0, info_connected = 0;
    char info_ssid[33] = "";
    if (CommunicationProtocol::consumeWifiInfo(info_wifi_on, info_bt,
                                               info_connected, info_ssid,
                                               sizeof(info_ssid))) {
        strncpy(s_saved_ssid, info_ssid, sizeof(s_saved_ssid) - 1);
        s_saved_ssid[sizeof(s_saved_ssid) - 1] = '\0';
    }

    // 2) Wynik skanu (komplet paginowanych ramek 0x81).
    uint8_t scan_status = 0, scan_count = 0;
    if (CommunicationProtocol::consumeWifiScan(scan_status, scan_count,
                                               s_scan_entries, kScanMax)) {
        if (s_state == ST_SCANNING) {
            s_state = ST_IDLE;
            set_panel_busy(false);
            rebuild_wifi_list(scan_status, scan_count);
        }
    }

    // 3) Wynik testu hasla + zapisu.
    uint8_t cred_code = 0;
    uint16_t cred_reason = 0;
    char cred_ssid[33] = "";
    if (CommunicationProtocol::consumeWifiCredResult(cred_code, cred_reason,
                                                     cred_ssid,
                                                     sizeof(cred_ssid))) {
        // Spozniony wynik POPRZEDNIEGO testu (np. nowa proba po timeoucie):
        // jesli nazwa sieci nie zgadza sie z biezacym wyborem — ignoruj
        // i czekaj dalej na wlasciwy wynik (inaczej pokazalibysmy cudzy).
        const bool foreign =
            (s_state == ST_TESTING) && cred_ssid[0] != '\0' &&
            strcmp(cred_ssid, s_selected_ssid) != 0;
        if (foreign) {
            ESP_LOGW(TAG, "stale cred result dropped ssid=%s (current=%s)",
                     cred_ssid, s_selected_ssid);
        } else if (s_state == ST_TESTING) {
            s_state = ST_IDLE;
            set_panel_busy(false);
            show_cred_result(cred_code, cred_ssid);
            if (cred_code == 0 || cred_code == 6) {
                CommunicationProtocol::sendWifiInfoRequest();
            }
            ESP_LOGI(TAG, "cred result code=%u reason=%u", (unsigned)cred_code,
                     (unsigned)cred_reason);
        }
    }

    // 4) Timeouty + licznik sekund.
    if (s_state == ST_SCANNING) {
        const int64_t elapsed_ms =
            (esp_timer_get_time() - s_state_started_us) / 1000;
        if (elapsed_ms > kScanTimeoutMs) {
            s_state = ST_IDLE;
            set_panel_busy(false);
            show_no_reply();
        }
    } else if (s_state == ST_TESTING) {
        const int64_t elapsed_ms =
            (esp_timer_get_time() - s_state_started_us) / 1000;
        const int remaining = kTestSeconds - static_cast<int>(elapsed_ms / 1000);
        if (remaining != s_last_remaining_s) {
            s_last_remaining_s = remaining;
            if (remaining > 0) {
                set_status(COL_INFO,
                           "\u0141\u0105cz\u0119 ZEGARA z sieci\u0105 \"%s\"... "
                           "sprawdzam has\u0142o (do %d s)",
                           s_selected_ssid, remaining);
            } else {
                // Po 15 s ZEGAR jeszcze walczy (np. DHCP) — nie zamrazaj
                // licznika na "0 s", powiedz wprost ze sprawdzanie trwa.
                set_status(COL_INFO,
                           "\u0141\u0105cz\u0119 ZEGARA z sieci\u0105 \"%s\"... "
                           "sprawdzam dalej, jeszcze chwila",
                           s_selected_ssid);
            }
        }
        if (elapsed_ms > kTestTimeoutMs) {
            s_state = ST_IDLE;
            set_panel_busy(false);
            show_no_reply();
        }
    }
}

// ─── API ekranu ───────────────────────────────────────────────────────

extern "C" void wifi_screen_init(void) {
    ESP_LOGI(TAG, "init WiFi panel (remote provisioning ZEGARA)");
    if (!objects.ustawienia_wifi) return;

    // Pasek komunikatow: pelna szerokosc, duza czytelnosc, dlugie teksty
    // plynnie sie przewijaja (zamiast ucinania).
    if (objects.wifi_status_labe) {
        lv_obj_set_pos(objects.wifi_status_labe, 16, 42);
        lv_obj_set_size(objects.wifi_status_labe, 768, 24);
        lv_label_set_long_mode(objects.wifi_status_labe,
                               LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(objects.wifi_status_labe,
                                    lv_color_hex(COL_INFO), LV_PART_MAIN);
    }

    if (objects.wifi_keyboard && objects.wifi_password_input) {
        lv_keyboard_set_textarea(objects.wifi_keyboard,
                                 objects.wifi_password_input);
        lv_textarea_set_max_length(objects.wifi_password_input, 64);
        lv_textarea_set_placeholder_text(objects.wifi_password_input,
                                         "Has\u0142o...");
    }

    // Stare przyciski z poprzednich wejsc (ekran jest persistentny).
    if (s_wifi_connect_btn) { lv_obj_del(s_wifi_connect_btn); s_wifi_connect_btn = NULL; }
    if (s_wifi_scan_btn) { lv_obj_del(s_wifi_scan_btn); s_wifi_scan_btn = NULL; }

    // Kolumna przyciskow jak dotad: x=672, szerokosc 118 — POLACZ wyzej.
    s_wifi_connect_btn = lv_button_create(objects.ustawienia_wifi);
    lv_obj_set_pos(s_wifi_connect_btn, 672, 91);
    lv_obj_set_size(s_wifi_connect_btn, 118, 54);
    lv_obj_set_style_bg_color(s_wifi_connect_btn, lv_color_hex(0x18161b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_wifi_connect_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_wifi_connect_btn, lv_color_hex(0x7a7580), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_wifi_connect_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_wifi_connect_btn, 12, LV_PART_MAIN);
    {
        lv_obj_t *lbl = lv_label_create(s_wifi_connect_btn);
        lv_label_set_text_static(lbl, "PO\u0141\u0104CZ");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &ui_font_consola_18, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_add_event_cb(s_wifi_connect_btn, connect_btn_event_handler,
                        LV_EVENT_CLICKED, NULL);

    s_wifi_scan_btn = lv_button_create(objects.ustawienia_wifi);
    lv_obj_set_pos(s_wifi_scan_btn, 672, 161);
    lv_obj_set_size(s_wifi_scan_btn, 118, 54);
    lv_obj_set_style_bg_color(s_wifi_scan_btn, lv_color_hex(0x18161b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_wifi_scan_btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_wifi_scan_btn, lv_color_hex(0x7a7580), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_wifi_scan_btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_wifi_scan_btn, 12, LV_PART_MAIN);
    {
        lv_obj_t *lbl = lv_label_create(s_wifi_scan_btn);
        lv_label_set_text_static(lbl, "OD\u015AWIE\u017B");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &ui_font_consola_18, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_add_event_cb(s_wifi_scan_btn, scan_btn_event_handler,
                        LV_EVENT_CLICKED, NULL);

    // Swiezy start panelu (ekran persistentny — czyscimy po poprzedniej wizycie).
    s_state = ST_IDLE;
    s_selected_ssid[0] = '\0';
    s_selected_open = false;
    s_last_remaining_s = -1;
    s_scan_count = 0;
    if (objects.wifi_list) {
        lv_obj_clean(objects.wifi_list);
    }
    if (objects.wifi_password_input) {
        lv_textarea_set_text(objects.wifi_password_input, "");
    }

    // Drenaz starych wynikow: odpowiedz sprzed wyjscia z ekranu (albo sprzed
    // chwili) nie moze udawac wyniku nowego skanu ani przyslonic swiezej.
    {
        uint8_t d_status = 0, d_count = 0;
        CommunicationProtocol::consumeWifiScan(d_status, d_count, nullptr, 0);
        uint8_t d_code = 0;
        uint16_t d_reason = 0;
        char d_ssid[33] = "";
        CommunicationProtocol::consumeWifiCredResult(d_code, d_reason, d_ssid,
                                                      sizeof(d_ssid));
        uint8_t d_wifi = 0, d_bt = 0, d_conn = 0;
        char d_info[33] = "";
        CommunicationProtocol::consumeWifiInfo(d_wifi, d_bt, d_conn, d_info,
                                                sizeof(d_info));
    }

    if (s_poll_timer == NULL) {
        s_poll_timer = lv_timer_create(poll_cb, kPollMs, NULL);
    }
    if (s_poll_timer) {
        lv_timer_resume(s_poll_timer);
    }

    panel_start_scan();
}

extern "C" void wifi_timers_pause(void) {
    if (s_poll_timer) {
        lv_timer_pause(s_poll_timer);
    }
    // Wyjscie w trakcie operacji: panel wraca do IDLE, wynik z ZEGARA
    // (jesli przyjdzie) zostanie zignorowany przy nastepnym wejsciu —
    // zadnych "duchow" poprzedniej proby.
    s_state = ST_IDLE;
    s_last_remaining_s = -1;
}

extern "C" const char *wifi_screen_get_selected_ssid(void) {
    return s_selected_ssid;
}
