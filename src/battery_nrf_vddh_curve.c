/*
 * SPDX-License-Identifier: MIT
 *
 * Sensor de bateria via VDDH del nRF52840 (VDDHDIV5), igual que el driver
 * zmk,battery-nrf-vddh de ZMK, pero convirtiendo mV -> % con una curva de
 * descarga LiPo real (tabla + interpolacion lineal) en lugar de la recta
 * 4.20 V -> 3.45 V de ZMK, que infravalora la carga durante casi toda la
 * vida de la bateria. El % de una LiPo depende solo del voltaje: la
 * capacidad en mAh no interviene.
 */

#define DT_DRV_COMPAT codekeeb_battery_nrf_vddh_curve

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define VDDHDIV (5)

static const struct device *adc = DEVICE_DT_GET(DT_NODELABEL(adc));

struct vddh_curve_value {
    int16_t adc_raw;
    uint16_t millivolts;
    uint8_t state_of_charge;
};

struct vddh_curve_data {
    struct adc_channel_cfg acc;
    struct adc_sequence as;
    struct vddh_curve_value value;
};

/* Curva de descarga tipica de una celda LiPo 1S en reposo. Entre puntos se
 * interpola linealmente. Fuente: curvas de descarga publicadas (Adafruit y
 * fabricantes); la zona 3.9-3.7 V es plana y concentra la mayor parte de la
 * capacidad, cosa que la recta original de ZMK ignora. */
static const struct {
    uint16_t mv;
    uint8_t pct;
} lipo_curve[] = {
    {4200, 100}, {4150, 95}, {4110, 90}, {4080, 85}, {4020, 80},
    {3980, 75},  {3950, 70}, {3910, 65}, {3870, 60}, {3850, 55},
    {3840, 50},  {3820, 45}, {3800, 40}, {3790, 35}, {3770, 30},
    {3750, 25},  {3730, 20}, {3710, 15}, {3690, 10}, {3610, 5},
    {3270, 0},
};

static uint8_t lipo_mv_to_pct(int16_t bat_mv) {
    if (bat_mv >= lipo_curve[0].mv) {
        return 100;
    }
    if (bat_mv <= lipo_curve[ARRAY_SIZE(lipo_curve) - 1].mv) {
        return 0;
    }
    for (size_t i = 1; i < ARRAY_SIZE(lipo_curve); i++) {
        if (bat_mv >= lipo_curve[i].mv) {
            const int16_t mv_hi = lipo_curve[i - 1].mv;
            const int16_t mv_lo = lipo_curve[i].mv;
            const int16_t pct_hi = lipo_curve[i - 1].pct;
            const int16_t pct_lo = lipo_curve[i].pct;
            return pct_lo + ((bat_mv - mv_lo) * (pct_hi - pct_lo)) / (mv_hi - mv_lo);
        }
    }
    return 0;
}

static int vddh_curve_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    if (chan != SENSOR_CHAN_GAUGE_VOLTAGE && chan != SENSOR_CHAN_GAUGE_STATE_OF_CHARGE &&
        chan != SENSOR_CHAN_ALL) {
        LOG_DBG("Selected channel is not supported: %d.", chan);
        return -ENOTSUP;
    }

    struct vddh_curve_data *drv_data = dev->data;
    struct adc_sequence *as = &drv_data->as;

    int rc = adc_read(adc, as);
    as->calibrate = false;

    if (rc != 0) {
        LOG_ERR("Failed to read ADC: %d", rc);
        return rc;
    }

    int32_t val = drv_data->value.adc_raw;
    rc = adc_raw_to_millivolts(adc_ref_internal(adc), drv_data->acc.gain, as->resolution, &val);
    if (rc != 0) {
        LOG_ERR("Failed to convert raw ADC to mV: %d", rc);
        return rc;
    }

    drv_data->value.millivolts = val * VDDHDIV;
    drv_data->value.state_of_charge = lipo_mv_to_pct(drv_data->value.millivolts);

    LOG_DBG("ADC raw %d ~ %d mV => %d%%", drv_data->value.adc_raw, drv_data->value.millivolts,
            drv_data->value.state_of_charge);

    return rc;
}

static int vddh_curve_channel_get(const struct device *dev, enum sensor_channel chan,
                                  struct sensor_value *val_out) {
    struct vddh_curve_data const *drv_data = dev->data;

    switch (chan) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        val_out->val1 = drv_data->value.millivolts / 1000;
        val_out->val2 = (drv_data->value.millivolts % 1000) * 1000U;
        break;

    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        val_out->val1 = drv_data->value.state_of_charge;
        val_out->val2 = 0;
        break;

    default:
        return -ENOTSUP;
    }

    return 0;
}

static const struct sensor_driver_api vddh_curve_api = {
    .sample_fetch = vddh_curve_sample_fetch,
    .channel_get = vddh_curve_channel_get,
};

static int vddh_curve_init(const struct device *dev) {
    struct vddh_curve_data *drv_data = dev->data;

    if (!device_is_ready(adc)) {
        LOG_ERR("ADC device is not ready %s", adc->name);
        return -ENODEV;
    }

    drv_data->as = (struct adc_sequence){
        .channels = BIT(0),
        .buffer = &drv_data->value.adc_raw,
        .buffer_size = sizeof(drv_data->value.adc_raw),
        .oversampling = 4,
        .calibrate = true,
    };

#ifdef CONFIG_ADC_NRFX_SAADC
    drv_data->acc = (struct adc_channel_cfg){
        .gain = ADC_GAIN_1_2,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
        .input_positive = SAADC_CH_PSELN_PSELN_VDDHDIV5,
    };

    drv_data->as.resolution = 12;
#else
#error Unsupported ADC
#endif

    const int rc = adc_channel_setup(adc, &drv_data->acc);
    LOG_DBG("VDDHDIV5 setup returned %d", rc);

    return rc;
}

static struct vddh_curve_data vddh_curve_data;

DEVICE_DT_INST_DEFINE(0, &vddh_curve_init, NULL, &vddh_curve_data, NULL, POST_KERNEL,
                      CONFIG_SENSOR_INIT_PRIORITY, &vddh_curve_api);
