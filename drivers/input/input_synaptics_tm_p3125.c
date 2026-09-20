/*
 * Copyright (c) 2026 ErgoDox Wireless Project
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT synaptics_tm_p3125

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(synaptics_tm_p3125, CONFIG_INPUT_LOG_LEVEL);

#define SYNAPTICS_REPORT_TOUCH 0x03
#define SYNAPTICS_REPORT_MOUSE 0x02

#define TAP_MAX_DURATION_MS    250
#define TAP_MAX_MOVE           15
#define SCROLL_THRESHOLD       12

struct synaptics_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec irq_gpio;
};

struct synaptics_data {
    const struct device *dev;
    struct k_work work;
    struct gpio_callback gpio_cb;

    /* 1-Finger tracking */
    bool prev_touching;
    uint16_t prev_x0;
    uint16_t prev_y0;

    /* Tap tracking */
    int64_t touch_start_time;
    int16_t total_move_x;
    int16_t total_move_y;

    /* 2-Finger scroll tracking */
    bool prev_two_finger;
    uint16_t prev_scroll_y;

    /* Physical clickpad button */
    bool prev_btn_left;
};

static void synaptics_work_handler(struct k_work *work)
{
    struct synaptics_data *data = CONTAINER_OF(work, struct synaptics_data, work);
    const struct device *dev = data->dev;
    const struct synaptics_config *config = dev->config;

    uint8_t buf[60];
    int ret = i2c_read_dt(&config->i2c, buf, sizeof(buf));
    if (ret < 0) {
        LOG_WRN("Touchpad I2C read error: %d", ret);
        return;
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
        uint16_t y1 = (uint16_t)(buf[11] | (buf[12] << 8));

        /* Physical button on clickpad */
        bool physical_btn = (buf[31] & 0x01) != 0;
        if (physical_btn != data->prev_btn_left) {
            data->prev_btn_left = physical_btn;
            input_report_key(dev, INPUT_BTN_LEFT, physical_btn ? 1 : 0, false, K_NO_WAIT);
        }

        /* Gestures: 2-finger scroll vs 1-finger cursor */
        if (tip0 && tip1) {
            uint16_t avg_y = (y0 + y1) / 2;
            if (data->prev_two_finger) {
                int16_t scroll_dy = (int16_t)avg_y - (int16_t)data->prev_scroll_y;
                if (scroll_dy > SCROLL_THRESHOLD) {
                    input_report_rel(dev, INPUT_REL_WHEEL, 1, true, K_NO_WAIT);
                    data->prev_scroll_y = avg_y;
                } else if (scroll_dy < -SCROLL_THRESHOLD) {
                    input_report_rel(dev, INPUT_REL_WHEEL, -1, true, K_NO_WAIT);
                    data->prev_scroll_y = avg_y;
                }
            } else {
                data->prev_scroll_y = avg_y;
                data->prev_two_finger = true;
            }
            data->prev_touching = false;
        } else {
            data->prev_two_finger = false;

            if (tip0) {
                if (data->prev_touching) {
                    int16_t dx = (int16_t)x0 - (int16_t)data->prev_x0;
                    int16_t dy = (int16_t)y0 - (int16_t)data->prev_y0;

                    data->total_move_x += (dx > 0 ? dx : -dx);
                    data->total_move_y += (dy > 0 ? dy : -dy);

                    if (dx != 0 || dy != 0) {
                        /* Direct proxy: moving up on touchpad moves cursor up */
                        input_report_rel(dev, INPUT_REL_X, dx, false, K_NO_WAIT);
                        input_report_rel(dev, INPUT_REL_Y, -dy, true, K_NO_WAIT);
                    }
                } else {
                    data->touch_start_time = k_uptime_get();
                    data->total_move_x = 0;
                    data->total_move_y = 0;
                }
                data->prev_x0 = x0;
                data->prev_y0 = y0;
                data->prev_touching = true;
            } else {
                /* Release event: check tap-to-click */
                if (data->prev_touching) {
                    int64_t duration = k_uptime_get() - data->touch_start_time;
                    if (duration < TAP_MAX_DURATION_MS &&
                        data->total_move_x < TAP_MAX_MOVE &&
                        data->total_move_y < TAP_MAX_MOVE) {
                        input_report_key(dev, INPUT_BTN_LEFT, 1, true, K_NO_WAIT);
                        k_msleep(15);
                        input_report_key(dev, INPUT_BTN_LEFT, 0, true, K_NO_WAIT);
                    }
                }
                data->prev_touching = false;
            }
        }
    } else if (report_id == SYNAPTICS_REPORT_MOUSE) {
        bool btn = (buf[3] & 0x01) != 0;
        if (btn != data->prev_btn_left) {
            data->prev_btn_left = btn;
            input_report_key(dev, INPUT_BTN_LEFT, btn ? 1 : 0, true, K_NO_WAIT);
        }
    }

    /* Check if more packets are pending */
    if (gpio_pin_get_dt(&config->irq_gpio) == 0) {
        k_work_submit(&data->work);
    }
}

