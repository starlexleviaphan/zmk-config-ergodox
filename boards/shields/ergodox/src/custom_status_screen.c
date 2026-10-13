/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>
#include "custom_status_screen.h"
#include "menu/menu_controller.h"
#include "menu/layer_inspector.h"


#if IS_ENABLED(CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE)
#include "widgets/output_status.h"
static struct zmk_widget_output_status output_status_widget;
#endif

#include "widgets/lock_status.h"
static struct zmk_widget_lock_status lock_status_widget;

#include "widgets/layer_badge.h"
static struct zmk_widget_layer_badge layer_badge_widget;

#include "widgets/wpm_gauge.h"
static struct zmk_widget_wpm_gauge wpm_gauge_widget;

#include "widgets/bongo_cat.h"
static struct zmk_widget_bongo_cat bongo_cat_widget;

#if IS_ENABLED(CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE)
#include "widgets/mod_status.h"
#include <fonts.h>
static struct zmk_widget_mod_status mod_status_widget;
#endif

#if IS_ENABLED(CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE)
#include "widgets/battery_status.h"
static struct zmk_widget_dongle_battery_status dongle_battery_status_widget;
#endif

/* Root container for active dashboard widgets */
static lv_obj_t *dashboard_root = NULL;
static lv_obj_t *cards[5]; /* header, layer, wpm, bongo, footer */
static uint8_t card_count = 0;
static lv_obj_t *bongo_card_obj = NULL;
static lv_obj_t *wpm_card_obj = NULL;

lv_obj_t *zmk_display_get_dashboard_root(void) {
    return dashboard_root;
}

lv_obj_t *zmk_display_get_bongo_obj(void) {
    return bongo_card_obj;
}

lv_obj_t *zmk_display_get_wpm_obj(void) {
    return wpm_card_obj;
}

static const struct theme_palette themes[4] = {
    [0] = { /* Cyberpunk */
        .bg = 0x0a0d14,
        .border = 0x1f293d,
        .accent = 0x00d2ff,
        .sel_bg = 0x16243b,
        .text_sel = 0x00f2fe,
    },
    [1] = { /* Monokai */
        .bg = 0x1e1f1c,
        .border = 0x49483e,
        .accent = 0xa6e22e,
        .sel_bg = 0x32352b,
        .text_sel = 0xe6db74,
    },
    [2] = { /* Matrix */
        .bg = 0x020d04,
        .border = 0x0a3d12,
        .accent = 0x00ff41,
        .sel_bg = 0x052e0a,
        .text_sel = 0x55ff77,
    },
    [3] = { /* Clean */
        .bg = 0x18181b,
        .border = 0x3f3f46,
        .accent = 0x38bdf8,
        .sel_bg = 0x27272a,
        .text_sel = 0xf4f4f5,
    },
};

const struct theme_palette *zmk_display_get_theme_palette(uint8_t theme_id) {
    if (theme_id >= 4) {
        theme_id = 0;
    }
    return &themes[theme_id];
}

void zmk_display_apply_theme(uint8_t theme_id) {
    if (theme_id >= 4) {
        theme_id = 0;
    }
    const struct theme_palette *pal = &themes[theme_id];
    for (uint8_t i = 0; i < card_count; i++) {
        if (cards[i]) {
            lv_obj_set_style_bg_color(cards[i], lv_color_hex(pal->bg), 0);
            lv_obj_set_style_border_color(cards[i], lv_color_hex(pal->border), 0);
        }
    }
}

