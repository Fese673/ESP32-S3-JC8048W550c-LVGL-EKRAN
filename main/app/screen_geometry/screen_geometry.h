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

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_GEOMETRY_H */
