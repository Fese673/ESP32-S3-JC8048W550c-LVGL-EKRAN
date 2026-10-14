// Wewnetrzny kontrakt miedzy screen_geometry_ui.cpp (widgety) a
// screen_geometry_logic.cpp (stan + timer). Nie uzywac poza folderem.

#ifndef SCREEN_GEOMETRY_INTERNAL_H
#define SCREEN_GEOMETRY_INTERNAL_H

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GEOM_ST_READY = 0,
    GEOM_ST_DRIVE_L,
    GEOM_ST_DRIVE_R,
    GEOM_ST_FLASH, // chwilowy komunikat (wybor / kalibracja / pozycja bazowa)
} geom_status_t;

// Flagi ramki 0x3B (lustro Esptogution.h: kStepperFlagCalOk / kStepperFlagRadarEn
// / kStepperFlagOnOff). Bit4 = "plynne wylaczenie" (dim 7-seg w STM32).
#define GEOM_FLAG_CAL_OK 0x01
#define GEOM_FLAG_RADAR_EN 0x02
#define GEOM_FLAG_ONOFF 0x10

// Automat LD2410C (OUT -> wybrany kat, cisza -> 0°) zyje w STM32
// (stepper_link.c: czas z truth — presety 30s/3m/10m/45m). Tu tylko HMI.

// --- logika -> UI ---
void        geom_ui_build(lv_obj_t *screen);
void        geom_ui_restyle(void); // reaplikacja stylu slidera (auto-styl main.cpp)
void        geom_ui_sync(void);    // odswiezenie labeli z getterow (tylko LVGL task)

// --- UI -> logika (wywolywane z eventow LVGL, nieblokuja) ---
void        geom_logic_hold_start(int dir); // dir: -1 dol, +1 gora
void        geom_logic_hold_stop(void);
void        geom_logic_stop_press(void); // przycisk STOP (natychmiast)
void        geom_logic_set_speed(int steps_per_s); // tylko RAM (drag suwaka)
void        geom_logic_speed_commit(void); // RELEASED/preset: SET_SPEED do STM32

// --- gettery stanu (UI tylko czyta) ---
int32_t     geom_logic_pos(void);
int         geom_logic_speed(void);
int         geom_logic_hold_dir(void);
geom_status_t geom_logic_status(void);
const char *geom_logic_status_text(void); // <64 B, bufor statyczny logiki

// --- presety kata (strojenie w kodzie) ---
int         geom_logic_preset_count(void);
int         geom_logic_preset_deg_at(int idx);

// --- wybor celu + WYKONAJ ---
// Preset wybiera kat docelowy. WYKONAJ wysyla go do ZEGAR ESP32, ktory
// zapisuje ustawienie i sam wykonuje jazde.
int         geom_logic_selected_preset(void);
// Tap presetu: single = wybor celu, double (2x ten sam w 650 ms) = pozycja
// bazowa STM32 po resecie (SET_HOME, zielony w UI).
void        geom_logic_preset_tap(int deg);
int         geom_logic_home_preset(void); // pozycja bazowa po resecie (default 0)
void        geom_logic_execute_selected(void);

// --- kalibracja reczna 0 -> 90 (ten sam przycisk, 3 stany: start/0/90) ---
void        geom_logic_cal_next(void); // kontekstowy krok: start / zatwierdz 0 / zatwierdz 90
const char *geom_logic_cal_btn_text(void); // "KALIBRUJ" / "ZATWIERDŹ 0°" / "ZATWIERDŹ 90°"
const char *geom_logic_cal_hint(void);     // instrukcja dla operatora (<64 B)
int         geom_logic_cal_saved_pulse(void); // 1 przez 1,5 s po potwierdzeniu zapisu

// --- automat LD2410C (przycisk na ekranie, flaga we flash STM32) ---
void        geom_logic_radar_toggle(void); // klik: optymistyczny flip + SET_RADAR
int         geom_logic_radar_en(void);     // 1 = automat wlaczony (adopcja z 0x3B)
int         geom_logic_radar_out(void);    // 1 = OUT HIGH (radar widzi kogos, live)

// --- czas auto-zjazdu radaru (panel opcji na ekranie RADAR) ---
// Presety w sekundach: 30/180/600/2700. Zapis + walidacja we flash STM32,
// echo 0x3B adoptowane z ochrona pending (tylko uzytkownik zmienia).
void        geom_logic_auto_close_set(uint16_t seconds);
int         geom_logic_auto_close_sec(void); // aktywna wartosc w sekundach

// --- "plynne wylaczenie" (przycisk na ekranie RADAR) ---
// Bramka premium dimu 7-seg w STM32 (bit 3 flags truth, echo 0x3B bit4).
// Klik: optymistyczny flip + SET_ONOFF + pending (potwierdzenie z echa).
void        geom_logic_onoff_toggle(void);
int         geom_logic_onoff_en(void); // 1 = wlaczone (adopcja z 0x3B)

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_GEOMETRY_INTERNAL_H */
