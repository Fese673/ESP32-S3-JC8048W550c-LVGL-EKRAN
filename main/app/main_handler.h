#ifndef MAIN_HANDLER_H
#define MAIN_HANDLER_H

#ifdef __cplusplus
extern "C" {
#endif

void    main_ui_init(void);
void    main_ui_set_radio_mode(uint8_t mode);
bool    main_ui_brt_needs_save(void);   // checks + clears dirty flag; call from Core 0 idle only
uint8_t main_ui_get_brt_pct(void);     // returns current brightness 0-100

#ifdef __cplusplus
}
#endif

#endif // MAIN_HANDLER_H
