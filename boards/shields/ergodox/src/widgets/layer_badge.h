/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_layer_badge {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *label;
    lv_obj_t *desc_label;
};

int zmk_widget_layer_badge_init(struct zmk_widget_layer_badge *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_layer_badge_obj(struct zmk_widget_layer_badge *widget);

/* Color and metadata lookup helpers for Cyber-HUD components */
uint32_t zmk_widget_layer_badge_get_color(uint8_t layer);
const char *zmk_widget_layer_badge_get_name(uint8_t layer);
const char *zmk_widget_layer_badge_get_desc(uint8_t layer);
void zmk_widget_layer_badge_apply_theme(uint8_t theme_id);
