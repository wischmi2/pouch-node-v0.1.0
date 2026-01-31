/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#include "credentials.h"
#include "sensors/sensor.h"
#include "sensors/ph2_sensor.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#include <pouch/pouch.h>
#include <pouch/events.h>
#include <pouch/uplink.h>
#include <pouch/downlink.h>
#include <pouch/transport/ble_gatt/peripheral.h>
#include <pouch/transport/ble_gatt/common/types.h>

#include <golioth/golioth.h>
#include <golioth/settings_callbacks.h>

#include <app_version.h>

/* This should be included via library path but explicitly defining here to fix build issue */
#ifndef GOLIOTH_BLE_GATT_UUID_SVC_VAL
#define GOLIOTH_BLE_GATT_UUID_SVC_VAL \
    BT_UUID_128_ENCODE(0x89a316ae, 0x89b7, 0x4ef6, 0xb1d3, 0x5c9a6e27d272)
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {});

static struct
{
    uint8_t uuid[16];
    struct golioth_ble_gatt_adv_data data;
} __packed service_data = {
    .uuid = {GOLIOTH_BLE_GATT_UUID_SVC_VAL},
    .data =
        {
            .version = (POUCH_VERSION << GOLIOTH_BLE_GATT_ADV_VERSION_POUCH_SHIFT)
                | (GOLIOTH_BLE_GATT_VERSION << GOLIOTH_BLE_GATT_ADV_VERSION_SELF_SHIFT),
            .flags = 0x0,
        },
};

static struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static struct bt_data sd[] = {
    BT_DATA(BT_DATA_SVC_DATA128, &service_data, sizeof(service_data)),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_DBG("Connection failed (err 0x%02x)", err);
    }
    else
    {
        LOG_DBG("Connected");
    }
}

void disconnect_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
    }
}

K_WORK_DELAYABLE_DEFINE(disconnect_work, disconnect_work_handler);

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_DBG("Disconnected (reason 0x%02x)", reason);

    k_work_schedule(&disconnect_work, K_SECONDS(1));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

void sync_request_work_handler(struct k_work *work)
{
    service_data.data.flags = 0x01;
    bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}

K_WORK_DELAYABLE_DEFINE(sync_request_work, sync_request_work_handler);

static void pouch_event_handler(enum pouch_event event, void *ctx)
{
    if (POUCH_EVENT_SESSION_START == event)
    {
        sensors_pouch_session_start();

        golioth_sync_to_cloud();
    }

    if (POUCH_EVENT_SESSION_END == event)
    {
        sensors_pouch_session_end();

        service_data.data.flags = 0x00;
        bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
        k_work_schedule(&sync_request_work, K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));
    }
}

POUCH_EVENT_HANDLER(pouch_event_handler, NULL);

static int led_setting_cb(bool new_value)
{
    LOG_INF("Received LED setting: %d", (int) new_value);

    if (DT_HAS_ALIAS(led0))
    {
        gpio_pin_set_dt(&led, new_value ? 1 : 0);
    }

    return 0;
}

GOLIOTH_SETTINGS_HANDLER(LED, led_setting_cb);

/*
 * pH2 calibration shell commands
 *
 * Usage:
 *   ph2 calib-low <ph>
 *   ph2 calib-high <ph>
 *   ph2 calib-guided [low_ph] [high_ph]
 */

static int cmd_ph2_calib_low(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: ph2 calib-low <ph>");
        return -EINVAL;
    }

    float ph = strtof(argv[1], NULL);
    int err = ph2_sensor_calibrate_low(ph);
    if (err) {
        shell_error(sh, "Calibration failed (err %d)", err);
    } else {
        shell_print(sh, "Captured low-point calibration at pH=%.3f", (double)ph);
    }

    return err;
}

static int cmd_ph2_calib_high(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2) {
        shell_error(sh, "Usage: ph2 calib-high <ph>");
        return -EINVAL;
    }

    float ph = strtof(argv[1], NULL);
    int err = ph2_sensor_calibrate_high(ph);
    if (err) {
        shell_error(sh, "Calibration failed (err %d)", err);
    } else {
        shell_print(sh, "Captured high-point calibration at pH=%.3f", (double)ph);
    }

    return err;
}

