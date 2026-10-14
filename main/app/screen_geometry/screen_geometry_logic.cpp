// Panel GEOMETRIA EKRANU — logika (stan + timer).
//
// ZRODLO DANYCH: ZEGAR ESP32 (ramka 0x3B). Guition nie jezdzi silnikiem
// sam — wysyla komendy 0x3A, ZEGAR zapisuje ustawienia i sam wykonuje jazde.
//
// ARCHITEKTURA:
//  - Wysylane komendy: EXECUTE / HOLD / STOP / STATUS_REQ / CAL_ZERO /
//    CAL_SPAN / SET_HOME / SET_TARGET / SET_SPEED / SET_RADAR / CAL_MODE /
//    SET_AUTOCLOSE / SET_SENS / SET_ONOFF. Wszystkie nieblokujace.
//  - STOP idempotentny: wysylany ZAWSZE (bez warunkow) + powtarzany co
//    200 ms az busy=0 (max 1 s) — polisa na zgubiona ramke.
//  - EXECUTE wysylany RAZ (powtorka = restart ruchu — debounce 500 ms).
//  - SET_SPEED tylko na commit (RELEASED suwaka / klik presetu).
//  - STATUS_REQ na wejscie na ekran + co 2 s (zywy POS w trakcie jazdy).

#include "screen_geometry.h"
#include "screen_geometry_internal.h"
#include "screens.h"
#include "eez-flow.h"
#include "lvgl.h"
#include "communication/CommunicationProtocol.h"
#include <stdio.h>

#define GEOM_TICK_MS 100
#define GEOM_SPEED_MIN 100
#define GEOM_SPEED_MAX 1000
#define GEOM_FLASH_MS 2500
#define GEOM_POLL_MS 2000
#define GEOM_STOP_REPEAT_MS 1000
#define GEOM_STOP_REPEAT_GAP 200
#define GEOM_CAL_PULSE_MS 1500 // puls przycisku KALIBRUJ po potwierdzeniu zapisu
// Lancuchowanie HOLD: kolejny chunk dopiero gdy poprzedni dojechal (stan
// idle) i min. 400 ms od poprzedniej wysylki. Chroni przed zalaniem FIFO
// (depth 4) i samonaprawia zgubiony chunk.
#define GEOM_HOLD_CHAIN_GAP 400
// Kalibracja: trzymaj automat radaru STM32 wstrzymany przez CAL_MODE.
// Keepalive co 5 s (STM32 timeout 30 s), gaszenie po zapisie/wyjsciu.
#define GEOM_CAL_HOLD_GAP 5000
// Debounce krokow kalibracji (CLICKED bywa 2x z jednego tapniecia).
#define GEOM_CAL_STEP_GAP_MS 500
// Debounce WYKONAJ: drugie EXECUTE w tym oknie = restart ruchu (zakaz).
#define GEOM_EXEC_GAP_MS 500

// Presety kata — strojenie w kodzie. 0° i 75° na zyczenie operatora.
// Klik = wybor celu (JEDŹ DO), DWUKLIK = pozycja bazowa po resecie (zielony).
static const int kAnglePresets[] = { 0, 15, 30, 45, 75, 90 };
#define GEOM_DBLCLICK_MS 650

typedef enum { CAL_IDLE = 0, CAL_WAIT_ZERO = 1, CAL_WAIT_90 = 2 } cal_state_t;

static lv_timer_t *s_timer = NULL;
static bool s_was_on_screen = false;
static bool s_built = false;

// Stan lokalny UI: predkosc, wybor celu, hold, kalibracja-kroki.
// ZASADA: Guition NIE pisze do NVS (zero blokowania flashem). Wlascicielem
// ustawien jest ZEGAR — predkosc i cel zyja w jego NVS i wracaja ramka 0x3B
// (adopcja przy zmianie). Tu tylko RAM + echo do ramek 0x3A.
static int s_speed = 400;
static int s_hold_dir = 0; // -1 = dol, 0 = stop, +1 = gora
static int s_selected_deg = 45; // wybrany preset (cel dla WYKONAJ)
static cal_state_t s_cal = CAL_IDLE;
static uint32_t s_lastCalHoldMs = 0; // keepalive CAL_MODE (automat STM32 off)
static uint32_t s_calStepMs = 0;     // debounce krokow kalibracji
static bool s_calIdleSeen = false;   // swieze busy=0 po STOPie sesji kalibracji
static uint32_t s_lastExecMs = 0;    // debounce WYKONAJ (dwuklik = restart — zakaz)

