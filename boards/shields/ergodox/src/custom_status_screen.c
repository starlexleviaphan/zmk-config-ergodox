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

    /* 1. Header Card (280x28 px): Output Status (USB/BLE) + Lock Status ([CAPS]) */
    lv_obj_t *header_card = lv_obj_create(dashboard_root);
    apply_card_style(header_card);
    lv_obj_set_size(header_card, 280, 28);
    lv_obj_set_pos(header_card, 0, 2);

#if IS_ENABLED(CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE)
    zmk_widget_output_status_init(&output_status_widget, header_card);
    lv_obj_t *output_obj = zmk_widget_output_status_obj(&output_status_widget);
    lv_obj_clear_flag(output_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(output_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(output_obj, 0, 0);
    lv_obj_set_style_pad_all(output_obj, 0, 0);
    lv_obj_set_size(output_obj, 130, 24);
    lv_obj_align(output_obj, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_align(output_status_widget.transport_label, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_align(output_status_widget.ble_label, LV_ALIGN_LEFT_MID, 80, 5);
#endif

    zmk_widget_lock_status_init(&lock_status_widget, header_card);
    lv_obj_align(zmk_widget_lock_status_obj(&lock_status_widget), LV_ALIGN_RIGHT_MID, -6, 0);

    /* 2. Center Left Card (145x140 px): Layer Badge + WPM Speedometer Gauge */
    lv_obj_t *center_left_card = lv_obj_create(dashboard_root);
    apply_card_style(center_left_card);
    lv_obj_set_size(center_left_card, 145, 140);
    lv_obj_set_pos(center_left_card, 2, 33);

    zmk_widget_layer_badge_init(&layer_badge_widget, center_left_card);
    lv_obj_t *layer_obj = zmk_widget_layer_badge_obj(&layer_badge_widget);
    lv_obj_set_size(layer_obj, 137, 50);
    lv_obj_align(layer_obj, LV_ALIGN_TOP_MID, 0, 6);

    zmk_widget_wpm_gauge_init(&wpm_gauge_widget, center_left_card);
    lv_obj_t *wpm_obj = zmk_widget_wpm_gauge_obj(&wpm_gauge_widget);
    lv_obj_set_size(wpm_obj, 137, 66);
    lv_obj_align(wpm_obj, LV_ALIGN_BOTTOM_MID, 0, -6);

    /* 3. Center Right Card (125x140 px): Typing-Reactive Bongo Cat Mascot */
    lv_obj_t *center_right_card = lv_obj_create(dashboard_root);
    apply_card_style(center_right_card);
    lv_obj_set_size(center_right_card, 125, 140);
    lv_obj_set_pos(center_right_card, 153, 33);

    zmk_widget_bongo_cat_init(&bongo_cat_widget, center_right_card);
    lv_obj_t *bongo_obj = zmk_widget_bongo_cat_obj(&bongo_cat_widget);
    lv_obj_align(bongo_obj, LV_ALIGN_CENTER, 0, 0);

    /* 4. Footer Card (280x62 px): Modifier Status + 3-Channel Battery Monitor */
    lv_obj_t *footer_card = lv_obj_create(dashboard_root);
    apply_card_style(footer_card);
    lv_obj_set_size(footer_card, 280, 62);
    lv_obj_set_pos(footer_card, 0, 176);

#if IS_ENABLED(CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE)
    zmk_widget_mod_status_init(&mod_status_widget, footer_card);
    lv_obj_t *mod_obj = zmk_widget_mod_status_obj(&mod_status_widget);
    lv_obj_clear_flag(mod_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(mod_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(mod_obj, 0, 0);
    lv_obj_set_style_pad_all(mod_obj, 0, 0);
    lv_obj_set_size(mod_obj, 200, 20);
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
    lv_obj_align(batt_obj, LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

    menu_controller_init(screen);

    return screen;
}
