/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_bongo_cat {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *canvas;
    lv_obj_t *head_label;
    lv_obj_t *paw_l;
    lv_obj_t *paw_r;
    lv_obj_t *desk;
};

int zmk_widget_bongo_cat_init(struct zmk_widget_bongo_cat *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_bongo_cat_obj(struct zmk_widget_bongo_cat *widget);
void zmk_widget_bongo_cat_apply_theme(uint8_t theme_id);
