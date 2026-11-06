/* Minimal compile-time API surface; not an emulation of ZMK listeners or HID. */
#ifndef TEST_ZMK_H
#define TEST_ZMK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DT_HAS_COMPAT_STATUS_OKAY(compat) 1
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0) fn(1)
#define DT_INST_PROP(n, prop) TEST_PROP_##n##_##prop
#define TEST_PROP_0_key0 0x00070004u
#define TEST_PROP_0_key1 0x00070007u
#define TEST_PROP_0_resolution 0u
#define TEST_PROP_1_key0 0x0007001au
#define TEST_PROP_1_key1 0x00070016u
#define TEST_PROP_1_resolution 1u
#define BUILD_ASSERT(condition, message) _Static_assert(condition, message)
#define LOG_MODULE_DECLARE(...) _Static_assert(1, "logging stub")
#define LOG_ERR(...) ((void)0)
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 50
#define ZMK_BEHAVIOR_OPAQUE 0
#define HID_USAGE_KEY 7u
#define ZMK_HID_USAGE(page, id) (((uint32_t)(page) << 16) | (id))
#define ZMK_HID_USAGE_PAGE(usage) (((usage) >> 16) & 0xffu)
#define ZMK_HID_USAGE_ID(usage) ((usage) & 0xffffu)

enum behavior_locality { BEHAVIOR_LOCALITY_CENTRAL };
struct zmk_behavior_binding {
    const char *behavior_dev;
    uint32_t param1;
};
struct zmk_behavior_binding_event {
    uint32_t position;
    int64_t timestamp;
};
struct behavior_driver_api {
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    enum behavior_locality locality;
};
struct device {
    const void *config;
    void *data;
    int (*init)(const struct device *);
    const struct behavior_driver_api *api;
};
#define BEHAVIOR_DT_INST_DEFINE(n, init_fn, pm, data_ptr, config_ptr, level, priority, api_ptr) \
    static struct device test_device_##n = {config_ptr, data_ptr, init_fn, api_ptr}

const struct device *zmk_behavior_get_binding(const char *name);
int raise_zmk_keycode_state_changed_from_encoded(uint32_t keycode, bool pressed, int64_t timestamp);

#endif
