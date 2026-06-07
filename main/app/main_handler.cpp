#include "main_handler.h"
#include "screens.h"
#include "fonts.h"
#include "lvgl.h"
#include "eez-flow.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_system.h"

// Forward-declare only the HAL function we need — including the full header
// pulls in i2c_master_bus_handle_t without the required driver includes.
extern "C" void sunton_esp32s3_backlight_set_brightness(uint8_t brightness_0_255);

extern "C" void action_send_radio_mode_switch(void);

// ---------------------------------------------------------------------------
// Shortcut panel + child buttons
// ---------------------------------------------------------------------------
static lv_obj_t *s_shortcut_panel    = NULL;

// Slot 1: WiFi / BT toggle
static lv_obj_t *s_radio_btn         = NULL;
static lv_obj_t *s_radio_label       = NULL;
static uint8_t   s_current_mode      = 0; // 0=WIFI, 1=BT, 2=TRANSITIONING

// Slot 2: Brightness popup trigger
static lv_obj_t *s_brt_btn           = NULL;
static lv_obj_t *s_brt_btn_label     = NULL;

// Slot 3: BPM shortcut
static lv_obj_t *s_bpm_shortcut_btn  = NULL;
static lv_obj_t *s_bpm_label         = NULL;

// Slot 4: RESET button
static lv_obj_t *s_zeg_shortcut_btn  = NULL;
static lv_obj_t *s_zeg_label         = NULL;

// ---------------------------------------------------------------------------
// Brightness popup (created once at init, shown/hidden with HIDDEN flag)
// ---------------------------------------------------------------------------
static lv_obj_t   *s_brt_popup       = NULL;
static lv_obj_t   *s_brt_slider      = NULL;
static lv_obj_t   *s_brt_val_lbl     = NULL;

// Runtime state — all written/read exclusively from LVGL task (Core 1),
// except s_brt_nvs_dirty which is read by Core 0 idle loop.
static lv_timer_t        *s_brt_timer     = NULL;
static bool               s_brt_visible   = false;
static uint8_t            s_brt_pct       = 80;

// Volatile dirty flag: written on Core 1 (LVGL), read/cleared on Core 0 (idle).
// uint8_t / bool writes are atomic on Xtensa LX7 — volatile ensures visibility.
static volatile bool      s_brt_nvs_dirty = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static const char *modeToLabel(uint8_t mode)
{
    switch (mode) {
        case 0:  return "BLT";  // currently WIFI -> press to switch to BT
        case 1:  return "WIFI"; // currently BT   -> press to switch to WIFI
        default: return "---";
    }
}

// Minimum LEDC duty that visibly lights this backlight (hardware characteristic).
// Below ~duty 140 the LCD backlight LEDs do not produce visible light.
// We map slider range [10..100]% -> duty [BRT_DUTY_MIN..255] to use
// the full slider throw for the visible brightness range only.
#define BRT_DUTY_MIN 140u

// Apply brightness: clamp, remap to visible duty range, store, drive LEDC.
// Only called from LVGL task (Core 1). Thread-safe, no flash write -> zero tearing.
static void brt_apply(uint8_t pct)
{
    if (pct < 10)  pct = 10;
    if (pct > 100) pct = 100;
    s_brt_pct = pct;
    // Map [10..100] -> [BRT_DUTY_MIN..255] linearly.
    // At pct=10  -> duty = BRT_DUTY_MIN (minimum visible glow)
    // At pct=20  -> duty ~= 153  (≈ what 60% used to produce — per hw calibration)
    // At pct=100 -> duty = 255   (maximum brightness)
    uint32_t duty = BRT_DUTY_MIN + ((uint32_t)(pct - 10u) * (255u - BRT_DUTY_MIN)) / 90u;
    sunton_esp32s3_backlight_set_brightness((uint8_t)duty);
}

