/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>
#include "menu_controller.h"

enum key_category {
    KEY_CAT_ALPHA = 0,
    KEY_CAT_MOD,
    KEY_CAT_LAYER,
    KEY_CAT_MEDIA,
    KEY_CAT_TRANS,
    KEY_CAT_COUNT
};

/* Core Layer Inspector lifecycle and navigation API */
int layer_inspector_init(lv_obj_t *screen);
int layer_inspector_show(uint8_t layer, bool is_left_half);
int layer_inspector_show_dashboard(uint8_t layer, bool is_left_half);
void layer_inspector_hide(void);
void layer_inspector_handle_nav(enum menu_nav_action action);
bool layer_inspector_is_active(void);
void layer_inspector_render(void);

void layer_inspector_on_key_position(uint32_t position);
void layer_inspector_on_layer_change(uint8_t layer);

uint8_t layer_inspector_get_current_layer(void);
bool layer_inspector_get_is_left_half(void);
lv_obj_t *layer_inspector_get_container(void);

