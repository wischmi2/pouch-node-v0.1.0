/* Water Detect 3 Click handling for XIAO nRF52840 node */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(main);

#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>

#include <pouch/uplink.h>
#include <pouch/types.h>

#include "water_sensor.h"

/* Water Detect 3 INT wired to XIAO D2 -> nRF P0.28 on GPIO0 */
/* Period between water status reports (seconds). */
#define WATER_REPORT_PERIOD_S 10

static const struct gpio_dt_spec water_detect = GPIO_DT_SPEC_GET_OR(DT_ALIAS(water_detect), gpios, {});

static bool pouch_session_active;
static bool last_water_state;

static void water_report_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(water_report_work, water_report_work_handler);

static void water_report_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!water_detect.port || !device_is_ready(water_detect.port))
    {
        LOG_WRN("Water sensor GPIO port not ready");
        k_work_schedule(&water_report_work, K_SECONDS(WATER_REPORT_PERIOD_S));
        return;
    }

    int val = gpio_pin_get_dt(&water_detect);
    if (val < 0)
    {
        LOG_WRN("Water sensor read failed (err %d)", val);
        k_work_schedule(&water_report_work, K_SECONDS(WATER_REPORT_PERIOD_S));
        return;
    }

    // With ACTIVE_LOW: 1 = water detected, 0 = no water (after Zephyr inversion)
    bool water_detected = (val > 0);

    if (water_detected != last_water_state)
    {
        LOG_INF("Water state changed: %s", water_detected ? "WET" : "DRY");
        last_water_state = water_detected;
    }

    /* Periodic status log so you can see it on the console. */
    LOG_INF("Water sensor status: %s (GPIO raw value: %d)", 
            water_detected ? "WET" : "DRY", val);

    if (pouch_session_active)
    {
        const char *payload = water_detected ? "{\"wet\":true}" : "{\"wet\":false}";
        size_t len = strlen(payload);

        int err = pouch_uplink_entry_write(".s/water",
                                           POUCH_CONTENT_TYPE_JSON,
                                           payload,
                                           len,
                                           K_NO_WAIT);
        if (err)
        {
            LOG_WRN("Water state uplink failed (err %d)", err);
        }
        else
        {
            LOG_INF("Water state reported to Pouch: %s", water_detected ? "WET" : "DRY");
        }
    }

    k_work_schedule(&water_report_work, K_SECONDS(WATER_REPORT_PERIOD_S));
}

int water_sensor_init(void)
{
    if (!water_detect.port)
    {
        LOG_WRN("Water sensor not configured in device tree");
        return -ENODEV;
    }

    if (!device_is_ready(water_detect.port))
    {
        LOG_ERR("Water sensor GPIO port not ready");
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&water_detect, GPIO_INPUT);
    if (ret < 0)
    {
        LOG_ERR("Failed to configure water sensor GPIO (err %d)", ret);
        return ret;
    }

    int val = gpio_pin_get_dt(&water_detect);
    if (val < 0)
    {
        LOG_WRN("Water sensor initial read failed (err %d)", val);
        last_water_state = false;
    }
    else
    {
        // With ACTIVE_LOW: 1 = water detected, 0 = no water (after Zephyr inversion)
        last_water_state = (val > 0);
    }

    LOG_INF("Water sensor initialized on P0.28 (D2), initial state: %s (GPIO raw: %d)", 
            last_water_state ? "WET" : "DRY", val);

    k_work_schedule(&water_report_work, K_SECONDS(WATER_REPORT_PERIOD_S));

    return 0;
}

void water_sensor_pouch_session_start(void)
{
    pouch_session_active = true;
    /* Report current state when a session comes up */
    k_work_submit(&water_report_work.work);
}

void water_sensor_pouch_session_end(void)
{
    pouch_session_active = false;
}
