/*
 * oap_data_env.h
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

#ifndef COMPONENTS_OAP_COMMON_INCLUDE_OAP_DATA_ENV_H_
#define COMPONENTS_OAP_COMMON_INCLUDE_OAP_DATA_ENV_H_

typedef enum {
	sensor_bmx280 = 0,
	sensor_mhz19,
	sensor_hcsr04,
	sensor_gpio
} sensor_type_t;

typedef struct {
	uint8_t sensor_idx;
	sensor_type_t sensor_type;
	
	union {
		struct {
			double temp;
			double pressure;
			double sealevel;
			double humidity;
			uint32_t altitude;
		} bmx280;
		struct {
			double temp;
			uint32_t co2;
		} mhz19;
		struct {
			uint32_t distance;
		} hcsr04;
		struct {
			int val;
			int64_t GPIlastLow;
			int64_t GPIlastHigh;
			int64_t GPICounter;
			int64_t GPICountDelta;
			int64_t GPOlastOut;
			int64_t GPOtriggerLength;
			int8_t GPOlastVal;
		} gpio;
	};
} env_data_t;

#endif /* COMPONENTS_OAP_COMMON_INCLUDE_OAP_DATA_ENV_H_ */
