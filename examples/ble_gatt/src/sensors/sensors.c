/* Aggregates all sensors used by the ble_gatt node. */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(main);

#include "sensor.h"
#include "ph2_sensor.h"

int sensors_init_all(void)
{
    int err;

    LOG_INF("Initializing sensors...");

    err = ph2_sensor_init();
    if (err) {
        LOG_ERR("pH2 sensor init failed: %d", err);
        return err;
    }

    LOG_INF("All sensors initialized successfully");
    return 0;
}

void sensors_pouch_session_start(void)
{
    LOG_DBG("Starting sensor data collection");
    ph2_sensor_pouch_session_start();
}

void sensors_pouch_session_end(void)
{
    LOG_DBG("Stopping sensor data collection");
    ph2_sensor_pouch_session_end();
}