// Stan z ZEGAR ESP32 (ramka 0x3B).
// s_remoteKnown: przyszla juz pierwsza ramka — strzega flasha SKALIBRACJA
// ZAPISANA, zeby nie odpalic przy starcie (calOk juz ustawione we flashu).
static bool s_remoteKnown = false;
static bool s_rBusy = false;
static int32_t s_rPos = 0;
static uint16_t s_rTarget = 45;
static bool s_rCalOk = false;
static uint32_t s_lastPollMs = 0;
// Ostatni target widziany w ramce 0x3B. Adopcja cel<-stan TYLKO przy zmianie
// po stronie ZEGAR-a (echo WYKONAJ / cel z NVS po reboocie). Bez tego poll co
// 2 s nadpisywalby swiezy wybor operatora starym targetem (wyscig JEDŹ DO).
static uint16_t s_lastStateTarget = 0xFFFF;
// To samo dla predkosci: adopcja speed<-stan TYLKO przy zmianie (NVS ZEGAR-a
// po reboocie). 0xFFFF w ramce = brak pola (nie adoptuj).
static uint16_t s_lastStateSpeed = 0xFFFF;
// Pozycja bazowa STM32 po resecie (double-click presetu, zielony w UI).
// Default 0 = klasyczne zero kalibracji. Adopcja z 0x3B przy zmianie.
static int s_home_deg = 0;
static uint16_t s_lastStateHome = 0xFFFF;
// Ochrona przed starym echo-stanem: po double-clicku czekamy na POTWIERDZENIE
// nowej wartosci z STM32 (REF echo). Dopoki pending — inne wartosci ignorowane
// (inaczej opozniony push ze stara wartoscia mrugal zielonym do 0).
static uint16_t s_home_pending = 0xFFFF;
static uint32_t s_home_pending_ms = 0;
static uint16_t s_target_pending = 0xFFFF;   // to samo dla wyboru celu (highlight)
static uint32_t s_target_pending_ms = 0;
// Auto-zjazd radaru (sekundy; presety 30/180/600/2700 jak w STM32).
// Default 30 = fabryczny — od startu jeden przycisk podswietlony.
static int s_auto_close_s = 30;
static uint16_t s_lastStateAutoClose = 0xFFFF;
static uint16_t s_aclose_pending = 0xFFFF;   // ochrona jak home/target
static uint32_t s_aclose_pending_ms = 0;
// Automat LD2410C (przycisk na ekranie, flaga we flash STM32).
// Adopcja z bitu flag 0x3B przy zmianie (jak speed/home). Default OFF.
static bool s_radar_en = false;
static bool s_lastStateRadarEn = false;
static bool s_radar_first_frame = true;
// Pending radaru: po kliknieciu przyjmujemy TYLKO potwierdzenie z STM32
// (bez tego optymistyczny flip zostawal na zawsze, gdy komenda zginela —
// napis "WYLACZONY" klamal, a automat w STM32 chodzil dalej).
static uint16_t s_radar_pending = 0xFFFF;   // 0/1 = czekamy; 0xFFFF = brak
static uint32_t s_radar_pending_ms = 0;

// "Plynne wylaczenie" (przycisk na ekranie RADAR): bramka dimu 7-seg
// w STM32. Adopcja z bit4 flag 0x3B + pending jak radar/home/auto-close.
static bool s_onoff_en = false;
static bool s_lastStateOnOff = false;
static bool s_onoff_first_frame = true;
static uint16_t s_onoff_pending = 0xFFFF;
static uint32_t s_onoff_pending_ms = 0;
// OUT radaru (push z 0x39 przez applyRadarToUi) — WYLACZNIE HMI (ramka
// przycisku zielona/czerwona). Automat ruchu zyje w STM32 (E1).
static bool s_radar_out = false;
// Double-click presetu: ostatni klik + czas.
static int s_lastTapDeg = -1;
static uint32_t s_lastTapMs = 0;
// Znacznik potwierdzenia zapisu kalibracji (flaga calOk 0->1) — puls przycisku.
static uint32_t s_calSavedAt = 0;

// Powtarzanie STOP (polisa na zgubiona ramke).
static bool s_stopRepeat = false;
static uint32_t s_stopRepeatUntil = 0;
static uint32_t s_stopRepeatLastTx = 0;

// Znacznik ostatniej wysylki HOLD (start + lancuch) — odstep miedzy chunkami.
static uint32_t s_lastHoldTx = 0;

static char s_flash[48] = "";
static uint32_t s_flash_t = 0;
static char s_driveBuf[32] = "";

