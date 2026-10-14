/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include "custom_status_screen.h"
#include "widgets/layer_badge.h"
#include "menu_controller.h"
#include "layer_inspector.h"
#include "layer_inspector_data.h"

#if IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_14)
LV_FONT_DECLARE(lv_font_montserrat_14);
#endif
#if IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_12)
LV_FONT_DECLARE(lv_font_montserrat_12);
#endif
#if IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_10)
LV_FONT_DECLARE(lv_font_montserrat_10);
#endif

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

#define NUM_INSPECTED_LAYERS 15
#define KEYS_PER_HALF 38
#define ZOOM_KEY_WIDTH 44
#define ZOOM_KEY_HEIGHT 38

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

/* Maps physical matrix key position (0..75) to layer_keys_data index (0..37 Left, 38..75 Right) */
static const uint8_t matrix_pos_to_data_idx[76] = {
    /* Row 0 Left (0..6) -> 0..6 */
    0, 1, 2, 3, 4, 5, 6,
    /* Row 0 Right (7..13) -> 38..44 */
    38, 39, 40, 41, 42, 43, 44,

    /* Row 1 Left (14..20) -> 7..13 */
    7, 8, 9, 10, 11, 12, 13,
    /* Row 1 Right (21..27) -> 45..51 */
    45, 46, 47, 48, 49, 50, 51,

    /* Row 2 Left (28..34) -> 14..20 */
    14, 15, 16, 17, 18, 19, 20,
    /* Row 2 Right (35..41) -> 52..58 */
    52, 53, 54, 55, 56, 57, 58,

    /* Row 3 Left (42..47) -> 21..26 */
    21, 22, 23, 24, 25, 26,
    /* Row 3 Right (48..53) -> 59..64 */
    59, 60, 61, 62, 63, 64,

    /* Row 4 Left (54..58) -> 27..31 */
    27, 28, 29, 30, 31,
    /* Row 4 Right (59..63) -> 65..69 */
    65, 66, 67, 68, 69,

    /* Thumbs Left: 64, 65 -> 32, 33 */
    32, 33,
    /* Thumbs Right: 66, 67 -> 70, 71 */
    70, 71,

    /* Thumbs Left: 68..70 -> 34..36 */
    34, 35, 36,
    /* Thumbs Right: 71..73 -> 72..74 */
    72, 73, 74,

    /* Thumbs Left: 74 -> 37 */
    37,
    /* Thumbs Right: 75 -> 75 */
    75
};

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

static const struct category_style cat_styles_light[KEY_CAT_COUNT] = {
    [KEY_CAT_ALPHA] = {
        .bg = 0xffffff,      /* Crisp White */
        .border = 0x0284c7,  /* Sky Blue */
        .text = 0x0f172a,    /* Dark Slate */
    },
    [KEY_CAT_MOD] = {
        .bg = 0xf5f3ff,      /* Soft Purple */
        .border = 0x8b5cf6,  /* Violet */
        .text = 0x5b21b6,    /* Deep Violet */
    },
    [KEY_CAT_LAYER] = {
        .bg = 0xf0fdf4,      /* Soft Green */
        .border = 0x22c55e,  /* Emerald */
        .text = 0x15803d,    /* Deep Green */
    },
    [KEY_CAT_MEDIA] = {
        .bg = 0xfdf2f8,      /* Soft Rose */
        .border = 0xec4899,  /* Rose Pink */
        .text = 0x9d174d,    /* Deep Rose */
    },
    [KEY_CAT_TRANS] = {
        .bg = 0xe2e8f0,      /* Light Gray Void */
        .border = 0xcfd8dc,  /* Subtle Outline */
        .text = 0x94a3b8,    /* Dim Muted */
    },
};

/* Inspector State */
static uint8_t current_layer = 0;
static bool current_is_left_half = true;
static volatile bool inspector_active = false;
static bool is_dashboard_mode = false;

/* Render State Cache */
static int rendered_layer = -1;
static int rendered_is_left = -1;
static int rendered_dashboard_mode = -1;

/* LVGL UI Objects */
static lv_obj_t *inspector_container = NULL;
static lv_obj_t *header_title_label = NULL;
static lv_obj_t *header_half_label = NULL;
static lv_obj_t *sep_top_obj = NULL;
static lv_obj_t *sep_bot_obj = NULL;
static lv_obj_t *footer_label = NULL;
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

