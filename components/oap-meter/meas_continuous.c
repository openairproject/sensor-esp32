/*
 * meas_continuous.c
 *
 *  Created on: Mar 25, 2017
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

#include "meas_continuous.h"

//static char* TAG = "meas_cont";
//
//typedef struct {
//	pmsx003_config_t config;
//	pm_data last;
//} sensor_model_t;
//
//uint8_t sensor_count;
//sensor_model_t* sensors;
//
//static void collect(pm_data* pm) {
//
//}
//
//static void start(pm_sensor_pair_config_t* pms_configs, meas_continuous_params_t* params, meas_strategy_callback callback) {
//	sensor_count = pms_configs->count;
//	sensors = malloc(sizeof(sensor_model_t)*sensor_count);
//	for (uint8_t c = 0; c < sensor_count; c++) {
//		sensor_model_t* sensor = sensors+c;
//		memset(sensor, 0, sizeof(sensor_model_t));
//		memcpy(&sensor->config, pms_configs->sensor[c], sizeof(pmsx003_config_t));
//	}
//}
//
pm_meter_t pm_meter_continuous = {
	.input = NULL,
	.start = NULL,
	.stop = NULL
};
