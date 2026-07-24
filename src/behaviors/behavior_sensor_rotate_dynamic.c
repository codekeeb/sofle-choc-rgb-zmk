/*
 * SPDX-License-Identifier: MIT
 *
 * CODEKEEB PATCH: sensor (encoder) rotation binding, editable at runtime
 * and persisted in flash -- ZMK's stock zmk,behavior-sensor-rotate-var
 * bakes its cw/ccw bindings in forever at compile time from devicetree,
 * with no public way to change them. This behavior keeps the exact same
 * rotation-processing logic (vendored from
 * app/src/behaviors/behavior_sensor_rotate_common.c, tap-based dispatch)
 * but stores its cw/ccw bindings in mutable per-instance config, exposes
 * a lookup table keyed by (layer_id, sensor_idx) so the studio subsystem
 * can find and rewrite the right instance, and persists changes via
 * settings the same way regular key bindings already do.
 */

#define DT_DRV_COMPAT zmk_behavior_sensor_rotate_dynamic

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>
#include <zmk/sensors.h>
#include <zmk/virtual_key_position.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/sensor_rotate_dynamic.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/transport/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct sensor_rotate_dynamic_config {
    uint8_t layer_id;
    uint8_t sensor_idx;
    int tap_ms;
};

struct sensor_rotate_dynamic_data {
    struct zmk_behavior_binding cw_binding;
    struct zmk_behavior_binding ccw_binding;
    struct sensor_value remainder[ZMK_KEYMAP_LAYERS_LEN];
    int triggers[ZMK_KEYMAP_LAYERS_LEN];
};

/* ---- (layer_id, sensor_idx) -> device lookup, for the studio subsystem ----
 *
 * Only a handful of instances ever exist (layers x sensors), so a small
 * static registry populated at init time and scanned linearly is plenty. */
#define MAX_SENSOR_ROTATE_DYNAMIC_INSTANCES (ZMK_KEYMAP_LAYERS_LEN * 4)

static const struct device *registry[MAX_SENSOR_ROTATE_DYNAMIC_INSTANCES];
static size_t registry_len;

static void register_instance(const struct device *dev) {
    if (registry_len >= MAX_SENSOR_ROTATE_DYNAMIC_INSTANCES) {
        LOG_ERR("sensor_rotate_dynamic registry full, dropping instance %s", dev->name);
        return;
    }
    registry[registry_len++] = dev;
}

static const struct device *lookup_instance(uint8_t layer_id, uint8_t sensor_idx) {
    for (size_t i = 0; i < registry_len; i++) {
        const struct sensor_rotate_dynamic_config *cfg = registry[i]->config;
        if (cfg->layer_id == layer_id && cfg->sensor_idx == sensor_idx) {
            return registry[i];
        }
    }
    return NULL;
}

/* ---- public read/write API (used by the studio subsystem) ---- */

const struct zmk_behavior_binding *
sensor_rotate_dynamic_get_binding(uint8_t layer_id, uint8_t sensor_idx, bool cw) {
    const struct device *dev = lookup_instance(layer_id, sensor_idx);
    if (!dev) {
        return NULL;
    }
    struct sensor_rotate_dynamic_data *data = dev->data;
    return cw ? &data->cw_binding : &data->ccw_binding;
}

#if IS_ENABLED(CONFIG_SETTINGS)
static void save_binding(const struct device *dev, bool cw) {
    const struct sensor_rotate_dynamic_config *cfg = dev->config;
    struct sensor_rotate_dynamic_data *data = dev->data;
    const struct zmk_behavior_binding *binding = cw ? &data->cw_binding : &data->ccw_binding;

    struct {
        zmk_behavior_local_id_t behavior_local_id;
        uint32_t param1;
        uint32_t param2;
    } __packed setting = {
        .behavior_local_id = zmk_behavior_get_local_id(binding->behavior_dev),
        .param1 = binding->param1,
        .param2 = binding->param2,
    };

    char key[40];
    snprintf(key, sizeof(key), "sensrot/%d/%d/%d", cfg->layer_id, cfg->sensor_idx, cw ? 1 : 0);
    settings_save_one(key, &setting, sizeof(setting));
}
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

