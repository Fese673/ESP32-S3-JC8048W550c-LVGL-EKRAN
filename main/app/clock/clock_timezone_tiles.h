#ifndef CLOCK_TIMEZONE_TILES_H
#define CLOCK_TIMEZONE_TILES_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void clock_timezone_tiles_init(void);

void clock_timezone_tiles_update(bool valid,
                                  int h_pl, int m_pl,
                                  int h_usa, int m_usa,
                                  int h_jp, int m_jp,
                                  int h_ru, int m_ru);

#ifdef __cplusplus
}
#endif

#endif
