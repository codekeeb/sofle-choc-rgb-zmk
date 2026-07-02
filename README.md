# Sofle Choc wireless RGB — ZMK config

Config de ZMK para Sofle Choc inalámbrico: nice!nano v2, OLED (`nice_oled`),
35 LEDs RGB WS2812 y ZMK Studio.

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

## Compilar y flashear

Push a `main` → GitHub Actions publica los `.uf2` (izquierda, derecha y
`settings_reset`) como artefacto. Flashear con doble-reset → unidad USB →
copiar el `.uf2`.