static int cmd_ph2_calib_guided(const struct shell *sh, size_t argc, char **argv)
{
    float low_ph = 7.0f;
    float high_ph = 4.0f;

    if (argc >= 2) {
        low_ph = strtof(argv[1], NULL);
    }
    if (argc >= 3) {
        high_ph = strtof(argv[2], NULL);
    }

    shell_print(sh, "Guided calibration (non-interactive): low=%.2f high=%.2f",
                (double)low_ph, (double)high_ph);
    shell_print(sh, "Make sure the probe is in the LOW buffer (%.2f) before running,",
                (double)low_ph);
    shell_print(sh, "then move it to the HIGH buffer (%.2f) when instructed.",
                (double)high_ph);

    int err = ph2_sensor_calibrate_low(low_ph);
    if (err) {
        shell_error(sh, "Low-point calibration failed (err %d)", err);
        return err;
    }

    shell_print(sh, "Low-point captured. Now move the probe to the HIGH buffer and run the command again if needed, or use ph2 calib-high.");

    /* For safety, do not automatically capture the high point here.
     * Users can explicitly call ph2 calib-high <ph> after moving the probe.
     */
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(ph2_sub,
    SHELL_CMD(calib-low, NULL, "Capture low-point calibration: ph2 calib-low <ph>", cmd_ph2_calib_low),
    SHELL_CMD(calib-high, NULL, "Capture high-point calibration: ph2 calib-high <ph>", cmd_ph2_calib_high),
    SHELL_CMD(calib-guided, NULL, "Run guided two-point calibration: ph2 calib-guided [low_ph] [high_ph]", cmd_ph2_calib_guided),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ph2, &ph2_sub, "pH2 sensor commands", NULL);

int main(void)
{
    // Early LED blink to confirm boot - 3 fast blinks
    if (DT_HAS_ALIAS(led0))
    {
        int err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        if (err == 0)
        {
            for (int i = 0; i < 3; i++)
            {
                gpio_pin_set_dt(&led, 1);
                k_msleep(100);
                gpio_pin_set_dt(&led, 0);
                k_msleep(100);
            }
            k_msleep(3000);  // 3 second pause
        }
    }

    LOG_INF("=== Boot Started ===");
    LOG_INF("Pouch SDK Version: " STRINGIFY(APP_BUILD_VERSION));
    LOG_INF("Pouch Protocol Version: %d", POUCH_VERSION);
    LOG_INF("Pouch BLE Transport Protocol Version: %d", GOLIOTH_BLE_GATT_VERSION);

    /* Inform the user how to perform calibration from the serial console. */
    LOG_INF("To calibrate the pH2 sensor, use shell commands:");
    LOG_INF("  ph2 calib-low <ph>   (e.g. ph2 calib-low 7.00)");
    LOG_INF("  ph2 calib-high <ph>  (e.g. ph2 calib-high 4.00)");
    LOG_INF("or start with: ph2 calib-guided 7.00 4.00");

    // LED: 1 slow blink after init message
    if (DT_HAS_ALIAS(led0))
    {
        gpio_pin_set_dt(&led, 1);
        k_msleep(200);
        gpio_pin_set_dt(&led, 0);
        k_msleep(3000);  // 3 second pause
    }

    LOG_INF("Initializing BLE GATT peripheral...");
    int err = golioth_ble_gatt_peripheral_init();
    if (err)
    {
        LOG_ERR("Failed to initialize Pouch BLE GATT peripheral (err %d)", err);
        return 0;
    }

    // LED: 2 blinks after BLE peripheral init
    if (DT_HAS_ALIAS(led0))
    {
        for (int i = 0; i < 2; i++)
        {
            gpio_pin_set_dt(&led, 1);
            k_msleep(100);
            gpio_pin_set_dt(&led, 0);
            k_msleep(100);
        }
        k_msleep(3000);  // 3 second pause
    }

    LOG_INF("Enabling Bluetooth...");
    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    LOG_INF("Bluetooth initialized");

    // LED: 3 blinks after Bluetooth init
    if (DT_HAS_ALIAS(led0))
    {
        for (int i = 0; i < 3; i++)
        {
            gpio_pin_set_dt(&led, 1);
            k_msleep(100);
            gpio_pin_set_dt(&led, 0);
            k_msleep(100);
        }
        k_msleep(3000);  // 3 second pause
    }

    LOG_INF("Loading credentials...");
    struct pouch_config config = {0};

    err = load_certificate(&config.certificate);
    if (err)
    {
        LOG_ERR("Failed to load certificate (err %d)", err);
        return 0;
    }

    config.private_key = load_private_key();
    if (config.private_key == PSA_KEY_ID_NULL)
    {
        LOG_ERR("Failed to load private key");
        return 0;
    }

    LOG_INF("Credentials loaded");

    // LED: 4 blinks after credentials loaded
    if (DT_HAS_ALIAS(led0))
    {
        for (int i = 0; i < 4; i++)
        {
            gpio_pin_set_dt(&led, 1);
            k_msleep(100);
            gpio_pin_set_dt(&led, 0);
            k_msleep(100);
        }
        k_msleep(3000);  // 3 second pause
    }

    LOG_INF("Initializing Pouch...");
    err = pouch_init(&config);
    if (err)
    {
        LOG_ERR("Pouch init failed (err %d)", err);
        return 0;
    }

    LOG_INF("Pouch initialized");

    // LED: 5 blinks after Pouch initialized
    if (DT_HAS_ALIAS(led0))
    {
        for (int i = 0; i < 5; i++)
        {
            gpio_pin_set_dt(&led, 1);
            k_msleep(100);
            gpio_pin_set_dt(&led, 0);
            k_msleep(100);
        }
        k_msleep(3000);  // 3 second pause
    }

    LOG_INF("Starting BLE advertising...");
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return 0;
    }

    LOG_INF("Advertising started");

    // LED: Solid on for 1 second to show successful init
    if (DT_HAS_ALIAS(led0))
    {
        err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        if (err == 0)
        {
            gpio_pin_set_dt(&led, 1);
            k_msleep(1000);
            gpio_pin_set_dt(&led, 0);
        }
    }

    LOG_INF("Initializing sensors...");
    // Initialize all sensors
    err = sensors_init_all();
    if (err)
    {
        LOG_WRN("Sensors init failed (err %d), continuing without them", err);
    }
    else
    {
        LOG_INF("Sensors initialized successfully");
    }

    LOG_INF("=== Boot Complete - Starting main loop ===");
    k_work_schedule(&sync_request_work, K_SECONDS(CONFIG_EXAMPLE_SYNC_PERIOD_S));

    while (1)
    {
        k_sleep(K_SECONDS(1));
    }
    return 0;
}
