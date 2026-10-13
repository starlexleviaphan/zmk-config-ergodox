/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>

#include "output_status.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool usb_is_hid_ready;
};

static struct output_status_state get_state(const zmk_event_t *_eh) {
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
        .usb_is_hid_ready = zmk_usb_is_hid_ready(),
    };
}

static void set_status_symbol(struct zmk_widget_output_status *widget, struct output_status_state state) {
    if (!widget || !widget->obj || !widget->label) {
        return;
    }

    char buf[48];
    bool is_usb = (state.selected_endpoint.transport == ZMK_TRANSPORT_USB);
    int profile_num = state.active_profile_index + 1;

    /* Single-line horizontal format:
     * When USB active: "#00f2fe USB#  #475569 BT%d#"
     * When BLE active: "#475569 USB#  #00f2fe BT%d#"
     */
    if (is_usb) {
        const char *usb_col = state.usb_is_hid_ready ? "00f2fe" : "ff1744";
        const char *ble_col = state.active_profile_connected ? "43e97b" : "475569";
        snprintf(buf, sizeof(buf), "#%s USB#  #%s BT%d#", usb_col, ble_col, profile_num);
    } else {
        const char *usb_col = state.usb_is_hid_ready ? "475569" : "333e4f";
        const char *ble_col = state.active_profile_connected ? "00f2fe" : "ff9100";
        snprintf(buf, sizeof(buf), "#%s USB#  #%s BT%d#", usb_col, ble_col, profile_num);
    }

    lv_label_set_recolor(widget->label, true);
    lv_label_set_text(widget->label, buf);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_output_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        set_status_symbol(widget, state);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, get_state);
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);

int zmk_widget_output_status_init(struct zmk_widget_output_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(widget->obj, 0, 0);
    lv_obj_set_style_pad_all(widget->obj, 0, 0);
    lv_obj_set_size(widget->obj, 140, 22);

    widget->label = lv_label_create(widget->obj);
    lv_obj_align(widget->label, LV_ALIGN_LEFT_MID, 0, 0);

    sys_slist_append(&widgets, &widget->node);

    struct output_status_state initial_state = get_state(NULL);
    set_status_symbol(widget, initial_state);

    widget_output_status_init();
    return 0;
}

lv_obj_t *zmk_widget_output_status_obj(struct zmk_widget_output_status *widget) {
    return widget->obj;
}