static bool geom_on_screen(void)
{
    return eez_flow_get_current_screen() == SCREEN_ID_GEOMETRIA_EKRANU
        && objects.geometria_ekranu != NULL;
}

static void geom_flash(const char *msg)
{
    snprintf(s_flash, sizeof(s_flash), "%s", msg ? msg : "");
    s_flash_t = lv_tick_get();
}

static void geom_send_hold(int dir)
{
    s_lastHoldTx = lv_tick_get();
    CommunicationProtocol::sendStepperHold((int8_t)dir, (uint16_t)s_speed);
}

static void geom_send_stop(void)
{
    CommunicationProtocol::sendStepperStop();
    s_stopRepeat = true;
    const uint32_t now = lv_tick_get();
    s_stopRepeatUntil = now + GEOM_STOP_REPEAT_MS;
    s_stopRepeatLastTx = now;
}

static void geom_all_stop(void)
{
    const bool was_cal = (s_cal != CAL_IDLE);
    s_hold_dir = 0;
    s_cal = CAL_IDLE;
    s_stopRepeat = false;
    // Przerwana kalibracja: zdejmij wstrzymanie automatu radaru w STM32.
    if (was_cal) {
        CommunicationProtocol::sendStepperCalMode(false);
    }
}

static void geom_timer_cb(lv_timer_t *t)
{
    (void)t;

    // Fallback gdyby init byl przed create_screens (defensywnie).
    if (!s_built && objects.geometria_ekranu != NULL) {
        geom_ui_build(objects.geometria_ekranu);
        s_built = true;
    }

    bool on = geom_on_screen();

    if (on && !s_was_on_screen) {
        s_was_on_screen = true;
        geom_ui_restyle(); // main.cpp nadpisuje slidery przy wejsciu na ekran
        CommunicationProtocol::requestStepperState(); // pelny stan na wejscie
        s_lastPollMs = lv_tick_get();
    }
    if (!on && s_was_on_screen) {
        // SAFETY: wyjscie z ekranu kasuje ruch reczny. Wyslij STOP na droge
        // (palec mogl zejsc z przycisku juz poza ekranem) takze wtedy, gdy
        // trwa polisa repeat STOP (busy jeszcze nie potwierdzone).
        // Uwaga: NIE STOPujemy automatu radaru (ZEGAR jedzie dalej).
        s_was_on_screen = false;
        if (s_hold_dir != 0 || s_stopRepeat) {
            CommunicationProtocol::sendStepperStop();
        }
        geom_all_stop();
    }
    if (!s_built) {
        return;
    }

    const uint32_t now = lv_tick_get();

    if (on) {
        // Poll stanu co 2 s na aktywnym ekranie (zywy POS + samonaprawa UI).
        if ((uint32_t)(now - s_lastPollMs) >= GEOM_POLL_MS) {
            s_lastPollMs = now;
            CommunicationProtocol::requestStepperState();
        }
        // Kalibracja w toku: odswiezaj CAL_MODE — automat radaru ma byc
        // wstrzymany w STM32 przez cala sesje (ruchy nie przeszkadzaja
        // w ustawianiu 0/90). Keepalive < timeout STM32 (30 s).
        if (s_cal != CAL_IDLE &&
            (uint32_t)(now - s_lastCalHoldMs) >= GEOM_CAL_HOLD_GAP) {
            s_lastCalHoldMs = now;
            CommunicationProtocol::sendStepperCalMode(true);
        }
    }

    // Ochrona pending: brak potwierdzenia z STM32 przez 3 s = komenda
    // prawdopodobnie zgubiona. Puszczamy ogrodzenie ORAZ wracamy do ostatniej
    // prawdy z 0x3B — bez rollbacku optymistyczny wybor zostawal na zawsze
    // (highlight klamal, gdy prawda sie nie zmienila).
    if (s_home_pending != 0xFFFF &&
        (uint32_t)(now - s_home_pending_ms) >= 3000U) {
        s_home_pending = 0xFFFF;
        if (s_lastStateHome != 0xFFFF && s_lastStateHome <= 360U) {
            s_home_deg = (int)s_lastStateHome;
        }
        CommunicationProtocol::requestStepperState();
        geom_flash("STATUS: POZ. BAZOWA BEZ POTWIERDZENIA");
    }
    if (s_target_pending != 0xFFFF &&
        (uint32_t)(now - s_target_pending_ms) >= 3000U) {
        s_target_pending = 0xFFFF;
        if (s_lastStateTarget != 0xFFFF && s_lastStateTarget <= 360U) {
            s_selected_deg = (int)s_lastStateTarget;
        }
        CommunicationProtocol::requestStepperState();
        geom_flash("STATUS: CEL BEZ POTWIERDZENIA");
    }
    if (s_aclose_pending != 0xFFFF &&
        (uint32_t)(now - s_aclose_pending_ms) >= 3000U) {
        s_aclose_pending = 0xFFFF;
        if (s_lastStateAutoClose != 0xFFFF) {
            s_auto_close_s = (int)s_lastStateAutoClose;
        }
        CommunicationProtocol::requestStepperState();
        geom_flash("STATUS: AUTO-ZJAZD BEZ POTWIERDZENIA");
    }
    // Brak potwierdzenia radaru: wroc do ostatniej prawdy z 0x3B i dopytaj
    // (UI nie moze klamac stanem optymistycznym).
    if (s_radar_pending != 0xFFFF &&
        (uint32_t)(now - s_radar_pending_ms) >= 3000U) {
        s_radar_pending = 0xFFFF;
        s_radar_en = s_lastStateRadarEn;
        CommunicationProtocol::requestStepperState();
        geom_flash("STATUS: RADAR BEZ POTWIERDZENIA");
    }
    // To samo dla "plynnego wylaczenia".
    if (s_onoff_pending != 0xFFFF &&
        (uint32_t)(now - s_onoff_pending_ms) >= 3000U) {
        s_onoff_pending = 0xFFFF;
        s_onoff_en = s_lastStateOnOff;
        CommunicationProtocol::requestStepperState();
        geom_flash("STATUS: PLYNNE WYL. BEZ POTWIERDZENIA");
    }

    if (!on) {
        return;
    }

    // Powtarzanie STOP az busy=0 (max 1 s). STOP idempotentny — bezpieczny.
    // Porownanie wrap-safe: (now - deadline) < 2^31 znaczy "deadline minal".
    if (s_stopRepeat) {
        if (!s_rBusy ||
            (uint32_t)(now - s_stopRepeatUntil) < 0x80000000UL) {
            s_stopRepeat = false;
        } else if ((uint32_t)(now - s_stopRepeatLastTx) >= GEOM_STOP_REPEAT_GAP) {
            s_stopRepeatLastTx = now;
            CommunicationProtocol::sendStepperStop();
        }
    }

    geom_ui_sync();
}

