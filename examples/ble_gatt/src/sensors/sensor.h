/* Common sensor interface for the ble_gatt node */

#ifndef SENSOR_H_
#define SENSOR_H_

/**
 * @brief Initialize all sensors
 * 
 * Called during application startup to initialize all sensor hardware.
 * 
 * @return 0 on success, negative error code on failure
 */
int sensors_init_all(void);

/**
 * @brief Notify sensors that Pouch session has started
 * 
 * Called when gateway connection is established. Sensors should begin
 * taking readings and uploading data.
 */
void sensors_pouch_session_start(void);

/**
 * @brief Notify sensors that Pouch session has ended
 * 
 * Called when gateway connection is lost. Sensors should stop
 * active data collection.
 */
void sensors_pouch_session_end(void);

#endif /* SENSOR_H_ */
