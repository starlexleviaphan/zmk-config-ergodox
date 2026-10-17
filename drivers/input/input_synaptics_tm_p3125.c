/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT synaptics_tm_p3125

#ifndef ZMK_TOUCHPAD_UNIT_TEST
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(synaptics_tm_p3125, LOG_LEVEL_INF);
#endif

#define SYNAPTICS_REPORT_TOUCH 0x03
#define SYNAPTICS_REPORT_MOUSE 0x02

/* I2C & BLE pipeline constants */
#define SYNAPTICS_PTP_REPORT_LEN 32
#define SYNAPTICS_MAX_PACKETS_PER_PASS 6
#define SYNAPTICS_BLE_FRAME_MS 8
#define SYNAPTICS_GAP_RESET_MS 45
#define SYNAPTICS_LIFTOFF_TIMEOUT_MS 250
#define SYNAPTICS_DRAG_TIMEOUT_MS 1500

/* Precision tap & drag timings (libinput & Windows PTP standard) */
#define TAP_MAX_DURATION_MS 350
#define TAP_MAX_DISPLACEMENT 130
#define TAP_DRAG_TIMEOUT_MS 500
#define TAP_DRAG_MAX_DISTANCE 420
#define DRAG_LOCK_CONT_MS 180

/* 1F->2F landing grace window & physical clickpad zones */
#define SYNAPTICS_2F_GRACE_MS 20
#define SYNAPTICS_2F_GRACE_MAX_DISP 35
#define CLICKPAD_RIGHT_X_MIN 650
#define CLICKPAD_RIGHT_Y_MIN 650

/* Scroll, pinch & gesture thresholds */
#define SCROLL_STEP 26
#define TWO_FINGER_DEADZONE_TRAVEL 45
#define TWO_FINGER_DEADZONE_SPAN 55
#define PINCH_MIN_INITIAL_SPAN 160
#define PINCH_STEP 85
#define THREE_FINGER_SWIPE_THRESHOLD 120

/* Palm rejection: edge exclusion margin and interior recovery (Sensor X range: 0..1219) */
#define PALM_EDGE_LEFT 70
#define PALM_EDGE_RIGHT 1150
#define PALM_INTERIOR_LEFT 100
#define PALM_INTERIOR_RIGHT 1120

enum synaptics_2f_mode {
  TWO_FINGER_IDLE = 0,
  TWO_FINGER_SCROLL,
  TWO_FINGER_PINCH,
};

static inline bool synaptics_slot_valid(uint8_t status) {
  return (status & 0x03) == 0x03;
}

struct synaptics_config {
  struct i2c_dt_spec i2c;
  struct gpio_dt_spec irq_gpio;
};

struct synaptics_data {
  const struct device *dev;
  struct k_work_delayable work;
  struct k_work_delayable init_work;
  struct k_work_delayable heartbeat_work;
  struct gpio_callback gpio_cb;

  /* Tap, pinch, and swipe release works */
  struct k_work_delayable tap_release_work;
  struct k_work_delayable tap_right_release_work;
  struct k_work_delayable tap_middle_release_work;
  struct k_work_delayable swipe_up_release_work;
  struct k_work_delayable swipe_down_release_work;
  struct k_work_delayable pinch_in_release_work;
  struct k_work_delayable pinch_out_release_work;
  struct k_work_delayable inertial_scroll_work;

  /* Active I2C address */
  uint16_t active_addr;

  /* Contact tracking across touch sequence */
  uint8_t gesture_max_fingers;

  /* 1-Finger tracking & Ballistics */
  bool prev_touching;
  uint16_t prev_x0;
  uint16_t prev_y0;
  int64_t prev_x0_time;
  uint16_t touch_start_x;
  uint16_t touch_start_y;
  uint16_t touch_max_disp;
  int64_t touch_start_time;
  int64_t last_packet_time;
  int16_t prev_raw_dx;
  int16_t prev_raw_dy;
  int16_t subpixel_rem_x;
  int16_t subpixel_rem_y;
  int16_t grace_dx;
  int16_t grace_dy;
  bool in_grace_window;

  /* Tap and Drag state machine */
  uint16_t last_tap_x;
  uint16_t last_tap_y;
  int64_t last_tap_release_time;
  bool is_tap_dragging;

  /* 2-Finger scroll, pinch & Inertia tracking */
  bool prev_two_finger;
  enum synaptics_2f_mode two_finger_mode;
  uint16_t two_finger_initial_span;
  uint16_t prev_span;
  uint16_t two_finger_travel;
  int16_t two_finger_span_delta;
  int16_t pinch_accum;
  int64_t last_pinch_time;
  uint16_t prev_scroll_x;
  uint16_t prev_scroll_y;
  int16_t scroll_accum_y;
  int16_t scroll_accum_x;
  int64_t two_finger_start_time;
  int64_t two_finger_release_time;
  bool two_finger_scrolled;
  int16_t last_scroll_dy;
  int16_t inertial_dy;

  /* 3-Finger gesture tracking */
  bool prev_three_finger;
  int64_t three_finger_start_time;
  uint16_t three_finger_start_y;
  bool three_finger_swiped;

  /* Physical clickpad button & zone state */
  bool prev_btn_left;
  uint16_t active_phys_btn;
  bool phys_btn_used_in_touch;

