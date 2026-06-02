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

// ─── Send functions (Gution → Zegar) ──────────────────────────────────

void sendTimerCmd(uint8_t cmd, uint32_t durationSec = 0);
void sendStopwatchCmd(uint8_t cmd);
void sendAlarmAction(uint8_t action);
void sendAlarmList();
void requestAlarmList();

// ─── State access (last received from Zegar) ──────────────────────────

TimerState getLastTimerState();
StopwatchState getLastStopwatchState();

// ─── Payload handlers (called from UartTransport frame dispatch) ──────

void handleTimerStatePayload(const uint8_t *payload, uint16_t len);
void handleStopwatchStatePayload(const uint8_t *payload, uint16_t len);
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

// ─── Status checks ────────────────────────────────────────────────────

bool isTimerRinging();  // ostatni stan timera to RINGING

}  // namespace TimerSync
}  // namespace CommunicationProtocol
