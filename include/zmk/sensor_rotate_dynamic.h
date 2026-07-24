/*
 * SPDX-License-Identifier: MIT
 *
 * CODEKEEB PATCH: runtime-editable sensor (encoder) rotation bindings.
 * See dts/bindings/behaviors/zmk,behavior-sensor-rotate-dynamic.yaml and
 * src/studio/sensor_binding_subsystem.c for the full picture.
 */

#pragma once

#include <zephyr/kernel.h>
#include <zmk/behavior.h>

enum sensor_rotate_dynamic_set_result {
    SENSOR_ROTATE_DYNAMIC_SET_OK = 0,
    SENSOR_ROTATE_DYNAMIC_SET_ERR_NOT_FOUND = -1,
    SENSOR_ROTATE_DYNAMIC_SET_ERR_INVALID_BEHAVIOR = -2,
};

/**
 * @brief Overwrite the clockwise or counter-clockwise binding of the
 * sensor_rotate_dynamic instance registered for (layer_id, sensor_idx).
 *
 * @param cw true to set the clockwise binding, false for counter-clockwise.
 * @return SENSOR_ROTATE_DYNAMIC_SET_OK, or a negative
 *         sensor_rotate_dynamic_set_result on failure.
 */
int sensor_rotate_dynamic_set_binding(uint8_t layer_id, uint8_t sensor_idx, bool cw,
                                      struct zmk_behavior_binding binding);

/**
 * @brief Read back the clockwise or counter-clockwise binding currently
 * assigned to the sensor_rotate_dynamic instance for (layer_id, sensor_idx).
 *
 * @return NULL if no instance is registered for that (layer, sensor) pair.
 */
const struct zmk_behavior_binding *
sensor_rotate_dynamic_get_binding(uint8_t layer_id, uint8_t sensor_idx, bool cw);

/**
 * @brief Apply a sensor-binding value RECEIVED FROM THE SPLIT PERIPHERAL
 * (see ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_SENSOR_ROTATE_DYNAMIC_SYNC
 * in the codekeeb/zmk fork's zmk/split/transport/types.h) to the LOCAL
 * instance for that (layer_id, sensor_idx), WITHOUT re-triggering another
 * sync back out over the split link. Called only from
 * app/src/split/central.c's peripheral event handler on the central; a
 * standalone (non-split) build or a peripheral build never calls this.
 *
 * behavior_local_id is looked up via
 * zmk_behavior_find_behavior_name_from_local_id(); if the resulting name
 * doesn't resolve to a real behavior device (which shouldn't normally
 * happen, since both halves compile the same keymap/behavior set), the
 * sync is dropped and logged rather than crashing.
 *
 * @return 0 on success (including "dropped, logged"), negative errno on
 *         a hard failure.
 */
int zmk_sensor_rotate_dynamic_apply_synced_binding(uint8_t layer_id, uint8_t sensor_idx, bool cw,
                                                   uint16_t behavior_local_id, uint32_t param1,
                                                   uint32_t param2);
