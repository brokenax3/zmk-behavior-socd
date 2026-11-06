/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Per-pair SOCD resolution. Only resolved output edges enter ZMK's normal
 * keycode event pipeline; suppressed physical inputs do not emit keycodes.
 */

#define DT_DRV_COMPAT zmk_behavior_socd

#include <errno.h>
#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>

#include <dt-bindings/zmk/socd.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_socd_config {
    uint32_t keys[2]; /* Encoded ZMK keycodes, including page and modifiers. */
    uint32_t resolution; /* Do not narrow before validating. */
};

enum socd_output {
    SOCD_OUTPUT_NONE,
    SOCD_OUTPUT_KEY0,
    SOCD_OUTPUT_KEY1,
};

struct behavior_socd_data {
    bool held[2];
    uint8_t last_pressed;
    /* Logical output dispatched to listeners, not confirmed HID/host state. */
    enum socd_output output;
};

static uint32_t socd_usage(uint32_t keycode) {
    uint16_t page = ZMK_HID_USAGE_PAGE(keycode);
    return ZMK_HID_USAGE(page ? page : HID_USAGE_KEY, ZMK_HID_USAGE_ID(keycode));
}

static bool socd_config_valid(const struct behavior_socd_config *cfg) {
    return cfg->resolution <= SOCD_KEY1_WINS &&
           socd_usage(cfg->keys[0]) != socd_usage(cfg->keys[1]);
}

static enum socd_output socd_resolve(const struct behavior_socd_config *cfg,
                                     const struct behavior_socd_data *data) {
    if (!data->held[0]) {
        return data->held[1] ? SOCD_OUTPUT_KEY1 : SOCD_OUTPUT_NONE;
    }
    if (!data->held[1]) {
        return SOCD_OUTPUT_KEY0;
    }

    switch (cfg->resolution) {
    case SOCD_LAST:
        return data->last_pressed == 0 ? SOCD_OUTPUT_KEY0 : SOCD_OUTPUT_KEY1;
    case SOCD_NEUTRAL:
        return SOCD_OUTPUT_NONE;
    case SOCD_KEY0_WINS:
        return SOCD_OUTPUT_KEY0;
    case SOCD_KEY1_WINS:
        return SOCD_OUTPUT_KEY1;
    default:
        return SOCD_OUTPUT_NONE; /* Callbacks reject invalid configurations. */
    }
}

static int socd_emit(uint32_t keycode, bool pressed, int64_t timestamp) {
    int ret = raise_zmk_keycode_state_changed_from_encoded(keycode, pressed, timestamp);
    if (ret < 0) {
        LOG_ERR("SOCD keycode 0x%08x state %d dispatch failed: %d", keycode, pressed, ret);
    }
    return ret;
}

static int socd_transition(const struct behavior_socd_config *cfg,
                           struct behavior_socd_data *data, int64_t timestamp) {
    enum socd_output desired = socd_resolve(cfg, data);
    if (desired == data->output) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (data->output != SOCD_OUTPUT_NONE) {
        uint32_t keycode = cfg->keys[data->output - SOCD_OUTPUT_KEY0];
        /* Dispatch can partially run listeners before returning an error.
         * Record the logical edge before dispatch; do not blindly retry it.
         */
        data->output = SOCD_OUTPUT_NONE;
        int ret = socd_emit(keycode, false, timestamp);
        if (ret < 0) {
            return ret; /* Never press a new winner after a failed release. */
        }
    }

    if (desired != SOCD_OUTPUT_NONE) {
        data->output = desired;
        return socd_emit(cfg->keys[desired - SOCD_OUTPUT_KEY0], true, timestamp);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int socd_binding_changed(struct zmk_behavior_binding *binding,
                                struct zmk_behavior_binding_event event, bool pressed) {
    if (binding->param1 > 1) {
        return -EINVAL;
    }
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    if (dev == NULL) {
        return -ENODEV;
    }
    const struct behavior_socd_config *cfg = dev->config;
    struct behavior_socd_data *data = dev->data;
    if (!socd_config_valid(cfg)) {
        return -EINVAL;
    }

    uint32_t side = binding->param1;
    if (data->held[side] == pressed) {
        return ZMK_BEHAVIOR_OPAQUE; /* Idempotent under the single-source contract. */
    }
    data->held[side] = pressed;
    if (pressed) {
        data->last_pressed = side;
    }
    return socd_transition(cfg, data, event.timestamp);
}

static int on_socd_binding_pressed(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    return socd_binding_changed(binding, event, true);
}

static int on_socd_binding_released(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    return socd_binding_changed(binding, event, false);
}

static int socd_init(const struct device *dev) {
    return socd_config_valid(dev->config) ? 0 : -EINVAL;
}

static const struct behavior_driver_api socd_driver_api = {
    .binding_pressed = on_socd_binding_pressed,
    .binding_released = on_socd_binding_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
};

#define SOCD_INST(n)                                                                               \
    BUILD_ASSERT(DT_INST_PROP(n, resolution) <= SOCD_KEY1_WINS, "Invalid SOCD resolution");          \
    static struct behavior_socd_data socd_data_##n;                                                 \
    static const struct behavior_socd_config socd_config_##n = {                                    \
        .keys = {DT_INST_PROP(n, key0), DT_INST_PROP(n, key1)},                                      \
        .resolution = DT_INST_PROP(n, resolution),                                                 \
    };                                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, socd_init, NULL, &socd_data_##n, &socd_config_##n, POST_KERNEL,        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &socd_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SOCD_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
