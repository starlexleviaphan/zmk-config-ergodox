/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/split/central.h>
#include <zmk/display.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/usb.h>

#include "widgets/battery_status.h"
#include "brightness.h"

#define NUM_PERIPHERALS 3

enum peripheral_role {
    ROLE_UNKNOWN = 0,
    ROLE_LEFT,
    ROLE_RIGHT,
    ROLE_TOUCHPAD,
};

#define COL_LEFT 0
#define COL_RIGHT 1
#define COL_TOUCHPAD 2

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct battery_state {
    uint8_t source;
    uint8_t level;
    bool usb_present;
};

struct battery_column {
    lv_obj_t *symbol;
    lv_obj_t *label;
};

static struct battery_column battery_objects[NUM_PERIPHERALS];

#define BAR_WIDTH 54
#define BAR_HEIGHT 6
static lv_color_t battery_image_buffer[NUM_PERIPHERALS][BAR_WIDTH * BAR_HEIGHT];

static int8_t last_battery_levels[NUM_PERIPHERALS] = {-1, -1, -1};
static int source_to_col[NUM_PERIPHERALS] = {COL_LEFT, COL_RIGHT, COL_TOUCHPAD};
static enum peripheral_role source_roles[NUM_PERIPHERALS] = {ROLE_UNKNOWN, ROLE_UNKNOWN, ROLE_UNKNOWN};

static inline bool is_right_key(uint32_t pos) {
    if (pos >= 76) return false;
    static const bool right_keys[76] = {
        /* Row 0: 0..6 (L), 7..13 (R) */
        0, 0, 0, 0, 0, 0, 0,  1, 1, 1, 1, 1, 1, 1,
        /* Row 1: 14..20 (L), 21..27 (R) */
        0, 0, 0, 0, 0, 0, 0,  1, 1, 1, 1, 1, 1, 1,
        /* Row 2: 28..34 (L), 35..41 (R) */
        0, 0, 0, 0, 0, 0, 0,  1, 1, 1, 1, 1, 1, 1,
        /* Row 3: 42..47 (L), 48..53 (R) */
        0, 0, 0, 0, 0, 0,     1, 1, 1, 1, 1, 1,
        /* Row 4: 54..58 (L), 59..63 (R) */
        0, 0, 0, 0, 0,        1, 1, 1, 1, 1,
        /* Thumbs: 64,65 (L), 66,67 (R) */
        0, 0, 1, 1,
        /* Thumbs: 68..70 (L), 71..73 (R) */
        0, 0, 0, 1, 1, 1,
        /* Thumbs: 74 (L), 75 (R) */
        0, 1
    };
    return right_keys[pos];
}

/* Periodic poll work item */
static struct k_work_delayable poll_battery_work;

static void update_display(void);

/* --- Persistent Settings for Peripheral Roles --- */
#if IS_ENABLED(CONFIG_SETTINGS)
static int role_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    if (settings_name_steq(name, "source_roles", &next) && !next) {
        if (len != sizeof(source_roles)) {
            return -EINVAL;
        }
        enum peripheral_role loaded_roles[NUM_PERIPHERALS];
        int rc = read_cb(cb_arg, loaded_roles, sizeof(loaded_roles));
        if (rc >= 0) {
            bool valid = true;
            int left_cnt = 0, right_cnt = 0, tp_cnt = 0;
            for (int i = 0; i < NUM_PERIPHERALS; i++) {
                if (loaded_roles[i] == ROLE_LEFT) left_cnt++;
                else if (loaded_roles[i] == ROLE_RIGHT) right_cnt++;
                else if (loaded_roles[i] == ROLE_TOUCHPAD) tp_cnt++;
                else if (loaded_roles[i] != ROLE_UNKNOWN) valid = false;
            }
            if (left_cnt > 1 || right_cnt > 1 || tp_cnt > 1) {
                valid = false;
            }

            if (valid) {
                for (int i = 0; i < NUM_PERIPHERALS; i++) {
                    source_roles[i] = loaded_roles[i];
                    if (source_roles[i] == ROLE_LEFT) source_to_col[i] = COL_LEFT;
                    else if (source_roles[i] == ROLE_RIGHT) source_to_col[i] = COL_RIGHT;
                    else if (source_roles[i] == ROLE_TOUCHPAD) source_to_col[i] = COL_TOUCHPAD;
                }
                LOG_INF("Loaded peripheral roles from settings: [%d, %d, %d]",
                        source_roles[0], source_roles[1], source_roles[2]);
                update_display();
                return 0;
            } else {
                LOG_WRN("Discarded invalid peripheral roles from settings");
            }
        }
    }
    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(ergodox_roles_v2, "ergodox_roles_v2", NULL, role_settings_set, NULL, NULL);

