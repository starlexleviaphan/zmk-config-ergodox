/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

struct theme_palette {
    uint32_t bg;         /* Card / container background */
    uint32_t border;     /* Card / container border */
    uint32_t accent;     /* Selected row border & primary accent */
    uint32_t sel_bg;     /* Selected row background */
    uint32_t text_sel;   /* Selected row text color */
    uint32_t text_unsel; /* Unselected text color */
    uint32_t screen_bg;  /* Root screen background */
};

lv_obj_t *zmk_display_status_screen(void);
lv_obj_t *zmk_display_get_dashboard_root(void);
lv_obj_t *zmk_display_get_bongo_obj(void);
lv_obj_t *zmk_display_get_wpm_obj(void);
const struct theme_palette *zmk_display_get_theme_palette(uint8_t theme_id);
void zmk_display_apply_theme(uint8_t theme_id);
void zmk_display_apply_animation(uint8_t anim_idx);

