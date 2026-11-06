/* Regression tests exercise the real callbacks and instance declarations. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "test_zmk.h"

#include "../src/behaviors/behavior_socd.c"

struct edge {
    uint32_t keycode;
    bool pressed;
    int64_t timestamp;
};
static struct edge edges[64];
static size_t edge_count;
static size_t fail_at;
static const struct device *override_device;

const struct device *zmk_behavior_get_binding(const char *name) {
    if (strcmp(name, "missing") == 0) {
        return NULL;
    }
    if (override_device != NULL) {
        return override_device;
    }
    return strcmp(name, "pair1") == 0 ? &test_device_1 : &test_device_0;
}

int raise_zmk_keycode_state_changed_from_encoded(uint32_t keycode, bool pressed,
                                                int64_t timestamp) {
    assert(edge_count < sizeof(edges) / sizeof(edges[0]));
    edges[edge_count++] = (struct edge){keycode, pressed, timestamp};
    /* A failure is deliberately ambiguous: earlier listeners may have run. */
    return fail_at == edge_count ? -EIO : 0;
}

static int change(const char *name, uint32_t side, bool pressed, int64_t timestamp) {
    struct zmk_behavior_binding binding = {.behavior_dev = name, .param1 = side};
    struct zmk_behavior_binding_event event = {.timestamp = timestamp};
    return pressed ? socd_driver_api.binding_pressed(&binding, event)
                   : socd_driver_api.binding_released(&binding, event);
}

static void expect_edge(size_t index, uint32_t keycode, bool pressed, int64_t timestamp) {
    assert(index < edge_count);
    assert(edges[index].keycode == keycode);
    assert(edges[index].pressed == pressed);
    assert(edges[index].timestamp == timestamp);
}

static void reset(void) {
    memset(&socd_data_0, 0, sizeof(socd_data_0));
    memset(&socd_data_1, 0, sizeof(socd_data_1));
    edge_count = 0;
    fail_at = 0;
    override_device = NULL;
}

static void exhaustive_sequences(void) {
    unsigned transitions = 0;
    for (uint32_t policy = 0; policy < 4; policy++) {
        for (unsigned path = 0; path < 4096; path++) {
            reset();
            struct behavior_socd_config cfg = {.keys = {0x70004, 0x70007},
                                               .resolution = policy};
            struct behavior_socd_data data = {0};
            struct device dev = {.config = &cfg, .data = &data};
            override_device = &dev;
            unsigned held = 0;
            unsigned last = 0;
            unsigned previous = 0; /* Model output bitmask, independent of driver enum. */
            unsigned reported = 0;
            for (unsigned step = 0; step < 12; step++) {
                unsigned side = (path >> step) & 1;
                bool pressed = (held & (1u << side)) == 0;
                held ^= 1u << side;
                if (pressed) {
                    last = side;
                }
                unsigned desired = held;
                if (held == 3) {
                    switch (policy) {
                    case SOCD_LAST: desired = 1u << last; break;
                    case SOCD_NEUTRAL: desired = 0; break;
                    case SOCD_KEY0_WINS: desired = 1; break;
                    case SOCD_KEY1_WINS: desired = 2; break;
                    }
                }
                edge_count = 0;
                int64_t timestamp = INT64_C(123456789000) + step;
                assert(change("pair0", side, pressed, timestamp) == 0);
                size_t expected_count = 0;
                if (desired != previous) {
                    if (previous) {
                        expect_edge(expected_count++, cfg.keys[previous == 2], false, timestamp);
                    }
                    if (desired) {
                        expect_edge(expected_count++, cfg.keys[desired == 2], true, timestamp);
                    }
                }
                assert(edge_count == expected_count);
                for (size_t i = 0; i < edge_count; i++) {
                    unsigned bit = edges[i].keycode == cfg.keys[0] ? 1 : 2;
                    if (edges[i].pressed) {
                        assert((reported & bit) == 0);
                        reported |= bit;
                    } else {
                        assert((reported & bit) != 0);
                        reported &= ~bit;
                    }
                    assert(reported != 3); /* No edge ever activates both opponents. */
                }
                assert(reported == desired);
                assert(data.held[0] == ((held & 1) != 0));
                assert(data.held[1] == ((held & 2) != 0));
                previous = desired;
                transitions++;
            }
        }
    }
    printf("PASS: %u exhaustive balanced transitions, including ordered edges and timestamps\n",
           transitions);
}

