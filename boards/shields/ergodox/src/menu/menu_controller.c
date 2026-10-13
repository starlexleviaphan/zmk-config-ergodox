/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/hid.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>

#include "custom_status_screen.h"
#include "menu_controller.h"
#include "layer_inspector.h"

#ifndef ARG_UNUSED
#define ARG_UNUSED(x) (void)(x)
#endif

/* HID Keyboard Usage Codes (Standard USB HID Page 0x07) */
#define HID_KEY_A      0x04
#define HID_KEY_D      0x07
#define HID_KEY_S      0x16
#define HID_KEY_W      0x1A
#define HID_KEY_RETURN 0x28
#define HID_KEY_ESCAPE 0x29
#define HID_KEY_SPACE  0x2C
#define HID_KEY_RIGHT  0x4F
#define HID_KEY_LEFT   0x50
#define HID_KEY_DOWN   0x51
#define HID_KEY_UP     0x52
#define HID_KEY_F21    0x70

/* ErgoDox Matrix Key Positions */
#define POS_ESC         0
#define POS_F21_L6      1
#define POS_W           16
#define POS_A           29
#define POS_S           30
#define POS_D           31
#define POS_SPACE_L     68
#define POS_ENTER_R     71
#define POS_SPACE_R     72

/* Controller State */
static enum screen_state current_screen_state = SCREEN_STATE_DASHBOARD;
static bool menu_is_active = false;
static uint8_t selected_item_idx = 0;
static uint32_t last_nav_timestamp = 0;
static uint32_t last_toggle_timestamp = 0;
static enum menu_nav_action last_nav_action = (enum menu_nav_action)-1;

/* Configurable Parameters */
static uint8_t current_brightness = 80; /* 10..100%, default 80% */
static const uint16_t timeout_values[] = {0, 60, 180, 300, 600, 900};
static const char * const timeout_labels[] = {"Off", "60s", "180s", "300s", "600s", "900s"};
#define TIMEOUT_OPTIONS_COUNT ((uint8_t)(sizeof(timeout_values) / sizeof(timeout_values[0])))
static uint8_t current_timeout_idx = 4; /* default 600s */

static bool current_orientation_flipped = true; /* default 180° */

static const char * const anim_labels[] = {"Bongo Cat", "WPM Gauge", "Off"};
#define ANIM_OPTIONS_COUNT ((uint8_t)(sizeof(anim_labels) / sizeof(anim_labels[0])))
static uint8_t current_anim_idx = 0; /* default Bongo Cat */

static const char * const theme_labels[] = {"Cyberpunk", "Monokai", "Matrix", "Clean"};
#define THEME_OPTIONS_COUNT ((uint8_t)(sizeof(theme_labels) / sizeof(theme_labels[0])))
static uint8_t current_theme_idx = 0; /* default Cyberpunk */

/* Menu Titles */
static const char * const menu_titles[MENU_ITEM_COUNT] = {
    [MENU_ITEM_BRIGHTNESS]   = "[ Яркость ]",
    [MENU_ITEM_TIMEOUT]      = "[ Таймаут сна ]",
    [MENU_ITEM_ORIENTATION]  = "[ Ориентация ]",
    [MENU_ITEM_ANIMATION]    = "[ Анимация ]",
    [MENU_ITEM_THEME]        = "[ Тема ]",
    [MENU_ITEM_LAYER_MAP]    = "[ Карта слоев ]",
    [MENU_ITEM_SAVE_EXIT]    = "[ Сохранить и выйти ]",
};

/* LVGL UI Objects */
static lv_obj_t *menu_root_container = NULL;
static lv_obj_t *row_containers[MENU_ITEM_COUNT];
static lv_obj_t *row_labels_left[MENU_ITEM_COUNT];
static lv_obj_t *row_labels_right[MENU_ITEM_COUNT];

static void menu_ui_update_handler(struct k_work *work);
static K_WORK_DEFINE(menu_ui_work, menu_ui_update_handler);

