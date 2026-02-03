/* Single-wire temperature sensor interface (DS18B20) */

#ifndef TEMP_SENSOR_H_
#define TEMP_SENSOR_H_

int temp_sensor_init(void);
void temp_sensor_pouch_session_start(void);
void temp_sensor_pouch_session_end(void);

/** Get last temperature in °C; returns 0 on success, -ENODATA if no reading yet. */
int temp_sensor_get_last(float *temp_c);

#endif /* TEMP_SENSOR_H_ */