// --- stan z ZEGAR ESP32 (wywolywane z updateUi = task LVGL) ---

void screen_geometry_set_remote_state(bool busy, int32_t pos_steps,
                                      uint16_t target_deg, uint8_t flags,
                                      uint16_t speed, uint16_t home_deg,
                                      uint16_t auto_close_s)
{
    const bool already = s_remoteKnown;
    const bool wasCal = s_rCalOk;
    s_remoteKnown = true;
    s_rBusy = busy;
    if (!busy) s_calIdleSeen = true; // swiezy postoj (bramka potwierdzen kalibracji)
    s_rPos = pos_steps;
    s_rTarget = target_deg;
    s_rCalOk = (flags & GEOM_FLAG_CAL_OK) != 0;
    // Adopcja celu: tylko gdy ZEGAR zglasza NOWY wybor (SET_TARGET) albo cel
    // odtworzony z NVS po reboocie. Ruchy automatow (radar) NIE zmieniaja
    // celu — podswietlenie presetu jest stabilne. Pending = czekamy na
    // potwierdzenie z STM32; stare echa w locie sa ignorowane.
    if (s_target_pending != 0xFFFF) {
        if (target_deg == s_target_pending) {
            s_target_pending = 0xFFFF;
            s_lastStateTarget = target_deg;
            s_selected_deg = (int)target_deg;
        }
    } else if (target_deg != s_lastStateTarget) {
        s_lastStateTarget = target_deg;
        s_selected_deg = (int)target_deg;
    }
    // Adopcja predkosci z NVS ZEGAR-a (np. po reboocie Guitiona wraca 1000
    // zamiast defaultu 400). Wybor operatora nietykany — tylko zmiana w ramce.
    if (speed != 0xFFFF && speed != s_lastStateSpeed) {
        s_lastStateSpeed = speed;
        if (speed >= GEOM_SPEED_MIN && speed <= GEOM_SPEED_MAX) {
            s_speed = (int)speed;
        }
    }
    // Adopcja pozycji bazowej po resecie (truth STM32). Pending po double-clicku:
    // przyjmujemy TYLKO potwierdzona wartosc, stare echo w locie ignorujemy
    // (bez tego zielony potrafil mrugnac/cofnac sie na 0).
    if (home_deg != 0xFFFF) {
        if (s_home_pending != 0xFFFF) {
            if (home_deg == s_home_pending) {
                s_home_pending = 0xFFFF;
                s_lastStateHome = home_deg;
                if (home_deg <= 360) {
                    s_home_deg = (int)home_deg;
                }
            }
        } else if (home_deg != s_lastStateHome) {
            s_lastStateHome = home_deg;
            if (home_deg <= 360) {
                s_home_deg = (int)home_deg;
            }
        }
    }
    // Adopcja czasu auto-zjazdu radaru (truth STM32). Pending po kliknieciu
    // przycisku: przyjmujemy TYLKO potwierdzona wartosc — stare echa w locie
    // nie moga cofnac swiezego wyboru operatora.
    if (auto_close_s != 0xFFFF) {
        if (s_aclose_pending != 0xFFFF) {
            if (auto_close_s == s_aclose_pending) {
                s_aclose_pending = 0xFFFF;
                s_lastStateAutoClose = auto_close_s;
                s_auto_close_s = (int)auto_close_s;
            }
        } else if (auto_close_s != s_lastStateAutoClose) {
            s_lastStateAutoClose = auto_close_s;
            s_auto_close_s = (int)auto_close_s;
        }
    }
    // Adopcja flagi automatu LD2410C z flash STM32. Pending po kliknieciu:
    // przyjmujemy TYLKO potwierdzona wartosc (stare echa w locie nie moga
    // cofnac swiezego wyboru); bez pending — adopcja przy zmianie ramki.
    {
        const bool re = (flags & GEOM_FLAG_RADAR_EN) != 0;
        if (s_radar_pending != 0xFFFF) {
            if ((re ? 1U : 0U) == s_radar_pending) {
                s_radar_pending = 0xFFFF;
                s_radar_first_frame = false;
                s_lastStateRadarEn = re;
                s_radar_en = re;
            }
        } else if (s_radar_first_frame || re != s_lastStateRadarEn) {
            s_radar_first_frame = false;
            s_lastStateRadarEn = re;
            s_radar_en = re;
        }
    }
    // Adopcja "plynnego wylaczenia" (bit4 flags 0x3B) — wzorzec radaru.
    {
        const bool ro = (flags & GEOM_FLAG_ONOFF) != 0;
        if (s_onoff_pending != 0xFFFF) {
            if ((ro ? 1U : 0U) == s_onoff_pending) {
                s_onoff_pending = 0xFFFF;
                s_onoff_first_frame = false;
                s_lastStateOnOff = ro;
                s_onoff_en = ro;
            }
        } else if (s_onoff_first_frame || ro != s_lastStateOnOff) {
            s_onoff_first_frame = false;
            s_lastStateOnOff = ro;
            s_onoff_en = ro;
        }
    }
    if (!busy) {
        s_stopRepeat = false;
        // Lancuch HOLD: palec nadal trzyma, chunk dojechal (idle), STOP nie
        // w drodze i min. 400 ms od poprzedniego chunka -> jedziemy dalej.
        if (s_hold_dir != 0 && !s_stopRepeat &&
            (uint32_t)(lv_tick_get() - s_lastHoldTx) >= GEOM_HOLD_CHAIN_GAP) {
            geom_send_hold(s_hold_dir);
        }
    }
    // KALIBRACJA ZAPISANA tylko na przejsciu 0->1 w trakcie sesji (nie po
    // reboocie — juz pierwsza ramka moze miec calOk). Odpala puls KALIBRUJ.
    if (s_rCalOk && !wasCal && already) {
        geom_flash("STATUS: KALIBRACJA ZAPISANA");
        s_calSavedAt = lv_tick_get();
        // STM32 dokonczyl zapis (calOk 0->1) — pusc automat radaru.
        CommunicationProtocol::sendStepperCalMode(false);
    }
}