// Hide popup, pause timer, set dirty flag.  Zero NVS I/O here.
static void brt_popup_hide(void)
{
    lv_obj_add_flag(s_brt_popup, LV_OBJ_FLAG_HIDDEN);
    s_brt_visible      = false;
    lv_timer_pause(s_brt_timer);
    s_brt_nvs_dirty    = true; // Core 0 idle loop will pick this up and save to NVS
}

// ---------------------------------------------------------------------------
// LVGL callbacks
// ---------------------------------------------------------------------------

// Auto-hide timer fires after 5 s of inactivity on the slider
static void brt_timer_cb(lv_timer_t *t)
{
    (void)t;
    brt_popup_hide();
}

// Slider VALUE_CHANGED: apply brightness, update label, reset auto-hide timer
static void brt_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    int32_t   val    = lv_slider_get_value(slider);
    brt_apply((uint8_t)val);

    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (int)val);
    lv_label_set_text(s_brt_val_lbl, buf);

    // Every touch resets the 5s countdown from scratch
    lv_timer_reset(s_brt_timer);
    lv_timer_resume(s_brt_timer);
}

// BRT button: toggle popup open/closed
static void on_brt_btn_pressed(lv_event_t *e)
{
    lv_event_code_t code   = lv_event_get_code(e);
    lv_obj_t       *target = static_cast<lv_obj_t *>(lv_event_get_target(e));

    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x342c3d), LV_PART_MAIN);
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        if (s_brt_visible) {
            brt_popup_hide(); // toggle OFF — sets dirty flag for deferred NVS save
        } else {
            // Sync slider & label to actual current brightness before showing
            lv_slider_set_value(s_brt_slider, (int32_t)s_brt_pct, LV_ANIM_OFF);
            char buf[8];
            snprintf(buf, sizeof(buf), "%d%%", (int)s_brt_pct);
            lv_label_set_text(s_brt_val_lbl, buf);

            lv_obj_remove_flag(s_brt_popup, LV_OBJ_FLAG_HIDDEN);
            s_brt_visible = true;
            lv_timer_reset(s_brt_timer);
            lv_timer_resume(s_brt_timer);
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x1e1b22), LV_PART_MAIN);
    }
}

// Slot 1 — WiFi/BT mode toggle
static void on_radio_btn_pressed(lv_event_t *e)
{
    lv_event_code_t code   = lv_event_get_code(e);
    lv_obj_t       *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x342c3d), LV_PART_MAIN);
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        action_send_radio_mode_switch();
        if (s_radio_label) {
            lv_label_set_text_static(s_radio_label, "---");
        }
    } else if (code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x1e1b22), LV_PART_MAIN);
    }
}

// Slot 3 — BPM screen shortcut
static void on_bpm_shortcut_pressed(lv_event_t *e)
{
    lv_event_code_t code   = lv_event_get_code(e);
    lv_obj_t       *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x342c3d), LV_PART_MAIN);
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        eez_flow_set_screen(SCREEN_ID_BPM, LV_SCR_LOAD_ANIM_NONE, 0, 0);
    } else if (code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x1e1b22), LV_PART_MAIN);
    }
}

// Slot 4 — RST (hard reset) button
static void on_reset_btn_pressed(lv_event_t *e)
{
    lv_event_code_t code   = lv_event_get_code(e);
    lv_obj_t       *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (code == LV_EVENT_PRESSED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x342c3d), LV_PART_MAIN);
    } else if (code == LV_EVENT_RELEASED) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x1e1b22), LV_PART_MAIN);
        esp_restart(); // hard reset — immediate, no cleanup needed
    } else if (code == LV_EVENT_PRESS_LOST) {
        lv_obj_set_style_bg_color(target, lv_color_hex(0x1e1b22), LV_PART_MAIN);
    }
}

// ---------------------------------------------------------------------------
// Exported API
// ---------------------------------------------------------------------------

void main_ui_set_radio_mode(uint8_t mode)
{
    s_current_mode = (mode <= 2) ? mode : 0;
    if (s_radio_label) {
        lv_label_set_text_static(s_radio_label, modeToLabel(s_current_mode));
    }
}

