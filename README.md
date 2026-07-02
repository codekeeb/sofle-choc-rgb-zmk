# Sofle Choc wireless RGB — ZMK config

Config de ZMK para Sofle Choc inalámbrico: nice!nano v2, OLED (`nice_oled`),
30 LEDs RGB por mitad (29 per-key + 1 en el encoder) y ZMK Studio.

> **Pines de datos RGB según variante de PCB del taller** (¡no mezclar!):
>
> | PCB                        | Pin de datos | LEDs por mitad |
> | -------------------------- | ------------ | -------------- |
> | Sofle RGB MX wireless      | P0.06        | 29             |
> | Sofle Choc wired           | P0.06        | 29             |
> | **Sofle Choc wireless** (este repo) | **P0.08** | **30** (5º = LED del encoder) |
>
> La cadena de esta variante: columna interior de arriba abajo (1-4), LED del
> encoder (5º), pulgares, y serpentea hasta la pinky.

- ZMK está **pineado a `v0.3`** en `config/west.yml` para builds estables y
  reproducibles (la rama `main` de ZMK rompe cosas periódicamente; en
  diciembre de 2025 cambió a Zephyr 4.1 y renombró todos los boards).

## Porcentaje de batería

El % de batería en ZMK **se calcula solo a partir del voltaje** de la celda;
la capacidad (mAh) no interviene, por eso da igual montar una batería de 110,
300 o 2000 mAh. El driver estándar de ZMK usa una recta 4,20 V = 100 % →
3,45 V = 0 %, que infravalora la carga durante casi toda la descarga (una LiPo
pasa la mayor parte de su vida entre 3,9 y 3,7 V, que en esa recta son solo
el 20-60 %).

Este repo incluye un driver propio (`src/battery_nrf_vddh_curve.c`, activado
en `config/boards/nice_nano_v2.overlay`) que mide igual (VDDH/5) pero convierte
con una curva de descarga LiPo real interpolada. La tabla se puede ajustar en
ese archivo si un lote de baterías se comporta distinto.

Notas:

- Con el RGB encendido el voltaje cae bajo carga (sag), así que el % baja unos
  puntos y se recupera al apagar el RGB. Es física de la batería, no un bug;
  cuanto más pequeña la batería, más se nota.
- Cargando por USB, VDDH ve el voltaje de carga y el % marca ~100 %: solo es
  fiable con el USB desconectado.
- Cada mitad muestra **su propia** batería en su OLED.

## Efectos RGB

ZMK de serie solo trae 4 efectos de underglow (solid, breathe, spectrum,
swirl). Este repo lo sustituye por un sistema de animaciones (código
vendorizado de [zmk-rgb-fx](https://github.com/crystalplanet/zmk-rgb-fx),
MIT, con varios fixes) con geometría real del teclado:

| Efecto       | Descripción                                             |
| ------------ | ------------------------------------------------------- |
| **Ripple**   | Ondas que emanan de cada tecla pulsada                  |
| **Gradient** | Gradiente de 3 colores animado en diagonal              |
| **Sparkle**  | Destellos aleatorios entre dos colores                  |
| **Solid**    | Color uniforme que cicla lentamente entre dos tonos     |

Teclas (capa lower, donde antes estaban las RGB):

- `RGBFX_TOGGLE` enciende/apaga · `RGBFX_NEXT/PREVIOUS` cambia de efecto
- `RGBFX_BRIGHTEN/DIM` sube/baja brillo (5 pasos) · estado persistente

La geometría (posición x,y de cada uno de los 29 LEDs y el mapa
tecla→LED) está en `config/sofle_left.overlay` y `config/sofle_right.overlay`;
los colores y tiempos de cada efecto se ajustan ahí mismo (formato
`HSL(h, s, l)`). Los fixes sobre el módulo original (en `src/`): typo
`key_position`→`key-pixels`, tabla de distancias del ripple sin rellenar,
y `locality` global para que las teclas RGB actúen en ambas mitades.

Ojo con la batería: ripple/sparkle/gradient redibujan a 30 FPS mientras hay
actividad; con celdas pequeñas (110-300 mAh) el efecto solid es el frugal.

## Compilar y flashear

Push a `main` → GitHub Actions publica los `.uf2` (izquierda, derecha y
`settings_reset`) como artefacto. Flashear con doble-reset → unidad USB →
copiar el `.uf2`.