void zmk_display_apply_animation(uint8_t anim_idx) {
    if (!bongo_card_obj || !wpm_card_obj) {
        return;
    }
    switch (anim_idx) {
    case 0: /* Bongo Cat */
        lv_obj_clear_flag(bongo_card_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wpm_card_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(wpm_card_obj, 6, 102);
        lv_obj_set_size(wpm_card_obj, 130, 68);
        if (cards[1]) lv_obj_clear_flag(cards[1], LV_OBJ_FLAG_HIDDEN); /* Layer badge */
        if (cards[4]) {
            lv_obj_set_pos(cards[4], 6, 176);
            lv_obj_set_size(cards[4], 268, 58);
        }
        layer_inspector_hide();
        break;
    case 1: /* WPM Gauge (Hero mode: center/right expanded) */
        lv_obj_add_flag(bongo_card_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wpm_card_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(wpm_card_obj, 142, 38);
        lv_obj_set_size(wpm_card_obj, 132, 132);
        if (cards[1]) lv_obj_clear_flag(cards[1], LV_OBJ_FLAG_HIDDEN); /* Layer badge */
        if (cards[4]) {
            lv_obj_set_pos(cards[4], 6, 176);
            lv_obj_set_size(cards[4], 268, 58);
        }
        layer_inspector_hide();
        break;
    case 2: /* Layer Map (Live Dynamic Dashboard Layout) */
        lv_obj_add_flag(bongo_card_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wpm_card_obj, LV_OBJ_FLAG_HIDDEN);
        if (cards[1]) lv_obj_add_flag(cards[1], LV_OBJ_FLAG_HIDDEN); /* Layer badge card hidden */
        if (cards[4]) {
            lv_obj_set_pos(cards[4], 6, 196);
            lv_obj_set_size(cards[4], 268, 38);
        }
        layer_inspector_show_dashboard(zmk_keymap_highest_layer_active(), layer_inspector_get_is_left_half());
        break;
    case 3: /* Off */
    default:
        lv_obj_add_flag(bongo_card_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wpm_card_obj, LV_OBJ_FLAG_HIDDEN);
        if (cards[1]) lv_obj_clear_flag(cards[1], LV_OBJ_FLAG_HIDDEN);
        if (cards[4]) {
            lv_obj_set_pos(cards[4], 6, 176);
            lv_obj_set_size(cards[4], 268, 58);
        }
        layer_inspector_hide();
        break;
    }
}


static void apply_card_style(lv_obj_t *card) {
    if (!card) {
        return;
    }
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    uint8_t cur_theme = get_active_theme();
    if (cur_theme >= 4) cur_theme = 0;
    lv_obj_set_style_bg_color(card, lv_color_hex(themes[cur_theme].bg), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(themes[cur_theme].border), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 2, 0);

    if (card_count < 5) {
        cards[card_count++] = card;
    }
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Dashboard Root Container (280x240 px) */
    dashboard_root = lv_obj_create(screen);
    lv_obj_set_size(dashboard_root, 280, 240);
    lv_obj_set_pos(dashboard_root, 0, 0);
    lv_obj_set_style_bg_color(dashboard_root, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(dashboard_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dashboard_root, 0, 0);
    lv_obj_set_style_pad_all(dashboard_root, 0, 0);
    lv_obj_clear_flag(dashboard_root, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. Header Card (268x28 px at x=6, y=6): Output Status (USB/BLE) + Lock Status ([CAPS]) */
    lv_obj_t *header_card = lv_obj_create(dashboard_root);
    apply_card_style(header_card);
    lv_obj_set_size(header_card, 268, 28);
    lv_obj_set_pos(header_card, 6, 6);

#if IS_ENABLED(CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE)
    zmk_widget_output_status_init(&output_status_widget, header_card);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_LEFT_MID, 6, 0);
#endif

    zmk_widget_lock_status_init(&lock_status_widget, header_card);
    lv_obj_align(zmk_widget_lock_status_obj(&lock_status_widget), LV_ALIGN_RIGHT_MID, -6, 0);

    /* 2. Layer Badge Card (130x60 px at x=6, y=38) */
    zmk_widget_layer_badge_init(&layer_badge_widget, dashboard_root);
    lv_obj_t *layer_obj = zmk_widget_layer_badge_obj(&layer_badge_widget);
    lv_obj_set_pos(layer_obj, 6, 38);
    apply_card_style(layer_obj);

    /* 3. WPM Speedometer Card (130x68 px at x=6, y=102) */
    zmk_widget_wpm_gauge_init(&wpm_gauge_widget, dashboard_root);
    lv_obj_t *wpm_obj = zmk_widget_wpm_gauge_obj(&wpm_gauge_widget);
    lv_obj_set_pos(wpm_obj, 6, 102);
    apply_card_style(wpm_obj);
    wpm_card_obj = wpm_obj;

    /* 4. Center Right: Typing-Reactive Bongo Cat Mascot Card (132x132 px at x=142, y=38) */
    zmk_widget_bongo_cat_init(&bongo_cat_widget, dashboard_root);
    lv_obj_t *bongo_obj = zmk_widget_bongo_cat_obj(&bongo_cat_widget);
    lv_obj_set_pos(bongo_obj, 142, 38);
    apply_card_style(bongo_obj);
    bongo_card_obj = bongo_obj;

    /* 5. Footer Card (268x58 px at x=6, y=176): Modifier Status + 3-Channel Battery Monitor */
    lv_obj_t *footer_card = lv_obj_create(dashboard_root);
    apply_card_style(footer_card);
    lv_obj_set_size(footer_card, 268, 58);
    lv_obj_set_pos(footer_card, 6, 176);

#if IS_ENABLED(CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE)
    zmk_widget_mod_status_init(&mod_status_widget, footer_card);
    lv_obj_t *mod_obj = zmk_widget_mod_status_obj(&mod_status_widget);
    lv_obj_clear_flag(mod_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(mod_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mod_obj, 0, 0);
    lv_obj_set_style_pad_all(mod_obj, 0, 0);
    lv_obj_set_size(mod_obj, 200, 18);
    lv_obj_set_style_text_font(mod_status_widget.label, &NerdFonts_Regular_20, 0);
    lv_obj_align(mod_obj, LV_ALIGN_TOP_MID, 0, 2);
#endif

#if IS_ENABLED(CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE)
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, footer_card);
    lv_obj_t *batt_obj = zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget);
    lv_obj_clear_flag(batt_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(batt_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(batt_obj, 0, 0);
    lv_obj_set_style_pad_all(batt_obj, 0, 0);
    lv_obj_align(batt_obj, LV_ALIGN_BOTTOM_MID, 0, -2);
#endif

    zmk_display_apply_theme(get_active_theme());
    zmk_display_apply_animation(get_animation_style());
    menu_controller_init(screen);

    return screen;
}

