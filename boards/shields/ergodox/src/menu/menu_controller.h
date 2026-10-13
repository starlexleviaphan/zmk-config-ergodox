/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <lvgl.h>

enum screen_state {
    SCREEN_STATE_DASHBOARD = 0,
    SCREEN_STATE_MENU,
    SCREEN_STATE_LAYER_MAP,
};

enum menu_item_id {
    MENU_ITEM_BRIGHTNESS = 0,
    MENU_ITEM_TIMEOUT,
    MENU_ITEM_ORIENTATION,
    MENU_ITEM_ANIMATION,
    MENU_ITEM_THEME,
    MENU_ITEM_LAYER_MAP,
    MENU_ITEM_SAVE_EXIT,
    MENU_ITEM_COUNT
};

enum menu_nav_action {
    MENU_NAV_UP = 0,
    MENU_NAV_DOWN,
    MENU_NAV_DEC,
    MENU_NAV_INC,
    MENU_NAV_SELECT,
    MENU_NAV_EXIT,
};

/* Core controller lifecycle & state API */
int menu_controller_init(lv_obj_t *screen);
bool menu_controller_is_active(void);
enum screen_state menu_controller_get_state(void);
void menu_controller_set_state(enum screen_state new_state);
void menu_controller_toggle(void);
void menu_controller_handle_nav(enum menu_nav_action action);
lv_obj_t *menu_controller_get_root(void);

/* Display settings accessors */
void set_screen_brightness(uint8_t percent);
uint8_t get_screen_brightness(void);

void set_sleep_timeout(uint16_t timeout_s);
uint16_t get_sleep_timeout(void);

void set_screen_orientation(bool flipped);
bool get_screen_orientation(void);

void set_animation_style(uint8_t anim);
uint8_t get_animation_style(void);

void set_active_theme(uint8_t theme);
uint8_t get_active_theme(void);

/* Subscreen navigation hook (Task 7) */
int layer_inspector_show(uint8_t layer, bool is_left_half);