static void invalid_inputs(void) {
    reset();
    assert(change("pair0", 0, true, 1) == 0);
    struct behavior_socd_data before = socd_data_0;
    const uint32_t invalid_sides[] = {2, UINT32_MAX};
    for (size_t i = 0; i < sizeof(invalid_sides) / sizeof(invalid_sides[0]); i++) {
        assert(change("pair0", invalid_sides[i], true, 2) == -EINVAL);
        assert(change("pair0", invalid_sides[i], false, 3) == -EINVAL);
    }
    assert(memcmp(&before, &socd_data_0, sizeof(before)) == 0);
    assert(edge_count == 1);
    assert(change("missing", 0, true, 4) == -ENODEV);

    const uint32_t invalid_policies[] = {4, 256, UINT32_MAX};
    for (size_t i = 0; i < sizeof(invalid_policies) / sizeof(invalid_policies[0]); i++) {
        struct behavior_socd_config cfg = {.keys = {0x70004, 0x70007},
                                           .resolution = invalid_policies[i]};
        struct behavior_socd_data data = {0};
        struct device dev = {.config = &cfg, .data = &data};
        override_device = &dev;
        assert(socd_init(&dev) == -EINVAL);
        size_t count = edge_count;
        assert(change("pair0", 0, true, 1) == -EINVAL);
        assert(change("pair0", 1, true, 2) == -EINVAL);
        assert(change("pair0", 0, false, 3) == -EINVAL);
        assert(edge_count == count);
        assert(!data.held[0] && !data.held[1] && data.output == SOCD_OUTPUT_NONE);
    }
    /* Same usage with different modifiers, or implicit/explicit keyboard page. */
    const uint32_t duplicates[] = {0x70004, 0x02070004, 0x00000004};
    for (size_t i = 0; i < sizeof(duplicates) / sizeof(duplicates[0]); i++) {
        struct behavior_socd_config cfg = {.keys = {0x70004, duplicates[i]}};
        struct behavior_socd_data data = {0};
        struct device dev = {.config = &cfg, .data = &data};
        override_device = &dev;
        assert(socd_init(&dev) == -EINVAL);
        assert(change("pair0", 0, true, 1) == -EINVAL);
    }
    puts("PASS: invalid selectors, missing devices, policies without narrowing, duplicate usages");
}

static void duplicate_callbacks(void) {
    reset();
    assert(change("pair0", 0, false, 1) == 0);
    assert(edge_count == 0);
    assert(change("pair0", 0, true, 2) == 0);
    assert(change("pair0", 0, true, 3) == 0);
    assert(edge_count == 1);
    assert(change("pair0", 1, true, 4) == 0);
    assert(edge_count == 3);
    assert(change("pair0", 0, true, 5) == 0); /* Duplicate cannot steal priority. */
    assert(edge_count == 3);
    assert(change("pair0", 0, false, 6) == 0); /* Suppressed release emits nothing. */
    assert(change("pair0", 0, false, 7) == 0);
    assert(edge_count == 3);
    assert(change("pair0", 1, false, 8) == 0);
    assert(change("pair0", 1, false, 9) == 0);
    assert(edge_count == 4);
    expect_edge(3, socd_config_0.keys[1], false, 8);
    puts("PASS: duplicate callbacks are idempotent and suppressed releases emit nothing");
}

