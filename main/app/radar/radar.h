#ifndef RADAR_H
#define RADAR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ETAP 3: tylko cache stanu (bez UI). ETAP 4 dobuduje ekran + timer.
// Wywolanie raz, spod lvgl_port_lock (jak screen_geometry_init).
void radar_init(void);

// Stan z ZEGAR ESP32 (ramka 0x39, przez CommUiMapper::applyRadarToUi).
// Wywolywane z updateUi() = task LVGL, wiec bezposrednio (jak music_ui_*).
// presence: b0=ktos, b1=ruch, b2=stan, b7=LINK-BAD, b6=GARBAGE.
void radar_set_remote_state(uint8_t presence, uint16_t mov_dist, uint8_t mov_energy,
                            uint16_t stat_dist, uint8_t stat_energy, uint16_t detect_dist,
                            const uint8_t *mov_gates, const uint8_t *stat_gates);

// Czulosc OUT radaru z ramki 0x3B (bity 2-3 flags, przez CommUiMapper).
// 0=SREDNIA, 1=WYSOKA, 2=MALA. Adopcja z ochrona pending — stare echa
// nie cofaja swiezego wyboru uzytkownika. Wartosci >2 ignorowane.
void radar_set_remote_sens(uint8_t code);

// Klik przycisku WYSOKA/SREDNIA/MALA (event LVGL): optymistyczny wybor +
// SET_RADAR_SENS do STM32 (persist we flash, echo wraca 0x3B).
void radar_logic_set_sens(uint8_t code);

// Aktywny preset czulosci (0/1/2) — UI tylko czyta (highlight).
uint8_t radar_logic_sens(void);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_H */
