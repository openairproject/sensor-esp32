/*
 * bmp280.h
 *
 *  Created on: Feb 8, 2017
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

#ifndef MAIN_BMP280_H_
#define MAIN_BMP280_H_

#include "oap_common.h"
#include "oap_data_env.h"

#define HW_BMX280_DEVICES_MAX 2

typedef void(*env_callback)(env_data_t*);

typedef struct bmx280_config_t {

	uint8_t i2c_num;
	uint8_t device_addr;
	uint8_t sda_pin;
	uint8_t scl_pin;
	uint8_t sensor_idx;	//sensor number (0 - 1)
	uint32_t interval;
	env_callback callback;
	int32_t altitude;
	int32_t tempOffset;
	int32_t humidityOffset;
} bmx280_config_t;

esp_err_t bmx280_init(bmx280_config_t* config);

esp_err_t bmx280_set_hardware_config(bmx280_config_t* bmx280_config, uint8_t sensor_idx);

#endif /* MAIN_BMP280_H_ */
