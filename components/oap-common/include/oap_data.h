/*
 * oap_data.h
 *
 *  Created on: Oct 1, 2017
 *      Author: kris
 *
 *  This file is part of OpenAirProject-ESP32.
 *
 *  OpenAirProject-ESP32 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  OpenAirProject-ESP32 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with OpenAirProject-ESP32.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef COMPONENTS_OAP_COMMON_INCLUDE_OAP_DATA_H_
#define COMPONENTS_OAP_COMMON_INCLUDE_OAP_DATA_H_

#include "oap_data_pm.h"
#include "oap_data_env.h"

// FIXME this stuff has to be generalized
typedef struct {
	pm_data_t* pm;
	pm_data_t* pm_aux;
	env_data_t* env;
	env_data_t* env_int;
	env_data_t* co2;
	env_data_t* distance1;
	env_data_t* distance2;
	env_data_t* gpio1;
	env_data_t* gpio2;
	env_data_t* gpio3;
	env_data_t* gpio4;
	long int local_time;
} oap_measurement_t;

typedef struct {
	int led;
	int heater;
	int fan;

	int indoor;
	int warm_up_time;
	int meas_time;
	int meas_interval;
	int meas_strategy;	//interval, continuos, etc
	int test;
	int altitude;
	int tempOffset;
	int humidityOffset;
	int pms_enabled;
	int hcsr04_enabled;
	int bmx280_enabled;
	int gpio_enabled;
	int ssd1306_enabled;
	int mhz19_enabled;
	int gpio_udp_enabled;
} oap_sensor_config_t;

typedef struct {
	env_data_t env_data;
	long timestamp;
} env_data_record_t;

extern env_data_record_t last_env_data[];

#endif /* COMPONENTS_OAP_COMMON_INCLUDE_OAP_DATA_H_ */
