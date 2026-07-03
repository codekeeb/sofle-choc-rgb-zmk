# Sofle Choc Wireless RGB — firmware ZMK · CODE/KEEB

Firmware ZMK para el **Sofle Choc inalámbrico RGB** de CODE/KEEB:
nice!nano v2, pantallas OLED verticales, 30 LEDs RGB por mitad
(29 per-key + 1 en el encoder), 2 encoders rotatorios y ZMK Studio.

Incluye un **motor de efectos RGB propio** (vendorizado y muy ampliado a
partir de [zmk-rgb-fx], MIT) y un **fork del módulo de pantallas**
([codekeeb/zmk-nice-oled], rama `selectable`) con funciones que no existen
en los originales.

---

## ⌨️ Controles

### Encoders

| Gesto                        | Capa base            | LOWER                  | RAISE       |
| ---------------------------- | -------------------- | ---------------------- | ----------- |
| **Girar encoder izquierdo**  | Volumen              | Modo RGB ±             | Brillo ±    |
| **Girar encoder derecho**    | Avance página rápido | Tono ±20°              | Velocidad ± |
| **Click encoder izquierdo**  | Silencio (mute)      | RGB on/off             | —           |
| **Click encoder derecho**    | Corte de corriente de los LEDs (`EP_TOG`) | Siguiente animación OLED | — |

### Teclas (capa LOWER, mitad izquierda)

| Tecla | Función                    |
| ----- | -------------------------- |
| R     | RGB on/off                 |
| T     | Siguiente modo RGB         |
| F / G | Tono − / +                 |
| V / B | Brillo − / +               |

### Otras

- **Caps Word**: RAISE + G (donde estaba CapsLock) — escribe EN MAYÚSCULAS
  hasta el primer espacio.
