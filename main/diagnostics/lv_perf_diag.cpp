/*
 * lv_perf_diag.cpp — LVGL performance + heap diagnostics.
 *
 * See lv_perf_diag.h and diagnostics/README.md.
 */

#if defined(CONFIG_LV_PERF_DIAG) && CONFIG_LV_PERF_DIAG

#include "diagnostics/lv_perf_diag.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "eez-flow.h"

#define DIAG_TAG                 "DIAG"
#define DIAG_INTERVAL_MS         ((uint32_t)CONFIG_LV_PERF_DIAG_INTERVAL_MS)
#define DIAG_RING_SIZE           8U

#if defined(CONFIG_LV_PERF_DIAG_HEARTBEAT_SKIP)
#define DIAG_HEARTBEAT_SKIP      ((uint8_t)CONFIG_LV_PERF_DIAG_HEARTBEAT_SKIP)
#else
#define DIAG_HEARTBEAT_SKIP      4U
#endif

/* ── Thresholds (tuned for ESP32-S3 + 15-screen EEZ UI) ─────────────────── */

#define SRAM_FREE_WARN_BYTES     (25U * 1024U)
#define SRAM_HEADROOM_WARN_BYTES 2048U
#define SRAM_DELTA_WARN_BYTES    2048U
#define SRAM_LARGEST_WARN_BYTES  4096U
#define PSRAM_MIN_WARN_BYTES     8192U
#define PSRAM_FREE_LOW_BYTES     (32U * 1024U)
#define REFR_STALL_WARN_US       150000LL
#define TICK_SLOW_WARN_US        20000LL

/* ── Alert flags ─────────────────────────────────────────────────────────── */

#define F_SRAM_LOW       (1U << 0)
#define F_SRAM_FLOOR     (1U << 1)
#define F_SRAM_DRAIN     (1U << 2)
#define F_SRAM_FRAG      (1U << 3)
#define F_PSRAM_SPIKE    (1U << 4)
#define F_DRAW_STALL     (1U << 5)
#define F_TICK_SLOW      (1U << 6)

typedef struct {
    int64_t  tick_sum_us;
    uint32_t tick_count;
    int64_t  handler_max_us;
    uint16_t screen_id;
} diag_sample_t;

typedef struct {
    uint32_t internal_free;
    uint32_t internal_min;
    uint32_t internal_largest;
    uint32_t psram_free;
    uint32_t psram_min;
    uint32_t psram_largest;
} heap_snapshot_t;

static diag_sample_t s_ring[DIAG_RING_SIZE];
static uint8_t       s_ring_idx = 0;
static uint8_t       s_ring_filled = 0;

static int64_t       s_tick_begin_us = 0;
static int64_t       s_handler_prev_us = 0;
static int64_t       s_handler_max_us = 0;
static uint32_t      s_tick_count_in_window = 0;

static bool          s_inited = false;
static lv_timer_t   *s_emit_timer = NULL;
static lv_timer_t   *s_handler_timer = NULL;

static uint32_t      s_int_at_last_emit = 0;
static uint32_t      s_int_at_last_screen = 0;
static uint16_t      s_last_screen_id = 0xFFFF;
static uint32_t      s_alert_flags = 0;
static uint8_t       s_quiet_ok_ticks = 0;
static bool          s_screen_changed_in_window = false;

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static const char *screen_name(int16_t id)
{
    switch (id) {
        case 1:  return "MAIN";
        case 2:  return "MENU";
        case 3:  return "USTAW";
        case 4:  return "STAT";
        case 5:  return "ZASOBY";
        case 6:  return "WIFI";
        case 7:  return "GRY";
        case 8:  return "ZEGAR";
        case 9:  return "LOGI";
        case 10: return "POGODA";
        case 11: return "PROG";
        case 12: return "BPM";
        case 13: return "GRY_T";
        case 14: return "MUZYKA";
        case 15: return "CZUJN";
        default: return "?";
    }
}

