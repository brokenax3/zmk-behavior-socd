/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * SOCD (Simultaneous Opposing Cardinal Directions) behavior for ZMK.
 *
 * Wraps a pair of opposing keys. When both keys are held simultaneously,
 * the configured resolution strategy decides which key is reported to the host.
 *
 * Usage in keymap:
 *   &socd_ad 0   — on the key position for key0 (e.g. A)
 *   &socd_ad 1   — on the key position for key1 (e.g. D)
 */

#define DT_DRV_COMPAT zmk_behavior_socd

#include <drivers/behavior.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>

#include <dt-bindings/zmk/socd.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_socd_config {
  uint32_t key0;      /* HID usage ID of first key */
  uint32_t key1;      /* HID usage ID of second key */
  uint8_t resolution; /* SOCD resolution strategy */
};

struct behavior_socd_data {
  bool key0_held;       /* Is key0 physically held? */
  bool key1_held;       /* Is key1 physically held? */
  bool key0_registered; /* Is key0 currently in the HID report? */
  bool key1_registered; /* Is key1 currently in the HID report? */
};

static int socd_register_key(uint32_t keycode) {
  int ret = zmk_hid_press(ZMK_HID_USAGE(HID_USAGE_KEY, keycode));
  if (ret < 0) {
    LOG_ERR("Failed to register keycode 0x%02x: %d", keycode, ret);
    return ret;
  }
  return zmk_endpoint_send_report(HID_USAGE_KEY);
}

static int socd_unregister_key(uint32_t keycode) {
  int ret = zmk_hid_release(ZMK_HID_USAGE(HID_USAGE_KEY, keycode));
  if (ret < 0) {
    LOG_ERR("Failed to unregister keycode 0x%02x: %d", keycode, ret);
    return ret;
  }
  return zmk_endpoint_send_report(HID_USAGE_KEY);
}

static int on_socd_binding_pressed(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
  const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
  const struct behavior_socd_config *cfg = dev->config;
  struct behavior_socd_data *data = dev->data;

  uint32_t param = binding->param1; /* 0 = key0, 1 = key1 */

  if (param == 0) {
    /* Key0 pressed */
    data->key0_held = true;

    switch (cfg->resolution) {
    case SOCD_LAST:
      /* Last key wins: unregister opposing if active, register this one */
      if (data->key1_registered) {
        socd_unregister_key(cfg->key1);
        data->key1_registered = false;
      }
      socd_register_key(cfg->key0);
      data->key0_registered = true;
      break;

    case SOCD_NEUTRAL:
      /* Neutral: if opposing is held, unregister it; don't register this one */
      if (data->key1_held && data->key1_registered) {
        socd_unregister_key(cfg->key1);
        data->key1_registered = false;
      }
      if (!data->key1_held) {
        socd_register_key(cfg->key0);
        data->key0_registered = true;
      }
      break;

    case SOCD_KEY0_WINS:
      /* Key0 always wins: register key0, unregister key1 if active */
      if (data->key1_registered) {
        socd_unregister_key(cfg->key1);
        data->key1_registered = false;
      }
      socd_register_key(cfg->key0);
      data->key0_registered = true;
      break;

    case SOCD_KEY1_WINS:
      /* Key1 wins: only register key0 if key1 is not held */
      if (!data->key1_held) {
        socd_register_key(cfg->key0);
        data->key0_registered = true;
      }
      break;
    }
  } else {
    /* Key1 pressed */
    data->key1_held = true;

    switch (cfg->resolution) {
    case SOCD_LAST:
      if (data->key0_registered) {
        socd_unregister_key(cfg->key0);
        data->key0_registered = false;
      }
      socd_register_key(cfg->key1);
      data->key1_registered = true;
      break;

    case SOCD_NEUTRAL:
      if (data->key0_held && data->key0_registered) {
        socd_unregister_key(cfg->key0);
        data->key0_registered = false;
      }
      if (!data->key0_held) {
        socd_register_key(cfg->key1);
        data->key1_registered = true;
      }
      break;

    case SOCD_KEY0_WINS:
      /* Key0 wins: only register key1 if key0 is not held */
      if (!data->key0_held) {
        socd_register_key(cfg->key1);
        data->key1_registered = true;
      }
      break;

    case SOCD_KEY1_WINS:
      /* Key1 always wins: register key1, unregister key0 if active */
      if (data->key0_registered) {
        socd_unregister_key(cfg->key0);
        data->key0_registered = false;
      }
      socd_register_key(cfg->key1);
      data->key1_registered = true;
      break;
    }
  }

  return ZMK_BEHAVIOR_OPAQUE;
}

static int on_socd_binding_released(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
  const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
  const struct behavior_socd_config *cfg = dev->config;
  struct behavior_socd_data *data = dev->data;

  uint32_t param = binding->param1;

  if (param == 0) {
    /* Key0 released */
    data->key0_held = false;

    if (data->key0_registered) {
      socd_unregister_key(cfg->key0);
      data->key0_registered = false;
    }

    /* Reactivation: if opposing key is still held, register it.
     * This applies to all resolution strategies — once the dominant key
     * is released, the still-held opposing key should always activate. */
    if (data->key1_held && !data->key1_registered) {
      socd_register_key(cfg->key1);
      data->key1_registered = true;
    }
  } else {
    /* Key1 released */
    data->key1_held = false;

    if (data->key1_registered) {
      socd_unregister_key(cfg->key1);
      data->key1_registered = false;
    }

    /* Reactivation: if opposing key is still held, register it */
    if (data->key0_held && !data->key0_registered) {
      socd_register_key(cfg->key0);
      data->key0_registered = true;
    }
  }

  return ZMK_BEHAVIOR_OPAQUE;
}

static int socd_init(const struct device *dev) { return 0; }

static const struct behavior_driver_api socd_driver_api = {
    .binding_pressed = on_socd_binding_pressed,
    .binding_released = on_socd_binding_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
};

#define SOCD_INST(n)                                                           \
  static struct behavior_socd_data socd_data_##n = {                           \
      .key0_held = false,                                                      \
      .key1_held = false,                                                      \
      .key0_registered = false,                                                \
      .key1_registered = false,                                                \
  };                                                                           \
  static const struct behavior_socd_config socd_config_##n = {                 \
      .key0 = DT_INST_PROP(n, key0),                                           \
      .key1 = DT_INST_PROP(n, key1),                                           \
      .resolution = DT_INST_PROP(n, resolution),                               \
  };                                                                           \
  BEHAVIOR_DT_INST_DEFINE(                                                     \
      n, socd_init, NULL, &socd_data_##n, &socd_config_##n, POST_KERNEL,       \
      CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &socd_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SOCD_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
