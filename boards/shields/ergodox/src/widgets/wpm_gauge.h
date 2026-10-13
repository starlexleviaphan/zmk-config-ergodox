/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_wpm_gauge {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *arc;
    lv_obj_t *label;
};

int zmk_widget_wpm_gauge_init(struct zmk_widget_wpm_gauge *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_wpm_gauge_obj(struct zmk_widget_wpm_gauge *widget);
uint32_t zmk_widget_wpm_gauge_get_color(uint8_t wpm);