  /* 8 ms BLE report coalescer & palm suppression state */
  int16_t pend_dx;
  int16_t pend_dy;
  int16_t pend_wheel;
  int16_t pend_hwheel;
  int64_t last_ble_report_time;
  bool has_ble_report;
  bool palm_suppressed;
};

static void synaptics_flush_reports(struct synaptics_data *data, int64_t now,
                                    bool force) {
  if (data->pend_dx == 0 && data->pend_dy == 0 && data->pend_wheel == 0 &&
      data->pend_hwheel == 0) {
    return;
  }

  if (force || !data->has_ble_report ||
      (now - data->last_ble_report_time) >= SYNAPTICS_BLE_FRAME_MS) {
    const struct device *dev = data->dev;

    if (data->pend_dx != 0) {
      bool sync_x = (data->pend_dy == 0 && data->pend_wheel == 0 &&
                     data->pend_hwheel == 0);
      input_report_rel(dev, INPUT_REL_X, data->pend_dx, sync_x, K_NO_WAIT);
      data->pend_dx = 0;
    }
    if (data->pend_dy != 0) {
      bool sync_y = (data->pend_wheel == 0 && data->pend_hwheel == 0);
      input_report_rel(dev, INPUT_REL_Y, data->pend_dy, sync_y, K_NO_WAIT);
      data->pend_dy = 0;
    }
    if (data->pend_wheel != 0) {
      bool sync_w = (data->pend_hwheel == 0);
      input_report_rel(dev, INPUT_REL_WHEEL, data->pend_wheel, sync_w,
                       K_NO_WAIT);
      data->pend_wheel = 0;
    }
    if (data->pend_hwheel != 0) {
      input_report_rel(dev, INPUT_REL_HWHEEL, data->pend_hwheel, true,
                       K_NO_WAIT);
      data->pend_hwheel = 0;
    }

    data->last_ble_report_time = now;
    data->has_ble_report = true;
  }
}

static void synaptics_apply_ballistics(struct synaptics_data *data,
                                       int16_t raw_dx, int16_t raw_dy,
                                       int16_t *out_dx, int16_t *out_dy) {
  int16_t abs_dx = raw_dx >= 0 ? raw_dx : -raw_dx;
  int16_t abs_dy = raw_dy >= 0 ? raw_dy : -raw_dy;
  int16_t v = abs_dx + abs_dy;

  int16_t smooth_dx;
  int16_t smooth_dy;
  if (v <= 6) {
    smooth_dx = (int16_t)((3 * (int32_t)raw_dx + data->prev_raw_dx) / 4);
    smooth_dy = (int16_t)((3 * (int32_t)raw_dy + data->prev_raw_dy) / 4);
    if (smooth_dx == 0 && raw_dx != 0) {
      smooth_dx = raw_dx;
    }
    if (smooth_dy == 0 && raw_dy != 0) {
      smooth_dy = raw_dy;
    }
  } else {
    smooth_dx = raw_dx;
    smooth_dy = raw_dy;
  }

  data->prev_raw_dx = raw_dx;
  data->prev_raw_dy = raw_dy;

  int32_t gain;
  if (v <= 6) {
    gain = 12;
  } else if (v <= 22) {
    gain = 16;
  } else if (v <= 50) {
    gain = 24;
  } else {
    gain = 32;
  }

  int32_t scaled_x = (int32_t)smooth_dx * gain + data->subpixel_rem_x;
  int32_t scaled_y = (int32_t)smooth_dy * gain + data->subpixel_rem_y;

  *out_dx = (int16_t)(scaled_x / 16);
  data->subpixel_rem_x = (int16_t)(scaled_x % 16);
  *out_dy = (int16_t)(scaled_y / 16);
  data->subpixel_rem_y = (int16_t)(scaled_y % 16);
}

static void synaptics_process_clickpad(struct synaptics_data *data,
                                       bool physical_btn, uint8_t contacts,
                                       uint16_t x0, uint16_t y0, int64_t now) {
  if (physical_btn || data->prev_btn_left) {
    data->phys_btn_used_in_touch = true;
  }
  if (physical_btn != data->prev_btn_left) {
    const struct device *dev = data->dev;
    data->prev_btn_left = physical_btn;
    synaptics_flush_reports(data, now, true);
    if (physical_btn) {
      uint16_t btn;
      if (contacts >= 3 || data->gesture_max_fingers >= 3) {
        btn = INPUT_BTN_MIDDLE;
      } else if (contacts == 2 || data->gesture_max_fingers == 2) {
        btn = INPUT_BTN_RIGHT;
      } else if (contacts == 1 && x0 > CLICKPAD_RIGHT_X_MIN &&
                 y0 > CLICKPAD_RIGHT_Y_MIN) {
        btn = INPUT_BTN_RIGHT;
      } else {
        btn = INPUT_BTN_LEFT;
      }
      data->active_phys_btn = btn;
      data->phys_btn_used_in_touch = true;
      input_report_key(dev, btn, 1, true, K_NO_WAIT);
    } else {
      uint16_t rel_btn =
          data->active_phys_btn ? data->active_phys_btn : INPUT_BTN_LEFT;
      input_report_key(dev, rel_btn, 0, true, K_NO_WAIT);
      data->active_phys_btn = 0;
    }
  }
}

