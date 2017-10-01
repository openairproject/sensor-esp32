/*
 * meas.h
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

#ifndef MAIN_MEAS_H_
#define MAIN_MEAS_H_

#include "oap_common.h"
#include "oap_data.h"

typedef struct {
	uint8_t heater_pin;
	uint8_t fan_pin;
} pm_meter_aux_t;

pm_meter_aux_t pm_meter_aux;

typedef struct {
  uint8_t count;
  pm_data_t pm_data[2];
} pm_data_pair_t;

typedef enum {
	PM_METER_START,
	PM_METER_RESULT,
	PM_METER_ERROR
} pm_meter_event_t;

typedef void(*pm_meter_output_f)(pm_meter_event_t event, void* pm_data);

typedef void(*pm_sensor_enable_handler_f)(uint8_t enable);

typedef struct {
  uint8_t count;
  pm_sensor_enable_handler_f handler[2];
} pm_sensor_enable_handler_pair_t;

/**
 * @brief starts PM meter. it will now start collecting input samples until stop method is called
 *
 * params:
 * 1. handlers to enable/disable pm_sensors;
 * 2. pm_meter parameters
 * 3. output callback
 */
typedef void(*pm_meter_start_f)(pm_sensor_enable_handler_pair_t*, void*, pm_meter_output_f);

/**
 * @brief stops PM meter. depending on a meter, this may generate 'RESULT' event.
 */
typedef void(*pm_meter_stop_f)();

typedef struct {
	pm_data_callback_f input;
	pm_meter_start_f start;
	pm_meter_stop_f stop;
} pm_meter_t;



#endif /* MAIN_MEAS_H_ */
