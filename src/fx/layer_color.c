/*
 * SPDX-License-Identifier: MIT
 *
 * Tinte por capa: mientras LOWER (rosa) o RAISE (morado) estan activas,
 * TODOS los LEDs de los pulgares de ambas mitades se pintan del color de
 * aviso. En RAISE ademas el panel Bluetooth (BT_CLR rojo, perfiles 0-4
 * amarillo con el activo en verde) y el cluster de flechas IJKL en amarillo.
 * Las capas solo las conoce la mitad central, asi que el estado se reenvia
 * al periferico por el canal de behaviors del split (behavior `rgblay`,
 * nombre <= 8 chars).
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <drivers/rgb_fx.h>

#include <zmk/behavior.h>
#include <zmk/ble.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/rgb_fx.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* 0 = sin tinte (capa base); indices en la tabla de colores. */
static uint8_t layer_tint = 0;

/* Colores de aviso por capa: [1] = LOWER (rosa), [2] = RAISE (morado). */
static const struct zmk_color_hsl layer_tint_colors[] = {
    {.h = 0, .s = 0, .l = 0},
    {.h = 330, .s = 100, .l = 50},  /* LOWER = rosa */
    {.h = 270, .s = 100, .l = 50},  /* RAISE = morado */
};

/* Todos los LEDs de los pulgares (mismos indices de cadena en ambas mitades). */
static const uint8_t layer_tint_thumb_px[] = {5, 6, 7, 16, 17};

/* Cluster de flechas en raise: I(UP) J(LEFT) K(DOWN) L(RIGHT).
 * Solo existe en la mitad derecha (periferico). */
static const uint8_t layer_tint_arrow_px[] = {13, 9, 14, 19};

/* Amarillo anaranjado saturado para las flechas. */
static const struct zmk_color_hsl layer_tint_arrow_color = {.h = 40, .s = 100, .l = 50};

/* Colores de aviso FIJOS: compensar el offset de tono global para que
 * no roten con el ajuste de tono. */
static struct zmk_color_rgb tint_to_rgb(struct zmk_color_hsl hsl) {
    struct zmk_color_rgb rgb;

    hsl.h = (hsl.h + 360 - zmk_rgb_fx_hue_offset) % 360;
    zmk_hsl_to_rgb(&hsl, &rgb);

    return rgb;
}

void zmk_rgb_fx_layer_color_apply(struct rgb_fx_pixel *pixels, size_t num_pixels) {
    if (layer_tint == 0 || layer_tint >= ARRAY_SIZE(layer_tint_colors)) {
        return;
    }

    struct zmk_color_rgb rgb = tint_to_rgb(layer_tint_colors[layer_tint]);

    /* TODOS los pulgares de esta mitad se pintan del color de la capa
     * (LOWER rosa / RAISE morado), en ambas mitades. */
    for (size_t j = 0; j < ARRAY_SIZE(layer_tint_thumb_px); j++) {
        if (layer_tint_thumb_px[j] < num_pixels) {
            pixels[layer_tint_thumb_px[j]].value = rgb;
        }
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    /* En RAISE, iluminar el cluster de flechas (solo mitad derecha). */
    if (layer_tint == 2) {
        struct zmk_color_rgb flechas = tint_to_rgb(layer_tint_arrow_color);

        for (size_t j = 0; j < ARRAY_SIZE(layer_tint_arrow_px); j++) {
            if (layer_tint_arrow_px[j] < num_pixels) {
                pixels[layer_tint_arrow_px[j]].value = flechas;
            }
        }
    }
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL) || !IS_ENABLED(CONFIG_ZMK_SPLIT)
    /* Panel Bluetooth en RAISE: BT_CLR (esquina sup. izq.) en rojo,
     * perfiles 0-4 en amarillo y el perfil ACTIVO en verde. */
    if (layer_tint == 2) {
        static const uint8_t bt_clr_px = 29;                 /* esquina sup. izq. (BT_CLR) */
        static const uint8_t bt_prof_px[] = {22, 21, 12, 11, 0}; /* teclas 1-5 */

        if (bt_clr_px < num_pixels) {
            pixels[bt_clr_px].value =
                tint_to_rgb((struct zmk_color_hsl){.h = 0, .s = 100, .l = 50});
        }

        struct zmk_color_rgb amarillo =
            tint_to_rgb((struct zmk_color_hsl){.h = 50, .s = 100, .l = 50});
        struct zmk_color_rgb verde =
            tint_to_rgb((struct zmk_color_hsl){.h = 120, .s = 100, .l = 50});
        uint8_t active = zmk_ble_active_profile_index();

        for (size_t i = 0; i < ARRAY_SIZE(bt_prof_px); i++) {
            if (bt_prof_px[i] < num_pixels) {
                pixels[bt_prof_px[i]].value = (i == active) ? verde : amarillo;
            }
        }
    }
#endif
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
    /* Cambio de perfil BT con RAISE apretada: redibujar el panel. */
    if (as_zmk_ble_active_profile_changed(eh) != NULL) {
        if (layer_tint == 2) {
            zmk_rgb_fx_request_frames(1);
        }
        return 0;
    }

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
ZMK_SUBSCRIPTION(rgb_fx_layer_color, zmk_ble_active_profile_changed);

#endif /* central */
