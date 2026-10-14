/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT m5stack_adv_tca8418
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include "adv_keyboard.h"
#include "adv_power.h"

LOG_MODULE_REGISTER(adv_keyboard, LOG_LEVEL_INF);
enum { CFG = 0x01, INT_STAT = 0x02, EVENT_COUNT = 0x03, EVENT_A = 0x04,
       KP_GPIO1 = 0x1d, KP_GPIO2 = 0x1e, KP_GPIO3 = 0x1f };
struct adv_config {
    struct i2c_dt_spec bus;
    struct gpio_dt_spec irq;
    uint32_t poll_ms;
};
struct adv_data {
    const struct device *dev;
    kscan_callback_t callback;
    struct k_work_delayable work;
    struct gpio_callback irq_cb;
    atomic_t enabled;
    uint64_t held;
};
static atomic_t last_error;
int adv_keyboard_error(void) { return atomic_get(&last_error); }
static void release_all(struct adv_data *d) {
    for (int n = 0; n < 56; n++) {
        if ((d->held & (UINT64_C(1) << n)) && d->callback)
            d->callback(d->dev, n / 14, n % 14, false);
    }
    d->held = 0;
    adv_power_input(0,false);
}
static int read_reg(const struct adv_config *c, uint8_t reg, uint8_t *value) {
    return i2c_reg_read_byte_dt(&c->bus, reg, value);
}
static int flush(const struct adv_config *c) {
    for (int n = 0; n < 16; n++) {
        uint8_t event;
        int err = read_reg(c, EVENT_A, &event);
        if (err) return err;
        if (!event) return i2c_reg_write_byte_dt(&c->bus, INT_STAT, 0x0f);
    }
    return -EOVERFLOW;
}
static void scan(struct k_work *work) {
    struct adv_data *d = CONTAINER_OF(k_work_delayable_from_work(work),
                                     struct adv_data, work);
    const struct adv_config *c = d->dev->config;
    if (!atomic_get(&d->enabled)) return;
    uint8_t status, count;
    int err = read_reg(c, INT_STAT, &status);
    if (!err) err = read_reg(c, EVENT_COUNT, &count);
    if (err) goto failed;
    if (status & 0x08) {
        /* FIFO overflow loses key releases: release held keys before flushing. */
        release_all(d);
        err = flush(c);
        if (err) goto failed;
        atomic_set(&last_error, -EOVERFLOW);
        goto again;
    }
    for (int n = 0; n < (count & 0x0f) && n < 10; n++) {
        uint8_t event, row, col;
        bool pressed;
        err = read_reg(c, EVENT_A, &event);
        if (err) goto failed;
        if (!adv_decode_key(event, &row, &col, &pressed)) continue;
        uint64_t bit = UINT64_C(1) << (row * 14 + col);
        if (!!(d->held & bit) == pressed) continue;
        if (pressed) d->held |= bit; else d->held &= ~bit;
        adv_power_input(0,d->held!=0);
        if (d->callback) d->callback(d->dev, row, col, pressed);
    }
    err = i2c_reg_write_byte_dt(&c->bus, INT_STAT, status & 0x0f);
    if (err) goto failed;
    atomic_clear(&last_error);
    goto again;
failed:
    if (atomic_get(&last_error) != err) LOG_ERR("TCA8418 I2C: %d", err);
    atomic_set(&last_error, err);
    release_all(d);
again:
    if (atomic_get(&d->enabled)) k_work_reschedule(&d->work, K_MSEC(c->poll_ms));
}
static void irq_handler(const struct device *port, struct gpio_callback *cb,
                        uint32_t pins) {
    struct adv_data *d = CONTAINER_OF(cb, struct adv_data, irq_cb);
    ARG_UNUSED(port); ARG_UNUSED(pins);
    if (atomic_get(&d->enabled)) k_work_reschedule(&d->work, K_MSEC(1));
}
static int configure(const struct device *dev, kscan_callback_t callback) {
    if (!callback) return -EINVAL;
    ((struct adv_data *)dev->data)->callback = callback;
    return 0;
}
static int enable(const struct device *dev) {
    struct adv_data *d = dev->data;
    if (!d->callback) return -EINVAL;
    atomic_set(&d->enabled, 1);
    k_work_reschedule(&d->work, K_NO_WAIT);
    return 0;
}
static int disable(const struct device *dev) {
    struct adv_data *d = dev->data;
    atomic_clear(&d->enabled);
    k_work_cancel_delayable(&d->work);
    return 0;
}
static int init(const struct device *dev) {
    const struct adv_config *c = dev->config;
    struct adv_data *d = dev->data;
    d->dev = dev;
    k_work_init_delayable(&d->work, scan);
    int err = 0;
    if (!i2c_is_ready_dt(&c->bus) || !gpio_is_ready_dt(&c->irq)) {
        err = -ENODEV; goto done;
    }
    /* Official M5Cardputer driver configures 7 rows and 8 columns. */
    const uint8_t regs[][2] = {{CFG, 0}, {KP_GPIO1, 0x7f},
                             {KP_GPIO2, 0xff}, {KP_GPIO3, 0}};
    for (int n = 0; n < ARRAY_SIZE(regs); n++) {
        err = i2c_reg_write_byte_dt(&c->bus, regs[n][0], regs[n][1]);
        if (err) goto done;
    }
    err = flush(c);
    if (err) goto done;
    err = gpio_pin_configure_dt(&c->irq, GPIO_INPUT);
    if (err) goto done;
    gpio_init_callback(&d->irq_cb, irq_handler, BIT(c->irq.pin));
    err = gpio_add_callback(c->irq.port, &d->irq_cb);
    if (err) goto done;
    err = gpio_pin_interrupt_configure_dt(&c->irq, GPIO_INT_EDGE_TO_ACTIVE);
    if (err) goto done;
    err = i2c_reg_write_byte_dt(&c->bus, CFG, 0x09); /* Key/overflow IRQ. */
done:
    atomic_set(&last_error, err);
    LOG_INF("ADV 7x8 -> 4x14 keyboard init: %d", err);
    return err;
}
static const struct kscan_driver_api api = {
    .config = configure, .enable_callback = enable, .disable_callback = disable,
};
#define ADV_DEFINE(n) \
    static struct adv_data data_##n; \
    static const struct adv_config cfg_##n = { \
        .bus = I2C_DT_SPEC_INST_GET(n), \
        .irq = GPIO_DT_SPEC_INST_GET(n, irq_gpios), \
        .poll_ms = DT_INST_PROP(n, poll_interval_ms), \
    }; \
    DEVICE_DT_INST_DEFINE(n, init, NULL, &data_##n, &cfg_##n, \
                          POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY, &api);
DT_INST_FOREACH_STATUS_OKAY(ADV_DEFINE)