/* ---- split relay: peripheral -> central ----
 *
 * ZMK Studio's RPC transport only runs on the central, but each half
 * processes ITS OWN encoder's sensor events locally (confirmed: sensor
 * events never cross the split on their own). So a peripheral-side
 * encoder's binding is only visible to Studio if the peripheral itself
 * pushes it over -- reported whenever the split link comes up (see the
 * zmk_split_peripheral_status_changed listener below), which covers
 * both first boot and any later reconnect. */
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

static void report_binding_to_central(const struct device *dev, bool cw) {
    const struct sensor_rotate_dynamic_config *cfg = dev->config;
    struct sensor_rotate_dynamic_data *data = dev->data;
    const struct zmk_behavior_binding *binding = cw ? &data->cw_binding : &data->ccw_binding;

    struct zmk_split_transport_peripheral_event ev = {
        .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_SENSOR_ROTATE_DYNAMIC_SYNC,
        .data = {.sensor_rotate_dynamic_sync = {
                     .layer_id = cfg->layer_id,
                     .sensor_index = cfg->sensor_idx,
                     .clockwise = cw ? 1 : 0,
                     .behavior_local_id = zmk_behavior_get_local_id(binding->behavior_dev),
                     .param1 = binding->param1,
                     .param2 = binding->param2,
                 }}};

    int err = zmk_split_peripheral_report_event(&ev);
    if (err) {
        LOG_WRN("Failed to report sensrot binding (layer %d, sensor %d, cw %d) to central: %d",
                cfg->layer_id, cfg->sensor_idx, cw, err);
    }
}

static void report_all_bindings_to_central(void) {
    /* The keymap declares one instance per (layer, sensor) for BOTH
     * sensors on every half (the same sofle.keymap compiles into both
     * binaries) -- but only the sensor that's physically wired to THIS
     * half ever gets real rotation events here. Reporting the other
     * sensor's instances too would push this half's meaningless factory
     * values over the central's own (correct, locally-processed) copy
     * of that sensor. This board's peripheral is always the right half,
     * i.e. sensor_index 1 (see sofle.keymap's enc_r0/1/2 vs enc_l0/1/2);
     * only report those. */
    for (size_t i = 0; i < registry_len; i++) {
        const struct sensor_rotate_dynamic_config *cfg = registry[i]->config;
        if (cfg->sensor_idx != 1) {
            continue;
        }
        report_binding_to_central(registry[i], true);
        report_binding_to_central(registry[i], false);
    }
}

/* Settings finish loading well before the split link is actually up
 * (BLE pairing/reconnect takes seconds), so pushing from a settings
 * commit callback would silently drop the message (no active transport
 * yet). Instead, wait for the connected notification, which is exactly
 * "the link is now usable" -- and this fires on every reconnect too,
 * not just first boot, so a central that misses the first push (e.g. it
 * booted after the peripheral) still converges once it's back. */
