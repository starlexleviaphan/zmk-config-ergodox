/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_lock_status {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *label;
};

int zmk_widget_lock_status_init(struct zmk_widget_lock_status *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_lock_status_obj(struct zmk_widget_lock_status *widget);