/* Orientation is applied on the display work queue via the Zephyr display
 * API, mirroring YADS screen_rotate_init.c (horizontal: FLIPPED -> 90, else 270). */
static void orientation_apply_handler(struct k_work *work) {
    ARG_UNUSED(work);
    const struct device *d = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(d)) {
        LOG_WRN("Display device not ready, orientation not applied");
        return;
    }
    int err = display_set_orientation(d, current_orientation_flipped ? DISPLAY_ORIENTATION_ROTATED_90
                                                                      : DISPLAY_ORIENTATION_ROTATED_270);
    if (err) {
        LOG_WRN("display_set_orientation failed (err %d)", err);
    }
    lv_obj_invalidate(lv_scr_act());
}
static K_WORK_DEFINE(orientation_apply_work, orientation_apply_handler);


extern void set_screen_brightness(uint8_t value, bool ambient);

/* Settings Accessors */
void menu_controller_set_brightness(uint8_t percent) {
    if (percent < 10) percent = 10;
    if (percent > 100) percent = 100;
    current_brightness = percent;
    set_screen_brightness(percent, false);
}

uint8_t menu_controller_get_brightness(void) {
    return current_brightness;
}

void set_sleep_timeout(uint16_t timeout_s) {
    for (uint8_t i = 0; i < TIMEOUT_OPTIONS_COUNT; i++) {
        if (timeout_values[i] == timeout_s) {
            current_timeout_idx = i;
            return;
        }
    }
    current_timeout_idx = 4;
}

uint16_t get_sleep_timeout(void) {
    return timeout_values[current_timeout_idx];
}

void set_screen_orientation(bool flipped) {
    current_orientation_flipped = flipped;
    k_work_submit_to_queue(zmk_display_work_q(), &orientation_apply_work);
}

bool get_screen_orientation(void) {
    return current_orientation_flipped;
}

void set_animation_style(uint8_t anim) {
    if (anim < ANIM_OPTIONS_COUNT) {
        current_anim_idx = anim;
    }
}

uint8_t get_animation_style(void) {
    return current_anim_idx;
}

void set_active_theme(uint8_t theme) {
    if (theme < THEME_OPTIONS_COUNT) {
        current_theme_idx = theme;
    }
}

uint8_t get_active_theme(void) {
    return current_theme_idx;
}

bool menu_controller_is_active(void) {
    return menu_is_active;
}

enum screen_state menu_controller_get_state(void) {
    return current_screen_state;
}

lv_obj_t *menu_controller_get_root(void) {
    return menu_root_container;
}

uint8_t menu_controller_get_selected_item(void) {
    return selected_item_idx;
}

void menu_controller_set_selected_item(uint8_t idx) {
    if (idx < MENU_ITEM_COUNT) {
        selected_item_idx = idx;
    }
}

/* --- Persistent Display Settings (written to flash only on Save & Exit) --- */
#if IS_ENABLED(CONFIG_SETTINGS)
static void settings_apply_handler(struct k_work *work) {
    /* Runs on the display work queue: same path as a menu orientation change */
    orientation_apply_handler(work);
    set_screen_brightness(current_brightness, false);
    k_work_submit_to_queue(zmk_display_work_q(), &menu_ui_work);
}

static K_WORK_DEFINE(settings_apply_work, settings_apply_handler);

static bool settings_read_u8(size_t len, settings_read_cb read_cb, void *cb_arg, uint8_t *out) {
    uint8_t v;
    if (len != sizeof(v) || read_cb(cb_arg, &v, sizeof(v)) != (ssize_t)sizeof(v)) {
        return false;
    }
    *out = v;
    return true;
}