static void synaptics_tap_release_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, tap_release_work);
  if (data->is_tap_dragging) {
    data->is_tap_dragging = false;
    data->last_tap_release_time = 0;
  }
  input_report_key(data->dev, INPUT_BTN_LEFT, 0, true, K_NO_WAIT);
}

static void synaptics_tap_right_release_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, tap_right_release_work);
  input_report_key(data->dev, INPUT_BTN_RIGHT, 0, true, K_NO_WAIT);
}

static void synaptics_tap_middle_release_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, tap_middle_release_work);
  input_report_key(data->dev, INPUT_BTN_MIDDLE, 0, true, K_NO_WAIT);
}

static void synaptics_swipe_up_release_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, swipe_up_release_work);
  input_report_key(data->dev, INPUT_BTN_3, 0, true, K_NO_WAIT);
}

static void synaptics_swipe_down_release_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, swipe_down_release_work);
  input_report_key(data->dev, INPUT_BTN_4, 0, true, K_NO_WAIT);
}

static void synaptics_pinch_in_release_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, pinch_in_release_work);
  input_report_key(data->dev, INPUT_BTN_5, 0, true, K_NO_WAIT);
}

static void synaptics_pinch_out_release_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, pinch_out_release_work);
  input_report_key(data->dev, INPUT_BTN_6, 0, true, K_NO_WAIT);
}

static void synaptics_inertial_scroll_handler(struct k_work *work) {
  (void)work;
  /* No-op: OS native smooth scrolling handles deceleration without reverse bounce */
}

static void synaptics_process_liftoff(struct synaptics_data *data) {
  const struct device *dev = data->dev;

  synaptics_flush_reports(data, k_uptime_get(), true);
  data->has_ble_report = false;

  data->grace_dx = 0;
  data->grace_dy = 0;
  data->in_grace_window = false;
  data->prev_raw_dx = 0;
  data->prev_raw_dy = 0;
  data->subpixel_rem_x = 0;
  data->subpixel_rem_y = 0;

  if (data->gesture_max_fingers >= 3) {
    if (data->prev_three_finger) {
      int64_t dur3 = k_uptime_get() - data->three_finger_start_time;
      if (!data->three_finger_swiped && !data->phys_btn_used_in_touch &&
          dur3 < TAP_MAX_DURATION_MS) {
        /* 3-Finger Tap: Middle Click */
        input_report_key(dev, INPUT_BTN_MIDDLE, 1, true, K_NO_WAIT);
        k_work_reschedule(&data->tap_middle_release_work, K_MSEC(40));
      }
      data->prev_three_finger = false;
    }
    if (data->three_finger_swiped) {
      /* Guarantee any 3-finger swipe modifier is immediately released upon finger lift */
      k_work_cancel_delayable(&data->swipe_up_release_work);
      k_work_cancel_delayable(&data->swipe_down_release_work);
      input_report_key(dev, INPUT_BTN_3, 0, true, K_NO_WAIT);
      input_report_key(dev, INPUT_BTN_4, 0, true, K_NO_WAIT);
      data->three_finger_swiped = false;
    }
  } else if (data->gesture_max_fingers == 2) {
    if (data->prev_two_finger) {
      int64_t dur2 = k_uptime_get() - data->two_finger_start_time;
      if (data->two_finger_mode == TWO_FINGER_IDLE &&
          !data->phys_btn_used_in_touch && dur2 < TAP_MAX_DURATION_MS) {
        /* 2-Finger Tap: Right Click */
        input_report_key(dev, INPUT_BTN_RIGHT, 1, true, K_NO_WAIT);
        k_work_reschedule(&data->tap_right_release_work, K_MSEC(40));
      }
      if (data->two_finger_mode == TWO_FINGER_PINCH) {
        k_work_cancel_delayable(&data->pinch_in_release_work);
        k_work_cancel_delayable(&data->pinch_out_release_work);
        input_report_key(dev, INPUT_BTN_5, 0, true, K_NO_WAIT);
        input_report_key(dev, INPUT_BTN_6, 0, true, K_NO_WAIT);
      }
      k_work_cancel_delayable(&data->inertial_scroll_work);
      data->inertial_dy = 0;
      data->last_scroll_dy = 0;
      data->scroll_accum_y = 0;
      data->scroll_accum_x = 0;
      data->pinch_accum = 0;
      data->two_finger_travel = 0;
      data->two_finger_span_delta = 0;
      data->two_finger_mode = TWO_FINGER_IDLE;
      data->prev_two_finger = false;
      data->two_finger_scrolled = false;
      data->two_finger_release_time = k_uptime_get();
    }
  } else if (data->gesture_max_fingers == 1 || data->prev_touching) {
    if (data->prev_touching) {
      /* Calculate true touch duration using the timestamp of the last active touch packet */
      int64_t touch_end = (data->last_packet_time > data->touch_start_time)
                              ? data->last_packet_time
                              : k_uptime_get();
      int64_t duration = touch_end - data->touch_start_time;

      if (data->is_tap_dragging) {
        /* Release Left button immediately upon finger liftoff */
        k_work_cancel_delayable(&data->tap_release_work);
        input_report_key(dev, INPUT_BTN_LEFT, 0, true, K_NO_WAIT);
        data->is_tap_dragging = false;
        data->last_tap_release_time = 0;
      } else if (!data->palm_suppressed && !data->phys_btn_used_in_touch &&
                 duration < TAP_MAX_DURATION_MS &&
                 data->touch_max_disp < TAP_MAX_DISPLACEMENT) {
        /* Single Tap: Click */
        input_report_key(dev, INPUT_BTN_LEFT, 1, true, K_NO_WAIT);
        k_work_reschedule(&data->tap_release_work, K_MSEC(40));
        data->last_tap_release_time = touch_end;
        data->last_tap_x = data->touch_start_x;
        data->last_tap_y = data->touch_start_y;
      }
      data->prev_touching = false;
    }
  }

  data->gesture_max_fingers = 0;
  data->prev_touching = false;
  data->prev_two_finger = false;
  data->prev_three_finger = false;
  data->palm_suppressed = false;
  data->phys_btn_used_in_touch = false;
}

