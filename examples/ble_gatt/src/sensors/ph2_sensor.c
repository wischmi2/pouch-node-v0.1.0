/* Mikroe pH2 Click sensor handling for XIAO nRF52840 node */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(main);

#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include <pouch/uplink.h>
#include <pouch/types.h>

#include "ph2_sensor.h"

/* Use the on-board I2C1 bus, which is routed to the XIAO connector. */
#define PH2_I2C_BUS_NODE DT_NODELABEL(i2c1)

/* Default MCP3221 address used on many Mikroe boards; adjust if needed. */
#define PH2_MCP3221_I2C_ADDR 0x4D

/* Period between pH reports (seconds). */
#define PH2_REPORT_PERIOD_S 10

static const struct device *ph2_i2c;
static bool pouch_session_active;

/* Simple two-point calibration: pH = slope * raw + offset */
static bool have_low_point;
static bool have_high_point;
static float cal_ph_low;
static float cal_ph_high;
static uint16_t cal_raw_low;
static uint16_t cal_raw_high;
static float cal_slope;
static float cal_offset = 7.0f; /* reasonable default near neutral */
static bool ph_calibrated;

static int ph2_sensor_read_raw(uint16_t *out_raw)
{
    if (!ph2_i2c) {
        return -ENODEV;
    }

    uint8_t buf[2];
    int err = i2c_read(ph2_i2c, buf, sizeof(buf), PH2_MCP3221_I2C_ADDR);
    if (err) {
        LOG_ERR("pH sensor I2C read failed (err %d)", err);
        return err;
    }

    uint16_t raw = ((uint16_t)buf[0] << 8) | buf[1];
    /* MCP3221 is 12-bit left-justified in 16-bit word */
    raw >>= 4;

    *out_raw = raw;
    return 0;
}

static void ph2_update_calibration(void)
{
    if (!have_low_point || !have_high_point) {
        return;
    }

    if (cal_raw_high == cal_raw_low) {
        LOG_WRN("pH calibration points have identical raw values; ignoring");
        return;
    }

    cal_slope = (cal_ph_high - cal_ph_low) /
                (float)(cal_raw_high - cal_raw_low);
    cal_offset = cal_ph_low - cal_slope * (float)cal_raw_low;
    ph_calibrated = true;

    LOG_INF("pH calibration updated: slope=%f offset=%f",
            (double)cal_slope,
            (double)cal_offset);
}

static float ph2_from_raw(uint16_t raw)
{
    if (ph_calibrated) {
        return cal_slope * (float)raw + cal_offset;
    }

    /* Fallback: map full ADC range roughly to pH 0-14. */
    float approx_slope = 14.0f / 4095.0f;
    return approx_slope * (float)raw;
}

static void ph2_report_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(ph2_report_work, ph2_report_work_handler);

static void ph2_report_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    uint16_t raw;
    int err = ph2_sensor_read_raw(&raw);
    if (err) {
        /* Try again later */
        k_work_schedule(&ph2_report_work, K_SECONDS(PH2_REPORT_PERIOD_S));
        return;
    }

    float ph = ph2_from_raw(raw);

    /* Always log to the serial console so readings are visible even
     * when Pouch/gateway is not working. Avoid float formatting in
     * logs (which may be disabled) by using fixed-point.
     */
    int32_t ph_milli = (int32_t)(ph * 1000.0f);
    int32_t ph_int = ph_milli / 1000;
    int32_t ph_frac = ph_milli >= 0 ? (ph_milli % 1000) : -(ph_milli % 1000);

    LOG_INF("pH reading: ph=%d.%03d raw=%u",
            (int)ph_int,
            (int)ph_frac,
            (unsigned int)raw);

    if (pouch_session_active) {
        char payload[64];
        int len = snprintk(payload, sizeof(payload),
                           "{\"ph\":%.3f,\"raw\":%u}",
                           (double)ph,
                           (unsigned int)raw);
        if (len > 0) {
            err = pouch_uplink_entry_write(".s/ph2",
                                           POUCH_CONTENT_TYPE_JSON,
                                           payload,
                                           (size_t)len,
                                           K_NO_WAIT);
            if (err) {
                LOG_WRN("pH uplink failed (err %d), logging only", err);
            }
        }
    }

    k_work_schedule(&ph2_report_work, K_SECONDS(PH2_REPORT_PERIOD_S));
}

int ph2_sensor_init(void)
{
    ph2_i2c = DEVICE_DT_GET(PH2_I2C_BUS_NODE);
    if (!device_is_ready(ph2_i2c)) {
        LOG_ERR("pH sensor I2C bus not ready");
        return -ENODEV;
    }

    LOG_INF("pH2 sensor initialized on I2C bus %s addr 0x%02x",
            ph2_i2c->name,
            PH2_MCP3221_I2C_ADDR);

    /* Start periodic local logging immediately; uplink is optional. */
    k_work_schedule(&ph2_report_work, K_SECONDS(PH2_REPORT_PERIOD_S));

    return 0;
}

void ph2_sensor_pouch_session_start(void)
{
    pouch_session_active = true;
}

void ph2_sensor_pouch_session_end(void)
{
    pouch_session_active = false;
}

int ph2_sensor_calibrate_low(float known_ph)
{
    uint16_t raw;
    int err = ph2_sensor_read_raw(&raw);
    if (err) {
        return err;
    }

    cal_ph_low = known_ph;
    cal_raw_low = raw;
    have_low_point = true;

    LOG_INF("Captured low pH calibration point: ph=%.3f raw=%u",
            (double)known_ph,
            (unsigned int)raw);

    ph2_update_calibration();
    return 0;
}

int ph2_sensor_calibrate_high(float known_ph)
{
    uint16_t raw;
    int err = ph2_sensor_read_raw(&raw);
    if (err) {
        return err;
    }

    cal_ph_high = known_ph;
    cal_raw_high = raw;
    have_high_point = true;

    LOG_INF("Captured high pH calibration point: ph=%.3f raw=%u",
            (double)known_ph,
            (unsigned int)raw);

    ph2_update_calibration();
    return 0;
}

int ph2_sensor_guided_calibration(float low_ph, float high_ph)
{
    int err;

    LOG_INF("Starting guided pH calibration: low=%f high=%f",
            (double)low_ph,
            (double)high_ph);

    err = ph2_sensor_calibrate_low(low_ph);
    if (err) {
        LOG_ERR("Failed low-point pH calibration (err %d)", err);
        return err;
    }

    err = ph2_sensor_calibrate_high(high_ph);
    if (err) {
        LOG_ERR("Failed high-point pH calibration (err %d)", err);
        return err;
    }

    LOG_INF("Guided pH calibration complete");
    return 0;
}
