/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>
#include "widgets/layer_badge.h"
#include "menu_controller.h"
#include "layer_inspector.h"
#include "layer_inspector_data.h"

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

#define NUM_INSPECTED_LAYERS 14
#define KEYS_PER_HALF 38

struct category_style {
    uint32_t bg;
    uint32_t border;
    uint32_t text;
};

static const struct category_style cat_styles[KEY_CAT_COUNT] = {
    [KEY_CAT_ALPHA] = {
        .bg = 0x0f172a,      /* Deep Slate/Navy */
        .border = 0x00d2ff,  /* Cyber Cyan */
        .text = 0xe2e8f0,    /* Light Slate */
    },
    [KEY_CAT_MOD] = {
        .bg = 0x241432,      /* Deep Violet */
        .border = 0xa18cd1,  /* Purple Glow */
        .text = 0xf1e6ff,    /* Soft Lilac */
    },
    [KEY_CAT_LAYER] = {
        .bg = 0x0e2a1b,      /* Deep Emerald */
        .border = 0x38ef7d,  /* Neon Green */
        .text = 0xd4ffeb,    /* Mint */
    },
    [KEY_CAT_MEDIA] = {
        .bg = 0x2d1428,      /* Deep Magenta */
        .border = 0xf355da,  /* Neon Pink */
        .text = 0xffd8fa,    /* Rose */
    },
    [KEY_CAT_TRANS] = {
        .bg = 0x080b10,      /* Dark Void */
        .border = 0x1e293b,  /* Muted Slate Border */
        .text = 0x475569,    /* Dim Slate */
    },
};

/* Inspector State */
static uint8_t current_layer = 0;
static bool current_is_left_half = true;
static volatile bool inspector_active = false;

/* LVGL UI Objects */
static lv_obj_t *inspector_container = NULL;
static lv_obj_t *header_title_label = NULL;
static lv_obj_t *header_half_label = NULL;
static lv_obj_t *footer_label = NULL;
static lv_obj_t *cell_objs[KEYS_PER_HALF];
static lv_obj_t *cell_labels[KEYS_PER_HALF];

uint8_t layer_inspector_get_current_layer(void) {
    return current_layer;
}

bool layer_inspector_get_is_left_half(void) {
    return current_is_left_half;
}

lv_obj_t *layer_inspector_get_container(void) {
    return inspector_container;
}

bool layer_inspector_is_active(void) {
    return inspector_active;
}