static void synaptics_work_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data = CONTAINER_OF(dwork, struct synaptics_data, work);
  const struct device *dev = data->dev;
  const struct synaptics_config *config = dev->config;

  int packets_read = 0;

  /* Read bounded batch of pending packets while INT is asserted (active-low GPIO returns > 0) */
  for (int iter = 0; iter < SYNAPTICS_MAX_PACKETS_PER_PASS; iter++) {
    int pin_active = gpio_pin_get_dt(&config->irq_gpio);
    if (pin_active <= 0) {
      break;
    }

    /* Read exact 32-byte PTP report to minimize I2C bus transaction time */
    uint8_t buf[SYNAPTICS_PTP_REPORT_LEN];
    int ret = i2c_read(config->i2c.bus, buf, sizeof(buf), data->active_addr);
    if (ret < 0) {
      break;
    }

    packets_read++;
    data->last_packet_time = k_uptime_get();

    uint8_t report_id = buf[2];

    if (report_id == SYNAPTICS_REPORT_TOUCH) {
      /* Slot 0 (Finger 1) */
      uint8_t status0 = buf[3];
      bool tip0 = data->is_tap_dragging ? ((status0 & 0x02) != 0)
                                        : synaptics_slot_valid(status0);
      uint16_t x0 = (uint16_t)(buf[4] | (buf[5] << 8));
      uint16_t y0 = (uint16_t)(buf[6] | (buf[7] << 8));

      /* Slot 1 (Finger 2) */
      uint8_t status1 = buf[8];
      bool tip1 = synaptics_slot_valid(status1);
      uint16_t x1 = (uint16_t)(buf[9] | (buf[10] << 8));
      uint16_t y1 = (uint16_t)(buf[11] | (buf[12] << 8));

      /* Slot 2 (Finger 3) & contact count */
      uint8_t status2 = buf[13];
      bool tip2 = synaptics_slot_valid(status2);
      uint16_t y2 = (uint16_t)(buf[16] | (buf[17] << 8));
      uint8_t contact_count = buf[30];
      (void)contact_count;

      /* Determine active contact count from validated hardware tip+confidence slots */
      uint8_t contacts = 0;
      if (tip0) contacts++;
      if (tip1) contacts++;
      if (tip2) contacts++;

      /* Track maximum concurrent fingers in the current touch sequence */
      if (contacts > data->gesture_max_fingers) {
        data->gesture_max_fingers = contacts;
      }

      /* Physical clickpad button & zone routing */
      bool physical_btn = (buf[31] & 0x01) != 0;
      synaptics_process_clickpad(data, physical_btn, contacts, x0, y0,
                                 k_uptime_get());

      /* ========================================================================= */
      /* CASE 1: ALL FINGERS LIFTED (Touch sequence completion & tap recognition)  */
      /* ========================================================================= */
      if (contacts == 0) {
        synaptics_process_liftoff(data);
      }
      /* ========================================================================= */
      /* CASE 2: ACTIVE 3-FINGER GESTURE (Task View / Show Desktop)                */
      /* ========================================================================= */
      else if (data->gesture_max_fingers >= 3) {
        data->prev_touching = false;
        data->prev_two_finger = false;
        data->inertial_dy = 0;
        data->grace_dx = 0;
        data->grace_dy = 0;
        data->in_grace_window = false;

        /* Only compute average Y from actively touching fingers */
        if (contacts >= 2) {
          uint16_t avg3_y;
          if (tip0 && tip1 && tip2) {
            avg3_y = (y0 + y1 + y2) / 3;
          } else if (tip0 && tip1) {
            avg3_y = (y0 + y1) / 2;
          } else {
            avg3_y = y0;
          }

          if (data->prev_three_finger) {
            int16_t swipe_dy = (int16_t)avg3_y - (int16_t)data->three_finger_start_y;
            if (!data->three_finger_swiped) {
              if (swipe_dy < -THREE_FINGER_SWIPE_THRESHOLD) {
                /* Swipe Up: Task View (Win+Tab via INPUT_BTN_3) with safe delayed release */
                input_report_key(dev, INPUT_BTN_3, 1, true, K_NO_WAIT);
                k_work_reschedule(&data->swipe_up_release_work, K_MSEC(40));
                data->three_finger_swiped = true;
              } else if (swipe_dy > THREE_FINGER_SWIPE_THRESHOLD) {
                /* Swipe Down: Show Desktop (Win+D via INPUT_BTN_4) with safe delayed release */
                input_report_key(dev, INPUT_BTN_4, 1, true, K_NO_WAIT);
                k_work_reschedule(&data->swipe_down_release_work, K_MSEC(40));
                data->three_finger_swiped = true;
              }
            }
          } else {
            data->three_finger_start_time = k_uptime_get();
            data->three_finger_start_y = avg3_y;
            data->three_finger_swiped = false;
            data->prev_three_finger = true;
          }
        }
      }
      /* ========================================================================= */
      /* CASE 3: ACTIVE 2-FINGER GESTURE (Anti-Jitter Tap, Scroll & Pinch-to-Zoom) */
      /* ========================================================================= */
      else if (data->gesture_max_fingers == 2) {
        data->prev_touching = false;
        data->inertial_dy = 0;
        data->grace_dx = 0;
        data->grace_dy = 0;
        data->in_grace_window = false;

        if (tip0 && tip1) {
          uint16_t avg_x = (x0 + x1) / 2;
          uint16_t avg_y = (y0 + y1) / 2;
          uint16_t span = (uint16_t)(((x0 > x1) ? (x0 - x1) : (x1 - x0)) +
                                     ((y0 > y1) ? (y0 - y1) : (y1 - y0)));

          if (data->prev_two_finger) {
            int16_t dx = (int16_t)avg_x - (int16_t)data->prev_scroll_x;
            int16_t dy = (int16_t)avg_y - (int16_t)data->prev_scroll_y;
            int16_t d_span = (int16_t)span - (int16_t)data->prev_span;

            data->prev_scroll_x = avg_x;
            data->prev_scroll_y = avg_y;
            data->prev_span = span;

            /* Filter out abnormal positional jumps */
            if (dx > -250 && dx < 250 && dy > -250 && dy < 250 &&
                d_span > -350 && d_span < 350) {
              int16_t abs_dx = (dx >= 0) ? dx : -dx;
              int16_t abs_dy = (dy >= 0) ? dy : -dy;

              data->two_finger_travel += (uint16_t)(abs_dx + abs_dy);
              data->two_finger_span_delta += d_span;
              int16_t abs_span_delta = (data->two_finger_span_delta >= 0)
                                           ? data->two_finger_span_delta
                                           : -data->two_finger_span_delta;

              if (data->two_finger_mode == TWO_FINGER_IDLE) {
                if (data->two_finger_initial_span >= PINCH_MIN_INITIAL_SPAN &&
                    abs_span_delta >= TWO_FINGER_DEADZONE_SPAN &&
                    (abs_span_delta * 2) >
                        ((int16_t)data->two_finger_travel * 3)) {
                  data->two_finger_mode = TWO_FINGER_PINCH;
                  data->two_finger_scrolled = true;
                  data->pinch_accum = data->two_finger_span_delta;
                } else if (data->two_finger_travel >=
                           TWO_FINGER_DEADZONE_TRAVEL) {
                  data->two_finger_mode = TWO_FINGER_SCROLL;
                  data->two_finger_scrolled = true;
                  if (abs_dy >= abs_dx) {
                    data->scroll_accum_y = dy;
                    data->scroll_accum_x = 0;
                  } else if (abs_dx > (abs_dy * 2)) {
                    data->scroll_accum_x = dx;
                    data->scroll_accum_y = 0;
                  }
                }
              } else if (data->two_finger_mode == TWO_FINGER_PINCH) {
                if ((d_span > 0 && data->pinch_accum < 0) ||
                    (d_span < 0 && data->pinch_accum > 0)) {
                  data->pinch_accum = 0;
                }
                data->pinch_accum += d_span;
              } else if (data->two_finger_mode == TWO_FINGER_SCROLL) {
                /* Directional lock: prioritize vertical if vertical movement dominates */
                if (abs_dy >= abs_dx) {
                  data->scroll_accum_y += dy;
                } else if (abs_dx > (abs_dy * 2)) {
                  /* Horizontal scroll only if distinctly horizontal */
                  data->scroll_accum_x += dx;
                }
              }

              int64_t now_ms = k_uptime_get();
              if (data->two_finger_mode == TWO_FINGER_PINCH &&
                  data->pinch_accum >= PINCH_STEP) {
                if ((now_ms - data->last_pinch_time) >= 60) {
                  data->last_pinch_time = now_ms;
                  k_work_cancel_delayable(&data->pinch_in_release_work);
                  input_report_key(dev, INPUT_BTN_5, 1, true, K_NO_WAIT);
                  k_work_reschedule(&data->pinch_in_release_work, K_MSEC(25));
                  data->pinch_accum = 0;
                }
              } else if (data->two_finger_mode == TWO_FINGER_PINCH &&
                         data->pinch_accum <= -PINCH_STEP) {
                if ((now_ms - data->last_pinch_time) >= 60) {
                  data->last_pinch_time = now_ms;
                  k_work_cancel_delayable(&data->pinch_out_release_work);
                  input_report_key(dev, INPUT_BTN_6, 1, true, K_NO_WAIT);
                  k_work_reschedule(&data->pinch_out_release_work, K_MSEC(25));
                  data->pinch_accum = 0;
                }
              }

              if (data->two_finger_mode == TWO_FINGER_SCROLL) {
                int16_t ticks_y = data->scroll_accum_y / SCROLL_STEP;
                if (ticks_y != 0) {
                  data->pend_wheel += ticks_y;
                  data->scroll_accum_y -= (ticks_y * SCROLL_STEP);
                }
                int16_t ticks_x = data->scroll_accum_x / SCROLL_STEP;
                if (ticks_x != 0) {
                  data->pend_hwheel += ticks_x;
                  data->scroll_accum_x -= (ticks_x * SCROLL_STEP);
                }
              }
            }
          } else {
            data->prev_scroll_x = avg_x;
            data->prev_scroll_y = avg_y;
            data->two_finger_initial_span = span;
            data->prev_span = span;
            data->two_finger_travel = 0;
            data->two_finger_span_delta = 0;
            data->scroll_accum_x = 0;
            data->scroll_accum_y = 0;
            data->pinch_accum = 0;
            data->two_finger_mode = TWO_FINGER_IDLE;
            data->two_finger_start_time = k_uptime_get();
            data->two_finger_scrolled = false;
            data->last_scroll_dy = 0;
            data->prev_two_finger = true;
          }
        } else {
          /* Dropout: one finger lifted or lost confidence (e.g. at sensor boundary) */
          if (data->two_finger_mode == TWO_FINGER_PINCH) {
            k_work_cancel_delayable(&data->pinch_in_release_work);
            k_work_cancel_delayable(&data->pinch_out_release_work);
            input_report_key(dev, INPUT_BTN_5, 0, true, K_NO_WAIT);
            input_report_key(dev, INPUT_BTN_6, 0, true, K_NO_WAIT);
            data->two_finger_mode = TWO_FINGER_IDLE;
            data->pinch_accum = 0;
          }
        }
      }
      /* ========================================================================= */
      /* CASE 4: ACTIVE 1-FINGER GESTURE (Cursor tracking & Tap-and-Drag)          */
      /* ========================================================================= */
      else if (data->gesture_max_fingers == 1 && tip0) {
        int64_t now = k_uptime_get();

        /* Fresh Touch Down when previous touch was lifted */
        if (!data->prev_touching) {
          /* Touch Down */
          data->touch_start_time = now;
          data->touch_start_x = x0;
          data->touch_start_y = y0;
          data->touch_max_disp = 0;
          data->inertial_dy = 0;
          data->prev_raw_dx = 0;
          data->prev_raw_dy = 0;
          data->subpixel_rem_x = 0;
          data->subpixel_rem_y = 0;
          data->palm_suppressed = (x0 < PALM_EDGE_LEFT || x0 > PALM_EDGE_RIGHT);

          int16_t d_tap_x = (int16_t)x0 - (int16_t)data->last_tap_x;
          int16_t d_tap_y = (int16_t)y0 - (int16_t)data->last_tap_y;
          if (d_tap_x < 0) d_tap_x = -d_tap_x;
          if (d_tap_y < 0) d_tap_y = -d_tap_y;

          /* Check 180 ms drag-lock continuation or Tap-and-Drag double tap condition */
          if (data->is_tap_dragging) {
            k_work_cancel_delayable(&data->tap_release_work);
            data->in_grace_window = false;
            data->grace_dx = 0;
            data->grace_dy = 0;
          } else if (!data->palm_suppressed && data->last_tap_release_time > 0 &&
                     (now - data->last_tap_release_time) < TAP_DRAG_TIMEOUT_MS &&
                     d_tap_x < TAP_DRAG_MAX_DISTANCE &&
                     d_tap_y < TAP_DRAG_MAX_DISTANCE) {
            data->is_tap_dragging = true;
            k_work_cancel_delayable(&data->tap_release_work);
            /* Guarantee host sees a clean release from Tap 1 before holding for drag */
            input_report_key(dev, INPUT_BTN_LEFT, 0, true, K_NO_WAIT);
            input_report_key(dev, INPUT_BTN_LEFT, 1, true, K_NO_WAIT);
            data->last_tap_release_time = 0;
            data->in_grace_window = false;
            data->grace_dx = 0;
            data->grace_dy = 0;
          } else {
            data->is_tap_dragging = false;
            data->in_grace_window = true;
            data->grace_dx = 0;
            data->grace_dy = 0;
          }

          data->prev_x0 = x0;
          data->prev_y0 = y0;
          data->prev_x0_time = now;
          data->prev_touching = true;
        } else if (data->palm_suppressed) {
          if (x0 >= PALM_INTERIOR_LEFT && x0 <= PALM_INTERIOR_RIGHT) {
            data->palm_suppressed = false;
            data->prev_x0 = x0;
            data->prev_y0 = y0;
            data->prev_x0_time = now;
            data->touch_start_x = x0;
            data->touch_start_y = y0;
          }
        } else if ((now - data->prev_x0_time) > SYNAPTICS_GAP_RESET_MS) {
          data->prev_x0 = x0;
          data->prev_y0 = y0;
          data->prev_x0_time = now;
          data->prev_raw_dx = 0;
          data->prev_raw_dy = 0;
          data->subpixel_rem_x = 0;
          data->subpixel_rem_y = 0;
        } else {
          /* Continuous dragging/moving: strictly compute delta from previous position */
          int16_t dx = (int16_t)x0 - (int16_t)data->prev_x0;
          int16_t dy = (int16_t)y0 - (int16_t)data->prev_y0;

          data->prev_x0 = x0;
          data->prev_y0 = y0;
          data->prev_x0_time = now;

          /* Filter out abnormal positional teleportation / glitch jumps */
          int16_t abs_dx = (dx >= 0) ? dx : -dx;
          int16_t abs_dy = (dy >= 0) ? dy : -dy;

          /* Reject sudden teleportation jump when not already in rapid motion */
          if ((abs_dx > 80 || abs_dy > 80) &&
              data->prev_raw_dx < 35 && data->prev_raw_dx > -35 &&
              data->prev_raw_dy < 35 && data->prev_raw_dy > -35) {
            data->prev_raw_dx = 0;
            data->prev_raw_dy = 0;
            data->subpixel_rem_x = 0;
            data->subpixel_rem_y = 0;
          } else if (abs_dx < 250 && abs_dy < 250) {
            /* Displacement from touch down point */
            int16_t cur_disp_x = (int16_t)x0 - (int16_t)data->touch_start_x;
            int16_t cur_disp_y = (int16_t)y0 - (int16_t)data->touch_start_y;
            if (cur_disp_x < 0) cur_disp_x = -cur_disp_x;
            if (cur_disp_y < 0) cur_disp_y = -cur_disp_y;
            if ((uint16_t)cur_disp_x > data->touch_max_disp) {
              data->touch_max_disp = (uint16_t)cur_disp_x;
            }
            if ((uint16_t)cur_disp_y > data->touch_max_disp) {
              data->touch_max_disp = (uint16_t)cur_disp_y;
            }

            int16_t b_dx = 0;
            int16_t b_dy = 0;
            synaptics_apply_ballistics(data, dx, dy, &b_dx, &b_dy);

            if (data->in_grace_window) {
              if ((now - data->touch_start_time) < SYNAPTICS_2F_GRACE_MS &&
                  data->touch_max_disp < SYNAPTICS_2F_GRACE_MAX_DISP) {
                data->grace_dx += b_dx;
                data->grace_dy += b_dy;
              } else {
                data->in_grace_window = false;
                data->pend_dx += data->grace_dx + b_dx;
                data->pend_dy += data->grace_dy + b_dy;
                data->grace_dx = 0;
                data->grace_dy = 0;
              }
            } else {
              data->pend_dx += b_dx;
              data->pend_dy += b_dy;
            }
          }
        }
      }
    } else if (report_id == SYNAPTICS_REPORT_MOUSE) {
      bool btn = (buf[3] & 0x01) != 0;
      synaptics_process_clickpad(data, btn, 0, 0, 0, k_uptime_get());
    }
  } /* End FIFO loop */

  /* Flush coalesced BLE movement/scroll reports when 8 ms frame window expires */
  synaptics_flush_reports(data, k_uptime_get(), false);

  /* Inactivity check: if sensor produced no packets for timeout, process liftoff */
  if (packets_read == 0) {
    int64_t inactive_ms = k_uptime_get() - data->last_packet_time;
    int64_t timeout_ms = data->is_tap_dragging ? SYNAPTICS_DRAG_TIMEOUT_MS : SYNAPTICS_LIFTOFF_TIMEOUT_MS;
    if (inactive_ms >= timeout_ms) {
      if (data->prev_touching || data->prev_two_finger || data->prev_three_finger ||
          data->gesture_max_fingers > 0 || data->is_tap_dragging) {
        synaptics_process_liftoff(data);
      }
    }
  }

  /* Scheduling:
   * 1. If INT pin is still asserted, reschedule in 1 ms to continue draining FIFO.
   * 2. If un-flushed deltas remain pending, schedule flush at SYNAPTICS_BLE_FRAME_MS.
   * 3. If fingers are still active or dragging, schedule check at 20 ms.
   */
  if (gpio_pin_get_dt(&config->irq_gpio) > 0) {
    k_work_reschedule(&data->work, K_MSEC(1));
  } else if (data->pend_dx != 0 || data->pend_dy != 0 ||
             data->pend_wheel != 0 || data->pend_hwheel != 0) {
    k_work_reschedule(&data->work, K_MSEC(SYNAPTICS_BLE_FRAME_MS));
  } else if (data->prev_touching || data->prev_two_finger ||
             data->prev_three_finger || data->gesture_max_fingers > 0 ||
             data->is_tap_dragging) {
    k_work_reschedule(&data->work, K_MSEC(20));
  }
}