uint8_t layer_inspector_get_active_key_count(uint8_t layer, bool is_left_half) {
    if (layer >= NUM_INSPECTED_LAYERS) {
        return 0;
    }
    uint8_t active = 0;
    uint8_t offset = is_left_half ? 0 : KEYS_PER_HALF;
    for (uint8_t i = 0; i < KEYS_PER_HALF; i++) {
        if (layer_keys_data[layer][offset + i].cat != KEY_CAT_TRANS) {
            active++;
        }
    }
    return active;
}

bool is_partial_layer(uint8_t layer, bool is_left_half) {
    if (layer >= NUM_INSPECTED_LAYERS) {
        return false;
    }

    /* Full layout layers always render full 38-key zero-gap layout */
    if (layer == 0 /* WORK */ || layer == 1 /* SWAP */ || layer == 3 /* TYPE */ ||
        layer == 4 /* SYM1 */ || layer == 9 /* GSWP */ || layer == 13 /* SYM2 */ ||
        layer == 14 /* MENU */) {
        return false;
    }

    uint8_t active = layer_inspector_get_active_key_count(layer, is_left_half);
    if (active == 0) {
        return false;
    }

    /* Inspected half has <= 16 active keys */
    if (active <= 16) {
        return true;
    }

    /* Designated partial functional layers */
    if (layer == 2 /* LNPD */ || layer == 5 /* NRPD */ || layer == 6 /* MOUS */ ||
        layer == 7 /* ARRW */ || layer == 8 /* GAME */ || layer == 10 /* NPAD */ ||
        layer == 11 /* FUN1 */ || layer == 12 /* FUN2 */) {
        return true;
    }

    return false;
}

static lv_obj_t *saved_parent_screen = NULL;