void layer_inspector_render(void) {
    if (!inspector_container) {
        return;
    }

    /* Runs on zmk_display_work_q(): the only place LVGL objects are mutated. */
    if (!inspector_active) {
        lv_obj_add_flag(inspector_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(inspector_container, LV_OBJ_FLAG_HIDDEN);

    if (current_layer >= NUM_INSPECTED_LAYERS) {
        current_layer = 0;
    }

    /* Update Header Layer Name & Accent Color */
    const char *layer_name = zmk_widget_layer_badge_get_name(current_layer);
    uint32_t layer_color = zmk_widget_layer_badge_get_color(current_layer);
    if (header_title_label) {
        lv_obj_set_style_text_color(header_title_label, lv_color_hex(layer_color), 0);
        lv_label_set_text_fmt(header_title_label, "#%u %s", current_layer, layer_name);
    }

    /* Update Active Half Indicator Banner */
    if (header_half_label) {
        if (current_is_left_half) {
            lv_label_set_text(header_half_label, "< LEFT HALF >");
            lv_obj_set_style_text_color(header_half_label, lv_color_hex(0x00f2fe), 0);
        } else {
            lv_label_set_text(header_half_label, "< RIGHT HALF >");
            lv_obj_set_style_text_color(header_half_label, lv_color_hex(0xff9a44), 0);
        }
    }

    /* Update 38 Key Cells for the Inspected Half */
    for (uint8_t i = 0; i < KEYS_PER_HALF; i++) {
        if (!cell_objs[i] || !cell_labels[i]) {
            continue;
        }

        const struct key_geometry *geom = current_is_left_half ? &left_half_geom[i] : &right_half_geom[i];
        lv_obj_set_pos(cell_objs[i], geom->x, geom->y);
        lv_obj_set_size(cell_objs[i], geom->w, geom->h);

        uint8_t key_idx = current_is_left_half ? i : (i + KEYS_PER_HALF);
        const struct key_cell_info *info = &layer_keys_data[current_layer][key_idx];

        uint8_t cat = info->cat;
        if (cat >= KEY_CAT_COUNT) {
            cat = KEY_CAT_ALPHA;
        }

        const struct category_style *style = &cat_styles[cat];
        lv_obj_set_style_bg_color(cell_objs[i], lv_color_hex(style->bg), 0);
        lv_obj_set_style_border_color(cell_objs[i], lv_color_hex(style->border), 0);
        lv_obj_set_style_text_color(cell_labels[i], lv_color_hex(style->text), 0);
        lv_label_set_text(cell_labels[i], info->label);
    }
}

int layer_inspector_init(lv_obj_t *screen) {
    if (!screen) {
        return -EINVAL;
    }

    /* Subscreen Root Container (280x240 px) */
    inspector_container = lv_obj_create(screen);
    lv_obj_set_size(inspector_container, 280, 240);
    lv_obj_set_pos(inspector_container, 0, 0);
    lv_obj_set_style_bg_color(inspector_container, lv_color_hex(0x0a0d14), 0);
    lv_obj_set_style_bg_opa(inspector_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(inspector_container, lv_color_hex(0x1f293d), 0);
    lv_obj_set_style_border_width(inspector_container, 1, 0);
    lv_obj_set_style_radius(inspector_container, 6, 0);
    lv_obj_set_style_pad_all(inspector_container, 0, 0);
    lv_obj_clear_flag(inspector_container, LV_OBJ_FLAG_SCROLLABLE);

    /* Initially Hidden */
    lv_obj_add_flag(inspector_container, LV_OBJ_FLAG_HIDDEN);

    /* Header Title Label (Top-Left) */
    header_title_label = lv_label_create(inspector_container);
    lv_label_set_text(header_title_label, "#0 WORK");
    lv_obj_set_style_text_color(header_title_label, lv_color_hex(0x00d2ff), 0);
    lv_obj_align(header_title_label, LV_ALIGN_TOP_LEFT, 10, 6);

    /* Active Half Banner Label (Top-Right) */
    header_half_label = lv_label_create(inspector_container);
    lv_label_set_text(header_half_label, "< LEFT HALF >");
    lv_obj_set_style_text_color(header_half_label, lv_color_hex(0x00f2fe), 0);
    lv_obj_align(header_half_label, LV_ALIGN_TOP_RIGHT, -10, 6);

    /* Top Separator Line */
    lv_obj_t *sep_top = lv_obj_create(inspector_container);
    lv_obj_set_size(sep_top, 268, 1);
    lv_obj_set_pos(sep_top, 6, 26);
    lv_obj_set_style_bg_color(sep_top, lv_color_hex(0x1f293d), 0);
    lv_obj_set_style_bg_opa(sep_top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep_top, 0, 0);

    /* 38 Pre-Allocated Key Cells */
    for (uint8_t i = 0; i < KEYS_PER_HALF; i++) {
        cell_objs[i] = lv_obj_create(inspector_container);
        lv_obj_clear_flag(cell_objs[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(cell_objs[i], 0, 0);
        lv_obj_set_style_radius(cell_objs[i], 3, 0);
        lv_obj_set_style_border_width(cell_objs[i], 1, 0);
        lv_obj_set_style_bg_opa(cell_objs[i], LV_OPA_COVER, 0);

        cell_labels[i] = lv_label_create(cell_objs[i]);
        lv_obj_center(cell_labels[i]);
    }

    /* Bottom Separator Line */
    lv_obj_t *sep_bot = lv_obj_create(inspector_container);
    lv_obj_set_size(sep_bot, 268, 1);
    lv_obj_set_pos(sep_bot, 6, 214);
    lv_obj_set_style_bg_color(sep_bot, lv_color_hex(0x1f293d), 0);
    lv_obj_set_style_bg_opa(sep_bot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep_bot, 0, 0);

    /* Navigation Footer Label */
    footer_label = lv_label_create(inspector_container);
    lv_label_set_text(footer_label, "W/S:Layer  A/D:Half  ESC:Menu");
    lv_obj_set_style_text_color(footer_label, lv_color_hex(0x707e94), 0);
    lv_obj_align(footer_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    inspector_active = false;
    layer_inspector_render();
    return 0;
}

int layer_inspector_show(uint8_t layer, bool is_left_half) {
    if (layer >= NUM_INSPECTED_LAYERS) {
        layer = 0;
    }
    current_layer = layer;
    current_is_left_half = is_left_half;
    inspector_active = true;

    /* State only; menu_controller schedules layer_inspector_render() on the display queue. */
    return 0;
}

void layer_inspector_hide(void) {
    /* State only; the next layer_inspector_render() on the display queue hides the container. */
    inspector_active = false;
}

void layer_inspector_handle_nav(enum menu_nav_action action) {
    if (!inspector_active) {
        return;
    }
    switch (action) {
    case MENU_NAV_DEC:
    case MENU_NAV_INC:
    case MENU_NAV_SELECT:
        /* Toggle Left / Right half */
        current_is_left_half = !current_is_left_half;
        break;

    case MENU_NAV_UP:
        /* Cycle layer up (0..13) */
        current_layer = (current_layer + 1) % NUM_INSPECTED_LAYERS;
        break;

    case MENU_NAV_DOWN:
        /* Cycle layer down (13..0) */
        current_layer = (current_layer == 0) ? (NUM_INSPECTED_LAYERS - 1) : (current_layer - 1);
        break;

    case MENU_NAV_EXIT:
        /* Return to Menu screen */
        layer_inspector_hide();
        menu_controller_set_state(SCREEN_STATE_MENU);
        break;
    default:
        break;
    }
}
