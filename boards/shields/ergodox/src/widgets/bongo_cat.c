/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_WPM)
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>
#endif

#include "bongo_cat.h"

static sys_slist_t bongo_cat_widgets = SYS_SLIST_STATIC_INIT(&bongo_cat_widgets);

/* Key Matrix Split Mapping:
 * Left keys (38 keys): 0..6, 14..20, 28..34, 42..47, 54..58, 64..65, 68..70, 74
 * Right keys (38 keys): 7..13, 21..27, 35..41, 48..53, 59..63, 66..67, 71..73, 75
 */
static inline bool is_right_key(uint32_t pos) {
    if (pos >= 76) return false;
    static const bool right_keys[76] = {
        /* Row 0: 0..6 (L), 7..13 (R) */
        0, 0, 0, 0, 0, 0, 0,  1, 1, 1, 1, 1, 1, 1,
        /* Row 1: 14..20 (L), 21..27 (R) */
        0, 0, 0, 0, 0, 0, 0,  1, 1, 1, 1, 1, 1, 1,
        /* Row 2: 28..34 (L), 35..41 (R) */
        0, 0, 0, 0, 0, 0, 0,  1, 1, 1, 1, 1, 1, 1,
        /* Row 3: 42..47 (L), 48..53 (R) */
        0, 0, 0, 0, 0, 0,     1, 1, 1, 1, 1, 1,
        /* Row 4: 54..58 (L), 59..63 (R) */
        0, 0, 0, 0, 0,        1, 1, 1, 1, 1,
        /* Thumbs: 64,65 (L), 66,67 (R) */
        0, 0, 1, 1,
        /* Thumbs: 68..70 (L), 71..73 (R) */
        0, 0, 0, 1, 1, 1,
        /* Thumbs: 74 (L), 75 (R) */
        0, 1
    };
    return right_keys[pos];
}

static bool paw_l_down = false;
static bool paw_r_down = false;
static uint8_t bongo_wpm = 0;

static struct k_work update_paws_work;
static struct k_work_delayable lift_paw_l_work;
static struct k_work_delayable lift_paw_r_work;

static const char *get_cat_face(uint8_t wpm) {
    if (wpm < 30) {
        return " /\\_/\\ \n( o.o )"; /* Low (<30): Normal cute face */
    } else if (wpm <= 70) {
        return " /\\_/\\ \n( ^.^ )"; /* Mid (30..70): Excited eyes */
    } else {
        return " /\\_/\\ \n( B-) )"; /* High (>70): Sunglasses / cool face */
    }
}

static lv_color_t get_face_color(uint8_t wpm) {
    if (wpm < 30) {
        return lv_color_hex(0x00d2ff); /* Cyber Cyan */
    } else if (wpm <= 70) {
        return lv_color_hex(0x76ff03); /* Lime Green */
    } else {
        return lv_color_hex(0xff9100); /* Neon Amber / Orange */
    }
}

static void render_bongo_cat(struct zmk_widget_bongo_cat *widget) {
    if (!widget || !widget->obj || !widget->head_label) {
        return;
    }

    /* Face and Head */
    lv_label_set_text(widget->head_label, get_cat_face(bongo_wpm));
    lv_obj_set_style_text_color(widget->head_label, get_face_color(bongo_wpm), 0);

    /* Left Paw */
    if (widget->paw_l) {
        if (paw_l_down) {
            lv_label_set_text(widget->paw_l, "o");
            lv_obj_align(widget->paw_l, LV_ALIGN_BOTTOM_LEFT, 20, -5);
            lv_obj_set_style_text_color(widget->paw_l, lv_color_hex(0x38ef7d), 0);
        } else {
            lv_label_set_text(widget->paw_l, "/");
            lv_obj_align(widget->paw_l, LV_ALIGN_BOTTOM_LEFT, 16, -16);
            lv_obj_set_style_text_color(widget->paw_l, lv_color_hex(0x9aa0a6), 0);
        }
    }

    /* Right Paw */
    if (widget->paw_r) {
        if (paw_r_down) {
            lv_label_set_text(widget->paw_r, "o");
            lv_obj_align(widget->paw_r, LV_ALIGN_BOTTOM_RIGHT, -20, -5);
            lv_obj_set_style_text_color(widget->paw_r, lv_color_hex(0x38ef7d), 0);
        } else {
            lv_label_set_text(widget->paw_r, "\\");
            lv_obj_align(widget->paw_r, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
            lv_obj_set_style_text_color(widget->paw_r, lv_color_hex(0x9aa0a6), 0);
        }
    }

    /* Desk */
    if (widget->desk) {
        lv_color_t desk_col = (paw_l_down || paw_r_down) ? lv_color_hex(0x38ef7d) : lv_color_hex(0x2d3139);
        lv_obj_set_style_bg_color(widget->desk, desk_col, 0);
    }

    /* Container dynamic border */
    bool active = (paw_l_down || paw_r_down);
    lv_obj_set_style_border_color(widget->obj, active ? lv_color_hex(0x00d2ff) : lv_color_hex(0x2d3139), 0);
    lv_obj_set_style_border_width(widget->obj, active ? 2 : 1, 0);
}

static void update_all_bongo_cats(void) {
    struct zmk_widget_bongo_cat *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&bongo_cat_widgets, widget, node) {
        render_bongo_cat(widget);
    }
}