static void synaptics_delayed_init_handler(struct k_work *work);
static void synaptics_heartbeat_handler(struct k_work *work);

static void synaptics_gpio_callback(const struct device *port,
                                    struct gpio_callback *cb,
                                    gpio_port_pins_t pins) {
  (void)port;
  (void)pins;
  struct synaptics_data *data =
      CONTAINER_OF(cb, struct synaptics_data, gpio_cb);
  k_work_reschedule(&data->work, K_NO_WAIT);
}

static void synaptics_heartbeat_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, heartbeat_work);
  const struct device *dev = data->dev;
  const struct synaptics_config *config = dev->config;

  /* Fail-safe watchdog: if INT pin is asserted while work is idle, wake it up immediately */
  if (gpio_pin_get_dt(&config->irq_gpio) > 0) {
    k_work_reschedule(&data->work, K_NO_WAIT);
  }
  k_work_schedule(&data->heartbeat_work, K_MSEC(100));
}

static void synaptics_delayed_init_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, init_work);
  const struct device *dev = data->dev;
  const struct synaptics_config *config = dev->config;

  if (!i2c_is_ready_dt(&config->i2c) || !gpio_is_ready_dt(&config->irq_gpio)) {
    LOG_ERR("I2C or IRQ GPIO not ready");
    return;
  }

  /* Step 1: Probe hardware address 0x2C directly */
  uint16_t target_addr = 0;
  uint8_t pwr_cmd[] = {0x22, 0x00, 0x00, 0x08};
  for (int retry = 0; retry < 5; retry++) {
    int ret = i2c_write(config->i2c.bus, pwr_cmd, sizeof(pwr_cmd), 0x2C);
    if (ret == 0) {
      target_addr = 0x2C;
      break;
    }
    k_msleep(30);
  }

  if (target_addr == 0) {
    target_addr = 0x2C;
  }
  data->active_addr = target_addr;

  k_msleep(50);

  /* Step 2: Software Reset command (Reg 0x0022, Opcode 0x01) */
  uint8_t reset_cmd[] = {0x22, 0x00, 0x00, 0x01};
  i2c_write(config->i2c.bus, reset_cmd, sizeof(reset_cmd), data->active_addr);

  for (int t = 0; t < 50; t++) {
    if (gpio_pin_get_dt(&config->irq_gpio) == 0) {
      break;
    }
    k_msleep(10);
  }

  uint8_t flush_buf[32];
  i2c_read(config->i2c.bus, flush_buf, sizeof(flush_buf), data->active_addr);
  k_msleep(50);

  /* Step 3: Switch to PTP Mode (SET_REPORT Feature Report 4 to value 3) */
  uint8_t ptp_cmd[] = {0x22, 0x00, 0x34, 0x03, 0x23,
                       0x00, 0x04, 0x00, 0x04, 0x03};
  i2c_write(config->i2c.bus, ptp_cmd, sizeof(ptp_cmd), data->active_addr);
  k_msleep(50);
  i2c_read(config->i2c.bus, flush_buf, sizeof(flush_buf), data->active_addr);

  /* Step 4: Attach active-edge interrupt */
  gpio_pin_interrupt_configure_dt(&config->irq_gpio, GPIO_INT_EDGE_TO_ACTIVE);
  gpio_init_callback(&data->gpio_cb, synaptics_gpio_callback, BIT(config->irq_gpio.pin));
  gpio_add_callback(config->irq_gpio.port, &data->gpio_cb);

  /* Schedule work immediately to drain any pending state */
  k_work_reschedule(&data->work, K_NO_WAIT);

  /* Start fail-safe heartbeat watchdog every 100 ms */
  k_work_schedule(&data->heartbeat_work, K_MSEC(100));

  LOG_INF("Synaptics TM-P3125 initialized successfully (Addr 0x%02X)", data->active_addr);
}

