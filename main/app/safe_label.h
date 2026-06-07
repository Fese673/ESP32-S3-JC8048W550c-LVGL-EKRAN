#ifndef SAFE_LABEL_H
#define SAFE_LABEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdlib.h>

static void safe_label_delete_cb(lv_event_t * e) {
    void * buffer = lv_event_get_user_data(e);
    if (buffer) {
        heap_caps_free(buffer);
    }
}

static inline void safe_lv_label_set_text(lv_obj_t *obj, const char *txt) {
    if (!obj || !txt) return;

    char *my_buf = (char *)lv_obj_get_user_data(obj);
    if (!my_buf) {
        /* Allocate from SPIRAM explicitly — preserves scarce SRAM for DMA/cache.
         * 64 bytes per label, allocated once, freed on LV_EVENT_DELETE. */
        my_buf = (char *)heap_caps_malloc(64, MALLOC_CAP_SPIRAM);
        if (my_buf) {
            lv_obj_set_user_data(obj, my_buf);
            lv_obj_add_event_cb(obj, safe_label_delete_cb, LV_EVENT_DELETE, my_buf);
            my_buf[0] = '\0';
        }
    }

    if (my_buf && strcmp(my_buf, txt) != 0) {
        strncpy(my_buf, txt, 63);
        my_buf[63] = '\0';
        lv_label_set_text_static(obj, my_buf);
    }
}

#define lv_label_set_text(obj, txt) safe_lv_label_set_text(obj, txt)

#ifdef __cplusplus
}
#endif

#endif
