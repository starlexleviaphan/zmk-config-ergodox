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
#include "custom_status_screen.h"

static sys_slist_t wpm_gauge_widgets = SYS_SLIST_STATIC_INIT(&wpm_gauge_widgets);
static uint8_t current_theme_id = 0;

struct wpm_gauge_state {
    uint8_t wpm;
};

uint32_t zmk_widget_wpm_gauge_get_color(uint8_t wpm) {
    bool is_light = (current_theme_id >= 4);
    if (wpm < 45) {
        return is_light ? 0x0284c7 : 0x00d2ff; /* Cyan / Sky Blue (<45) */
    } else if (wpm <= 80) {
        return is_light ? 0x16a34a : 0x76ff03; /* Green / Lime (45..80) */
    } else if (wpm <= 110) {
        return is_light ? 0xd97706 : 0xff9100; /* Amber / Orange (81..110) */
    } else {
        return is_light ? 0xdc2626 : 0xff1744; /* Crimson Red (>110) */
    }
}

uint8_t zmk_widget_wpm_gauge_smooth(uint8_t current_smooth, uint8_t raw_wpm) {
    if (raw_wpm == 0) {
        return (current_smooth > 3) ? (current_smooth - 3) : 0;
    }
    return (uint8_t)((raw_wpm * 1 + current_smooth * 3) / 4);
}

static void set_wpm_gauge_state(struct zmk_widget_wpm_gauge *widget, struct wpm_gauge_state state) {
    if (!widget || !widget->obj || !widget->arc || !widget->label) {
        return;
    }

    static uint8_t smooth_wpm = 0;
    smooth_wpm = zmk_widget_wpm_gauge_smooth(smooth_wpm, state.wpm);
    widget->smooth_wpm = smooth_wpm;

    uint8_t clamped = (smooth_wpm > 120) ? 120 : smooth_wpm;
    uint32_t color_hex = zmk_widget_wpm_gauge_get_color(smooth_wpm);
    lv_color_t accent = lv_color_hex(color_hex);

    /* Update gauge arc value & indicator color */
    lv_arc_set_value(widget->arc, clamped);
    lv_obj_set_style_arc_color(widget->arc, accent, LV_PART_INDICATOR);

    /* Update arc track color for current theme */
    bool is_light = (current_theme_id >= 4);
    lv_color_t track_col = is_light ? lv_color_hex(0xcfd8dc) : lv_color_hex(0x222630);
    lv_obj_set_style_arc_color(widget->arc, track_col, LV_PART_MAIN);

    /* Update dynamic text readout & glow */
    lv_label_set_text_fmt(widget->label, "WPM: %u", (unsigned int)smooth_wpm);
    lv_obj_set_style_text_color(widget->label, accent, 0);

    /* Container accent border */
    const struct theme_palette *pal = zmk_display_get_theme_palette(current_theme_id);
    lv_color_t border_col = (smooth_wpm > 0) ? accent : (pal ? lv_color_hex(pal->border) : lv_color_hex(0x1f293d));
    lv_obj_set_style_border_color(widget->obj, border_col, 0);
    lv_obj_set_style_border_width(widget->obj, (smooth_wpm > 0) ? 2 : 1, 0);
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
    lv_obj_set_size(widget->obj, 130, 68);
    lv_obj_set_style_radius(widget->obj, 6, 0);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(widget->obj, 4, 0);
    lv_obj_set_style_border_width(widget->obj, 1, 0);

    /* Arc Speedometer Gauge (0..120 WPM, size 38x38 with 10px clear margin to text) */
    widget->arc = lv_arc_create(widget->obj);
    lv_obj_set_size(widget->arc, 38, 38);
    lv_arc_set_rotation(widget->arc, 135);
    lv_arc_set_bg_angles(widget->arc, 0, 270);
    lv_arc_set_range(widget->arc, 0, 120);
    lv_arc_set_value(widget->arc, 0);
    lv_obj_remove_style(widget->arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(widget->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(widget->arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(widget->arc, 4, LV_PART_INDICATOR);
    lv_obj_align(widget->arc, LV_ALIGN_LEFT_MID, 8, 0);

    /* WPM Text Readout with Montserrat 12 (compact, zero overlap) */
    widget->label = lv_label_create(widget->obj);
#if IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_12)
    lv_obj_set_style_text_font(widget->label, &lv_font_montserrat_12, 0);
#endif
    lv_obj_align(widget->label, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_label_set_text(widget->label, "WPM: 0");

    widget->smooth_wpm = 0;
    sys_slist_append(&wpm_gauge_widgets, &widget->node);

    struct wpm_gauge_state initial_state = wpm_gauge_get_state(NULL);
    set_wpm_gauge_state(widget, initial_state);

    widget_wpm_gauge_init();
    return 0;
}

void zmk_widget_wpm_gauge_apply_theme(uint8_t theme_id) {
    current_theme_id = theme_id;
    struct zmk_widget_wpm_gauge *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&wpm_gauge_widgets, widget, node) {
        struct wpm_gauge_state cur = { .wpm = widget->smooth_wpm };
        set_wpm_gauge_state(widget, cur);
    }
}

lv_obj_t *zmk_widget_wpm_gauge_obj(struct zmk_widget_wpm_gauge *widget) {
    return widget->obj;
}
