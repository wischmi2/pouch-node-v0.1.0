/* Common sensor interface for the ble_gatt node */

#ifndef SENSOR_H_
#define SENSOR_H_

int sensors_init_all(void);
void sensors_pouch_session_start(void);
void sensors_pouch_session_end(void);

#endif /* SENSOR_H_ */