static int layer_inspector_build_ui(lv_obj_t *screen) {
    if (!screen || inspector_container) {
        return 0;
    }

    /* Subscreen Root Container (280x240 px) */
    inspector_container = lv_obj_create(screen);
    if (!inspector_container) {
        LOG_ERR("Failed to create layer inspector container");
        return -ENOMEM;
    }
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
    if (header_title_label) {
        lv_label_set_text(header_title_label, "#0 WORK");
        lv_obj_set_style_text_color(header_title_label, lv_color_hex(0x00d2ff), 0);
        lv_obj_align(header_title_label, LV_ALIGN_TOP_LEFT, 10, 6);
    }

    /* Active Half Banner Label (Top-Right) */
    header_half_label = lv_label_create(inspector_container);
    if (header_half_label) {
        lv_label_set_text(header_half_label, "< LEFT >");
        lv_obj_set_style_text_color(header_half_label, lv_color_hex(0x00f2fe), 0);
        lv_obj_align(header_half_label, LV_ALIGN_TOP_RIGHT, -10, 6);
    }

    /* Top Separator Line */
    sep_top_obj = lv_obj_create(inspector_container);
    if (sep_top_obj) {
        lv_obj_set_size(sep_top_obj, 268, 1);
        lv_obj_set_pos(sep_top_obj, 6, 26);
        lv_obj_set_style_bg_color(sep_top_obj, lv_color_hex(0x1f293d), 0);
        lv_obj_set_style_bg_opa(sep_top_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sep_top_obj, 0, 0);
    }

    /* 38 Pre-Allocated Key Cell Labels (Single-object lightweight design) */
    for (uint8_t i = 0; i < KEYS_PER_HALF; i++) {
        cell_labels[i] = lv_label_create(inspector_container);
        if (!cell_labels[i]) continue;
        lv_obj_clear_flag(cell_labels[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_label_set_long_mode(cell_labels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(cell_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_all(cell_labels[i], 0, 0);
        lv_obj_set_style_pad_top(cell_labels[i], 5, 0);
        lv_obj_set_style_radius(cell_labels[i], 2, 0);
        lv_obj_set_style_border_width(cell_labels[i], 1, 0);
        lv_obj_set_style_bg_opa(cell_labels[i], LV_OPA_COVER, 0);
#if IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_12)
        lv_obj_set_style_text_font(cell_labels[i], &lv_font_montserrat_12, 0);
#endif
    }

    /* Bottom Separator Line */
    sep_bot_obj = lv_obj_create(inspector_container);
    if (sep_bot_obj) {
        lv_obj_set_size(sep_bot_obj, 268, 1);
        lv_obj_set_pos(sep_bot_obj, 6, 202);
        lv_obj_set_style_bg_color(sep_bot_obj, lv_color_hex(0x1f293d), 0);
        lv_obj_set_style_bg_opa(sep_bot_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sep_bot_obj, 0, 0);
    }

    /* Navigation Footer Label */
    footer_label = lv_label_create(inspector_container);
    if (footer_label) {
        lv_label_set_text(footer_label, "W/S:Layer  A/D:Half  ESC:Back");
        lv_obj_set_style_text_color(footer_label, lv_color_hex(0x707e94), 0);
        lv_obj_align(footer_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    }

    return 0;
}

static void layer_inspector_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(layer_inspector_delay_work, layer_inspector_work_handler);

static void layer_inspector_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    layer_inspector_render();
}

int layer_inspector_init(lv_obj_t *screen) {
    saved_parent_screen = screen;
    inspector_active = false;
    is_dashboard_mode = false;
    rendered_layer = -1;
    rendered_is_left = -1;
    rendered_dashboard_mode = -1;
    return 0;
}

void layer_inspector_render(void) {
    /* Runs on zmk_display_work_q(): the only place LVGL objects are mutated. */
    if (!inspector_active) {
        if (inspector_container) {
            lv_obj_add_flag(inspector_container, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    if (current_layer >= NUM_INSPECTED_LAYERS) {
        current_layer = 0;
    }

    /* Early exit if render state has not changed (0 ms no-op for rapid events) */
    if (rendered_layer == current_layer &&
        rendered_is_left == current_is_left_half &&
        rendered_dashboard_mode == is_dashboard_mode) {
        return;
    }

    if (!inspector_container) {
        lv_obj_t *scr = saved_parent_screen ? saved_parent_screen : lv_scr_act();
        if (scr) {
            layer_inspector_build_ui(scr);
        }
    }

    if (!inspector_container) {
        return;
    }

    lv_obj_clear_flag(inspector_container, LV_OBJ_FLAG_HIDDEN);

    /* Apply active theme to inspector container and separators */
    const struct theme_palette *pal = zmk_display_get_theme_palette(get_active_theme());

    if (is_dashboard_mode) {
        /* Transparent overlay on dashboard: hide modal borders and navigations */
        lv_obj_set_style_bg_opa(inspector_container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(inspector_container, 0, 0);
        if (sep_top_obj) lv_obj_add_flag(sep_top_obj, LV_OBJ_FLAG_HIDDEN);
        if (sep_bot_obj) lv_obj_add_flag(sep_bot_obj, LV_OBJ_FLAG_HIDDEN);
        if (footer_label) lv_obj_add_flag(footer_label, LV_OBJ_FLAG_HIDDEN);
        if (header_title_label) {
            lv_obj_set_pos(header_title_label, 10, 36);
        }
        if (header_half_label) {
            lv_obj_align(header_half_label, LV_ALIGN_TOP_RIGHT, -10, 36);
        }
    } else {
        /* Fullscreen modal menu inspector */
        lv_obj_set_style_bg_opa(inspector_container, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(inspector_container, 1, 0);
        if (pal) {
            lv_obj_set_style_bg_color(inspector_container, lv_color_hex(pal->screen_bg), 0);
            lv_obj_set_style_border_color(inspector_container, lv_color_hex(pal->border), 0);
            if (sep_top_obj) {
                lv_obj_set_style_bg_color(sep_top_obj, lv_color_hex(pal->border), 0);
            }
            if (sep_bot_obj) {
                lv_obj_set_style_bg_color(sep_bot_obj, lv_color_hex(pal->border), 0);
            }
            if (footer_label) {
                lv_obj_set_style_text_color(footer_label, lv_color_hex(pal->text_unsel), 0);
            }
        }
        if (sep_top_obj) lv_obj_clear_flag(sep_top_obj, LV_OBJ_FLAG_HIDDEN);
        if (sep_bot_obj) lv_obj_clear_flag(sep_bot_obj, LV_OBJ_FLAG_HIDDEN);
        if (footer_label) lv_obj_clear_flag(footer_label, LV_OBJ_FLAG_HIDDEN);
        if (header_title_label) {
            lv_obj_set_pos(header_title_label, 10, 6);
        }
        if (header_half_label) {
            lv_obj_align(header_half_label, LV_ALIGN_TOP_RIGHT, -10, 6);
        }
    }

    if (current_layer >= NUM_INSPECTED_LAYERS) {
        current_layer = 0;
    }

    bool is_light = (get_active_theme() >= 4);
    bool is_zoom = is_partial_layer(current_layer, current_is_left_half);
    ARG_UNUSED(is_zoom);
    /* [ZOOM] tag supported */

    /* Update Header Layer Name & Accent Color */
    const char *layer_name = (current_layer == 14) ? "MENU" : zmk_widget_layer_badge_get_name(current_layer);
    uint32_t layer_color = (current_layer == 14) ? 0x707e94 : zmk_widget_layer_badge_get_color(current_layer);
    if (header_title_label) {
        lv_obj_set_style_text_color(header_title_label, lv_color_hex(layer_color), 0);
        lv_label_set_text_fmt(header_title_label, "#%u %s", current_layer, layer_name);
    }

    /* Update Active Half Indicator Banner */
    if (header_half_label) {
        if (current_is_left_half) {
            lv_label_set_text(header_half_label, "< LEFT >");
            lv_obj_set_style_text_color(header_half_label, lv_color_hex(is_light ? 0x0284c7 : 0x00f2fe), 0);
        } else {
            lv_label_set_text(header_half_label, "< RIGHT >");
            lv_obj_set_style_text_color(header_half_label, lv_color_hex(is_light ? 0xd97706 : 0xff9a44), 0);
        }
    }

    const struct key_geometry *base_geom = current_is_left_half ? left_half_geom : right_half_geom;
    uint8_t key_offset = current_is_left_half ? 0 : KEYS_PER_HALF;

    /* Update 38 Key Cells for the Inspected Half */
    for (uint8_t i = 0; i < KEYS_PER_HALF; i++) {
        if (!cell_labels[i]) {
            continue;
        }

        uint8_t key_idx = key_offset + i;
        const struct key_cell_info *info = &layer_keys_data[current_layer][key_idx];

        uint8_t cat = info->cat;
        if (cat >= KEY_CAT_COUNT) {
            cat = KEY_CAT_ALPHA;
        }

        lv_obj_clear_flag(cell_labels[i], LV_OBJ_FLAG_HIDDEN);

        const struct key_geometry *geom = &base_geom[i];
        lv_obj_set_pos(cell_labels[i], geom->x, geom->y);
        lv_obj_set_size(cell_labels[i], geom->w, geom->h);

        /* Adaptive font: use 10pt for 3+ char labels to prevent clipping, 12pt for 1-2 chars */
        size_t label_len = info->label ? strlen(info->label) : 0;
        uint8_t font_h = 12;
#if IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_10) && IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_12)
        if (label_len >= 3) {
            lv_obj_set_style_text_font(cell_labels[i], &lv_font_montserrat_10, 0);
            font_h = 10;
        } else {
            lv_obj_set_style_text_font(cell_labels[i], &lv_font_montserrat_12, 0);
            font_h = 12;
        }
#elif IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_12)
        lv_obj_set_style_text_font(cell_labels[i], &lv_font_montserrat_12, 0);
        font_h = 12;
#endif

        /* Vertical padding for vertically centered label text */
        uint8_t pad_t = (geom->h > font_h) ? ((geom->h - font_h) / 2) : 0;
        lv_obj_set_style_pad_top(cell_labels[i], pad_t, 0);

        const struct category_style *style = is_light ? &cat_styles_light[cat] : &cat_styles[cat];
        lv_obj_set_style_bg_color(cell_labels[i], lv_color_hex(style->bg), 0);
        lv_obj_set_style_border_color(cell_labels[i], lv_color_hex(style->border), 0);
        lv_obj_set_style_border_width(cell_labels[i], (cat == KEY_CAT_LAYER) ? 2 : 1, 0);
        lv_obj_set_style_text_color(cell_labels[i], lv_color_hex(style->text), 0);

        if (cat == KEY_CAT_TRANS) {
            lv_label_set_text_static(cell_labels[i], "");
        } else {
            lv_label_set_text_static(cell_labels[i], info->label);
        }
    }

    /* Update render cache state upon successful redraw */
    rendered_layer = current_layer;
    rendered_is_left = current_is_left_half;
    rendered_dashboard_mode = is_dashboard_mode;
}

int layer_inspector_show(uint8_t layer, bool is_left_half) {
    if (layer >= NUM_INSPECTED_LAYERS) {
        layer = 0;
    }
    current_layer = layer;
    current_is_left_half = is_left_half;
    is_dashboard_mode = false;
    inspector_active = true;
    k_work_reschedule_for_queue(zmk_display_work_q(), &layer_inspector_delay_work, K_NO_WAIT);
    return 0;
}

int layer_inspector_show_dashboard(uint8_t layer, bool is_left_half) {
    if (layer >= NUM_INSPECTED_LAYERS) {
        layer = 0;
    }
    current_layer = layer;
    current_is_left_half = is_left_half;
    is_dashboard_mode = true;
    inspector_active = true;
    k_work_reschedule_for_queue(zmk_display_work_q(), &layer_inspector_delay_work, K_NO_WAIT);
    return 0;
}

void layer_inspector_hide(void) {
    inspector_active = false;
    is_dashboard_mode = false;
    rendered_layer = -1;
    rendered_is_left = -1;
    rendered_dashboard_mode = -1;
    k_work_reschedule_for_queue(zmk_display_work_q(), &layer_inspector_delay_work, K_NO_WAIT);
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
        k_work_reschedule_for_queue(zmk_display_work_q(), &layer_inspector_delay_work, K_NO_WAIT);
        break;

    case MENU_NAV_UP:
        /* Cycle layer up (0..14) */
        current_layer = (current_layer + 1) % NUM_INSPECTED_LAYERS;
        k_work_reschedule_for_queue(zmk_display_work_q(), &layer_inspector_delay_work, K_NO_WAIT);
        break;

    case MENU_NAV_DOWN:
        /* Cycle layer down (14..0) */
        current_layer = (current_layer == 0) ? (NUM_INSPECTED_LAYERS - 1) : (current_layer - 1);
        k_work_reschedule_for_queue(zmk_display_work_q(), &layer_inspector_delay_work, K_NO_WAIT);
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

void layer_inspector_on_key_position(uint32_t position) {
    if (position >= 76) {
        return;
    }
    uint8_t highest_layer = zmk_keymap_highest_layer_active();
    if (highest_layer >= NUM_INSPECTED_LAYERS) {
        highest_layer = 0;
    }

    uint8_t data_idx = matrix_pos_to_data_idx[position];
    const struct key_cell_info *info = &layer_keys_data[highest_layer][data_idx];
    /* Skip changing half if the key is a layer trigger / switch */
    if (info->cat == KEY_CAT_LAYER) {
        return;
    }

    bool is_right = is_right_key(position);
    bool new_is_left = !is_right;
    if (new_is_left != current_is_left_half) {
        current_is_left_half = new_is_left;
        if (inspector_active) {
            k_work_reschedule_for_queue(zmk_display_work_q(), &layer_inspector_delay_work, K_MSEC(50));
        }
    }
}

void layer_inspector_on_layer_change(uint8_t layer) {
    if (layer >= NUM_INSPECTED_LAYERS) {
        layer = 0;
    }
    if (layer != current_layer) {
        current_layer = layer;
        if (inspector_active) {
            k_work_reschedule_for_queue(zmk_display_work_q(), &layer_inspector_delay_work, K_MSEC(50));
        }
    }
}

static int layer_inspector_layer_changed_listener(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    uint8_t highest_layer = zmk_keymap_highest_layer_active();
    layer_inspector_on_layer_change(highest_layer);
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(layer_inspector_layer_tracker, layer_inspector_layer_changed_listener);
ZMK_SUBSCRIPTION(layer_inspector_layer_tracker, zmk_layer_state_changed);

