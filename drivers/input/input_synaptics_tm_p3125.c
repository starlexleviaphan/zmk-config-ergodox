/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT synaptics_tm_p3125

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(synaptics_tm_p3125, LOG_LEVEL_INF);

#define SYNAPTICS_REPORT_TOUCH 0x03
#define SYNAPTICS_REPORT_MOUSE 0x02

/* Precision tap & drag timings (libinput & Windows PTP standard) */
#define TAP_MAX_DURATION_MS 260
#define TAP_MAX_DISPLACEMENT 55
#define TAP_DRAG_TIMEOUT_MS 300
#define TAP_DRAG_MAX_DISTANCE 80

/* Scroll & gesture thresholds */
#define SCROLL_THRESHOLD 35
#define THREE_FINGER_SWIPE_THRESHOLD 100

/* Palm rejection: edge exclusion margin (Sensor X range: 0..1219) */
#define PALM_EDGE_LEFT 70
#define PALM_EDGE_RIGHT 1150

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

  /* Tap and swipe release works */
  struct k_work_delayable tap_release_work;
  struct k_work_delayable tap_right_release_work;
  struct k_work_delayable tap_middle_release_work;
  struct k_work_delayable swipe_up_release_work;
  struct k_work_delayable swipe_down_release_work;
  struct k_work_delayable inertial_scroll_work;

  /* Active I2C address */
  uint16_t active_addr;

  /* Contact tracking across touch sequence */
  uint8_t gesture_max_fingers;

  /* 1-Finger tracking & Ballistics */
  bool prev_touching;
  uint16_t prev_x0;
  uint16_t prev_y0;
  uint16_t touch_start_x;
  uint16_t touch_start_y;
  uint16_t touch_max_disp;
  int64_t touch_start_time;

  /* Tap and Drag state machine */
  uint16_t last_tap_x;
  uint16_t last_tap_y;
  int64_t last_tap_release_time;
  bool is_tap_dragging;

  /* 2-Finger scroll & Inertia tracking */
  bool prev_two_finger;
  uint16_t prev_scroll_x;
  uint16_t prev_scroll_y;
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

  /* Physical clickpad button */
  bool prev_btn_left;
};

static void synaptics_tap_release_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, tap_release_work);
  if (!data->is_tap_dragging) {
    input_report_key(data->dev, INPUT_BTN_LEFT, 0, true, K_NO_WAIT);
  }
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

static void synaptics_inertial_scroll_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, inertial_scroll_work);

  if (data->inertial_dy != 0) {
    int dir = (data->inertial_dy > 0) ? 1 : -1;
    input_report_rel(data->dev, INPUT_REL_WHEEL, dir, true, K_NO_WAIT);

    /* Exponential decay (decay factor 0.8) */
    data->inertial_dy = (data->inertial_dy * 8) / 10;
    if (data->inertial_dy > 1 || data->inertial_dy < -1) {
      k_work_schedule(&data->inertial_scroll_work, K_MSEC(25));
    } else {
      data->inertial_dy = 0;
    }
  }
}

