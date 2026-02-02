/*
 * Copyright (c) 2025 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#include "credentials.h"
#include "sensors/sensor.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>

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

/**
 * Bluetooth connection established callback.
 * Called when a BLE central (gateway) connects to this peripheral node.
 */
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

/**
 * Work handler that restarts BLE advertising after disconnect.
 * Delayed by 1 second to allow clean disconnect processing.
 */
void disconnect_work_handler(struct k_work *work)
{
    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to start (err %d)", err);
    }
}

K_WORK_DELAYABLE_DEFINE(disconnect_work, disconnect_work_handler);

/**
 * Bluetooth disconnection callback.
 * Schedules work to restart advertising so gateway can reconnect.
 */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_DBG("Disconnected (reason 0x%02x)", reason);

    k_work_schedule(&disconnect_work, K_SECONDS(1));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/**
 * Work handler that sets the "sync requested" flag in BLE advertising data.
 * Signals to gateway that node has data ready to upload.
 */
void sync_request_work_handler(struct k_work *work)
{
    service_data.data.flags = 0x01;
    bt_le_adv_update_data(ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
}

K_WORK_DELAYABLE_DEFINE(sync_request_work, sync_request_work_handler);

/**
 * Handles Pouch session lifecycle events.
 * - SESSION_START: Collects sensor data and closes uplink
 * - SESSION_END: Clears sync flag and schedules next sync request
 */
static void pouch_event_handler(enum pouch_event event, void *ctx)
{
    if (POUCH_EVENT_SESSION_START == event)
    {
        sensors_pouch_session_start();

        // Now close the uplink - data has been written synchronously
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

/**
 * Golioth settings callback for LED control.
 * Receives LED on/off commands from cloud and controls GPIO.
 */
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

/**
 * Node application entry point.
 * 1. Blinks LED to confirm boot
 * 2. Initializes BLE GATT peripheral with Pouch service
 * 3. Loads device certificate and private key
 * 4. Initializes Pouch protocol stack
 * 5. Starts BLE advertising to be discovered by gateway
 * 6. Initializes all sensors (water, temperature, etc.)
 * 7. Schedules periodic sync requests to upload data
 */
int main(void)
{
    int err;

    // Early LED blink to confirm boot - 3 fast blinks
    if (DT_HAS_ALIAS(led0))
    {
        err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
        if (err == 0)
        {
            for (int i = 0; i < 3; i++)
            {
                gpio_pin_set_dt(&led, 1);
                k_msleep(100);
                gpio_pin_set_dt(&led, 0);
                k_msleep(100);
            }
            k_msleep(3000);
        }
    }

    LOG_INF("=== Boot Started ===");
    LOG_INF("Pouch SDK Version: " STRINGIFY(APP_BUILD_VERSION));
    LOG_INF("Pouch Protocol Version: %d", POUCH_VERSION);
    LOG_INF("Pouch BLE Transport Protocol Version: %d", GOLIOTH_BLE_GATT_VERSION);

    // LED: 1 slow blink after init message
    if (DT_HAS_ALIAS(led0))
    {
        gpio_pin_set_dt(&led, 1);
        k_msleep(200);
        gpio_pin_set_dt(&led, 0);
        k_msleep(3000);
    }

    LOG_INF("Initializing BLE GATT peripheral...");
    err = golioth_ble_gatt_peripheral_init();
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
        k_msleep(3000);
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
        k_msleep(3000);
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
        k_msleep(3000);
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
        k_msleep(3000);
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
        gpio_pin_set_dt(&led, 1);
        k_msleep(1000);
        gpio_pin_set_dt(&led, 0);
    }

    LOG_INF("Initializing sensors...");
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
