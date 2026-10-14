/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/events/layer_state_changed.h>

#include "layer_badge.h"
#include "custom_status_screen.h"

#define NUM_LAYERS 14
static uint8_t current_theme_id = 0;

struct layer_info {
    const char *name;
    const char *desc;
    uint32_t color_hex;
};

static const struct layer_info layer_table[NUM_LAYERS] = {
    [0]  = { .name = "WORK", .desc = "Work / Home Row Mods",    .color_hex = 0x00d2ff },
    [1]  = { .name = "SWAP", .desc = "Left Mirror Alphas",      .color_hex = 0x38ef7d },
    [2]  = { .name = "LNPD", .desc = "Left Numpad Cluster",     .color_hex = 0xf9d423 },
    [3]  = { .name = "TYPE", .desc = "Typist (No HRM)",         .color_hex = 0x00f2fe },
    [4]  = { .name = "SYM1", .desc = "Symbols & Hotkeys",       .color_hex = 0xa18cd1 },
    [5]  = { .name = "NRPD", .desc = "Right Numpad Cluster",    .color_hex = 0xff9a44 },
    [6]  = { .name = "MOUS", .desc = "Mouse & Menu OSD",        .color_hex = 0x4facfe },
    [7]  = { .name = "ARRW", .desc = "Navigation & Arrows",     .color_hex = 0x43e97b },
    [8]  = { .name = "GAME", .desc = "WASD Gaming (0ms)",       .color_hex = 0xff0844 },
    [9]  = { .name = "GSWP", .desc = "Gaming Swap Layer",       .color_hex = 0xf857a6 },
    [10] = { .name = "NPAD", .desc = "3x4 Numeric Keypad",      .color_hex = 0xf6d365 },
    [11] = { .name = "FUN1", .desc = "Function Keys F1-F12",    .color_hex = 0xb721ff },
    [12] = { .name = "FUN2", .desc = "Extended Keys F13-F24",   .color_hex = 0x6f86d6 },
    [13] = { .name = "SYM2", .desc = "Dongle & Bluetooth",      .color_hex = 0xf355da },
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct layer_badge_state {
    uint8_t layer;
};

uint32_t zmk_widget_layer_badge_get_color(uint8_t layer) {
    if (layer >= NUM_LAYERS) {
        layer = 0;
    }
    return layer_table[layer].color_hex;
}

const char *zmk_widget_layer_badge_get_name(uint8_t layer) {
    if (layer >= NUM_LAYERS) {
        layer = 0;
    }
    return layer_table[layer].name;
}

const char *zmk_widget_layer_badge_get_desc(uint8_t layer) {
    if (layer >= NUM_LAYERS) {
        layer = 0;
    }
    return layer_table[layer].desc;
}

static void set_layer_badge_state(struct zmk_widget_layer_badge *widget, struct layer_badge_state state) {
    if (!widget || !widget->obj || !widget->label) {
        return;
    }

    uint8_t layer = state.layer;
    if (layer >= NUM_LAYERS) {
        layer = 0;
    }

    const struct layer_info *info = &layer_table[layer];
    bool is_light = (current_theme_id >= 4);
    uint32_t hex = info->color_hex;

    /* High contrast color adjustments for crisp rendering on light themes */
    if (is_light) {
        if (hex == 0x00d2ff || hex == 0x00f2fe || hex == 0x4facfe) {
            hex = 0x0284c7; /* Sky / Deep Blue */
        } else if (hex == 0x38ef7d || hex == 0x43e97b) {
            hex = 0x16a34a; /* Forest Green */
        } else if (hex == 0xf9d423 || hex == 0xf6d365) {
            hex = 0xd97706; /* Amber */
        }
    }
    lv_color_t accent = lv_color_hex(hex);

    /* Dynamic accent border on container */
    lv_obj_set_style_border_color(widget->obj, accent, 0);
    lv_obj_set_style_border_width(widget->obj, 2, 0);

    /* Layer name styled with accent color */
    lv_obj_set_style_text_color(widget->label, accent, 0);
    lv_label_set_text_fmt(widget->label, "#%u %s", layer, info->name);
}

void layer_badge_update_cb(struct layer_badge_state state) {
    struct zmk_widget_layer_badge *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_layer_badge_state(widget, state);
    }
}

void zmk_widget_layer_badge_apply_theme(uint8_t theme_id) {
    current_theme_id = theme_id;
    struct zmk_widget_layer_badge *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        struct layer_badge_state st = { .layer = zmk_keymap_highest_layer_active() };
        set_layer_badge_state(widget, st);
    }
}

static struct layer_badge_state layer_badge_get_state(const zmk_event_t *eh) {
    return (struct layer_badge_state){
        .layer = zmk_keymap_highest_layer_active(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_badge, struct layer_badge_state,
                            layer_badge_update_cb, layer_badge_get_state)

ZMK_SUBSCRIPTION(widget_layer_badge, zmk_layer_state_changed);

int zmk_widget_layer_badge_init(struct zmk_widget_layer_badge *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(widget->obj, 130, 60);
    lv_obj_set_style_radius(widget->obj, 6, 0);
    lv_obj_set_style_bg_color(widget->obj, lv_color_hex(0x0a0d14), 0);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(widget->obj, 4, 0);
    lv_obj_set_style_border_width(widget->obj, 2, 0);

    widget->label = lv_label_create(widget->obj);
    lv_obj_center(widget->label);
    widget->desc_label = NULL;

    sys_slist_append(&widgets, &widget->node);

    struct layer_badge_state initial_state = layer_badge_get_state(NULL);
    set_layer_badge_state(widget, initial_state);

    widget_layer_badge_init();
    return 0;
}

lv_obj_t *zmk_widget_layer_badge_obj(struct zmk_widget_layer_badge *widget) {
    return widget->obj;
}
