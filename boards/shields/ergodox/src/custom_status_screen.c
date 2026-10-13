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

lv_obj_t *zmk_display_get_dashboard_root(void) {
    return dashboard_root;
}

static void apply_card_style(lv_obj_t *card) {
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0a0d14), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x1f293d), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_pad_all(card, 2, 0);
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

    /* 3. WPM Speedometer Card (130x68 px at x=6, y=102) */
    zmk_widget_wpm_gauge_init(&wpm_gauge_widget, dashboard_root);
    lv_obj_t *wpm_obj = zmk_widget_wpm_gauge_obj(&wpm_gauge_widget);
    lv_obj_set_pos(wpm_obj, 6, 102);

    /* 4. Center Right: Typing-Reactive Bongo Cat Mascot Card (132x132 px at x=142, y=38) */
    zmk_widget_bongo_cat_init(&bongo_cat_widget, dashboard_root);
    lv_obj_t *bongo_obj = zmk_widget_bongo_cat_obj(&bongo_cat_widget);
    lv_obj_set_pos(bongo_obj, 142, 38);

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

    menu_controller_init(screen);

    return screen;
}
