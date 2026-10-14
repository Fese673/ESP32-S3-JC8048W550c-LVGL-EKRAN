// Cache stanu radaru + timer 100 ms ekranu. Calosc w tasku LVGL,
// zero blokowania, zero alokacji — tylko kopia 26 B pod mutexem
// zdjetym juz w consumeRadarState.
#include "app/radar/radar.h"
#include "app/radar/radar_internal.h"
#include "communication/CommunicationProtocol.h"
#include "screens.h"
#include "eez-flow.h"
#include "lvgl.h"
#include <string.h>

namespace {

struct RadarCache {
    uint8_t presence = 0x80;  // LINK-BAD do pierwszej ramki
    uint16_t mov_dist = 0;
    uint8_t mov_energy = 0;
    uint16_t stat_dist = 0;
    uint8_t stat_energy = 0;
    uint16_t detect_dist = 0;
    uint8_t mov_gates[9] = {0};
    uint8_t stat_gates[9] = {0};
    uint8_t sens = 0;  // czulosc OUT: 0=SREDNIA, 1=WYSOKA, 2=MALA
};

RadarCache s_cache;

bool s_built = false;
bool s_was_on_screen = false;

// Ochrona pending (jak home/target w geometrii): po kliknieciu przycisku
// przyjmujemy TYLKO potwierdzenie z STM32 — stare echo w locie nie cofa
// swiezego wyboru. 0xFF = brak oczekiwania; timeout 3 s.
uint8_t s_sens_pending = 0xFF;
uint32_t s_sens_pending_ms = 0;
uint8_t s_sens_last_remote = 0xFF; // ostatnia POTWIERDZONA czulosc (0xFF = brak)

bool radar_on_screen(void) {
    return eez_flow_get_current_screen() == SCREEN_ID_RADAR
        && objects.radar != NULL;
}

void radar_timer_cb(lv_timer_t *t) {
    (void)t;

    // Fallback gdyby init byl przed create_screens (defensywnie).
    if (!s_built && objects.radar != NULL) {
        radar_ui_build(objects.radar);
        s_built = true;
    }

    const bool on = radar_on_screen();
    if (on && !s_was_on_screen) {
        s_was_on_screen = true;
        // Swiezy stan steppera na wejscie (czulosc OUT z flag 0x3B;
        // push przychodzi tez po HELLO, poll = samonaprawa).
        CommunicationProtocol::requestStepperState();
    }
    if (!on && s_was_on_screen) s_was_on_screen = false;
    if (!on || !s_built) return;

    // Timeout ochrony pending: brak potwierdzenia z STM32 przez 3 s
    // (komenda prawdopodobnie zgubiona). Rollback do ostatniej POTWIERDZONEJ
    // czulosci (bez tego highlight klamal) + dopytaj o stan.
    if (s_sens_pending != 0xFF &&
        (uint32_t)(lv_tick_get() - s_sens_pending_ms) >= 3000U) {
        s_sens_pending = 0xFF;
        if (s_sens_last_remote <= 2U) {
            s_cache.sens = s_sens_last_remote;
        }
        CommunicationProtocol::requestStepperState();
    }

    // Push ze strony ZEGAR-a (250 ms) — ekran tylko odswieza cache.
    radar_ui_sync(s_cache.presence, s_cache.mov_dist, s_cache.mov_energy,
                  s_cache.stat_dist, s_cache.stat_energy, s_cache.detect_dist,
                  s_cache.mov_gates, s_cache.stat_gates, s_cache.sens);
}

}  // namespace

void radar_init(void) {
    memset(&s_cache, 0, sizeof(s_cache));
    s_cache.presence = 0x80;
    lv_timer_create(radar_timer_cb, 100, NULL);
}

void radar_set_remote_state(uint8_t presence, uint16_t mov_dist, uint8_t mov_energy,
                            uint16_t stat_dist, uint8_t stat_energy, uint16_t detect_dist,
                            const uint8_t *mov_gates, const uint8_t *stat_gates) {
    s_cache.presence = presence;
    s_cache.mov_dist = mov_dist;
    s_cache.mov_energy = mov_energy;
    s_cache.stat_dist = stat_dist;
    s_cache.stat_energy = stat_energy;
    s_cache.detect_dist = detect_dist;
    if (mov_gates != nullptr) memcpy(s_cache.mov_gates, mov_gates, 9);
    if (stat_gates != nullptr) memcpy(s_cache.stat_gates, stat_gates, 9);
}

void radar_set_remote_sens(uint8_t code) {
    if (code > 2U) return;
    if (s_sens_pending != 0xFF) {
        // Czekamy na potwierdzenie wyboru — stare echa ignorujemy.
        if (code == s_sens_pending) {
            s_sens_pending = 0xFF;
            s_cache.sens = code;
            s_sens_last_remote = code;
        }
        return;
    }
    s_cache.sens = code;
    s_sens_last_remote = code;
}

void radar_logic_set_sens(uint8_t code) {
    if (code > 2U) return;
    s_cache.sens = code;  // optymistycznie (highlight od razu)
    s_sens_pending = code;
    s_sens_pending_ms = lv_tick_get();
    CommunicationProtocol::sendStepperSetSens(code);
    CommunicationProtocol::requestStepperState(); // szybkie potwierdzenie
}

uint8_t radar_logic_sens(void) {
    return s_cache.sens;
}
