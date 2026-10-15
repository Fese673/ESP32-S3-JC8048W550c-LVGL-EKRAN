#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * zegar_mode_ui — dwa okrągłe przyciski na ekranie ZEGAR (prawy górny róg
 * kart STOPER i MINUTNIK). Wybierają, co pokazuje FIZYCZNY 7-seg ZEGARA:
 *   0 = zegar (oba zgaszone), 1 = stoper, 2 = minutnik.
 *
 * Zasady (spójność Guition <-> ZEGAR):
 *  - ZEGAR jest właścicielem trybu; klik wysyła 0x17, a wygląd przycisków
 *    odzwierciedla echo 0x18. Klik jest optymistyczny (przygaszony) do czasu
 *    potwierdzenia, ale po 2 s bez echa wraca do prawdy — nigdy nie "kłamie"
 *    na stałe.
 *  - Klik zapalonego kołka = wyłączenie (oba zgaszone -> 7-seg wraca do zegara).
 *  - Klik ma 2 s okno potwierdzenia; brak echa = rollback do potwierdzonego stanu.
 *  - Po restarcie ZEGARA tryb = zegar (bez persistencji) — echo z HELLO gasi kołka.
 *  - Kolka są tworzone raz (ekran persistentny), styl: puste = biała obramówka,
 *    zapalone = biały wypełnienie; w czasie potwierdzania nieco przygaszone.
 */

/* Wejście na ekran ZEGAR: utwórz kołka (raz) i poproś o świeży tryb. */
void zegar_mode_ui_init(void);

/* Tick 100 ms (z LVGL): echo z ZEGARA + okno potwierdzenia. */
void zegar_mode_ui_tick(void);

#ifdef __cplusplus
}
#endif