void screen_geometry_set_radar_out(bool out)
{
    s_radar_out = out;
}

// --- API dla UI (eventy) ---

void geom_logic_hold_start(int dir)
{
    if (dir != -1 && dir != 1) return;
    s_flash[0] = '\0';
    s_stopRepeat = false;
    s_hold_dir = dir;
    geom_send_hold(dir);
}

void geom_logic_hold_stop(void)
{
    if (s_hold_dir != 0) {
        s_hold_dir = 0;
        geom_send_stop(); // puszczenie = STOP (ucina chunka) + repeat
    }
}

void geom_logic_stop_press(void)
{
    // STOP kasuje wszystko: jazde i kalibracje. Komenda jest idempotentna,
    // wiec wysylamy ZAWSZE — wczesniej warunek "had_hold || s_rBusy" gubil
    // STOP tuz po WYKONAJ (ramka busy=1 jeszcze nie dotarla), a silnik jechal.
    geom_all_stop();
    s_flash[0] = '\0';
    geom_send_stop();
}

void geom_logic_set_speed(int steps_per_s)
{
    if (steps_per_s < GEOM_SPEED_MIN) steps_per_s = GEOM_SPEED_MIN;
    if (steps_per_s > GEOM_SPEED_MAX) steps_per_s = GEOM_SPEED_MAX;
    s_speed = steps_per_s;
}