static int sensor_rotate_dynamic_split_listener(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *ev;
    if ((ev = as_zmk_split_peripheral_status_changed(eh)) == NULL) {
        return -ENOTSUP;
    }

    if (ev->connected) {
        report_all_bindings_to_central();
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(sensor_rotate_dynamic_split, sensor_rotate_dynamic_split_listener);
ZMK_SUBSCRIPTION(sensor_rotate_dynamic_split, zmk_split_peripheral_status_changed);

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

int sensor_rotate_dynamic_set_binding(uint8_t layer_id, uint8_t sensor_idx, bool cw,
                                      struct zmk_behavior_binding binding) {
    const struct device *dev = lookup_instance(layer_id, sensor_idx);
    if (!dev) {
        return SENSOR_ROTATE_DYNAMIC_SET_ERR_NOT_FOUND;
    }

    if (zmk_behavior_validate_binding(&binding) < 0) {
        return SENSOR_ROTATE_DYNAMIC_SET_ERR_INVALID_BEHAVIOR;
    }

    struct sensor_rotate_dynamic_data *data = dev->data;
    if (cw) {
        data->cw_binding = binding;
    } else {
        data->ccw_binding = binding;
    }

#if IS_ENABLED(CONFIG_SETTINGS)
    save_binding(dev, cw);
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    report_binding_to_central(dev, cw);
#endif

    return SENSOR_ROTATE_DYNAMIC_SET_OK;
}

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

int zmk_sensor_rotate_dynamic_apply_synced_binding(uint8_t layer_id, uint8_t sensor_idx, bool cw,
                                                   uint16_t behavior_local_id, uint32_t param1,
                                                   uint32_t param2) {
    const struct device *dev = lookup_instance(layer_id, sensor_idx);
    if (!dev) {
        LOG_WRN("Got a sensrot sync for unknown (layer %d, sensor %d), ignoring", layer_id,
                sensor_idx);
        return 0;
    }

    const char *behavior_name = zmk_behavior_find_behavior_name_from_local_id(behavior_local_id);
    if (!behavior_name) {
        LOG_WRN("Sensrot sync (layer %d, sensor %d, cw %d) names an unknown behavior, ignoring",
                layer_id, sensor_idx, cw);
        return 0;
    }

    struct sensor_rotate_dynamic_data *data = dev->data;
    struct zmk_behavior_binding synced = {
        .behavior_dev = behavior_name,
        .param1 = param1,
        .param2 = param2,
    };
    if (cw) {
        data->cw_binding = synced;
    } else {
        data->ccw_binding = synced;
    }

#if IS_ENABLED(CONFIG_SETTINGS)
    /* The central persists it too, so it survives even if it boots
     * without the peripheral connected (using the last known value). */
    save_binding(dev, cw);
#endif

    return 0;
}

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) */

/* ---- settings load ---- */

#if IS_ENABLED(CONFIG_SETTINGS)

struct sensrot_setting_wire {
    zmk_behavior_local_id_t behavior_local_id;
    uint32_t param1;
    uint32_t param2;
} __packed;

static int sensor_rotate_dynamic_settings_set(const char *name, size_t len,
                                              settings_read_cb read_cb, void *cb_arg) {
    /* name is "<layer_id>/<sensor_idx>/<cw>" (the "sensrot/" prefix is
     * already stripped by the settings subsystem for our handler). */
    char *endptr;
    uint8_t layer_id = strtoul(name, &endptr, 10);
    if (*endptr != '/') {
        return -EINVAL;
    }
    uint8_t sensor_idx = strtoul(endptr + 1, &endptr, 10);
    if (*endptr != '/') {
        return -EINVAL;
    }
    bool cw = strtoul(endptr + 1, &endptr, 10) != 0;
    if (*endptr != '\0') {
        return -EINVAL;
    }

    const struct device *dev = lookup_instance(layer_id, sensor_idx);
    if (!dev) {
        LOG_WRN("Loaded sensrot setting for unknown (layer %d, sensor %d), ignoring", layer_id,
                sensor_idx);
        return 0;
    }

    struct sensrot_setting_wire wire = {0};
    int err = read_cb(cb_arg, &wire, MIN(len, sizeof(wire)));
    if (err <= 0) {
        return err;
    }

    const char *behavior_name = zmk_behavior_find_behavior_name_from_local_id(wire.behavior_local_id);
    if (!behavior_name) {
        LOG_WRN("sensrot setting (layer %d, sensor %d, cw %d) names an unknown behavior, ignoring",
                layer_id, sensor_idx, cw);
        return 0;
    }

    struct sensor_rotate_dynamic_data *data = dev->data;
    struct zmk_behavior_binding loaded = {
        .behavior_dev = behavior_name,
        .param1 = wire.param1,
        .param2 = wire.param2,
    };
    if (cw) {
        data->cw_binding = loaded;
    } else {
        data->ccw_binding = loaded;
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(sensrot, "sensrot", NULL, sensor_rotate_dynamic_settings_set, NULL,
                               NULL);

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

/* ---- rotation processing (vendored from behavior_sensor_rotate_common.c) ---- */

static int on_sensor_binding_accept_data(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event,
                                         const struct zmk_sensor_config *sensor_config,
                                         size_t channel_data_size,
                                         const struct zmk_sensor_channel_data *channel_data) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct sensor_rotate_dynamic_data *data = dev->data;

    const struct sensor_value value = channel_data[0].value;
    int triggers;

    if (value.val1 == 0) {
        triggers = value.val2;
    } else {
        struct sensor_value remainder = data->remainder[event.layer];

        remainder.val1 += value.val1;
        remainder.val2 += value.val2;

        if (remainder.val2 >= 1000000 || remainder.val2 <= 1000000) {
            remainder.val1 += remainder.val2 / 1000000;
            remainder.val2 %= 1000000;
        }

        int trigger_degrees = 360 / sensor_config->triggers_per_rotation;
        triggers = remainder.val1 / trigger_degrees;
        remainder.val1 %= trigger_degrees;

        data->remainder[event.layer] = remainder;
    }

    data->triggers[event.layer] = triggers;
    return 0;
}

static int on_sensor_binding_process(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event,
                                     enum behavior_sensor_binding_process_mode mode) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct sensor_rotate_dynamic_config *cfg = dev->config;
    struct sensor_rotate_dynamic_data *data = dev->data;

    if (mode != BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_TRIGGER) {
        data->triggers[event.layer] = 0;
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    int triggers = data->triggers[event.layer];

    struct zmk_behavior_binding triggered_binding;
    if (triggers > 0) {
        triggered_binding = data->cw_binding;
    } else if (triggers < 0) {
        triggers = -triggers;
        triggered_binding = data->ccw_binding;
    } else {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    event.source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL;
#endif

    for (int i = 0; i < triggers; i++) {
        zmk_behavior_queue_add(&event, triggered_binding, true, cfg->tap_ms);
        zmk_behavior_queue_add(&event, triggered_binding, false, 0);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api sensor_rotate_dynamic_driver_api = {
    .sensor_binding_accept_data = on_sensor_binding_accept_data,
    .sensor_binding_process = on_sensor_binding_process,
};

static int sensor_rotate_dynamic_init(const struct device *dev) {
    register_instance(dev);
    return 0;
}

/* Extracts the full {behavior_dev, param1, param2} for bindings entry
 * `idx`, same as keymap.c's _TRANSFORM_SENSOR_ENTRY/ZMK_KEYMAP_EXTRACT_BINDING
 * -- NOT just the phandle name like zmk,behavior-sensor-rotate-var does,
 * since our factory bindings (e.g. &rgbfx RGBFX_NEXT(0)) carry real
 * params that a name-only extraction would silently drop to 0. */
#define _SENSOR_ROTATE_DYNAMIC_BINDING(n, idx)                                                     \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, idx)),                  \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param1), (0),          \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param1))),                      \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(n, bindings, idx, param2), (0),          \
                              (DT_INST_PHA_BY_IDX(n, bindings, idx, param2))),                      \
    }

#define SENSOR_ROTATE_DYNAMIC_INST(n)                                                             \
    static struct sensor_rotate_dynamic_config sensor_rotate_dynamic_config_##n = {                \
        .layer_id = DT_INST_PROP(n, layer_id),                                                    \
        .sensor_idx = DT_INST_PROP(n, sensor_index),                                              \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                        \
    };                                                                                            \
    static struct sensor_rotate_dynamic_data sensor_rotate_dynamic_data_##n = {                    \
        .cw_binding = _SENSOR_ROTATE_DYNAMIC_BINDING(n, 0),                                        \
        .ccw_binding = _SENSOR_ROTATE_DYNAMIC_BINDING(n, 1),                                       \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, sensor_rotate_dynamic_init, NULL,                                  \
                            &sensor_rotate_dynamic_data_##n, &sensor_rotate_dynamic_config_##n,    \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                      \
                            &sensor_rotate_dynamic_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SENSOR_ROTATE_DYNAMIC_INST)