static void snapshot_heap(heap_snapshot_t *out)
{
    out->internal_free    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    out->internal_min     = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    out->internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    out->psram_free       = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    out->psram_min        = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    out->psram_largest    = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

static uint32_t evaluate_alerts(const heap_snapshot_t *hp,
                                int32_t int_delta_emit,
                                int64_t handler_max_us,
                                int64_t tick_avg_us)
{
    uint32_t flags = 0;

    if (hp->internal_free < SRAM_FREE_WARN_BYTES) {
        flags |= F_SRAM_LOW;
    }
    if (hp->internal_free > hp->internal_min &&
        (hp->internal_free - hp->internal_min) < SRAM_HEADROOM_WARN_BYTES) {
        flags |= F_SRAM_FLOOR;
    }
    if (int_delta_emit < 0 && (-int_delta_emit) >= (int32_t)SRAM_DELTA_WARN_BYTES) {
        flags |= F_SRAM_DRAIN;
    }
    if (hp->internal_largest < SRAM_LARGEST_WARN_BYTES) {
        flags |= F_SRAM_FRAG;
    }
    /* psram_min is lifetime — warn only when free pool is also tight */
    if (hp->psram_min < PSRAM_MIN_WARN_BYTES &&
        hp->psram_free < PSRAM_FREE_LOW_BYTES) {
        flags |= F_PSRAM_SPIKE;
    }
    if (handler_max_us >= REFR_STALL_WARN_US) {
        flags |= F_DRAW_STALL;
    }
    if (tick_avg_us >= TICK_SLOW_WARN_US) {
        flags |= F_TICK_SLOW;
    }
    return flags;
}

static void append_alerts(char *buf, size_t buf_size, uint32_t flags)
{
    buf[0] = '\0';
    if (flags == 0) {
        return;
    }

    const struct {
        uint32_t flag;
        const char *tag;
    } tags[] = {
        { F_SRAM_LOW,    "SRAM_LOW" },
        { F_SRAM_FLOOR,  "SRAM_FLOOR" },
        { F_SRAM_DRAIN,  "SRAM_DRAIN" },
        { F_SRAM_FRAG,   "SRAM_FRAG" },
        { F_PSRAM_SPIKE, "PSRAM_SPIKE" },
        { F_DRAW_STALL,  "DRAW_STALL" },
        { F_TICK_SLOW,   "TICK_SLOW" },
    };

    size_t used = 0;
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        if (!(flags & tags[i].flag)) {
            continue;
        }
        int n = snprintf(buf + used, buf_size - used,
                         used > 0 ? " %s" : "%s", tags[i].tag);
        if (n < 0 || (size_t)n >= buf_size - used) {
            break;
        }
        used += (size_t)n;
    }
}

static void format_kb_delta(char *out, size_t out_size, int32_t delta_bytes)
{
    if (delta_bytes == 0) {
        snprintf(out, out_size, "0");
        return;
    }
    int32_t kb10 = delta_bytes / 102; /* 0.1 KB units */
    if (kb10 > -10 && kb10 < 10) {
        snprintf(out, out_size, "%+dB", (int)delta_bytes);
    } else {
        int32_t akb = kb10 < 0 ? -kb10 : kb10;
        snprintf(out, out_size, "%+d.%dK", (int)(kb10 / 10), (int)(akb % 10));
    }
}

/* ── LVGL timer callbacks ─────────────────────────────────────────────────── */

static void handler_tick_cb(lv_timer_t *t)
{
    (void)t;
    int64_t now = esp_timer_get_time();
    if (s_handler_prev_us != 0) {
        int64_t dt = now - s_handler_prev_us;
        if (dt > s_handler_max_us) {
            s_handler_max_us = dt;
        }
    }
    s_handler_prev_us = now;
}