static void save_roles_to_settings(void) {
    int err = settings_save_one("ergodox_roles_v2/source_roles", source_roles, sizeof(source_roles));
    if (err) {
        LOG_WRN("Failed to save peripheral roles to settings (err %d)", err);
    } else {
        LOG_INF("Saved peripheral roles to settings: [%d, %d, %d]",
                source_roles[0], source_roles[1], source_roles[2]);
    }
}
#else
static void save_roles_to_settings(void) {}
#endif

static void init_peripheral_tracking(void) {
    for (int i = 0; i < NUM_PERIPHERALS; i++) {
        last_battery_levels[i] = -1;
    }
}

static void draw_battery(lv_obj_t *canvas, int8_t level) {
    if (level < 1) {
        lv_canvas_fill_bg(canvas, lv_palette_main(LV_PALETTE_RED), LV_OPA_COVER);
        return;
    }

    lv_color_t border_color = (level <= 10) ? lv_palette_main(LV_PALETTE_YELLOW) : lv_color_white();
    lv_canvas_fill_bg(canvas, border_color, LV_OPA_COVER);

    int inner_w = 46;
    int filled_w = (level * inner_w) / 100;
    if (filled_w > inner_w) filled_w = inner_w;
    int uncharged_w = inner_w - filled_w;

    if (uncharged_w > 0) {
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = lv_color_black();
        rect_dsc.bg_opa = LV_OPA_COVER;
        lv_canvas_draw_rect(canvas, 1 + filled_w, 1, uncharged_w, 4, &rect_dsc);
    }

    // Anti-aliased / rounded corners
    lv_canvas_set_px(canvas, 0, 0, lv_color_black());
    lv_canvas_set_px(canvas, 0, BAR_HEIGHT - 1, lv_color_black());
    lv_canvas_set_px(canvas, BAR_WIDTH - 1, 0, lv_color_black());
    lv_canvas_set_px(canvas, BAR_WIDTH - 1, BAR_HEIGHT - 1, lv_color_black());

    // Terminal nub on positive (right) side
    lv_canvas_set_px(canvas, BAR_WIDTH - 2, 0, lv_color_black());
    lv_canvas_set_px(canvas, BAR_WIDTH - 2, BAR_HEIGHT - 1, lv_color_black());
    lv_canvas_set_px(canvas, BAR_WIDTH - 1, 1, lv_color_black());
    lv_canvas_set_px(canvas, BAR_WIDTH - 1, BAR_HEIGHT - 2, lv_color_black());
}

static void update_display(void) {
    const char *col_tags[NUM_PERIPHERALS] = {"L", "R", "TP"};

    for (int col = 0; col < NUM_PERIPHERALS; col++) {
        int src = -1;
        for (int i = 0; i < NUM_PERIPHERALS; i++) {
            if (source_roles[i] != ROLE_UNKNOWN && source_to_col[i] == col) {
                src = i;
                break;
            }
        }

        int8_t level = (src >= 0) ? last_battery_levels[src] : -1;
        lv_obj_t *symbol = battery_objects[col].symbol;
        lv_obj_t *label = battery_objects[col].label;

        if (!symbol || !label) continue;

        draw_battery(symbol, level);

        if (level > 0) {
            if (level <= 10) {
                lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_YELLOW), 0);
            } else {
                lv_obj_set_style_text_color(label, lv_color_white(), 0);
            }
            lv_label_set_text_fmt(label, "%s %u%%", col_tags[col], (uint8_t)level);
        } else {
            lv_obj_set_style_text_color(label, lv_palette_main(LV_PALETTE_RED), 0);
            lv_label_set_text_fmt(label, "%s --", col_tags[col]);
        }

        lv_obj_clear_flag(symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void handle_key_press(uint8_t source, uint32_t position) {
    if (source >= NUM_PERIPHERALS || position >= 76) return;

    enum peripheral_role new_role = is_right_key(position) ? ROLE_RIGHT : ROLE_LEFT;

    if (source_roles[source] == new_role) return;

    LOG_INF("Key pos %u from source %u -> role %s",
            position, source, (new_role == ROLE_RIGHT) ? "RIGHT" : "LEFT");

    /* Clear any other source that was assigned this role */
    for (int i = 0; i < NUM_PERIPHERALS; i++) {
        if (i != source && source_roles[i] == new_role) {
            source_roles[i] = ROLE_UNKNOWN;
        }
    }

    source_roles[source] = new_role;
    source_to_col[source] = (new_role == ROLE_RIGHT) ? COL_RIGHT : COL_LEFT;

    /* Deduce the third peripheral if left and right are known */
    int left_idx = -1, right_idx = -1, unk_idx = -1;
    for (int i = 0; i < NUM_PERIPHERALS; i++) {
        if (source_roles[i] == ROLE_LEFT) left_idx = i;
        else if (source_roles[i] == ROLE_RIGHT) right_idx = i;
        else unk_idx = i;
    }

    if (left_idx != -1 && right_idx != -1 && unk_idx != -1) {
        source_roles[unk_idx] = ROLE_TOUCHPAD;
        source_to_col[unk_idx] = COL_TOUCHPAD;
        LOG_INF("Deduced touchpad at source %d", unk_idx);
    }

    update_display();

    if (left_idx != -1 && right_idx != -1 && unk_idx != -1) {
        save_roles_to_settings();
    }
}

static void set_battery_symbol(lv_obj_t *widget, struct battery_state state) {
    if (state.source >= NUM_PERIPHERALS) return;

    int8_t prev = last_battery_levels[state.source];
    bool reconnecting = (prev < 1 && state.level >= 1);
    last_battery_levels[state.source] = state.level;

    if (reconnecting) {
#if CONFIG_DONGLE_SCREEN_IDLE_TIMEOUT_S > 0
        LOG_INF("Peripheral %d reconnected (%d%%), waking screen", state.source, state.level);
        brightness_wake_screen_on_reconnect();
#endif
    }

    update_display();
}

void battery_status_update_cb(struct battery_state state) {
    struct zmk_widget_dongle_battery_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_battery_symbol(widget->obj, state);
    }
}

