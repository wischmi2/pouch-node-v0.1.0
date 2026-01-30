/* Aggregates all sensors used by the ble_gatt node. */

#include "sensor.h"
#include "water_sensor.h"

int sensors_init_all(void)
{
    int err;

    err = water_sensor_init();
    if (err)
    {
        return err;
    }

    return 0;
}

void sensors_pouch_session_start(void)
{
    water_sensor_pouch_session_start();
}

void sensors_pouch_session_end(void)
{
    water_sensor_pouch_session_end();
}