static void emit_cb(lv_timer_t *t)
{
    (void)t;

    /* Close current ring slot for this interval. */
    diag_sample_t *cur = &s_ring[s_ring_idx];
    cur->tick_count = s_tick_count_in_window;
    cur->handler_max_us = s_handler_max_us;

    uint8_t slots_used = s_ring_filled;
    if (slots_used < DIAG_RING_SIZE) {
        slots_used++;
    }

    heap_snapshot_t hp;
    snapshot_heap(&hp);

    int32_t int_delta_emit = (int32_t)hp.internal_free - (int32_t)s_int_at_last_emit;

    /* Metrics for this interval only (ring history caused sticky false alarms). */
    int64_t refr_us = cur->handler_max_us;
    int64_t tick_us = cur->tick_sum_us;
    uint16_t cur_screen = s_last_screen_id;

    uint32_t flags = evaluate_alerts(&hp, int_delta_emit, refr_us, tick_us);
    bool has_alert = flags != 0;
    bool alerts_changed = flags != s_alert_flags;
    bool force_emit = s_screen_changed_in_window || (has_alert && alerts_changed);
    bool should_log = true;

    if (!force_emit) {
        s_quiet_ok_ticks++;
        if (s_quiet_ok_ticks <= DIAG_HEARTBEAT_SKIP) {
            should_log = false;
        } else {
            s_quiet_ok_ticks = 0;
        }
    } else {
        s_quiet_ok_ticks = 0;
    }

    (void)slots_used;

    if (should_log) {
        char alerts[96];
        append_alerts(alerts, sizeof(alerts), flags);

        char d_emit[16];
        format_kb_delta(d_emit, sizeof(d_emit), int_delta_emit);

        uint32_t int_headroom = (hp.internal_free > hp.internal_min)
            ? (hp.internal_free - hp.internal_min) : 0;

        const char *level = has_alert ? "WARN" : "ok";

#if defined(CONFIG_LV_PERF_DIAG_VERBOSE) && CONFIG_LV_PERF_DIAG_VERBOSE
        uint32_t idle_pct = lv_os_get_idle_percent();
        ESP_LOGI(DIAG_TAG,
                 "[%s] %s int=%uK(%s head=%uK blk=%uK) psram=%uK(min=%uK blk=%uK) "
                 "refr=%llums tick=%llums%s%s idle=%u%%",
                 level,
                 screen_name((int16_t)cur_screen),
                 (unsigned)(hp.internal_free / 1024U), d_emit,
                 (unsigned)(int_headroom / 1024U),
                 (unsigned)(hp.internal_largest / 1024U),
                 (unsigned)(hp.psram_free / 1024U),
                 (unsigned)(hp.psram_min / 1024U),
                 (unsigned)(hp.psram_largest / 1024U),
                 (unsigned long long)(refr_us / 1000LL),
                 (unsigned long long)(tick_us / 1000LL),
                 alerts[0] ? " | " : "",
                 alerts[0] ? alerts : "",
                 (unsigned)idle_pct);
#else
        if (has_alert) {
            ESP_LOGW(DIAG_TAG,
                     "[%s] %s int=%uK(%s head=%uB blk=%uK) psram=%uK(min=%uK) "
                     "refr=%llums tick=%llums | %s",
                     level,
                     screen_name((int16_t)cur_screen),
                     (unsigned)(hp.internal_free / 1024U), d_emit,
                     (unsigned)int_headroom,
                     (unsigned)(hp.internal_largest / 1024U),
                     (unsigned)(hp.psram_free / 1024U),
                     (unsigned)(hp.psram_min / 1024U),
                     (unsigned long long)(refr_us / 1000LL),
                     (unsigned long long)(tick_us / 1000LL),
                     alerts);
        } else {
            ESP_LOGI(DIAG_TAG,
                     "[ok] %s int=%uK blk=%uK psram=%uK refr=%llums tick=%llums",
                     screen_name((int16_t)cur_screen),
                     (unsigned)(hp.internal_free / 1024U),
                     (unsigned)(hp.internal_largest / 1024U),
                     (unsigned)(hp.psram_free / 1024U),
                     (unsigned long long)(refr_us / 1000LL),
                     (unsigned long long)(tick_us / 1000LL));
        }
#endif
    }

    s_int_at_last_emit = hp.internal_free;
    s_handler_max_us = 0;
    s_tick_count_in_window = 0;
    s_screen_changed_in_window = false;
    s_alert_flags = flags;

    s_ring_idx = (uint8_t)((s_ring_idx + 1U) % DIAG_RING_SIZE);
    if (s_ring_filled < DIAG_RING_SIZE) {
        s_ring_filled++;
    }
    memset(&s_ring[s_ring_idx], 0, sizeof(diag_sample_t));
}