// Wybor presetu = wybor celu (bez ruchu). Ruch robi WYKONAJ / reczne DÓŁ-GÓRA.
// Wybor leci tez do STM32 (SET_TARGET, persist we flash); pending chroni
// podswietlenie przed starym echo-stanem w locie.
static void geom_preset_select(int deg)
{
    if (s_cal != CAL_IDLE) {
        geom_flash("STATUS: TRWA KALIBRACJA");
        return;
    }
    s_selected_deg = deg;
    s_target_pending = (uint16_t)deg;
    s_target_pending_ms = lv_tick_get();
    CommunicationProtocol::sendStepperSetTarget((uint16_t)deg);
    s_flash[0] = '\0';
}

// WYKONAJ: ramka do ZEGAR ESP32 (zapis + wykonanie po jego stronie,
// predkosc ze slidera). Raz — bez powtorek.
void geom_logic_execute_selected(void)
{
    if (s_cal != CAL_IDLE) {
        geom_flash("STATUS: TRWA KALIBRACJA");
        return;
    }
    // Debounce: dwuklik WYKONAJ = 2x EXECUTE = restart ruchu (kontrakt:
    // EXECUTE wysylany RAZ). Min. 500 ms od poprzedniego.
    const uint32_t now = lv_tick_get();
    if ((uint32_t)(now - s_lastExecMs) < GEOM_EXEC_GAP_MS) {
        return;
    }
    s_lastExecMs = now;
    s_hold_dir = 0;
    s_stopRepeat = false;
    // Cel jawnie do flash STM32 (na wypadek wyboru przed pierwsza ramka stanu).
    s_target_pending = (uint16_t)s_selected_deg;
    s_target_pending_ms = lv_tick_get();
    CommunicationProtocol::sendStepperSetTarget((uint16_t)s_selected_deg);
    CommunicationProtocol::sendStepperExecute((uint16_t)s_selected_deg,
                                              (uint16_t)s_speed);
    // POS/busy przyjda stanem 0x3B
}

// LD2410C: klik = optymistyczny flip + SET_RADAR (persist we flash STM32,
// echo wraca bitem flag 0x3B). Automat ruchu dziala w STM32 — tu tylko HMI.
// Pending: swiezy wybor potwierdza tylko echo z STM32; przy braku
// potwierdzenia (3 s) timer wraca do ostatniej prawdy z 0x3B.
void geom_logic_radar_toggle(void)
{
    const bool nv = !s_radar_en;
    s_radar_en = nv;
    s_radar_pending = nv ? 1U : 0U;
    s_radar_pending_ms = lv_tick_get();
    CommunicationProtocol::sendStepperSetRadar(nv);
    CommunicationProtocol::requestStepperState(); // szybkie potwierdzenie
}

int geom_logic_radar_en(void)
{
    return s_radar_en ? 1 : 0;
}

// "Plynne wylaczenie": klik = optymistyczny flip + SET_ONOFF; potwierdzenie
// przychodzi bitem 4 flag 0x3B (truth STM32), pending jak wyzej.
void geom_logic_onoff_toggle(void)
{
    const bool nv = !s_onoff_en;
    s_onoff_en = nv;
    s_onoff_pending = nv ? 1U : 0U;
    s_onoff_pending_ms = lv_tick_get();
    CommunicationProtocol::sendStepperSetOnOff(nv);
    CommunicationProtocol::requestStepperState(); // szybkie potwierdzenie
}

int geom_logic_onoff_en(void)
{
    return s_onoff_en ? 1 : 0;
}

int geom_logic_radar_out(void)
{
    return s_radar_out ? 1 : 0;
}