static void bongo_cat_update_paws_handler(struct k_work *work) {
    ARG_UNUSED(work);
    update_all_bongo_cats();
}

static void bongo_cat_lift_l_handler(struct k_work *work) {
    ARG_UNUSED(work);
    paw_l_down = false;
    update_all_bongo_cats();
}

static void bongo_cat_lift_r_handler(struct k_work *work) {
    ARG_UNUSED(work);
    paw_r_down = false;
    update_all_bongo_cats();
}

struct bongo_cat_state {
    uint8_t wpm;
};

void bongo_cat_wpm_update_cb(struct bongo_cat_state state) {
    bongo_wpm = state.wpm;
    update_all_bongo_cats();
}

static struct bongo_cat_state bongo_cat_get_state(const zmk_event_t *eh) {
    uint8_t wpm = 0;
#if IS_ENABLED(CONFIG_ZMK_WPM)
    const struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    if (ev) {
        wpm = ev->state;
    } else {
        wpm = zmk_wpm_get_state();
    }
#endif
    return (struct bongo_cat_state){ .wpm = wpm };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_bongo_cat, struct bongo_cat_state,
                            bongo_cat_wpm_update_cb, bongo_cat_get_state)

#if IS_ENABLED(CONFIG_ZMK_WPM)
ZMK_SUBSCRIPTION(widget_bongo_cat, zmk_wpm_state_changed);
#endif

static int position_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state) {
        return 0;
    }

    if (is_right_key(ev->position)) {
        paw_r_down = true;
        k_work_reschedule_for_queue(zmk_display_work_q(), &lift_paw_r_work, K_MSEC(150));
    } else {
        paw_l_down = true;
        k_work_reschedule_for_queue(zmk_display_work_q(), &lift_paw_l_work, K_MSEC(150));
    }

    k_work_submit_to_queue(zmk_display_work_q(), &update_paws_work);
    return 0;
}

ZMK_LISTENER(bongo_cat_key_tracker, position_state_changed_listener);
ZMK_SUBSCRIPTION(bongo_cat_key_tracker, zmk_position_state_changed);

int zmk_widget_bongo_cat_init(struct zmk_widget_bongo_cat *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(widget->obj, 106, 52);
    lv_obj_set_style_radius(widget->obj, 6, 0);
    lv_obj_set_style_bg_color(widget->obj, lv_color_hex(0x12151c), 0);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(widget->obj, 2, 0);
    lv_obj_set_style_border_width(widget->obj, 1, 0);
    lv_obj_set_style_border_color(widget->obj, lv_color_hex(0x2d3139), 0);

    widget->canvas = NULL;

    /* Head & Face label */
    widget->head_label = lv_label_create(widget->obj);
    lv_obj_align(widget->head_label, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_text_color(widget->head_label, lv_color_hex(0x00d2ff), 0);

    /* Desk representation */
    widget->desk = lv_obj_create(widget->obj);
    lv_obj_set_size(widget->desk, 76, 3);
    lv_obj_align(widget->desk, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(widget->desk, lv_color_hex(0x2d3139), 0);
    lv_obj_set_style_bg_opa(widget->desk, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(widget->desk, 0, 0);
    lv_obj_set_style_radius(widget->desk, 1, 0);

    /* Left and Right Paws */
    widget->paw_l = lv_label_create(widget->obj);
    lv_obj_set_style_text_color(widget->paw_l, lv_color_hex(0x9aa0a6), 0);
    lv_obj_align(widget->paw_l, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    lv_label_set_text(widget->paw_l, "/");

    widget->paw_r = lv_label_create(widget->obj);
    lv_obj_set_style_text_color(widget->paw_r, lv_color_hex(0x9aa0a6), 0);
    lv_obj_align(widget->paw_r, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    lv_label_set_text(widget->paw_r, "\\");

    static bool work_inited = false;
    if (!work_inited) {
        k_work_init(&update_paws_work, bongo_cat_update_paws_handler);
        k_work_init_delayable(&lift_paw_l_work, bongo_cat_lift_l_handler);
        k_work_init_delayable(&lift_paw_r_work, bongo_cat_lift_r_handler);
        work_inited = true;
    }

    sys_slist_append(&bongo_cat_widgets, &widget->node);

    struct bongo_cat_state initial_state = bongo_cat_get_state(NULL);
    bongo_wpm = initial_state.wpm;
    render_bongo_cat(widget);

    widget_bongo_cat_init();
    return 0;
}

lv_obj_t *zmk_widget_bongo_cat_obj(struct zmk_widget_bongo_cat *widget) {
    return widget->obj;
}