/* ── Public API ───────────────────────────────────────────────────────────── */

void lv_perf_diag_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = true;

    memset(s_ring, 0, sizeof(s_ring));
    s_ring_idx = 0;
    s_ring_filled = 0;
    s_tick_begin_us = 0;
    s_handler_prev_us = 0;
    s_handler_max_us = 0;
    s_tick_count_in_window = 0;
    s_last_screen_id = 0xFFFF;
    s_quiet_ok_ticks = 0;
    s_screen_changed_in_window = false;
    s_alert_flags = 0;

    heap_snapshot_t hp;
    snapshot_heap(&hp);
    s_int_at_last_emit = hp.internal_free;
    s_int_at_last_screen = hp.internal_free;

    s_handler_timer = lv_timer_create(handler_tick_cb, 1, NULL);
    if (s_handler_timer == NULL) {
        ESP_LOGE(DIAG_TAG, "handler timer create failed");
        s_inited = false;
        return;
    }
    lv_timer_set_repeat_count(s_handler_timer, -1);

    s_emit_timer = lv_timer_create(emit_cb, (int32_t)DIAG_INTERVAL_MS, NULL);
    if (s_emit_timer == NULL) {
        ESP_LOGE(DIAG_TAG, "emit timer create failed");
        s_inited = false;
        return;
    }
    lv_timer_set_repeat_count(s_emit_timer, -1);

    ESP_LOGI(DIAG_TAG,
             "init interval=%ums quiet_skip=%u | int=%uK blk=%uK psram=%uK",
             (unsigned)DIAG_INTERVAL_MS,
             (unsigned)DIAG_HEARTBEAT_SKIP,
             (unsigned)(hp.internal_free / 1024U),
             (unsigned)(hp.internal_largest / 1024U),
             (unsigned)(hp.psram_free / 1024U));
}

void lv_perf_diag_tick_begin(void)
{
    if (!s_inited) {
        return;
    }
    s_tick_begin_us = esp_timer_get_time();
    s_tick_count_in_window++;
}

void lv_perf_diag_tick_end(void)
{
    if (!s_inited) {
        return;
    }
    int64_t begin = s_tick_begin_us;
    if (begin == 0) {
        return;
    }
    s_tick_begin_us = 0;

    int64_t dur = esp_timer_get_time() - begin;
    uint8_t idx = s_ring_idx;
    s_ring[idx].tick_sum_us += dur;

    int16_t scr = eez_flow_get_current_screen();
    s_ring[idx].screen_id = (uint16_t)scr;
    s_last_screen_id = (uint16_t)scr;
}

void lv_perf_diag_screen_change(int16_t from_screen, int16_t to_screen)
{
    if (!s_inited || to_screen < 0) {
        return;
    }

    heap_snapshot_t hp;
    snapshot_heap(&hp);

    int32_t delta_scr = (int32_t)hp.internal_free - (int32_t)s_int_at_last_screen;
    s_int_at_last_screen = hp.internal_free;
    /* Reset heartbeat SRAM baseline — avoids false SRAM_DRAIN vs init snapshot. */
    s_int_at_last_emit = hp.internal_free;
    s_screen_changed_in_window = true;
    s_quiet_ok_ticks = 0;

    if (from_screen < 0) {
        return;
    }

    char d_scr[16];
    format_kb_delta(d_scr, sizeof(d_scr), delta_scr);

    const char *warn = "";
    if (delta_scr < -(int32_t)SRAM_DELTA_WARN_BYTES) {
        warn = " SRAM_LOSS";
    } else if (hp.internal_largest < SRAM_LARGEST_WARN_BYTES) {
        warn = " SRAM_FRAG";
    }

    ESP_LOGI(DIAG_TAG,
             ">> %s->%s int=%uK(%s blk=%uK) psram=%uK%s",
             screen_name(from_screen),
             screen_name(to_screen),
             (unsigned)(hp.internal_free / 1024U),
             d_scr,
             (unsigned)(hp.internal_largest / 1024U),
             (unsigned)(hp.psram_free / 1024U),
             warn);
}

#else

typedef int lv_perf_diag_unused_dummy_t_;

#endif /* CONFIG_LV_PERF_DIAG */
