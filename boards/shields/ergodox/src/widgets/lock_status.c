/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#include <dt-bindings/zmk/hid_indicators.h>

#ifndef HID_INDICATOR_CAPS_LOCK
#define HID_INDICATOR_CAPS_LOCK (1 << 1)
#endif

#if IS_ENABLED(CONFIG_ZMK_CAPS_WORD)
#include <zmk/events/caps_word_state_changed.h>
#include <zmk/caps_word.h>
#endif

#include "lock_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct lock_status_state {
    bool caps_lock;
    bool caps_word;
};

static void set_lock_status_state(struct zmk_widget_lock_status *widget, struct lock_status_state state) {
    if (!widget || !widget->obj || !widget->label) {
        return;
    }

    bool active = state.caps_lock || state.caps_word;

    if (active) {
        lv_color_t accent;
        lv_color_t bg;

        if (state.caps_word && !state.caps_lock) {
            accent = lv_color_hex(0xffb300); /* Neon amber for Caps Word */
            bg = lv_color_hex(0x261a00);
            lv_label_set_text(widget->label, "CW");
        } else {
            accent = lv_color_hex(0x00e5ff); /* Bright cyan for Caps Lock */
            bg = lv_color_hex(0x002633);
            lv_label_set_text(widget->label, "CAPS");
        }

        lv_obj_set_style_bg_color(widget->obj, bg, 0);
        lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(widget->obj, accent, 0);
        lv_obj_set_style_border_width(widget->obj, 2, 0);
        lv_obj_set_style_text_color(widget->label, accent, 0);
        lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_color(widget->obj, lv_color_hex(0x121218), 0);
        lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(widget->obj, lv_color_hex(0x2d3139), 0);
        lv_obj_set_style_border_width(widget->obj, 1, 0);
        lv_obj_set_style_text_color(widget->label, lv_color_hex(0x4a505b), 0);
        lv_label_set_text(widget->label, "CAPS");
    }
}

void lock_status_update_cb(struct lock_status_state state) {
    struct zmk_widget_lock_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_lock_status_state(widget, state);
    }
}

static struct lock_status_state lock_status_get_state(const zmk_event_t *eh) {
    bool caps = false;
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);
    if (ev) {
        caps = (ev->indicators & HID_INDICATOR_CAPS_LOCK) != 0;
    } else {
        caps = (zmk_hid_indicators_get_current_profile() & HID_INDICATOR_CAPS_LOCK) != 0;
    }
#endif

    bool caps_w = false;
#if IS_ENABLED(CONFIG_ZMK_CAPS_WORD)
    const struct zmk_caps_word_state_changed *ev_cw = as_zmk_caps_word_state_changed(eh);
    if (ev_cw) {
        caps_w = ev_cw->state;
    } else {
        caps_w = zmk_caps_word_state();
    }
#endif

    return (struct lock_status_state){
        .caps_lock = caps,
        .caps_word = caps_w,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_lock_status, struct lock_status_state,
                            lock_status_update_cb, lock_status_get_state)

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
ZMK_SUBSCRIPTION(widget_lock_status, zmk_hid_indicators_changed);
#endif

#if IS_ENABLED(CONFIG_ZMK_CAPS_WORD)
ZMK_SUBSCRIPTION(widget_lock_status, zmk_caps_word_state_changed);
#endif

int zmk_widget_lock_status_init(struct zmk_widget_lock_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(widget->obj, 52, 22);
    lv_obj_set_style_radius(widget->obj, 4, 0);
    lv_obj_set_style_pad_all(widget->obj, 0, 0);
    lv_obj_set_style_border_width(widget->obj, 1, 0);

    widget->label = lv_label_create(widget->obj);
    lv_obj_center(widget->label);

    sys_slist_append(&widgets, &widget->node);

    struct lock_status_state initial_state = lock_status_get_state(NULL);
    set_lock_status_state(widget, initial_state);

    widget_lock_status_init();
    return 0;
}

lv_obj_t *zmk_widget_lock_status_obj(struct zmk_widget_lock_status *widget) {
    return widget->obj;
}
