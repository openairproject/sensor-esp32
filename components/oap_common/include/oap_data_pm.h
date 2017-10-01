/*
 * oap_data_pm.h
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

#ifndef COMPONENTS_OAP_COMMON_INCLUDE_OAP_DATA_PM_H_
#define COMPONENTS_OAP_COMMON_INCLUDE_OAP_DATA_PM_H_

typedef struct {
	uint16_t pm1_0;
	uint16_t pm2_5;
	uint16_t pm10;
	uint8_t sensor_idx;
} pm_data_t;

typedef void(*pm_data_callback_f)(pm_data_t*);

#endif /* COMPONENTS_OAP_COMMON_INCLUDE_OAP_DATA_PM_H_ */
