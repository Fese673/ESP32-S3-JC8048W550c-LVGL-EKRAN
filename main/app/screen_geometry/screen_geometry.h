#ifndef SCREEN_GEOMETRY_H
#define SCREEN_GEOMETRY_H

#ifdef __cplusplus
extern "C" {
#endif

// Tworzy panele na objects.geometria_ekranu i startuje 100 ms timer.
// Wywolanie raz, spod lvgl_port_lock (jak music_ui_handler_init).
// Calosc zyje w tasku LVGL — zero blokowania, zero alokacji w ticku.
void screen_geometry_init(void);

// Stan z ZEGAR ESP32 (ramka 0x3B, przez CommUiMapper::applyStepperStateToUi).
// Wywolywane z updateUi() = task LVGL, wiec bezposrednio (jak music_ui_*).
// ZEGAR jest wlascicielem POS/busy/celu/predkosci/pozycji bazowej.
// speed = kroki/s z NVS ZEGAR-a (0xFFFF = brak w ramce, nie adoptuj).
// home_deg = pozycja bazowa STM32 po resecie (0xFFFF = brak, nie adoptuj).
// auto_close_s = czas auto-zjazdu radaru w sekundach (0xFFFF = brak, nie adoptuj).
void screen_geometry_set_remote_state(bool busy, int32_t pos_steps,
                                      uint16_t target_deg, uint8_t flags,
                                      uint16_t speed, uint16_t home_deg,
                                      uint16_t auto_close_s);

// OUT radaru LD2410C (bit b3 ramki 0x39, przez CommUiMapper::applyRadarToUi).
// Push z updateUi() = task LVGL. Automat w geom_timer_cb.
void screen_geometry_set_radar_out(bool out);

// Panel opcji radaru (ekran RADAR): automat WL/WYL + czas auto-zjazdu.
// Ten sam stan co przycisk "RADAR MIKROFALOWY" na Geometrii (mirror 0x3B,
// komendy 0x3A SET_RADAR / SET_AUTOCLOSE, walidacja + flash w STM32).
void screen_geometry_radar_toggle(void);
int  screen_geometry_radar_enabled(void);
void screen_geometry_auto_close_set(uint16_t seconds);
int  screen_geometry_auto_close_sec(void);

// "Plynne wylaczenie" (przycisk na ekranie RADAR): bramka premium dimu
// 7-seg przy zjezdzie (bit3 flags truth, echo 0x3B bit4). Ruch bez zmian.
void screen_geometry_onoff_toggle(void);
int  screen_geometry_onoff_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_GEOMETRY_H */