static void synaptics_gpio_callback(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
    struct synaptics_data *data = CONTAINER_OF(cb, struct synaptics_data, gpio_cb);
    k_work_submit(&data->work);
}

static int synaptics_init(const struct device *dev)
{
    const struct synaptics_config *config = dev->config;
    struct synaptics_data *data = dev->data;

    data->dev = dev;
    k_work_init(&data->work, synaptics_work_handler);

    if (!i2c_is_ready_dt(&config->i2c)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&config->irq_gpio)) {
        LOG_ERR("IRQ GPIO not ready");
        return -ENODEV;
    }

    /* Configure INT line as input with internal pull-up */
    int ret = gpio_pin_configure_dt(&config->irq_gpio, GPIO_INPUT | GPIO_PULL_UP);
    if (ret < 0) {
        LOG_ERR("Failed to configure IRQ GPIO: %d", ret);
        return ret;
    }

    /* Give the sensor 200 ms to stabilize power rail after controller boot */
    k_msleep(200);

    /* Step 1: Power On Command (Reg 0x0022, Opcode 0x08 = Set Power, Value 0x00 = Full Power) */
    uint8_t pwr_cmd[] = { 0x22, 0x00, 0x00, 0x08 };
    bool pwr_ok = false;
    for (int retry = 0; retry < 5; retry++) {
        ret = i2c_write_dt(&config->i2c, pwr_cmd, sizeof(pwr_cmd));
        if (ret == 0) {
            LOG_INF("Touchpad Power On ACK on attempt %d", retry + 1);
            pwr_ok = true;
            break;
        }
        LOG_WRN("Touchpad Power On attempt %d failed (err %d), retrying in 50ms...", retry + 1, ret);
        k_msleep(50);
    }

    if (!pwr_ok) {
        LOG_ERR("Failed to communicate with Synaptics touchpad on I2C address 0x2C");
    }

    k_msleep(50);

    /* Step 2: Software Reset command (Reg 0x0022, Opcode 0x01) */
    uint8_t reset_cmd[] = { 0x22, 0x00, 0x00, 0x01 };
    ret = i2c_write_dt(&config->i2c, reset_cmd, sizeof(reset_cmd));
    if (ret < 0) {
        LOG_WRN("Touchpad soft reset write failed: %d", ret);
    }

    /* Wait for INT to go LOW (up to 500 ms) and clear reset response */
    for (int t = 0; t < 50; t++) {
        if (gpio_pin_get_dt(&config->irq_gpio) == 0) {
            LOG_INF("Touchpad INT active after reset (t=%d ms)", t * 10);
            break;
        }
        k_msleep(10);
    }

    uint8_t flush_buf[60];
    ret = i2c_read_dt(&config->i2c, flush_buf, sizeof(flush_buf));
    if (ret < 0) {
        LOG_WRN("Touchpad flush read failed: %d", ret);
    }
    k_msleep(50);

    /* Step 3: Switch to PTP Mode (SET_REPORT Feature Report 4 to value 3) */
    uint8_t ptp_cmd[] = { 0x22, 0x00, 0x34, 0x03, 0x23, 0x00, 0x04, 0x00, 0x04, 0x03 };
    ret = i2c_write_dt(&config->i2c, ptp_cmd, sizeof(ptp_cmd));
    if (ret < 0) {
        LOG_WRN("Touchpad PTP mode command failed: %d", ret);
    }
    k_msleep(50);
    i2c_read_dt(&config->i2c, flush_buf, sizeof(flush_buf));

    /* Step 4: Attach falling-edge interrupt */
    ret = gpio_pin_interrupt_configure_dt(&config->irq_gpio, GPIO_INT_EDGE_FALLING);
    if (ret < 0) {
        LOG_ERR("Failed to configure interrupt: %d", ret);
        return ret;
    }

    gpio_init_callback(&data->gpio_cb, synaptics_gpio_callback, BIT(config->irq_gpio.pin));
    ret = gpio_add_callback(config->irq_gpio.port, &data->gpio_cb);
    if (ret < 0) {
        LOG_ERR("Failed to add GPIO callback: %d", ret);
        return ret;
    }

    /* Process any initial packet if INT line is already low */
    if (gpio_pin_get_dt(&config->irq_gpio) == 0) {
        k_work_submit(&data->work);
    }

    LOG_INF("Synaptics TM-P3125 initialized successfully");
    return 0;
}

#define SYNAPTICS_INIT(inst)                                                       \
    static struct synaptics_data synaptics_data_##inst;                            \
    static const struct synaptics_config synaptics_config_##inst = {               \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                                         \
        .irq_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),                        \
    };                                                                             \
    DEVICE_DT_INST_DEFINE(inst, synaptics_init, NULL,                              \
                          &synaptics_data_##inst, &synaptics_config_##inst,        \
                          POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(SYNAPTICS_INIT)
