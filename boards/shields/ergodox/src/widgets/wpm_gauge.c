/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>

#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>
#endif

#include "wpm_gauge.h"

static sys_slist_t wpm_gauge_widgets = SYS_SLIST_STATIC_INIT(&wpm_gauge_widgets);

struct wpm_gauge_state {
    uint8_t wpm;
};

uint32_t zmk_widget_wpm_gauge_get_color(uint8_t wpm) {
    if (wpm < 45) {
        return 0x00d2ff; /* Cyan (<45) */
    } else if (wpm <= 80) {
        return 0x76ff03; /* Lime Green (45..80) */
    } else if (wpm <= 110) {
        return 0xff9100; /* Orange (81..110) */
    } else {
        return 0xff1744; /* Crimson Red (>110) */
    }
}

static void set_wpm_gauge_state(struct zmk_widget_wpm_gauge *widget, struct wpm_gauge_state state) {
    if (!widget || !widget->obj || !widget->arc || !widget->label) {
        return;
    }

    uint8_t wpm = state.wpm;
    uint8_t clamped = (wpm > 120) ? 120 : wpm;
    uint32_t color_hex = zmk_widget_wpm_gauge_get_color(wpm);
    lv_color_t accent = lv_color_hex(color_hex);

    /* Update gauge arc value & color */
    lv_arc_set_value(widget->arc, clamped);
    lv_obj_set_style_arc_color(widget->arc, accent, LV_PART_INDICATOR);

    /* Update dynamic text readout & glow */
    lv_label_set_text_fmt(widget->label, "WPM: %u", (unsigned int)wpm);
    lv_obj_set_style_text_color(widget->label, accent, 0);

    /* Container accent border */
    lv_obj_set_style_border_color(widget->obj, accent, 0);
    lv_obj_set_style_border_width(widget->obj, (wpm > 0) ? 2 : 1, 0);
}

void wpm_gauge_update_cb(struct wpm_gauge_state state) {
    struct zmk_widget_wpm_gauge *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&wpm_gauge_widgets, widget, node) {
        set_wpm_gauge_state(widget, state);
    }
}

static struct wpm_gauge_state wpm_gauge_get_state(const zmk_event_t *eh) {
    uint8_t wpm = 0;
#if IS_ENABLED(CONFIG_ZMK_WPM)
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    if (ev) {
        wpm = ev->state;
    } else {
        wpm = zmk_wpm_get_state();
    }
#endif
    return (struct wpm_gauge_state){ .wpm = wpm };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_gauge, struct wpm_gauge_state,
                            wpm_gauge_update_cb, wpm_gauge_get_state)

#if IS_ENABLED(CONFIG_ZMK_WPM)
ZMK_SUBSCRIPTION(widget_wpm_gauge, zmk_wpm_state_changed);
#endif

int zmk_widget_wpm_gauge_init(struct zmk_widget_wpm_gauge *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(widget->obj, 106, 52);
    lv_obj_set_style_radius(widget->obj, 6, 0);
    lv_obj_set_style_bg_color(widget->obj, lv_color_hex(0x12151c), 0);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(widget->obj, 4, 0);
    lv_obj_set_style_border_width(widget->obj, 1, 0);
    lv_obj_set_style_border_color(widget->obj, lv_color_hex(0x2d3139), 0);

    /* Arc Speedometer Gauge (0..120 WPM) */
    widget->arc = lv_arc_create(widget->obj);
    lv_obj_set_size(widget->arc, 44, 44);
    lv_arc_set_rotation(widget->arc, 135);
    lv_arc_set_bg_angles(widget->arc, 0, 270);
    lv_arc_set_range(widget->arc, 0, 120);
    lv_arc_set_value(widget->arc, 0);
    lv_obj_remove_style(widget->arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(widget->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(widget->arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(widget->arc, lv_color_hex(0x222630), LV_PART_MAIN);
    lv_obj_set_style_arc_width(widget->arc, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(widget->arc, lv_color_hex(0x00d2ff), LV_PART_INDICATOR);
    lv_obj_align(widget->arc, LV_ALIGN_LEFT_MID, 2, 0);

    /* WPM Text Readout */
    widget->label = lv_label_create(widget->obj);
    lv_obj_set_style_text_color(widget->label, lv_color_hex(0x00d2ff), 0);
    lv_obj_align(widget->label, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_label_set_text(widget->label, "WPM: 0");

    sys_slist_append(&wpm_gauge_widgets, &widget->node);

    struct wpm_gauge_state initial_state = wpm_gauge_get_state(NULL);
    set_wpm_gauge_state(widget, initial_state);

    widget_wpm_gauge_init();
    return 0;
}

lv_obj_t *zmk_widget_wpm_gauge_obj(struct zmk_widget_wpm_gauge *widget) {
    return widget->obj;
}