static void synaptics_work_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data = CONTAINER_OF(dwork, struct synaptics_data, work);
  const struct device *dev = data->dev;
  const struct synaptics_config *config = dev->config;

  int16_t acc_dx = 0;
  int16_t acc_dy = 0;
  bool has_rel = false;

  /* Read all pending packets while INT is asserted (active-low GPIO returns > 0) */
  for (int iter = 0; iter < 16; iter++) {
    int pin_active = gpio_pin_get_dt(&config->irq_gpio);
    if (pin_active <= 0) {
      break;
    }

    /* Fast 32-byte read: covers Report ID, 3 finger slots, contact count, and buttons */
    uint8_t buf[32];
    int ret = i2c_read(config->i2c.bus, buf, sizeof(buf), data->active_addr);
    if (ret < 0) {
      break;
    }

    uint8_t report_id = buf[2];

    if (report_id == SYNAPTICS_REPORT_TOUCH) {
      /* Slot 0 (Finger 1) */
      uint8_t status0 = buf[3];
      bool tip0 = (status0 & 0x02) != 0;
      uint16_t x0 = (uint16_t)(buf[4] | (buf[5] << 8));
      uint16_t y0 = (uint16_t)(buf[6] | (buf[7] << 8));

      /* Slot 1 (Finger 2) */
      uint8_t status1 = buf[8];
      bool tip1 = (status1 & 0x02) != 0;
      uint16_t x1 = (uint16_t)(buf[9] | (buf[10] << 8));
      uint16_t y1 = (uint16_t)(buf[11] | (buf[12] << 8));

      /* Slot 2 (Finger 3) & contact count */
      uint8_t status2 = buf[13];
      bool tip2 = (status2 & 0x02) != 0;
      uint16_t y2 = (uint16_t)(buf[16] | (buf[17] << 8));
      uint8_t contact_count = buf[30];

      /* Physical clickpad button */
      bool physical_btn = (buf[31] & 0x01) != 0;
      if (physical_btn != data->prev_btn_left) {
        data->prev_btn_left = physical_btn;
        input_report_key(dev, INPUT_BTN_LEFT, physical_btn ? 1 : 0, true, K_NO_WAIT);
      }

      /* Determine active contact count */
      uint8_t contacts = 0;
      if (tip0) contacts++;
      if (tip1) contacts++;
      if (tip2) contacts++;
      if (contact_count > contacts) contacts = contact_count;

      /* Track maximum concurrent fingers in the current touch sequence */
      if (contacts > data->gesture_max_fingers) {
        data->gesture_max_fingers = contacts;
      }

      /* ========================================================================= */
      /* CASE 1: ALL FINGERS LIFTED (Touch sequence completion & tap recognition)  */
      /* ========================================================================= */
      if (contacts == 0) {
        /* Release 3-finger touch */
        if (data->gesture_max_fingers >= 3) {
          if (data->prev_three_finger) {
            int64_t dur3 = k_uptime_get() - data->three_finger_start_time;
            if (!data->three_finger_swiped && dur3 < TAP_MAX_DURATION_MS) {
              /* 3-Finger Tap: Middle Click */
              input_report_key(dev, INPUT_BTN_MIDDLE, 1, true, K_NO_WAIT);
              k_work_schedule(&data->tap_middle_release_work, K_MSEC(40));
            }
            data->prev_three_finger = false;
          }
          data->three_finger_swiped = false;
        }
        /* Release 2-finger touch */
        else if (data->gesture_max_fingers == 2) {
          if (data->prev_two_finger) {
            int64_t dur2 = k_uptime_get() - data->two_finger_start_time;
            if (!data->two_finger_scrolled && dur2 < TAP_MAX_DURATION_MS) {
              /* 2-Finger Tap: Right Click */
              input_report_key(dev, INPUT_BTN_RIGHT, 1, true, K_NO_WAIT);
              k_work_schedule(&data->tap_right_release_work, K_MSEC(50));
            } else if (data->two_finger_scrolled &&
                       (data->last_scroll_dy > 45 || data->last_scroll_dy < -45)) {
              /* Kinetic scroll momentum */
              data->inertial_dy = data->last_scroll_dy / 2;
              k_work_schedule(&data->inertial_scroll_work, K_MSEC(25));
            }
            data->prev_two_finger = false;
            data->two_finger_scrolled = false;
            data->two_finger_release_time = k_uptime_get();
          }
        }
        /* Release 1-finger touch */
        else if (data->gesture_max_fingers == 1) {
          if (data->prev_touching) {
            int64_t duration = k_uptime_get() - data->touch_start_time;

            if (data->is_tap_dragging) {
              /* Drop: Release Left button immediately on finger lift */
              data->is_tap_dragging = false;
              input_report_key(dev, INPUT_BTN_LEFT, 0, true, K_NO_WAIT);
              data->last_tap_release_time = 0;
            } else if (duration < TAP_MAX_DURATION_MS &&
                       data->touch_max_disp < TAP_MAX_DISPLACEMENT) {
              /* Single Tap: Click */
              input_report_key(dev, INPUT_BTN_LEFT, 1, true, K_NO_WAIT);
              k_work_schedule(&data->tap_release_work, K_MSEC(35));
              data->last_tap_release_time = k_uptime_get();
              data->last_tap_x = data->touch_start_x;
              data->last_tap_y = data->touch_start_y;
            }
            data->prev_touching = false;
          }
        }

        /* Reset gesture sequence tracker when all fingers leave pad */
        data->gesture_max_fingers = 0;
      }
      /* ========================================================================= */
      /* CASE 2: ACTIVE 3-FINGER GESTURE (Task View / Show Desktop)                */
      /* ========================================================================= */
      else if (data->gesture_max_fingers >= 3) {
        data->prev_touching = false;
        data->prev_two_finger = false;
        data->inertial_dy = 0;

        uint16_t avg3_y = (y0 + y1 + y2) / 3;

        if (data->prev_three_finger) {
          int16_t swipe_dy = (int16_t)avg3_y - (int16_t)data->three_finger_start_y;
          if (!data->three_finger_swiped) {
            if (swipe_dy < -THREE_FINGER_SWIPE_THRESHOLD) {
              /* Swipe Up: Task View (Win+Tab via INPUT_BTN_3) with safe delayed release */
              input_report_key(dev, INPUT_BTN_3, 1, true, K_NO_WAIT);
              k_work_schedule(&data->swipe_up_release_work, K_MSEC(50));
              data->three_finger_swiped = true;
            } else if (swipe_dy > THREE_FINGER_SWIPE_THRESHOLD) {
              /* Swipe Down: Show Desktop (Win+D via INPUT_BTN_4) with safe delayed release */
              input_report_key(dev, INPUT_BTN_4, 1, true, K_NO_WAIT);
              k_work_schedule(&data->swipe_down_release_work, K_MSEC(50));
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
      /* ========================================================================= */
      /* CASE 3: ACTIVE 2-FINGER GESTURE (Vertical & Horizontal Scroll)            */
      /* ========================================================================= */
      else if (data->gesture_max_fingers == 2) {
        data->prev_touching = false;
        data->inertial_dy = 0;

        uint16_t avg_y = (y0 + y1) / 2;
        uint16_t avg_x = (x0 + x1) / 2;

        if (data->prev_two_finger) {
          int16_t scroll_dy = (int16_t)avg_y - (int16_t)data->prev_scroll_y;
          int16_t scroll_dx = (int16_t)avg_x - (int16_t)data->prev_scroll_x;

          if (scroll_dy > SCROLL_THRESHOLD) {
            input_report_rel(dev, INPUT_REL_WHEEL, 1, true, K_NO_WAIT);
            data->prev_scroll_y = avg_y;
            data->last_scroll_dy = scroll_dy;
            data->two_finger_scrolled = true;
          } else if (scroll_dy < -SCROLL_THRESHOLD) {
            input_report_rel(dev, INPUT_REL_WHEEL, -1, true, K_NO_WAIT);
            data->prev_scroll_y = avg_y;
            data->last_scroll_dy = scroll_dy;
            data->two_finger_scrolled = true;
          }

          if (scroll_dx > SCROLL_THRESHOLD) {
            input_report_rel(dev, INPUT_REL_HWHEEL, 1, true, K_NO_WAIT);
            data->prev_scroll_x = avg_x;
            data->two_finger_scrolled = true;
          } else if (scroll_dx < -SCROLL_THRESHOLD) {
            input_report_rel(dev, INPUT_REL_HWHEEL, -1, true, K_NO_WAIT);
            data->prev_scroll_x = avg_x;
            data->two_finger_scrolled = true;
          }
        } else {
          data->prev_scroll_y = avg_y;
          data->prev_scroll_x = avg_x;
          data->two_finger_start_time = k_uptime_get();
          data->two_finger_scrolled = false;
          data->last_scroll_dy = 0;
          data->prev_two_finger = true;
        }
      }
      /* ========================================================================= */
      /* CASE 4: ACTIVE 1-FINGER GESTURE (Cursor tracking & Tap-and-Drag)          */
      /* ========================================================================= */
      else if (data->gesture_max_fingers == 1 && tip0) {
        if (k_uptime_get() - data->two_finger_release_time >= 50) {
          data->inertial_dy = 0;

          if (data->prev_touching) {
            int16_t dx = (int16_t)x0 - (int16_t)data->prev_x0;
            int16_t dy = (int16_t)y0 - (int16_t)data->prev_y0;

            if (dx > -400 && dx < 400 && dy > -400 && dy < 400) {
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

              /* Pure 1:1 direct linear motion */
              acc_dx += dx;
              acc_dy += dy;
              has_rel = true;
            }
          } else {
            /* Touch Down */
            int64_t now = k_uptime_get();
            data->touch_start_time = now;
            data->touch_start_x = x0;
            data->touch_start_y = y0;
            data->touch_max_disp = 0;

            int16_t d_tap_x = (int16_t)x0 - (int16_t)data->last_tap_x;
            int16_t d_tap_y = (int16_t)y0 - (int16_t)data->last_tap_y;
            if (d_tap_x < 0) d_tap_x = -d_tap_x;
            if (d_tap_y < 0) d_tap_y = -d_tap_y;

            /* Check Tap-and-Drag double tap condition */
            if ((now - data->last_tap_release_time) < TAP_DRAG_TIMEOUT_MS &&
                d_tap_x < TAP_DRAG_MAX_DISTANCE && d_tap_y < TAP_DRAG_MAX_DISTANCE) {
              data->is_tap_dragging = true;
              k_work_cancel_delayable(&data->tap_release_work);
              input_report_key(dev, INPUT_BTN_LEFT, 1, true, K_NO_WAIT);
              data->last_tap_release_time = 0;
            } else {
              data->is_tap_dragging = false;
            }
          }

          data->prev_x0 = x0;
          data->prev_y0 = y0;
          data->prev_touching = true;
        }
      }
    } else if (report_id == SYNAPTICS_REPORT_MOUSE) {
      bool btn = (buf[3] & 0x01) != 0;
      if (btn != data->prev_btn_left) {
        data->prev_btn_left = btn;
        input_report_key(dev, INPUT_BTN_LEFT, btn ? 1 : 0, true, K_NO_WAIT);
      }
    }
  } /* End FIFO loop */

  /* Emit aggregated movement across drained packets */
  if (has_rel && (acc_dx != 0 || acc_dy != 0)) {
    input_report_rel(dev, INPUT_REL_X, acc_dx, false, K_NO_WAIT);
    input_report_rel(dev, INPUT_REL_Y, acc_dy, true, K_NO_WAIT);
  }

  /* Polling management:
   * 1. If INT pin is still asserted (active-low GPIO returns > 0), re-schedule immediately
   *    so no edge interrupt is missed and hardware FIFO is fully drained!
   * 2. If fingers are active or gestures ongoing, poll fast (4 ms).
   */
  if (gpio_pin_get_dt(&config->irq_gpio) > 0) {
    k_work_schedule(&data->work, K_MSEC(2));
  } else if (data->prev_touching || data->prev_two_finger || data->prev_three_finger ||
             data->is_tap_dragging || data->inertial_dy != 0 || data->gesture_max_fingers > 0) {
    k_work_schedule(&data->work, K_MSEC(4));
  }
}

static void synaptics_delayed_init_handler(struct k_work *work);
static void synaptics_heartbeat_handler(struct k_work *work);

static void synaptics_gpio_callback(const struct device *port,
                                    struct gpio_callback *cb,
                                    gpio_port_pins_t pins) {
  struct synaptics_data *data =
      CONTAINER_OF(cb, struct synaptics_data, gpio_cb);
  k_work_schedule(&data->work, K_NO_WAIT);
}

static void synaptics_heartbeat_handler(struct k_work *work) {
  struct k_work_delayable *dwork = k_work_delayable_from_work(work);
  struct synaptics_data *data =
      CONTAINER_OF(dwork, struct synaptics_data, heartbeat_work);
  const struct device *dev = data->dev;
  const struct synaptics_config *config = dev->config;

  /* Fail-safe watchdog: if INT pin is asserted while work is idle, wake it up immediately */
  if (gpio_pin_get_dt(&config->irq_gpio) > 0) {
    k_work_schedule(&data->work, K_NO_WAIT);
  }
  k_work_schedule(&data->heartbeat_work, K_SECONDS(3));
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
  k_work_schedule(&data->work, K_NO_WAIT);

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
  k_work_init_delayable(&data->inertial_scroll_work, synaptics_inertial_scroll_handler);

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