static int display_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    uint8_t v = 0;
    bool applied = false;

    if (settings_name_steq(name, "brightness", &next) && !next) {
        if (settings_read_u8(len, read_cb, cb_arg, &v) && v >= 10 && v <= 100) {
            current_brightness = v;
            applied = true;
        } else {
            LOG_WRN("Discarded invalid brightness from settings");
        }
    } else if (settings_name_steq(name, "timeout_s", &next) && !next) {
        uint16_t t;
        bool found = false;
        if (len == sizeof(t) && read_cb(cb_arg, &t, sizeof(t)) == (ssize_t)sizeof(t)) {
            for (uint8_t i = 0; i < TIMEOUT_OPTIONS_COUNT; i++) {
                if (timeout_values[i] == t) {
                    current_timeout_idx = i;
                    found = true;
                    break;
                }
            }
        }
        if (found) {
            applied = true;
        } else {
            LOG_WRN("Discarded invalid timeout from settings");
        }
    } else if (settings_name_steq(name, "flipped", &next) && !next) {
        if (settings_read_u8(len, read_cb, cb_arg, &v) && v <= 1) {
            current_orientation_flipped = (v != 0);
            applied = true;
        } else {
            LOG_WRN("Discarded invalid orientation from settings");
        }
    } else if (settings_name_steq(name, "anim_style", &next) && !next) {
        if (settings_read_u8(len, read_cb, cb_arg, &v) && v < ANIM_OPTIONS_COUNT) {
            current_anim_idx = v;
            applied = true;
        } else {
            LOG_WRN("Discarded invalid animation style from settings");
        }
    } else if (settings_name_steq(name, "theme_id", &next) && !next) {
        if (settings_read_u8(len, read_cb, cb_arg, &v) && v < THEME_OPTIONS_COUNT) {
            current_theme_idx = v;
            applied = true;
        } else {
            LOG_WRN("Discarded invalid theme from settings");
        }
    }

    if (applied) {
        k_work_submit_to_queue(zmk_display_work_q(), &settings_apply_work);
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(ergodox_display, "ergodox_display", NULL, display_settings_set, NULL, NULL);

static void save_one_setting(const char *key, const void *val, size_t len) {
    int err = settings_save_one(key, val, len);
    if (err) {
        LOG_WRN("Failed to save %s (err %d)", key, err);
    }
}

static void save_display_settings(void) {
    uint8_t b = current_brightness;
    uint16_t t = timeout_values[current_timeout_idx];
    uint8_t f = current_orientation_flipped ? 1 : 0;
    uint8_t a = current_anim_idx;
    uint8_t th = current_theme_idx;

    save_one_setting("ergodox_display/brightness", &b, sizeof(b));
    save_one_setting("ergodox_display/timeout_s", &t, sizeof(t));
    save_one_setting("ergodox_display/flipped", &f, sizeof(f));
    save_one_setting("ergodox_display/anim_style", &a, sizeof(a));
    save_one_setting("ergodox_display/theme_id", &th, sizeof(th));
}
#else
static void save_display_settings(void) {}
#endif

/* UI Redraw Routine */
static void render_menu_items(void) {
    if (!menu_root_container) {
        return;
    }

    for (uint8_t i = 0; i < MENU_ITEM_COUNT; i++) {
        bool selected = (i == selected_item_idx);

        if (selected) {
            lv_obj_set_style_bg_color(row_containers[i], lv_color_hex(0x161d2d), 0);
            lv_obj_set_style_bg_opa(row_containers[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(row_containers[i], lv_color_hex(0x00d2ff), 0);
            lv_obj_set_style_border_width(row_containers[i], 1, 0);
            lv_obj_set_style_radius(row_containers[i], 4, 0);
            lv_obj_set_style_text_color(row_labels_left[i], lv_color_hex(0x00f2fe), 0);
            lv_obj_set_style_text_color(row_labels_right[i], lv_color_hex(0x00f2fe), 0);
            lv_label_set_text_fmt(row_labels_left[i], "> %s", menu_titles[i]);
        } else {
            lv_obj_set_style_bg_opa(row_containers[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row_containers[i], 0, 0);
            lv_obj_set_style_text_color(row_labels_left[i], lv_color_hex(0x8a99ad), 0);
            lv_obj_set_style_text_color(row_labels_right[i], lv_color_hex(0x8a99ad), 0);
            lv_label_set_text_fmt(row_labels_left[i], "  %s", menu_titles[i]);
        }

        /* Format right-aligned value labels */
        switch (i) {
        case MENU_ITEM_BRIGHTNESS:
            if (selected) {
                lv_label_set_text_fmt(row_labels_right[i], "< %u%% >", current_brightness);
            } else {
                lv_label_set_text_fmt(row_labels_right[i], "%u%%", current_brightness);
            }
            break;
        case MENU_ITEM_TIMEOUT:
            if (selected) {
                lv_label_set_text_fmt(row_labels_right[i], "< %s >", timeout_labels[current_timeout_idx]);
            } else {
                lv_label_set_text_fmt(row_labels_right[i], "%s", timeout_labels[current_timeout_idx]);
            }
            break;
        case MENU_ITEM_ORIENTATION:
            if (selected) {
                lv_label_set_text_fmt(row_labels_right[i], "< %s >", current_orientation_flipped ? "180°" : "Normal");
            } else {
                lv_label_set_text_fmt(row_labels_right[i], "%s", current_orientation_flipped ? "180°" : "Normal");
            }
            break;
        case MENU_ITEM_ANIMATION:
            if (selected) {
                lv_label_set_text_fmt(row_labels_right[i], "< %s >", anim_labels[current_anim_idx]);
            } else {
                lv_label_set_text_fmt(row_labels_right[i], "%s", anim_labels[current_anim_idx]);
            }
            break;
        case MENU_ITEM_THEME:
            if (selected) {
                lv_label_set_text_fmt(row_labels_right[i], "< %s >", theme_labels[current_theme_idx]);
            } else {
                lv_label_set_text_fmt(row_labels_right[i], "%s", theme_labels[current_theme_idx]);
            }
            break;
        case MENU_ITEM_LAYER_MAP:
            if (selected) {
                lv_label_set_text(row_labels_right[i], "[ Enter ]");
            } else {
                lv_label_set_text(row_labels_right[i], "Enter");
            }
            break;
        case MENU_ITEM_SAVE_EXIT:
            if (selected) {
                lv_label_set_text(row_labels_right[i], "[ Enter ]");
            } else {
                lv_label_set_text(row_labels_right[i], "Enter");
            }
            break;
        default:
            break;
        }
    }
}

static void menu_ui_update_handler(struct k_work *work) {
    ARG_UNUSED(work);
    lv_obj_t *dash = zmk_display_get_dashboard_root();

    switch (current_screen_state) {
    case SCREEN_STATE_DASHBOARD:
        if (menu_root_container) {
            lv_obj_add_flag(menu_root_container, LV_OBJ_FLAG_HIDDEN);
        }
        if (dash) {
            lv_obj_clear_flag(dash, LV_OBJ_FLAG_HIDDEN);
        }
        layer_inspector_render();
        break;

    case SCREEN_STATE_MENU:
        if (dash) {
            lv_obj_add_flag(dash, LV_OBJ_FLAG_HIDDEN);
        }
        if (menu_root_container) {
            lv_obj_clear_flag(menu_root_container, LV_OBJ_FLAG_HIDDEN);
        }
        layer_inspector_render();
        render_menu_items();
        break;

    case SCREEN_STATE_LAYER_MAP:
        if (menu_root_container) {
            lv_obj_add_flag(menu_root_container, LV_OBJ_FLAG_HIDDEN);
        }
        if (dash) {
            lv_obj_add_flag(dash, LV_OBJ_FLAG_HIDDEN);
        }
        layer_inspector_render();
        break;
    }
}

/* State Transitions */
void menu_controller_set_state(enum screen_state new_state) {
    if (current_screen_state == SCREEN_STATE_LAYER_MAP && new_state != SCREEN_STATE_LAYER_MAP) {
        layer_inspector_hide();
    }
    current_screen_state = new_state;

    switch (new_state) {
    case SCREEN_STATE_DASHBOARD:
        menu_is_active = false;
        zmk_hid_keyboard_clear();
        break;

    case SCREEN_STATE_MENU:
        menu_is_active = true;
        zmk_hid_keyboard_clear();
        break;

    case SCREEN_STATE_LAYER_MAP:
        menu_is_active = true;
        zmk_hid_keyboard_clear();
        layer_inspector_show(zmk_keymap_highest_layer_active(), true);
        break;
    }

    /* Dispatch all LVGL mutations safely to display thread */
    k_work_submit_to_queue(zmk_display_work_q(), &menu_ui_work);
}

void menu_controller_toggle(void) {
    uint32_t now = k_uptime_get_32();
    if ((now - last_toggle_timestamp) < 200) {
        return;
    }
    last_toggle_timestamp = now;

    if (current_screen_state == SCREEN_STATE_DASHBOARD) {
        menu_controller_set_state(SCREEN_STATE_MENU);
    } else {
        menu_controller_set_state(SCREEN_STATE_DASHBOARD);
    }
}

/* Menu Navigation Input Handler */
void menu_controller_handle_nav(enum menu_nav_action action) {
    uint32_t now = k_uptime_get_32();
    if (action == last_nav_action && (now - last_nav_timestamp) < 60) {
        return;
    }
    last_nav_action = action;
    last_nav_timestamp = now;

    if (current_screen_state == SCREEN_STATE_LAYER_MAP) {
        layer_inspector_handle_nav(action);
        k_work_submit_to_queue(zmk_display_work_q(), &menu_ui_work);
        return;
    }

    switch (action) {
    case MENU_NAV_UP:
        selected_item_idx = (selected_item_idx == 0) ? (MENU_ITEM_COUNT - 1) : (selected_item_idx - 1);
        break;

    case MENU_NAV_DOWN:
        selected_item_idx = (selected_item_idx + 1) % MENU_ITEM_COUNT;
        break;

    case MENU_NAV_DEC:
        switch (selected_item_idx) {
        case MENU_ITEM_BRIGHTNESS:
            if (current_brightness > 10) {
                current_brightness -= 10;
            }
            menu_controller_set_brightness(current_brightness);
            break;
        case MENU_ITEM_TIMEOUT:
            if (current_timeout_idx > 0) {
                current_timeout_idx--;
            }
            set_sleep_timeout(timeout_values[current_timeout_idx]);
            break;
        case MENU_ITEM_ORIENTATION:
            set_screen_orientation(!current_orientation_flipped);
            break;
        case MENU_ITEM_ANIMATION:
            if (current_anim_idx > 0) {
                current_anim_idx--;
            } else {
                current_anim_idx = ANIM_OPTIONS_COUNT - 1;
            }
            set_animation_style(current_anim_idx);
            break;
        case MENU_ITEM_THEME:
            if (current_theme_idx > 0) {
                current_theme_idx--;
            } else {
                current_theme_idx = THEME_OPTIONS_COUNT - 1;
            }
            set_active_theme(current_theme_idx);
            break;
        default:
            break;
        }
        break;

    case MENU_NAV_INC:
        switch (selected_item_idx) {
        case MENU_ITEM_BRIGHTNESS:
            if (current_brightness < 100) {
                current_brightness += 10;
            }
            menu_controller_set_brightness(current_brightness);
            break;
        case MENU_ITEM_TIMEOUT:
            if (current_timeout_idx + 1 < TIMEOUT_OPTIONS_COUNT) {
                current_timeout_idx++;
            }
            set_sleep_timeout(timeout_values[current_timeout_idx]);
            break;
        case MENU_ITEM_ORIENTATION:
            set_screen_orientation(!current_orientation_flipped);
            break;
        case MENU_ITEM_ANIMATION:
            current_anim_idx = (current_anim_idx + 1) % ANIM_OPTIONS_COUNT;
            set_animation_style(current_anim_idx);
            break;
        case MENU_ITEM_THEME:
            current_theme_idx = (current_theme_idx + 1) % THEME_OPTIONS_COUNT;
            set_active_theme(current_theme_idx);
            break;
        case MENU_ITEM_LAYER_MAP:
            menu_controller_set_state(SCREEN_STATE_LAYER_MAP);
            return;
        default:
            break;
        }
        break;

    case MENU_NAV_SELECT:
        if (selected_item_idx == MENU_ITEM_ORIENTATION) {
            set_screen_orientation(!current_orientation_flipped);
        } else if (selected_item_idx == MENU_ITEM_LAYER_MAP) {
            menu_controller_set_state(SCREEN_STATE_LAYER_MAP);
            return;
        } else if (selected_item_idx == MENU_ITEM_SAVE_EXIT) {
            save_display_settings();
            menu_controller_set_state(SCREEN_STATE_DASHBOARD);
            return;
        }
        break;

    case MENU_NAV_EXIT:
        if (current_screen_state == SCREEN_STATE_LAYER_MAP) {
            menu_controller_set_state(SCREEN_STATE_MENU);
        } else {
            menu_controller_set_state(SCREEN_STATE_DASHBOARD);
        }
        return;
    }

    /* Dispatch all LVGL mutations safely to display thread */
    k_work_submit_to_queue(zmk_display_work_q(), &menu_ui_work);
}

/* Event Listeners */
int menu_position_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state) {
        return 0;
    }

    /* Toggle trigger: position 1 on Layer 6 (MOUS) */
    if (ev->position == POS_F21_L6 && zmk_keymap_highest_layer_active() == 6) {
        menu_controller_toggle();
        zmk_hid_keyboard_clear();
        return 0;
    }

    if (!menu_is_active) {
        return 0;
    }

    /* Suppress keystrokes from host */
    zmk_hid_keyboard_clear();

    /* Matrix navigation routing */
    if (ev->position == POS_W) {
        menu_controller_handle_nav(MENU_NAV_UP);
    } else if (ev->position == POS_S) {
        menu_controller_handle_nav(MENU_NAV_DOWN);
    } else if (ev->position == POS_A) {
        menu_controller_handle_nav(MENU_NAV_DEC);
    } else if (ev->position == POS_D) {
        menu_controller_handle_nav(MENU_NAV_INC);
    } else if (ev->position == POS_ENTER_R ||
               ev->position == POS_SPACE_L || ev->position == POS_SPACE_R) {
        menu_controller_handle_nav(MENU_NAV_SELECT);
    } else if (ev->position == POS_ESC) {
        menu_controller_handle_nav(MENU_NAV_EXIT);
    }

    return 0;
}

ZMK_LISTENER(menu_pos_tracker, menu_position_state_changed_listener);
ZMK_SUBSCRIPTION(menu_pos_tracker, zmk_position_state_changed);

int menu_keycode_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (!ev || !ev->state) {
        return 0;
    }

    uint32_t usage = ev->keycode & 0xFF;

    /* Toggle trigger: F21 keycode */
    if (usage == HID_KEY_F21) {
        menu_controller_toggle();
        zmk_hid_keyboard_clear();
        return 0;
    }

    if (!menu_is_active) {
        return 0;
    }

    /* Suppress keystrokes from host */
    zmk_hid_keyboard_clear();

    /* Keycode navigation routing */
    if (usage == HID_KEY_W || usage == HID_KEY_UP) {
        menu_controller_handle_nav(MENU_NAV_UP);
    } else if (usage == HID_KEY_S || usage == HID_KEY_DOWN) {
        menu_controller_handle_nav(MENU_NAV_DOWN);
    } else if (usage == HID_KEY_A || usage == HID_KEY_LEFT) {
        menu_controller_handle_nav(MENU_NAV_DEC);
    } else if (usage == HID_KEY_D || usage == HID_KEY_RIGHT) {
        menu_controller_handle_nav(MENU_NAV_INC);
    } else if (usage == HID_KEY_RETURN || usage == HID_KEY_SPACE) {
        menu_controller_handle_nav(MENU_NAV_SELECT);
    } else if (usage == HID_KEY_ESCAPE) {
        menu_controller_handle_nav(MENU_NAV_EXIT);
    }

    return 0;
}

ZMK_LISTENER(menu_key_tracker, menu_keycode_state_changed_listener);
ZMK_SUBSCRIPTION(menu_key_tracker, zmk_keycode_state_changed);

/* Master Menu Root Initialization */
int menu_controller_init(lv_obj_t *screen) {
    if (!screen) {
        return -EINVAL;
    }

    /* Menu Root Container (280x240 px) */
    menu_root_container = lv_obj_create(screen);
    lv_obj_set_size(menu_root_container, 280, 240);
    lv_obj_set_pos(menu_root_container, 0, 0);
    lv_obj_set_style_bg_color(menu_root_container, lv_color_hex(0x0a0d14), 0);
    lv_obj_set_style_bg_opa(menu_root_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(menu_root_container, lv_color_hex(0x1f293d), 0);
    lv_obj_set_style_border_width(menu_root_container, 1, 0);
    lv_obj_set_style_radius(menu_root_container, 6, 0);
    lv_obj_set_style_pad_all(menu_root_container, 0, 0);
    lv_obj_clear_flag(menu_root_container, LV_OBJ_FLAG_SCROLLABLE);

    /* Initially Hidden (Dashboard is default) */
    lv_obj_add_flag(menu_root_container, LV_OBJ_FLAG_HIDDEN);

    /* Header Title */
    lv_obj_t *header_label = lv_label_create(menu_root_container);
    lv_label_set_text(header_label, ">> CYBER-HUD CONFIG <<");
    lv_obj_set_style_text_color(header_label, lv_color_hex(0x00d2ff), 0);
    lv_obj_align(header_label, LV_ALIGN_TOP_MID, 0, 6);

    /* Top Separator */
    lv_obj_t *sep_top = lv_obj_create(menu_root_container);
    lv_obj_set_size(sep_top, 268, 1);
    lv_obj_set_pos(sep_top, 6, 26);
    lv_obj_set_style_bg_color(sep_top, lv_color_hex(0x1f293d), 0);
    lv_obj_set_style_bg_opa(sep_top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep_top, 0, 0);

    /* 7 Menu Items */
    for (uint8_t i = 0; i < MENU_ITEM_COUNT; i++) {
        row_containers[i] = lv_obj_create(menu_root_container);
        lv_obj_set_size(row_containers[i], 268, 22);
        lv_obj_set_pos(row_containers[i], 6, 32 + (i * 24));
        lv_obj_clear_flag(row_containers[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(row_containers[i], 0, 0);
        lv_obj_set_style_bg_opa(row_containers[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row_containers[i], 0, 0);

        row_labels_left[i] = lv_label_create(row_containers[i]);
        lv_obj_align(row_labels_left[i], LV_ALIGN_LEFT_MID, 6, 0);

        row_labels_right[i] = lv_label_create(row_containers[i]);
        lv_obj_align(row_labels_right[i], LV_ALIGN_RIGHT_MID, -6, 0);
    }

    /* Bottom Separator */
    lv_obj_t *sep_bot = lv_obj_create(menu_root_container);
    lv_obj_set_size(sep_bot, 268, 1);
    lv_obj_set_pos(sep_bot, 6, 204);
    lv_obj_set_style_bg_color(sep_bot, lv_color_hex(0x1f293d), 0);
    lv_obj_set_style_bg_opa(sep_bot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep_bot, 0, 0);

    /* Navigation Footer */
    lv_obj_t *footer_label = lv_label_create(menu_root_container);
    lv_label_set_text(footer_label, "W/S:Nav  A/D:Adj  RET:Sel  ESC:Exit");
    lv_obj_set_style_text_color(footer_label, lv_color_hex(0x707e94), 0);
    lv_obj_align(footer_label, LV_ALIGN_BOTTOM_MID, 0, -4);

    render_menu_items();
    layer_inspector_init(screen);
    return 0;
}