- **ZMK Studio**: la mitad izquierda lleva Studio; conectar por USB, abrir
  [zmk.studio](https://zmk.studio) y desbloquear con LOWER + Z
  (`studio_unlock`) para **remapear en vivo sin flashear**.

Todos los ajustes RGB (modo, tono, brillo, velocidad, on/off) y la
animación OLED elegida **persisten en flash**: sobreviven a apagados y
reflasheos (solo los borra `settings_reset`).

---

## 🌈 Sistema RGB

Motor de animaciones por coordenadas: cada LED tiene posición (x,y) en un
lienzo **continuo de 0 a 240 que abarca ambas mitades** (izquierda 0-117,
derecha 123-240), así los degradados horizontales cruzan la costura sin
salto. Los mapas viven en `config/sofle_left.overlay` y
`config/sofle_right.overlay`.

### Modos (ciclo con LOWER + encoder izquierdo)

| #  | Modo         | Descripción                                                |
| -- | ------------ | ---------------------------------------------------------- |
| 1  | Gradient     | Degradado 3 colores animado en diagonal                     |
| 2  | Ripple       | Ondas azules desde cada tecla pulsada (por mitad)           |
| 3  | Sparkle      | Destellos cian/magenta                                      |
| 4  | Solid        | Color uniforme ciclando ámbar↔rosa                          |
| 5  | Fire         | Gradiente vertical rojo-naranja-amarillo, rápido            |
| 6  | Ocean        | Azul-cian-verde horizontal, muy lento                       |
| 7  | Sparkle oro  | Destellos dorados rápidos                                   |
| 8  | Ripple rosa  | Ondas rosas más rápidas y anchas                            |
| 9  | Sunset       | **Estático**: naranja-coral-rosa-morado-azul (S100, arco justo) |
| 10 | Heatmap      | Cada tecla se enciende al pulsarla y se desvanece (~1,2 s)  |

### Controles globales (afectan a todos los modos)

- **Tono**: offset de 0-359° aplicado en la conversión HSL→RGB — rota la
  paleta completa de cualquier modo sin destruirla. Pasos de 20°.
- **Brillo**: 5 pasos (mínimo 1: apagar es cosa del toggle — un brillo 0
  persistido dejaba el teclado negro para siempre).
- **Velocidad**: 5 pasos (0.25×–4×) escalando el periodo del tick de
  animación — acelera/frena todos los efectos por igual.
- **Color por capa**: LOWER pinta los **pulgares de fucsia** y RAISE de
  **verde** + el **cluster de flechas (I/J/K/L) en morado** en la derecha
  (aviso visual; colores fijos, no rotan con el tono). El estado viaja a
  la mitad derecha por el canal de behaviors del split (behavior `rgblay`).
- **Auto-off**: a los 60 s sin actividad (`CONFIG_ZMK_IDLE_TIMEOUT`) los
  efectos paran y el strip se apaga en negro; revive con cualquier tecla.
  Cada mitad gestiona su propia inactividad.

### Editar paletas y velocidades

Cada modo es un nodo en los dos `sofle_*.overlay` (¡editar AMBOS!):

- `colors = <HSL(tono, saturación, luminosidad) ...>` — tono 0-359
  (0 rojo, 60 amarillo, 120 verde, 180 cian, 240 azul, 300 magenta);
  S=100 y L=50 es el color puro más vivo (L>50 lava hacia blanco).
- `duration` — segundos por ciclo (gradient/solid/sparkle) o ms de viaje
  de onda (ripple) o ms de desvanecido (heatmap). Menor = más rápido.
- `gradient-width` — con N colores hay N segmentos (el último vuelve al
  primero); para mostrar el arco completo sin repetición en el teclado
  (240 unidades): `width = 240 * N / (N-1)`.
- En degradados con tonos cálidos: el rojo (330°-30°) es perceptualmente
  plano — insertar una parada intermedia (p. ej. coral 355°) da más tonos
  visibles a ese tramo (así está hecho el Sunset).

### Arquitectura y fixes sobre zmk-rgb-fx original

Código en `src/` + `dts/` + `include/` (el repo es un módulo Zephyr).
Sobre el original se corrigió/añadió: typo que anulaba `key-pixels`, tabla
de distancias del ripple sin rellenar, `locality` global del behavior,
arranque del efecto entrante al cambiar de modo, saneo del estado
persistido, y las funciones de tono/velocidad/tinte-por-capa/auto-off/
heatmap descritas arriba.

---

## 🖥️ Pantallas OLED

Módulo: [codekeeb/zmk-nice-oled] rama **`selectable`** (fork de
[mctechnology17/zmk-nice-oled] con mejoras propias).

- **Izquierda (central)**: batería gráfica, salida BT/USB, capa, perfil,
  y **Bongo Cat** aporreando al ritmo de tus WPM.
- **Derecha (periférico)**: batería gráfica + **animación seleccionable en
  caliente** (LOWER + click encoder derecho, persiste):
  1. Gema/cristal giratoria · 2. Gato · 3. Cabeza 3D · 4. Astronauta ·
  5. Pokémon · 6. **Logo CODE/KEEB** (estático)
- **Batería gráfica**: icono de pila con relleno proporcional + rayo al
  cargar (`NICE_OLED_WIDGET_BATTERY_GRAPHIC`), en vez del número.

Mejoras del fork sobre el módulo original: selector de animación en
caliente con persistencia (behavior `&oledanim` + evento + settings),
batería gráfica, default de `ANIMATION_PERIPHERAL_MS` que faltaba (rompía
el build con Smart Battery), y el asset del logo.

---

## 🔋 Batería

- **Porcentaje real**: driver propio (`src/battery_nrf_vddh_curve.c`) con
  curva de descarga LiPo interpolada (21 puntos) en vez de la recta
  4,20→3,45 V de ZMK que infravalora la carga casi toda la descarga.
  El % depende SOLO del voltaje: los mAh de la celda no intervienen.
- Con el USB conectado, VDDH ve el cargador y marca ~100%: solo es fiable
  a batería.
- Con RGB encendido el voltaje cae bajo carga (sag) y el % baja unos
  puntos; se recupera al apagarlo. Física, no bug.
- `CONFIG_BOARD_ENABLE_DCDC_HV=y`: ZMK ≥ v0.3 lo desactivó por defecto
  (clones sin inductor); en placas con inductor conviene activarlo.
- El **brillo RGB inicial es bajo (20%)** a propósito: 30 LEDs a plena
  potencia piden ~500 mA y pueden desplomar el raíl con celdas pequeñas.

---

## 🔧 Hardware: pines por variante de PCB del taller

> **¡No mezclar!** Cada variante usa un pin de datos distinto.

| PCB                                  | Pin datos LED | LEDs por mitad |
| ------------------------------------ | ------------- | -------------- |
| Sofle RGB MX wireless                | P0.06         | 29             |
| Sofle Choc wired                     | P0.06         | 29             |
| **Sofle Choc wireless** (este repo)  | **P0.08**     | **30** (5º = LED del encoder) |

Orden de la cadena (esta variante): columna interior de arriba abajo
(1-4), **LED del encoder (5º)**, pulgares, y serpentina hasta la pinky.

> ⚠️ **El bloque `&spi3`/pinctrl del keymap NO es redundante**: el shield
> `sofle` de ZMK ≥ v0.3 trae su propio `boards/nice_nano_v2.overlay` con
> MOSI en P0.06 y chain 36, y se aplica DESPUÉS del overlay del config.
> El keymap es lo último que entra al devicetree: solo desde ahí se impone
> el pin real (P0.08). Se borró una vez por "duplicado" y costó un día
> entero de depuración (verificado leyendo `PSEL.MOSI` del SPIM3 en
> caliente por USB logging).

---

## 🏗️ Builds

Push a `main` → GitHub Actions publica el artefacto `firmware` con:
`sofle_left` (con ZMK Studio), `sofle_right`, `settings_reset`, y dos
variantes de depuración con logs USB (`sofle_left_usb_logging`,
`sofle_right_usb_logging`).

**Versiones clavadas** para builds reproducibles (`config/west.yml`):

- **ZMK `v0.3`** (la rama `main` cambió a Zephyr 4.1 en dic-2025 y renombró
  los boards; los builds se rompen solos si no se pinea). El workflow
  también va a `@v0.3`.
- **Zephyr pineado por SHA**: la rama `v3.5.0+zmk-fixes` del fork de
  Zephyr es móvil (recibió un bump del HAL de Nordic en 2025); se
  sobreescribe el proyecto desde este manifest (el top-level gana al
  import).
- **zmk-nice-oled**: fork propio, rama `selectable`.

### Flashear

Doble pulsación de reset → unidad USB → copiar el `.uf2`. Tras cambios de
firmware con estado raro: `settings_reset` en ambas mitades y reflashear
(borra también los emparejamientos BT).

### Depurar

Flashear la variante `*_usb_logging`, conectar por USB y leer el puerto
serie (VID 0x1D50, 115200). Imprime el pipeline RGB (`fx tick` con los
valores enviados), el estado de `ext_power` y **los registros reales del
SPIM3** (`PSEL.MOSI` = pin físico en uso) — la herramienta que resolvió
el apagón del RGB.

---

## 🚑 Problemas conocidos y lecciones

| Síntoma | Causa / solución |
| --- | --- |
| Teclas RGB/OLED solo actúan en la mitad izquierda | El transporte split trunca el nombre del behavior a **8 caracteres**. Nodos de behavior siempre ≤8 (`rgbfx`, `rgblay`, `oledanim`). |
| Mitades "conectadas" pero la derecha no escribe | Bond a medias (log: `Link is not encrypted`). Re-emparejar con **todos los demás teclados ZMK del taller apagados** (los centrales ajenos secuestran periféricos). |
| LEDs muertos con SPI aparentemente OK | Pin de datos pisado por el overlay del shield (ver aviso arriba) o raíl sin corriente. Verificar con el build de logging (registros SPIM3). |
| RGB negro que no responde a nada | Estado persistido tóxico (brillo 0 / apagado). Ya se sanea al arrancar; si acaso, `settings_reset`. |
| Build roto tras crear repo nuevo | ZMK sin pinear. Copiar el `west.yml` de aquí. |

---

## Créditos

- Motor RGB basado en [zmk-rgb-fx] de Kuba Birecki (MIT) — muy modificado.
- Pantallas basadas en [mctechnology17/zmk-nice-oled] (MIT) — vía fork propio.
- [ZMK Firmware](https://zmk.dev) `v0.3`.

[zmk-rgb-fx]: https://github.com/crystalplanet/zmk-rgb-fx
[codekeeb/zmk-nice-oled]: https://github.com/codekeeb/zmk-nice-oled/tree/selectable
[mctechnology17/zmk-nice-oled]: https://github.com/mctechnology17/zmk-nice-oled