// Called from Core 0 idle loop only.  Reads and atomically clears dirty flag.
bool main_ui_brt_needs_save(void)
{
    if (s_brt_nvs_dirty) {
        s_brt_nvs_dirty = false;
        return true;
    }
    return false;
}

// Called from Core 0 idle loop only after main_ui_brt_needs_save() returns true.
uint8_t main_ui_get_brt_pct(void)
{
    return s_brt_pct;
}

// ---------------------------------------------------------------------------
// Helper: create a shortcut tile button inside s_shortcut_panel
// Returns the created button object.
// ---------------------------------------------------------------------------
static lv_obj_t *make_slot_btn(int x, int y, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_obj_create(s_shortcut_panel);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 58, 38);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1e1b22), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x4a4750), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_PRESS_LOST, NULL);
    return btn;
}

// Helper: add centered label to a button
static lv_obj_t *make_btn_label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text_static(lbl, text);
    return lbl;
}

// ---------------------------------------------------------------------------
// Initialisation — runs once inside lvgl_port_lock at boot
// ---------------------------------------------------------------------------
void main_ui_init(void)
{
    if (!objects.main || s_shortcut_panel != NULL) {
        return;
    }

    // -------------------------------------------------------------------
    // 1. Read saved brightness from NVS (namespace "display", key "brightness")
    // -------------------------------------------------------------------
    {
        nvs_handle_t h;
        if (nvs_open("display", NVS_READONLY, &h) == ESP_OK) {
            uint8_t saved = 80;
            nvs_get_u8(h, "brightness", &saved);
            nvs_close(h);
            if (saved < 10)  saved = 10;
            if (saved > 100) saved = 100;
            s_brt_pct = saved;
        }
        // Apply immediately (before any frame is drawn)
        sunton_esp32s3_backlight_set_brightness((uint8_t)((s_brt_pct * 255u) / 100u));
    }

    // -------------------------------------------------------------------
    // 2. Master shortcut panel  (x=18, y=18, 140×100, 30% opacity)
    // -------------------------------------------------------------------
    s_shortcut_panel = lv_obj_create(objects.main);
    lv_obj_set_pos(s_shortcut_panel, 18, 18);
    lv_obj_set_size(s_shortcut_panel, 140, 100);
    lv_obj_remove_flag(s_shortcut_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_shortcut_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_shortcut_panel, lv_color_hex(0x18161b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_shortcut_panel, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_shortcut_panel, lv_color_hex(0x7a7580), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_shortcut_panel, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_shortcut_panel, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_radius(s_shortcut_panel, 12, LV_PART_MAIN);

    // Slot 1 (Top-Left): WiFi/BT toggle
    s_radio_btn   = make_slot_btn(8,  8,  on_radio_btn_pressed);
    s_radio_label = make_btn_label(s_radio_btn, modeToLabel(s_current_mode));

    // Slot 2 (Top-Right): Brightness popup trigger
    s_brt_btn       = make_slot_btn(74, 8,  on_brt_btn_pressed);
    s_brt_btn_label = make_btn_label(s_brt_btn, "BRT");

    // Slot 3 (Bottom-Left): BPM screen shortcut
    s_bpm_shortcut_btn = make_slot_btn(8,  54, on_bpm_shortcut_pressed);
    s_bpm_label        = make_btn_label(s_bpm_shortcut_btn, "BPM");

    // Slot 4 (Bottom-Right): RST — hard reset button
    s_zeg_shortcut_btn = make_slot_btn(74, 54, on_reset_btn_pressed);
    s_zeg_label        = make_btn_label(s_zeg_shortcut_btn, "RST");

    // -------------------------------------------------------------------
    // 3. Brightness popup
    //    Positioned at x = 18+140+8 = 166, y = 18 (aligned with panel top)
    //    w = 54, h = 166
    //    Parent: objects.main (NOT s_shortcut_panel) to allow overflow
    // -------------------------------------------------------------------
    s_brt_popup = lv_obj_create(objects.main);
    lv_obj_set_pos(s_brt_popup, 166, 18);
    lv_obj_set_size(s_brt_popup, 54, 166);
    lv_obj_remove_flag(s_brt_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_brt_popup, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_brt_popup, lv_color_hex(0x18161b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_brt_popup, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_brt_popup, lv_color_hex(0x4a4750), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_brt_popup, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_brt_popup, 12, LV_PART_MAIN);
    // Hidden by default — shown only when BRT button is pressed
    lv_obj_add_flag(s_brt_popup, LV_OBJ_FLAG_HIDDEN);

    // Value label: "XX%" at the top
    s_brt_val_lbl = lv_label_create(s_brt_popup);
    lv_obj_align(s_brt_val_lbl, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(s_brt_val_lbl, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_brt_val_lbl, &lv_font_montserrat_14, LV_PART_MAIN);
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", (int)s_brt_pct);
        lv_label_set_text(s_brt_val_lbl, buf);
    }

    // Vertical slider: pos(21, 32), size(12, 110)  — range 10..100
    s_brt_slider = lv_slider_create(s_brt_popup);
    lv_obj_set_pos(s_brt_slider, 21, 32);
    lv_obj_set_size(s_brt_slider, 12, 110);
    lv_slider_set_range(s_brt_slider, 10, 100);
    lv_slider_set_value(s_brt_slider, (int32_t)s_brt_pct, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_brt_slider, brt_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Premium slider styling — identical palette to music EQ sliders
    lv_obj_set_style_bg_color(s_brt_slider, lv_color_hex(0x28252c), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_brt_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_brt_slider, lv_color_hex(0x85808a), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_brt_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_brt_slider, lv_color_hex(0xffffff), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_brt_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(s_brt_slider, lv_color_hex(0x7a7580), LV_PART_KNOB);
    lv_obj_set_style_border_width(s_brt_slider, 1, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_brt_slider, -1, LV_PART_KNOB);

    // Bottom label intentionally removed — not needed visually.

    // -------------------------------------------------------------------
    // 4. Auto-hide timer — 5000 ms, created PAUSED.
    //    Resumed when popup opens; paused when popup closes.
    // -------------------------------------------------------------------
    s_brt_timer = lv_timer_create(brt_timer_cb, 5000, NULL);
    lv_timer_pause(s_brt_timer);

    // -------------------------------------------------------------------
    // 5. Wire the Sensors Wikipedia menu button programmatically
    //    We find the sensors button by getting the first child of the parent
    //    of Shortcuts button (objects.obj4) and binding it to SCREEN_ID_CZUJNIKI.
    // -------------------------------------------------------------------
    if (objects.obj4) {
        lv_obj_t *parent_cnt = lv_obj_get_parent(objects.obj4);
        if (parent_cnt && lv_obj_get_child_cnt(parent_cnt) > 0) {
            lv_obj_t *czujniki_btn = lv_obj_get_child(parent_cnt, 0);
            if (czujniki_btn) {
                // Clear any EEZ callbacks just in case
                uint32_t ev_cnt = lv_obj_get_event_count(czujniki_btn);
                for (uint32_t j = ev_cnt; j > 0; j--) {
                    lv_obj_remove_event(czujniki_btn, j - 1);
                }
                
                // Add click callback to open SCREEN_ID_CZUJNIKI
                lv_obj_add_event_cb(czujniki_btn, [](lv_event_t *e) {
                    lv_event_code_t code = lv_event_get_code(e);
                    if (code == LV_EVENT_RELEASED) {
                        eez_flow_set_screen(SCREEN_ID_CZUJNIKI, LV_SCR_LOAD_ANIM_NONE, 0, 0);
                    }
                }, LV_EVENT_ALL, NULL);
            }
        }
    }
}
