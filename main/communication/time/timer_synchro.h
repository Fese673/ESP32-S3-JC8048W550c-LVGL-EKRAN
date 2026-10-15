#pragma once

#include <stdint.h>
#include <stdbool.h>

// ─── Frame types (shared with Zegar TimeSyncProtocol) ─────────────────
namespace CommunicationProtocol {
namespace TimerSync {

constexpr uint8_t kTypeTimerState     = 0x0F;
constexpr uint8_t kTypeTimerCmd       = 0x12;
constexpr uint8_t kTypeEditLock       = 0x13;
constexpr uint8_t kTypeAlarmAction    = 0x14;
constexpr uint8_t kTypeStopwatchState = 0x15;
constexpr uint8_t kTypeStopwatchCmd   = 0x16;
constexpr uint8_t kTypeAlarmList      = 0x0D;
constexpr uint8_t kTypeSetAlarm       = 0x0E;
// Tryb 7-seg (kolka przy kartach STOPER/MINUTNIK na ekranie ZEGAR):
// ZEGAR jest wlascicielem, my tylko klikamy (0x17) i odzwierciedlamy echo (0x18).
constexpr uint8_t kTypeSegModeCmd     = 0x17; // G→Z: [mode 0/1/2]
constexpr uint8_t kTypeSegModeState   = 0x18; // Z→G: [mode]
constexpr uint8_t kSegModeClock       = 0;    // zwykly zegar
constexpr uint8_t kSegModeStopwatch   = 1;    // czas stopera na 7-seg
constexpr uint8_t kSegModeTimer       = 2;    // odliczanie minutnika na 7-seg

// ─── State structs ────────────────────────────────────────────────────

struct TimerState {
    uint8_t state;          // 0=IDLE, 1=RUNNING, 2=PAUSED, 3=RINGING
    uint32_t remainingSec;
    uint32_t durationSec;
    bool valid;
};

struct StopwatchState {
    uint8_t state;          // 0=IDLE, 1=RUNNING, 2=STOPPED
    uint32_t elapsedMs;
    bool valid;
};

// ─── Initialization (call before tasks start) ─────────────────────────

void init();

// ─── Send functions (Gution → Zegar) ──────────────────────────────────

void sendTimerCmd(uint8_t cmd, uint32_t durationSec = 0);
void sendStopwatchCmd(uint8_t cmd);
void sendAlarmAction(uint8_t action);
void sendAlarmList();
void requestAlarmList();
// Tryb 7-seg: klik kolka (0=zegar/off, 1=stoper, 2=minutnik) + prosba o stan.
void sendSegModeCmd(uint8_t mode);
void requestSegMode();

// ─── State access (last received from Zegar) ──────────────────────────

TimerState getLastTimerState();
StopwatchState getLastStopwatchState();
// Ostatnie potwierdzenie trybu 7-seg (valid=false dopoki nie przyszlo).
bool getLastSegMode(uint8_t &mode);
// Jednorazowy odczyt echa (dirty) — dla zegar_mode_ui.
bool consumeSegMode(uint8_t &mode);

// ─── Payload handlers (called from UartTransport frame dispatch) ──────

void handleTimerStatePayload(const uint8_t *payload, uint16_t len);
void handleStopwatchStatePayload(const uint8_t *payload, uint16_t len);
void handleSegModeStatePayload(const uint8_t *payload, uint16_t len);
void handleEditLockPayload(const uint8_t *payload, uint16_t len);
void handleAlarmListPayload(const uint8_t *payload, uint16_t len);
void handleSetAlarmPayload(const uint8_t *payload, uint16_t len);

// ─── Consume pattern (called from LVGL task in updateUi) ──────────────

bool consumeTimerState(TimerState &out);
bool consumeStopwatchState(StopwatchState &out);
bool consumeEditLock(bool &out);
bool consumeAlarmListDirty();

// ─── UI mapping (called from consume in LVGL task) ────────────────────

void applyTimerStateToUi(const TimerState &ts);
void applyEditLockToUi(bool locked);

// ─── Periodic tick (called from LVGL timer or main loop) ──────────────

void tick();  // pending cmd timeout, periodic housekeeping

// ─── Invalidation (peer restart) ──────────────────────────────────────

// Unieważnia wszystkie stany otrzymane z ZEGARA (stoper/timer/lock/segmode).
// Wołane po wykryciu restartu ZEGARA (nowy bootId w HELLO_ACK): stare dane
// nie mogą dalej uchodzić za prawdę (stoper "RUNNING" przywrócony do żywej
// korekcji dryfu = zamrożony/kłamliwy licznik; zablokowana edycja alarmów
// po restarcie = trwała blokada UI). Świeży stan przyjdzie w pełnym pushu
// po HELLO.
void invalidatePeerState();

// ─── Status checks ────────────────────────────────────────────────────

bool isTimerRinging();  // ostatni stan timera to RINGING

}  // namespace TimerSync
}  // namespace CommunicationProtocol