static void independent_instances(void) {
    reset();
    assert(test_device_0.init(&test_device_0) == 0);
    assert(test_device_1.init(&test_device_1) == 0);
    assert(test_device_0.api->locality == BEHAVIOR_LOCALITY_CENTRAL);
    assert(change("pair0", 0, true, 1) == 0);
    assert(change("pair1", 0, true, 2) == 0);
    assert(change("pair0", 1, true, 3) == 0);
    assert(change("pair1", 1, true, 4) == 0);
    assert(socd_data_0.output == SOCD_OUTPUT_KEY1);
    assert(socd_data_1.output == SOCD_OUTPUT_NONE);
    assert(change("pair1", 0, false, 5) == 0);
    assert(change("pair0", 1, false, 6) == 0);
    assert(socd_data_0.output == SOCD_OUTPUT_KEY0);
    assert(socd_data_1.output == SOCD_OUTPUT_KEY1);
    assert(edge_count == 8);
    puts("PASS: generated instances initialize independently and use central locality");
}

static void encoded_keycodes(void) {
    const uint32_t pairs[][2] = {
        {0x02070004, 0x00070007}, /* LS(A), D */
        {0x000c00e9, 0x000c00ea}, /* Consumer volume up/down. */
        {0x000700e1, 0x000700e5}, /* Explicit left/right Shift. */
    };
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        reset();
        struct behavior_socd_config cfg = {.keys = {pairs[i][0], pairs[i][1]}};
        struct behavior_socd_data data = {0};
        struct device dev = {.config = &cfg, .data = &data};
        override_device = &dev;
        assert(socd_init(&dev) == 0);
        assert(change("pair0", 0, true, 11) == 0);
        assert(change("pair0", 1, true, 12) == 0);
        assert(change("pair0", 1, false, 13) == 0);
        assert(change("pair0", 0, false, 14) == 0);
        assert(edge_count == 6);
        expect_edge(0, pairs[i][0], true, 11);
        expect_edge(1, pairs[i][0], false, 12);
        expect_edge(2, pairs[i][1], true, 12);
        expect_edge(3, pairs[i][1], false, 13);
        expect_edge(4, pairs[i][0], true, 13);
        expect_edge(5, pairs[i][0], false, 14);
    }
    puts("PASS: modifier-wrapped, consumer, and explicit modifier keycodes forwarded intact");
}

static void dispatch_errors(void) {
    reset();
    fail_at = 1;
    assert(change("pair0", 0, true, 1) == -EIO);
    assert(socd_data_0.output == SOCD_OUTPUT_KEY0); /* Dispatched, not acknowledged. */
    assert(change("pair0", 0, true, 2) == 0); /* No ambiguous retry. */
    assert(edge_count == 1);
    fail_at = 0;
    assert(change("pair0", 0, false, 3) == 0);
    expect_edge(1, socd_config_0.keys[0], false, 3);

    reset();
    assert(change("pair0", 0, true, 1) == 0);
    fail_at = 2;
    assert(change("pair0", 1, true, 2) == -EIO);
    assert(edge_count == 2); /* Failed release does not press the new winner. */
    assert(socd_data_0.output == SOCD_OUTPUT_NONE);
    assert(socd_data_0.held[0] && socd_data_0.held[1]);
    assert(change("pair0", 1, true, 3) == 0);
    assert(edge_count == 2);

    reset();
    assert(change("pair0", 0, true, 1) == 0);
    fail_at = 3;
    assert(change("pair0", 1, true, 2) == -EIO);
    assert(edge_count == 3);
    assert(socd_data_0.output == SOCD_OUTPUT_KEY1);
    fail_at = 0;
    assert(change("pair0", 1, false, 3) == 0);
    expect_edge(3, socd_config_0.keys[1], false, 3);
    expect_edge(4, socd_config_0.keys[0], true, 3);
    puts("PASS: dispatch errors propagate, do not blindly retry, and stop failed winner switches");
}

int main(void) {
    exhaustive_sequences();
    invalid_inputs();
    duplicate_callbacks();
    independent_instances();
    encoded_keycodes();
    dispatch_errors();
    puts("All SOCD host regression tests passed.");
    return 0;
}