// Auto-zjazd radaru: klik presetu = optymistyczny wybor + SET_AUTOCLOSE
// (persist we flash STM32, echo wraca 0x3B). Pending chroni przed starym
// echo-stanem. Wartosci spoza presetow (30/180/600/2700 — ac_sanitize
// w STM32 stepper_truth.c) ignorowane.
void geom_logic_auto_close_set(uint16_t seconds)
{
    if (seconds != 30U && seconds != 180U && seconds != 600U && seconds != 2700U) {
        return;
    }
    s_auto_close_s = (int)seconds;
    s_aclose_pending = seconds;
    s_aclose_pending_ms = lv_tick_get();
    CommunicationProtocol::sendStepperSetAutoClose(seconds);
    char buf[48];
    if (seconds < 120U) {
        snprintf(buf, sizeof(buf), "STATUS: AUTO-ZJAZD RADARU: %us",
                 (unsigned)seconds);
    } else {
        snprintf(buf, sizeof(buf), "STATUS: AUTO-ZJAZD RADARU: %um",
                 (unsigned)(seconds / 60U));
    }
    geom_flash(buf);
}

int geom_logic_auto_close_sec(void)
{
    return s_auto_close_s;
}

// --- publiczne API (screen_geometry.h) dla panelu opcji na ekranie RADAR ---
void screen_geometry_radar_toggle(void) { geom_logic_radar_toggle(); }
int  screen_geometry_radar_enabled(void) { return geom_logic_radar_en(); }
void screen_geometry_auto_close_set(uint16_t seconds) { geom_logic_auto_close_set(seconds); }
int  screen_geometry_auto_close_sec(void) { return geom_logic_auto_close_sec(); }
void screen_geometry_onoff_toggle(void) { geom_logic_onoff_toggle(); }
int  screen_geometry_onoff_enabled(void) { return geom_logic_onoff_en(); }

// Tap presetu: pojedynczy = wybor celu (JEDŹ DO + zapis w NVS ZEGAR-a),
// podwojny (2x w GEOM_DBLCLICK_MS ten sam) = pozycja bazowa STM32 po
// resecie (preferencja w truth, zielony).
void geom_logic_preset_tap(int deg)
{
    if (s_cal != CAL_IDLE) {
        geom_flash("STATUS: TRWA KALIBRACJA");
        return;
    }
    const uint32_t now = lv_tick_get();
    if (deg == s_lastTapDeg && (uint32_t)(now - s_lastTapMs) < GEOM_DBLCLICK_MS) {
        s_lastTapDeg = -1; // trzeci klik to znowu single
        s_home_deg = deg;
        // Pending: dopoki STM32 nie potwierdzi (REF echo), ZADNA inna
        // wartosc nie nadpisze zielonego (stary echo w locie ignorowany).
        s_home_pending = (uint16_t)deg;
        s_home_pending_ms = now;
        CommunicationProtocol::sendStepperSetHome((uint16_t)deg);
        char buf[48];
        snprintf(buf, sizeof(buf), "STATUS: POZYCJA BAZOWA PO RESECIE: %d°", deg);
        geom_flash(buf);
        return;
    }
    s_lastTapDeg = deg;
    s_lastTapMs = now;
    geom_preset_select(deg);
}

// Kalibracja reczna: jeden przycisk, 3 stany (start / 0° / 90°).
// Potwierdzenia wysylaja CAL_ZERO/CAL_SPAN (STM32 zapisuje + wraca do 0 sam).
// Potwierdzenie zapisu = flaga calOk 0->1 w stanie 0x3B.
// Zabezpieczenia: debounce krokow (500 ms) + potwierdzenia TYLKO na
// potwierdzonym postoju (swieza ramka busy=0 po STOPie sesji) — bez tego
// double-tap przelatywal cala kalibracje, a CAL_ZERO lecialo w ruchu
// (STM32 odrzuca po cichu, UI awansowalo dalej na zlej referencji).
void geom_logic_cal_next(void)
{
    const uint32_t now = lv_tick_get();
    if ((uint32_t)(now - s_calStepMs) < GEOM_CAL_STEP_GAP_MS) {
        return; // debounce: drugi CLICKED z tego samego tapniecia
    }

    if (s_cal == CAL_IDLE) {
        s_calStepMs = now;
        s_calIdleSeen = false; // czekamy na SWIEZE busy=0 po STOPie sesji
        s_hold_dir = 0;
        s_stopRepeat = false;
        CommunicationProtocol::sendStepperStop();
        s_lastCalHoldMs = now;
        CommunicationProtocol::sendStepperCalMode(true);
        s_cal = CAL_WAIT_ZERO;
        return;
    }

    if (s_rBusy || !s_calIdleSeen) {
        geom_flash(s_cal == CAL_WAIT_ZERO
                       ? "STATUS: ZATRZYMAJ W 0° I ZATWIERDŹ"
                       : "STATUS: ZATRZYMAJ PRZED ZAPISEM 90°");
        return; // NIE awansujemy — postac i poczekaj na potwierdzony stop
    }

    s_calStepMs = now;
    if (s_cal == CAL_WAIT_ZERO) {
        CommunicationProtocol::sendStepperCal(0);
        s_cal = CAL_WAIT_90;
    } else {
        CommunicationProtocol::sendStepperCal(1);
        // CAL_MODE zdejmie calOk 0->1 (zapis potwierdzony) albo wyjscie
        // z ekranu / auto-timeout STM32.
        s_cal = CAL_IDLE;
    }
}

