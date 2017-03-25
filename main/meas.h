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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "oap_common.h"
#include "pmsx003.h"

typedef struct {
  uint8_t count;
  pm_data data[2];
} pm_data_duo_t;

typedef struct {
  uint8_t count;
  pms_config_t* sensor[2];
} pms_configs_t;

typedef enum {
	MEAS_START,
	MEAS_RESULT,
	MEAS_ERROR
} meas_event_t;

typedef void(*meas_strategy_callback)(meas_event_t event, void* data);

typedef void(*meas_strategy_start)(pms_configs_t*, void*, meas_strategy_callback);

typedef struct meas_strategy {
	pms_callback collect;
	meas_strategy_start start;
} meas_strategy_t;

#endif /* MAIN_MEAS_H_ */