static int synaptics_init(const struct device *dev) {
  const struct synaptics_config *config = dev->config;
  struct synaptics_data *data = dev->data;

  data->dev = dev;
  k_work_init_delayable(&data->work, synaptics_work_handler);
  k_work_init_delayable(&data->init_work, synaptics_delayed_init_handler);
  k_work_init_delayable(&data->heartbeat_work, synaptics_heartbeat_handler);
  k_work_init_delayable(&data->tap_release_work, synaptics_tap_release_handler);
  k_work_init_delayable(&data->tap_right_release_work, synaptics_tap_right_release_handler);
  k_work_init_delayable(&data->tap_middle_release_work, synaptics_tap_middle_release_handler);
  k_work_init_delayable(&data->swipe_up_release_work, synaptics_swipe_up_release_handler);
  k_work_init_delayable(&data->swipe_down_release_work, synaptics_swipe_down_release_handler);
  k_work_init_delayable(&data->pinch_in_release_work, synaptics_pinch_in_release_handler);
  k_work_init_delayable(&data->pinch_out_release_work, synaptics_pinch_out_release_handler);
  k_work_init_delayable(&data->inertial_scroll_work, synaptics_inertial_scroll_handler);
  data->last_pinch_time = -100;

  if (!i2c_is_ready_dt(&config->i2c) || !gpio_is_ready_dt(&config->irq_gpio)) {
    return -ENODEV;
  }

  int ret = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT | GPIO_PULL_UP);
  if (ret < 0) {
    return ret;
  }

  k_work_schedule(&data->init_work, K_MSEC(3500));
  return 0;
}

#ifndef ZMK_TOUCHPAD_UNIT_TEST
#define SYNAPTICS_INIT(inst)                                                   \
  static struct synaptics_data synaptics_data_##inst;                          \
  static const struct synaptics_config synaptics_config_##inst = {             \
      .i2c = I2C_DT_SPEC_INST_GET(inst),                                       \
      .irq_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),                      \
  };                                                                           \
  DEVICE_DT_INST_DEFINE(inst, synaptics_init, NULL, &synaptics_data_##inst,    \
                        &synaptics_config_##inst, POST_KERNEL,                 \
                        CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(SYNAPTICS_INIT)
#endif
