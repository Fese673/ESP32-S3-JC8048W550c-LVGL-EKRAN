#include "timer_synchro.h"
#include "communication/UartTransport.h"
#include "communication/CobsFraming.h"
#include "app/alarm_logic.h"
#include "app/alarm_ui.h"
#include "eez-flow.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

// ─── Flow variable IDs (fallback if missing from vars.h) ──────────────
#ifndef FLOW_GLOBAL_VARIABLE_TIMER_REMAINING
#define FLOW_GLOBAL_VARIABLE_TIMER_REMAINING 70
#endif
#ifndef FLOW_GLOBAL_VARIABLE_TIMER_PROGRESS
#define FLOW_GLOBAL_VARIABLE_TIMER_PROGRESS 71
#endif
#ifndef FLOW_GLOBAL_VARIABLE_TIMER_STATE
#define FLOW_GLOBAL_VARIABLE_TIMER_STATE 72
#endif

namespace CommunicationProtocol {
namespace TimerSync {

static constexpr char kTag[] = "TIMER_SYNC";

// ─── Mutex ────────────────────────────────────────────────────────────

static SemaphoreHandle_t s_mutex = nullptr;

void init() {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

static void lock() {
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
}

static void unlock() {
    if (s_mutex) xSemaphoreGive(s_mutex);
}

// ─── Internal state ───────────────────────────────────────────────────

static TimerState s_timerState = {};
static volatile bool s_timerDirty = false;

static StopwatchState s_stopwatchState = {};
static volatile bool s_stopwatchDirty = false;

static volatile bool s_editLocked = false;
static volatile bool s_editLockDirty = false;

static volatile bool s_alarmListDirty = false;

// Tryb 7-seg: ostatnie echo z ZEGARA (0=zegar, 1=stoper, 2=minutnik).
static uint8_t s_segMode = 0;
static bool s_segModeValid = false;
static volatile bool s_segModeDirty = false;

// ─── Send functions ───────────────────────────────────────────────────

static void sendRaw(uint8_t type, const uint8_t *payload, uint16_t len) {
    sendRawFrame(type, 0, payload, len);
}

void sendTimerCmd(uint8_t cmd, uint32_t durationSec) {
    if (cmd == 0 && durationSec > 0) {
        uint8_t payload[5] = { cmd,
            (uint8_t)(durationSec & 0xFF),
            (uint8_t)((durationSec >> 8) & 0xFF),
            (uint8_t)((durationSec >> 16) & 0xFF),
            (uint8_t)((durationSec >> 24) & 0xFF) };
        sendRaw(kTypeTimerCmd, payload, 5);
    } else {
        uint8_t payload[1] = { cmd };
        sendRaw(kTypeTimerCmd, payload, 1);
    }
}

void sendStopwatchCmd(uint8_t cmd) {
    uint8_t payload[1] = { cmd };
    sendRaw(kTypeStopwatchCmd, payload, 1);
}

void sendAlarmAction(uint8_t action) {
    uint8_t payload[1] = { action };
    sendRaw(kTypeAlarmAction, payload, 1);
}

void sendAlarmList() {
    alarm_t list[ALARM_MAX_COUNT];
    const int count = alarm_get_all(list, ALARM_MAX_COUNT);
    if (count < 0) {
        return;
    }

    uint8_t payload[1 + (ALARM_MAX_COUNT * 7)];
    uint8_t *cursor = payload;
    *cursor++ = static_cast<uint8_t>(count);

    for (int i = 0; i < count; ++i) {
        uint8_t dayMask = 0;
        for (int d = 0; d < 7; ++d) {
            if (list[i].days[d]) {
                dayMask |= static_cast<uint8_t>(1U << d);
            }
        }

        *cursor++ = static_cast<uint8_t>(list[i].hour);
        *cursor++ = static_cast<uint8_t>(list[i].minute);
        *cursor++ = list[i].enabled ? 1U : 0U;
        *cursor++ = dayMask;
        *cursor++ = list[i].single_shot ? 1U : 0U;
        *cursor++ = 0;
        *cursor++ = 0;
    }

    sendRaw(kTypeAlarmList, payload, static_cast<uint16_t>(cursor - payload));
}

void requestAlarmList() {
    uint8_t payload[1] = { kTypeAlarmList };
    sendRaw(0x10, payload, 1); // 0x10 is kTypeRequest
}

void sendSegModeCmd(uint8_t mode) {
    if (mode > kSegModeTimer) return; // 0/1/2
    uint8_t payload[1] = { mode };
    sendRaw(kTypeSegModeCmd, payload, 1);
}

void requestSegMode() {
    uint8_t payload[1] = { kTypeSegModeState };
    sendRaw(0x10, payload, 1); // 0x10 is kTypeRequest
}

// ─── State access ─────────────────────────────────────────────────────

TimerState getLastTimerState() {
    lock();
    TimerState copy = s_timerState;
    unlock();
    return copy;
}

StopwatchState getLastStopwatchState() {
    lock();
    StopwatchState copy = s_stopwatchState;
    unlock();
    return copy;
}

bool getLastSegMode(uint8_t &mode) {
    lock();
    const bool valid = s_segModeValid;
    if (valid) mode = s_segMode;
    unlock();
    return valid;
}

// ─── Payload handlers ─────────────────────────────────────────────────

void handleTimerStatePayload(const uint8_t *payload, uint16_t len) {
    if (!payload || len < 9) return;
    lock();
    s_timerState.state = payload[0];
    s_timerState.remainingSec = (uint32_t)payload[1]
        | ((uint32_t)payload[2] << 8)
        | ((uint32_t)payload[3] << 16)
        | ((uint32_t)payload[4] << 24);
    s_timerState.durationSec = (uint32_t)payload[5]
        | ((uint32_t)payload[6] << 8)
        | ((uint32_t)payload[7] << 16)
        | ((uint32_t)payload[8] << 24);
    s_timerState.valid = true;
    s_timerDirty = true;
    unlock();
}

void handleStopwatchStatePayload(const uint8_t *payload, uint16_t len) {
    if (!payload || len < 5) return;
    lock();
    s_stopwatchState.state = payload[0];
    s_stopwatchState.elapsedMs = (uint32_t)payload[1]
        | ((uint32_t)payload[2] << 8)
        | ((uint32_t)payload[3] << 16)
        | ((uint32_t)payload[4] << 24);
    s_stopwatchState.valid = true;
    s_stopwatchDirty = true;
    unlock();
}

void handleEditLockPayload(const uint8_t *payload, uint16_t len) {
    lock();
    s_editLocked = (len >= 1 && payload[0] != 0);
    s_editLockDirty = true;
    unlock();
}

void handleSegModeStatePayload(const uint8_t *payload, uint16_t len) {
    if (!payload || len < 1) return;
    if (payload[0] > kSegModeTimer) return; // 0/1/2 — inne ignorujemy
    lock();
    s_segMode = payload[0];
    s_segModeValid = true;
    s_segModeDirty = true;
    unlock();
}

void invalidatePeerState() {
    lock();
    /* Wszystkie stany z ZEGARA traca waznosc po jego restarcie: stoper nie
     * moze dalej zyc (korekcja dryfu przypinalaby wyswietlacz do starej
     * wartosci), edycja alarmow nie moze zostac zablokowana na stale, a
     * tryb 7-seg wraca do prawdy (ZEGAR po restarcie zawsze = zegar).
     * Swiezy, pelny stan przysle ZEGAR w odpowiedzi na nasze HELLO. */
    s_stopwatchState.valid = false;
    s_timerState.valid = false;
    s_segModeValid = false;
    /* Tryb 7-seg: ZEGAR po restarcie ZAWSZE startuje w trybie zegara —
     * bez resetu konsument dostalby raz stary tryb jako "swiezy resync"
     * (echo 0x18 i tak przyjdzie w pushu po HELLO, a to domyka luke). */
    s_segMode = kSegModeClock;
    s_editLocked = false;
    s_stopwatchDirty = true;
    s_timerDirty = true;
    s_segModeDirty = true;
    s_editLockDirty = true;
    unlock();
}

void handleAlarmListPayload(const uint8_t *payload, uint16_t len) {
    if (!payload || len < 1) return;
    int count = payload[0];
    if (count > ALARM_MAX_COUNT) count = ALARM_MAX_COUNT;
    if ((uint16_t)(1 + count * 7) > len) return;

    alarm_t list[ALARM_MAX_COUNT];
    for (int i = 0; i < count; ++i) {
        const uint8_t *p = payload + 1 + i * 7;
        list[i].hour = p[0];
        list[i].minute = p[1];
        list[i].enabled = p[2] != 0;
        uint8_t dayMask = p[3];
        for (int d = 0; d < 7; ++d) {
            list[i].days[d] = (dayMask >> d) & 1;
        }
        list[i].single_shot = (p[4] & 0x01) != 0;
    }
    alarm_sync_from_list(list, count);
    lock();
    s_alarmListDirty = true;
    unlock();
}

void handleSetAlarmPayload(const uint8_t *payload, uint16_t len) {
    // Forward to alarm_logic — this is called from UartTransport
    // but the actual alarm sync logic lives in alarm_logic
    (void)payload;
    (void)len;
}

// ─── Consume pattern ──────────────────────────────────────────────────

bool consumeTimerState(TimerState &out) {
    lock();
    bool dirty = s_timerDirty;
    if (dirty) {
        out = s_timerState;
        s_timerDirty = false;
    }
    unlock();
    return dirty;
}

bool consumeStopwatchState(StopwatchState &out) {
    lock();
    bool dirty = s_stopwatchDirty;
    if (dirty) {
        out = s_stopwatchState;
        s_stopwatchDirty = false;
    }
    unlock();
    return dirty;
}

bool consumeEditLock(bool &out) {
    lock();
    bool dirty = s_editLockDirty;
    if (dirty) {
        out = s_editLocked;
        s_editLockDirty = false;
    }
    unlock();
    return dirty;
}

bool consumeAlarmListDirty() {
    lock();
    bool dirty = s_alarmListDirty;
    if (dirty) s_alarmListDirty = false;
    unlock();
    return dirty;
}

bool consumeSegMode(uint8_t &mode) {
    lock();
    bool dirty = s_segModeDirty;
    if (dirty) {
        mode = s_segMode;
        s_segModeDirty = false;
    }
    unlock();
    return dirty;
}

// ─── UI mapping ───────────────────────────────────────────────────────

void applyTimerStateToUi(const TimerState &ts) {
    if (!ts.valid) return;

    static char remainingBuf[16];
    uint32_t totalSec = ts.remainingSec;
    int h = (int)(totalSec / 3600);
    int m = (int)((totalSec % 3600) / 60);
    int s = (int)(totalSec % 60);
    snprintf(remainingBuf, sizeof(remainingBuf), h > 0 ? "%02d:%02d:%02d" : "%02d:%02d", h, m, s);
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIMER_REMAINING, eez::Value(remainingBuf));

    static char progressBuf[16];
    if (ts.durationSec > 0) {
        uint32_t pct = (ts.remainingSec * 100) / ts.durationSec;
        snprintf(progressBuf, sizeof(progressBuf), "%u%%", (unsigned)pct);
    } else {
        snprintf(progressBuf, sizeof(progressBuf), "---");
    }
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIMER_PROGRESS, eez::Value(progressBuf));
    eez::flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIMER_STATE, eez::Value((int)ts.state));
}

void applyEditLockToUi(bool locked) {
    alarm_ui_set_edit_locked(locked);
}

// ─── Tick ─────────────────────────────────────────────────────────────

void tick() {
    // Placeholder for periodic housekeeping
}

// ─── Status checks ────────────────────────────────────────────────────

bool isTimerRinging() {
    lock();
    bool ringing = s_timerState.valid && s_timerState.state == 3;
    unlock();
    return ringing;
}

}  // namespace TimerSync
}  // namespace CommunicationProtocol
