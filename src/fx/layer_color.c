/*
 * SPDX-License-Identifier: MIT
 *
 * Tinte por capa: mientras lower/raise estan activas, todo el teclado se
 * pinta de un color de aviso (fucsia / verde). Las capas solo las conoce
 * la mitad central, asi que el estado se reenvia al periferico por el
 * canal de behaviors del split (behavior `rgblay`, nombre <= 8 chars).
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <drivers/rgb_fx.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_fx.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 0 = sin tinte (capa base); indices en la tabla de colores. */
static uint8_t layer_tint = 0;

/* Colores de aviso por capa: [1] = lower (fucsia), [2] = raise (verde). */
static const struct zmk_color_hsl layer_tint_colors[] = {
    {.h = 0, .s = 0, .l = 0},
    {.h = 315, .s = 100, .l = 50},
    {.h = 110, .s = 100, .l = 50},
};

void zmk_rgb_fx_layer_color_apply(struct rgb_fx_pixel *pixels, size_t num_pixels) {
    if (layer_tint == 0 || layer_tint >= ARRAY_SIZE(layer_tint_colors)) {
        return;
    }

    /* Colores de aviso FIJOS: compensar el offset de tono global para que
     * el fucsia/verde no roten con lower+F/G. */
    struct zmk_color_hsl hsl = layer_tint_colors[layer_tint];
    hsl.h = (hsl.h + 360 - zmk_rgb_fx_hue_offset) % 360;

    struct zmk_color_rgb rgb;
    zmk_hsl_to_rgb(&hsl, &rgb);

    for (size_t i = 0; i < num_pixels; i++) {
        pixels[i].value = rgb;
    }
}

static void layer_tint_set(uint8_t tint) {
    if (tint == layer_tint) {
        return;
    }

    layer_tint = tint;
    zmk_rgb_fx_request_frames(1);
}

/* ---- behavior rgblay: recibe el estado en el periferico ---- */

#define DT_DRV_COMPAT zmk_behavior_rgb_fx_layer

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    layer_tint_set(binding->param1);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_rgb_fx_layer_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_rgb_fx_layer_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */

/* ---- central: escucha los cambios de capa y reenvia al periferico ---- */

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)

#include <zmk/split/central.h>

static int layer_color_listener(const zmk_event_t *eh) {
    if (as_zmk_layer_state_changed(eh) == NULL) {
        return -ENOTSUP;
    }

    uint8_t highest = zmk_keymap_highest_layer_active();
    uint8_t tint = (highest == 1 || highest == 2) ? highest : 0;

    layer_tint_set(tint);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    struct zmk_behavior_binding binding = {
        .behavior_dev = "rgblay",
        .param1 = tint,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    for (uint8_t source = 0; source < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; source++) {
        zmk_split_central_invoke_behavior(source, &binding, event, true);
    }
#endif

    return 0;
}

ZMK_LISTENER(rgb_fx_layer_color, layer_color_listener);
ZMK_SUBSCRIPTION(rgb_fx_layer_color, zmk_layer_state_changed);

#endif /* central */
