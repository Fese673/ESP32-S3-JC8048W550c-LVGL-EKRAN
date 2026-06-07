#ifndef CLOCK_LOGIC_H
#define CLOCK_LOGIC_H

#ifdef __cplusplus
extern "C" {
#endif

// Uruchamia zadanie zegara (Core 0, tylko śledzenie czasu)
void clock_logic_start();

// Aktualizuje EEZ Flow global variables + world clocks
// Wołane z LVGL task (Core 1) — bezpieczne, nieblokujące
void clock_ui_update(void);

#ifdef __cplusplus
}
#endif

#endif // CLOCK_LOGIC_H