// Zatwierdzenie predkosci dla auto-powrotu STM32 (SET_SPEED do flash).
// Wolane na RELEASED suwaka i przy presetach — nie spamuje w trakcie drag.
void geom_logic_speed_commit(void)
{
    CommunicationProtocol::sendStepperSetSpeed((uint16_t)s_speed);
}

// --- gettery ---

int32_t geom_logic_pos(void) { return s_rPos; }
int geom_logic_speed(void) { return s_speed; }
int geom_logic_hold_dir(void) { return s_hold_dir; }
int geom_logic_selected_preset(void) { return s_selected_deg; }
int geom_logic_home_preset(void) { return s_home_deg; }

// Puls potwierdzenia zapisu kalibracji (przycisk KALIBRUJ swieci na zielono).
int geom_logic_cal_saved_pulse(void)
{
    if (s_calSavedAt == 0) return 0;
    return (uint32_t)(lv_tick_get() - s_calSavedAt) < GEOM_CAL_PULSE_MS ? 1 : 0;
}

int geom_logic_preset_count(void)
{
    return (int)(sizeof(kAnglePresets) / sizeof(kAnglePresets[0]));
}

int geom_logic_preset_deg_at(int idx)
{
    int n = geom_logic_preset_count();
    if (idx < 0 || idx >= n) return 0;
    return kAnglePresets[idx];
}

const char *geom_logic_cal_btn_text(void)
{
    switch (s_cal) {
        case CAL_WAIT_ZERO: return "ZATWIERDŹ 0°";
        case CAL_WAIT_90:   return "ZATWIERDŹ 90°";
        default:            return "KALIBRUJ";
    }
}

const char *geom_logic_cal_hint(void)
{
    switch (s_cal) {
        case CAL_WAIT_ZERO: return "1/2: JEDŹ DO 0° I ZATWIERDŹ.";
        case CAL_WAIT_90:   return "2/2: JEDŹ DO 90° (PION) I ZATWIERDŹ.";
        default:
            if (s_rCalOk) {
                return "KALIBRACJA ZAPISANA. POWTÓRZ: 0°, POTEM 90°.";
            }
            return "KALIBRACJA RĘCZNA: NAJPIERW 0°, POTEM 90°.";
    }
}

geom_status_t geom_logic_status(void)
{
    if (s_flash[0] != '\0' && lv_tick_elaps(s_flash_t) < GEOM_FLASH_MS) {
        return GEOM_ST_FLASH;
    }
    // busy=1 to ruch (reczny hold albo jazda ZEGAR-a do celu).
    if (!s_rBusy) return GEOM_ST_READY;
    return (s_hold_dir < 0) ? GEOM_ST_DRIVE_L : GEOM_ST_DRIVE_R;
}

const char *geom_logic_status_text(void)
{
    switch (geom_logic_status()) {
        case GEOM_ST_DRIVE_L: return "STATUS: JAZDA W DÓŁ";
        case GEOM_ST_DRIVE_R:
            if (s_hold_dir == 0) {
                // Ruch z WYKONAJ (ZEGAR jedzie do celu) — brak hold_dir.
                snprintf(s_driveBuf, sizeof(s_driveBuf), "STATUS: JAZDA DO %d°",
                         (int)s_rTarget);
                return s_driveBuf;
            }
            return "STATUS: JAZDA W GÓRĘ";
        case GEOM_ST_FLASH:   return s_flash;
        default:              return "STATUS: GOTOWY";
    }
}

// --- init publiczny ---

void screen_geometry_init(void)
{
    if (s_timer != NULL) {
        return;
    }
    if (objects.geometria_ekranu != NULL && !s_built) {
        geom_ui_build(objects.geometria_ekranu);
        s_built = true;
    }
    s_timer = lv_timer_create(geom_timer_cb, GEOM_TICK_MS, NULL);
}
