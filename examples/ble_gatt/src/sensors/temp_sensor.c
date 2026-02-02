/* Single-wire temperature sensor (DS18B20) handling */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(main);

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#include <pouch/types.h>
#include <pouch/uplink.h>

#include "temp_sensor.h"

/* Period between temperature reports (seconds). */
#define TEMP_REPORT_PERIOD_S 10

#if DT_NODE_HAS_STATUS(DT_ALIAS(temp0), okay)
#define TEMP_NODE DT_ALIAS(temp0)
#else
#error "temp0 alias is not defined in the devicetree"
#endif

static const struct device *temp_dev;
static bool pouch_session_active;
static bool have_last_reading;
static struct sensor_value last_temp_c;

static int temp_sensor_read(struct sensor_value *out_temp)
{
    if (!temp_dev) {
        return -ENODEV;
    }

    int err = sensor_sample_fetch(temp_dev);
    if (err) {
        LOG_WRN("Temp sensor sample fetch failed (err %d)", err);
        return err;
    }

    err = sensor_channel_get(temp_dev, SENSOR_CHAN_AMBIENT_TEMP, out_temp);
    if (err) {
        LOG_WRN("Temp sensor channel read failed (err %d)", err);
        return err;
    }

    return 0;
}

static void temp_value_to_milli(const struct sensor_value *val, int32_t *out_milli)
{
    int32_t milli = val->val1 * 1000;
    milli += val->val2 / 1000;
    *out_milli = milli;
}

static void temp_send_to_golioth(const struct sensor_value *temp_c)
{
    if (!pouch_session_active) {
        return;
    }

    int32_t milli;
    temp_value_to_milli(temp_c, &milli);
    int32_t temp_int = milli / 1000;
    int32_t temp_frac = (milli >= 0) ? (milli % 1000) : -(milli % 1000);

    char payload[48];
    int len = snprintk(payload, sizeof(payload),
                       "{\"temp_c\":%d.%03d}",
                       (int)temp_int, (int)temp_frac);
    if (len <= 0) {
        return;
    }

    LOG_INF("Golioth uplink: path=.s/temp len=%d payload=%.*s", len, len, payload);
    int err = pouch_uplink_entry_write(".s/temp",
                                       POUCH_CONTENT_TYPE_JSON,
                                       payload,
                                       (size_t)len,
                                       K_NO_WAIT);
    if (err) {
        LOG_WRN("Temp uplink failed (err %d)", err);
    }
}

static void temp_report_work_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(temp_report_work, temp_report_work_handler);

static void temp_report_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    struct sensor_value temp_c;
    int err = temp_sensor_read(&temp_c);
    if (err) {
        k_work_schedule(&temp_report_work, K_SECONDS(TEMP_REPORT_PERIOD_S));
        return;
    }

    have_last_reading = true;
    last_temp_c = temp_c;

    int32_t milli;
    temp_value_to_milli(&temp_c, &milli);
    int32_t temp_int = milli / 1000;
    int32_t temp_frac = (milli >= 0) ? (milli % 1000) : -(milli % 1000);

    LOG_INF("Temp reading: %d.%03d C", (int)temp_int, (int)temp_frac);

    if (pouch_session_active) {
        temp_send_to_golioth(&temp_c);
    }

    k_work_schedule(&temp_report_work, K_SECONDS(TEMP_REPORT_PERIOD_S));
}

int temp_sensor_init(void)
{
    temp_dev = DEVICE_DT_GET(TEMP_NODE);
    if (!device_is_ready(temp_dev)) {
        LOG_ERR("Temp sensor device not ready");
        return -ENODEV;
    }

    LOG_INF("Temp sensor initialized: %s", temp_dev->name);
    k_work_schedule(&temp_report_work, K_SECONDS(TEMP_REPORT_PERIOD_S));

    return 0;
}

void temp_sensor_pouch_session_start(void)
{
    pouch_session_active = true;
    LOG_INF("Temp sensor: Pouch session started");

    if (have_last_reading) {
        temp_send_to_golioth(&last_temp_c);
    }
}

void temp_sensor_pouch_session_end(void)
{
    pouch_session_active = false;
    LOG_INF("Temp sensor: Pouch session ended");
}