static struct battery_state peripheral_battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev = as_zmk_peripheral_battery_state_changed(eh);
    return (struct battery_state){
        .source = ev->source,
        .level = ev->state_of_charge,
    };
}

static struct battery_state battery_status_get_state(const zmk_event_t *eh) {
    return peripheral_battery_status_get_state(eh);
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_dongle_battery_status, struct battery_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_dongle_battery_status, zmk_peripheral_battery_state_changed);

static int position_state_changed_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev && ev->state && ev->source < NUM_PERIPHERALS) {
        handle_key_press(ev->source, ev->position);
    }
    return 0;
}

ZMK_LISTENER(battery_key_tracker, position_state_changed_listener);
ZMK_SUBSCRIPTION(battery_key_tracker, zmk_position_state_changed);

/* Periodic battery refresh to sync battery levels continuously */
static void poll_battery_work_handler(struct k_work *work) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    bool changed = false;
    for (uint8_t i = 0; i < NUM_PERIPHERALS; i++) {
        uint8_t level = 0;
        if (zmk_split_central_get_peripheral_battery_level(i, &level) == 0) {
            if (last_battery_levels[i] != (int8_t)level) {
                last_battery_levels[i] = (int8_t)level;
                changed = true;
            }
        }
    }
    if (changed) {
        update_display();
    }
#endif
    /* Poll every 10 seconds */
    k_work_schedule_for_queue(zmk_display_work_q(), &poll_battery_work, K_SECONDS(10));
}

int zmk_widget_dongle_battery_status_init(struct zmk_widget_dongle_battery_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 240, 40);

    const int col_centers[NUM_PERIPHERALS] = {-80, 0, 80};

    for (int col = 0; col < NUM_PERIPHERALS; col++) {
        lv_obj_t *image_canvas = lv_canvas_create(widget->obj);
        lv_obj_t *battery_label = lv_label_create(widget->obj);

        lv_canvas_set_buffer(image_canvas, battery_image_buffer[col], BAR_WIDTH, BAR_HEIGHT, LV_IMG_CF_TRUE_COLOR);

        lv_obj_align(image_canvas, LV_ALIGN_BOTTOM_MID, col_centers[col], -6);
        lv_obj_align(battery_label, LV_ALIGN_TOP_MID, col_centers[col], 2);

        battery_objects[col] = (struct battery_column){
            .symbol = image_canvas,
            .label = battery_label,
        };
    }

    sys_slist_append(&widgets, &widget->node);
    init_peripheral_tracking();

    /* Initial fetch from central battery cache if peripherals already reported */
#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    for (uint8_t i = 0; i < NUM_PERIPHERALS; i++) {
        uint8_t level = 0;
        if (zmk_split_central_get_peripheral_battery_level(i, &level) == 0 && level > 0) {
            last_battery_levels[i] = (int8_t)level;
        }
    }
#endif

    update_display();
    widget_dongle_battery_status_init();

    /* Initialize and schedule periodic battery polling */
    k_work_init_delayable(&poll_battery_work, poll_battery_work_handler);
    k_work_schedule_for_queue(zmk_display_work_q(), &poll_battery_work, K_SECONDS(5));

    return 0;
}

lv_obj_t *zmk_widget_dongle_battery_status_obj(struct zmk_widget_dongle_battery_status *widget) {
    return widget->obj;
}
